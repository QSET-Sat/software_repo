# C transport and planned FreeRTOS architecture

The hardware-independent controller contract and host mock are implemented and
compiled by `make test`. The board/FreeRTOS integration remains a design for a
later milestone. The MCU, controller, vendor SDK, task sizes/priorities, queue
depths, and interrupt rules will be fixed after board selection.

## Implemented portable boundary

- `qset_can_transport.h` defines allocation-free controller instances bound to
  either the primary or backup bus. It exposes nonblocking initialization,
  start/stop, raw RX/TX, controller-state and latest-fault queries, an
  asynchronous bus-off recovery request, and a diagnostics getter. The getter
  returns every counter together with the current bus state and latest driver
  fault, which it reads live from the adapter on each call.
- Controller events (RX overruns, entries into error-warning, error-passive,
  and bus-off, and failed bus-off recoveries) are counted by the vendor
  adapter and collected by the transport. The adapter increments its own
  saturating counts, normally in its ISR, and implements `take_events`, which
  returns and resets all of them in one step. The transport calls it on every
  receive and adds the results to its saturating `rx_controller_overruns`,
  `controller_warning_events`, `controller_passive_events`, `bus_off_events`,
  and `recovery_failures` counters. Reading diagnostics never clears them.
- Bus-off recovery attempts are counted by the transport: every
  `qset_can_controller_recover_bus_off` call that reaches the driver adds one
  to `recovery_attempts`. If the driver rejects the request immediately, the
  transport also adds one to `recovery_failures`. An accepted attempt that
  later ends back in bus-off is counted by the adapter as a recovery failure.
- RX queue overflows are counted by the transport. The RX queue is not built
  yet; when it is, its push must drop the new frame when the queue is full and
  call `qset_can_controller_record_rx_queue_overflow` once for that frame. The
  call is nonblocking and ISR-safe, and `rx_queue_overflows` saturates.
- TX queue-full failures are counted by the transport. The TX queue is not
  built yet; when it is, its enqueue must refuse the frame when the queue is
  full, return an immediate error to the caller, and call
  `qset_can_controller_record_tx_queue_full` once for that refusal. Several
  tasks may send, so the call must be made inside the same lock or critical
  section the queue uses to check for full. A busy controller seen by the TX
  task is a normal wait and is not counted.
- Invalid received frames are counted once each, at whichever level rejects
  them. Receive automatically discards and counts driver frames with an
  impossible Classic CAN shape (ID above `0x7FF` or DLC above 8). The future RX
  task must call `qset_can_controller_record_invalid_frame` once for each frame
  that protocol validation rejects. Frames removed by hardware acceptance
  filters, and invalid frames passed to transmit, are not counted.
- Late, duplicate, and unexpected-sequence status responses are counted by the
  transport. Request correlation is not built yet; when it is, it accepts a
  response matching the node's outstanding request and otherwise calls exactly
  one of `qset_can_controller_record_late_response` (matches the node's last
  request that failed after its final timeout),
  `qset_can_controller_record_duplicate_response` (matches the node's last
  answered request), or `qset_can_controller_record_unexpected_sequence`
  (anything else). It calls them only from the task that owns correlation.
  These counters stay zero on nodes that do not issue requests.
- Suppressed protocol violation reports are counted by the transport. The
  violation reporter and its rate limiter are not built yet; when they are,
  the reporter calls `qset_can_controller_record_suppressed_violation_report`
  once for each report it would send but skips because the per
  reason/offending-ID limit (1 per second) or the global limit (5 per second)
  is reached. Reports lost to bus-off or a full TX queue are not counted here.
- Receive items wrap `qset_can_frame_t` with C-only bus-channel and monotonic
  microsecond metadata. The frame type and Rust ABI remain unchanged.
- Vendor adapters implement a required operations table and translate their
  native errors to portable results and faults. Vendor, RTOS, and hardware
  types are not permitted in the portable headers.
- The transport checks only standard Classic CAN shape. Protocol ID assignment
  and payload validation remain Rust responsibilities.
- `qset_can_mock.h` supplies a deterministic 16-frame FIFO loopback adapter for
  host tests. It does not model RTOS scheduling or physical-bus timing.
- Calls are serialized by their owner. A future RX/TX task or ISR handoff owns
  synchronization; the portable wrapper and mock contain no locks. The one
  exception is inside each vendor adapter: `take_events` must read and reset
  its counts atomically with respect to its own ISR, for example by masking
  the CAN interrupt, so no event is lost.
- The diagnostics snapshot is made consistent by a platform-supplied critical
  section. Diagnostics counters can be written by the RX ISR and by several
  tasks while another task reads them, so firmware installs enter/exit
  functions with `qset_can_controller_set_critical_section`. The getter copies
  every counter inside one enter/exit pair and reads the live bus state and
  latest fault from the adapter only after exiting, so the adapter is never
  called with interrupts masked. The section must be nonblocking and must stop
  ISRs and higher-priority tasks; a mutex is not enough. On FreeRTOS:

  ```c
  static void can_diag_enter(void *context) { (void)context; taskENTER_CRITICAL(); }
  static void can_diag_exit(void *context) { (void)context; taskEXIT_CRITICAL(); }

  qset_can_controller_set_critical_section(&can_primary, can_diag_enter,
                                           can_diag_exit, 0);
  ```

  It is held only for a fixed-size copy. Host tests may leave it unset. Each
  counter is a naturally aligned 32-bit word, written whole by its single
  owner, so writers need no extra locking.

## Ownership and data flow

```text
CAN controller → C ISR → C RX queue → C RX task → Rust frame validation
                                             → future Rust message decoder
future Rust encoder → C TX request → C TX queue → C TX task → CAN controller
```

- **C platform adapter:** implement the portable operations table, configure
  the controller and transceiver, receive and transmit raw standard data
  frames, map hardware errors, and filter RTR, extended, and controller error
  events before protocol validation.
- **C/FreeRTOS layer:** use `xQueueSendFromISR` to hand complete received
  frames to an RX task and request a context switch when required. Keep ISR
  work bounded. TX requests enter a bounded queue; a TX task owns controller
  access, retries required by the eventual transport policy, and completion
  reporting. Queue-full and driver failures must be observable to the caller
  or diagnostics rather than silently dropped.
- **Rust protocol core:** validate ID/DLC, interpret the tier and node, and,
  once specified, encode/decode payloads and higher protocol services. It
  neither calls FreeRTOS nor touches hardware. C carries raw frames without
  editing their ID, DLC, or on-wire payload bytes.

## Implementing controller event reporting in a board adapter

Some controller events can only be seen by the board adapter, so each adapter
counts them itself and the portable transport collects the counts through
`take_events`, which fills a `qset_can_driver_events_t`. The adapter never
includes or writes transport diagnostics. The events are:

- **RX overrun:** a frame arrived while the controller's hardware receive
  buffer was still full, so the hardware dropped a frame before software could
  copy it out.
- **Error-warning:** the controller's transmit or receive error count crossed
  the warning level (typically 96) while error-active.
- **Error-passive:** an error count crossed the passive level (128) while
  error-active or error-warning.
- **Bus-off:** the transmit error count exceeded 255 while the controller was
  running, so it removed itself from the bus.
- **Recovery failure:** a recovery attempt the adapter accepted (the
  controller went to recovering) ended back in bus-off instead of
  error-active. If `recover_bus_off` itself returns an error, do not count it
  here; the transport counts that failure.

What the adapter must do:

1. **Find each indication in the chip's reference manual.** Most CAN
   controllers have a receive FIFO or mailbox overrun status flag and
   error-state change flags (warning, passive, bus-off), usually with interrupt
   enables. Enable those interrupts, or check the flags in the existing ISR.
2. **Count events, not frames or time.** For overruns, many controllers only
   report that at least one frame was lost; add one each time the flag is seen
   set and do not estimate the number of lost frames. For error states, add
   one each time the controller enters a worse state, for the state entered.
   Moving to a healthier state is not counted. A recovery attempt that ends
   back in bus-off is not a new bus-off event.
3. **Clear each flag after counting it**, as the manual describes, so the next
   event is detected.
4. **Keep counts, not yes/no flags.** Several events between two transport
   polls must all be counted. Saturate each count at `UINT32_MAX`; never wrap.
5. **Make read-and-reset atomic against the ISR.** Between reading the counts
   and setting them to zero, the ISR must not run, or an event arriving in
   between is lost. Mask the CAN interrupt (or use the RTOS's critical-section
   API) around those steps only, and keep the section short.
6. **Report failure without losing counts.** If the callback cannot read the
   counts, return an error and leave both `*out_events` and the stored counts
   unchanged. The transport adds nothing and picks them up on the next call.
7. **Do not report events through `get_state` or `get_last_fault` only.**
   Those still report the current state and latest fault, but neither is a
   counter, and the transport does not count by polling them, because short
   episodes between polls would be missed.

Chip-neutral example. `board_can_*` stands for the board's own register access
or vendor HAL calls:

```c
typedef struct {
    /* volatile: written by the ISR, read by task code. */
    volatile qset_can_driver_events_t events;
    /* Set by recover_bus_off when the controller accepts a recovery request. */
    volatile uint8_t recovering;
    /* ...other adapter state... */
} board_can_context_t;

static void board_can_count(volatile uint32_t *count)
{
    if (*count != UINT32_MAX) {
        (*count)++;
    }
}

/* Called from the board's CAN receive/error interrupt handler. */
static void board_can_handle_events(board_can_context_t *ctx)
{
    if (board_can_rx_overrun_flag_is_set()) {
        board_can_count(&ctx->events.rx_overruns);
        board_can_clear_rx_overrun_flag();
    }
    if (board_can_entered_warning()) {
        board_can_count(&ctx->events.warning_events);
        board_can_clear_warning_flag();
    }
    if (board_can_entered_passive()) {
        board_can_count(&ctx->events.passive_events);
        board_can_clear_passive_flag();
    }
    if (board_can_entered_bus_off()) {
        if (ctx->recovering) {
            /* Back in bus-off during a recovery attempt: a failure. */
            board_can_count(&ctx->events.recovery_failures);
            ctx->recovering = 0u;
        } else {
            board_can_count(&ctx->events.bus_off_events);
        }
        board_can_clear_bus_off_flag();
    }
    if (ctx->recovering && board_can_rejoined_bus()) {
        ctx->recovering = 0u;
    }
}

/* Ops-table callback, called by the transport from task context. */
static qset_can_driver_result_t board_can_take_events(void *context,
                                                      qset_can_driver_events_t *out_events)
{
    board_can_context_t *ctx = (board_can_context_t *)context;
    qset_can_driver_events_t taken;

    if (ctx == 0 || out_events == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    board_can_mask_interrupt();
    taken.rx_overruns = ctx->events.rx_overruns;
    taken.warning_events = ctx->events.warning_events;
    taken.passive_events = ctx->events.passive_events;
    taken.bus_off_events = ctx->events.bus_off_events;
    taken.recovery_failures = ctx->events.recovery_failures;
    ctx->events.rx_overruns = 0u;
    ctx->events.warning_events = 0u;
    ctx->events.passive_events = 0u;
    ctx->events.bus_off_events = 0u;
    ctx->events.recovery_failures = 0u;
    board_can_unmask_interrupt();

    *out_events = taken;
    return QSET_CAN_DRIVER_OK;
}
```

Register the callback as `.take_events = board_can_take_events` in the
adapter's `qset_can_driver_ops_t`. Init rejects an operations table that leaves
it out. `mock_take_events` and `mock_set_state` in `protocol/src/qset_can_mock.c`
are the host reference for the same contract; the mock has no interrupt to
mask.

The same Rust core is linked into OBC and subsystem firmware. An OBC-specific
application will issue commands; other applications will consume commands
addressed to them and publish their own telemetry/errors. Sender authority
cannot be inferred from a command ID and needs a separate system policy.

## Integration checkpoints

1. Choose MCU, CAN peripheral, transceiver, compiler target, FreeRTOS port,
   vendor HAL, and `no_std` panic/fault policy.
2. Implement a board adapter with the header's frame type. Confirm its RX
   path preserves the ID, DLC, and first `dlc` data bytes exactly.
3. Set queue depth, task priority/stack, and bus-off/recovery behavior from
   traffic and hardware requirements. Review ISR safety against the chosen
   FreeRTOS port.
4. Test RX, TX, queue overflow, controller event counting, controller
   errors, and recovery with a real second CAN node before enabling mission
   messages.
