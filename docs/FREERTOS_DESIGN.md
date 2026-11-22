# C transport and planned FreeRTOS architecture

The hardware-independent controller contract and host mock are implemented and
compiled by `make test`. The board/FreeRTOS integration remains a design for a
later milestone. The MCU, controller, vendor SDK, task sizes/priorities, queue
depths, and interrupt rules will be fixed after board selection.

## Implemented portable boundary

- `qset_can_transport.h` defines allocation-free controller instances bound to
  either the primary or backup bus. It exposes nonblocking initialization,
  start/stop, raw RX/TX, controller-state and latest-fault queries, an
  asynchronous bus-off recovery request, and a diagnostics getter.
- RX controller overruns are counted by the vendor adapter and collected by the
  transport. The adapter increments its own saturating count for every
  overrun event, normally in its ISR, and implements `take_rx_overruns`, which
  returns and resets that count. The transport calls it on every receive and
  adds the result to its saturating `rx_controller_overruns` counter. Reading
  diagnostics never clears them.
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
  exception is inside each vendor adapter: `take_rx_overruns` must read and
  reset its count atomically with respect to its own ISR, for example by
  masking the CAN interrupt, so no overrun is lost.

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

## Implementing RX overrun reporting in a board adapter

An RX controller overrun happens when a frame arrives while the controller's
hardware receive buffer is still full, so the hardware drops a frame before
software can copy it out. Only the board adapter can see this, so each adapter
counts overruns itself and the portable transport collects the count through
`take_rx_overruns`. The adapter never includes or writes transport diagnostics.

What the adapter must do:

1. **Find the overrun indication in the chip's reference manual.** Most CAN
   controllers have a receive FIFO or mailbox overrun status flag, often with
   an interrupt enable. Enable that interrupt, or check the flag in the
   existing receive ISR.
2. **Count events, not frames.** Many controllers only report that at least
   one frame was lost, not how many. Add one to the count each time the flag
   is seen set, then clear the flag as the manual describes so the next
   overrun is detected. Do not try to estimate the number of lost frames.
3. **Keep a count, not a yes/no flag.** Several overruns between two transport
   polls must all be counted. Saturate at `UINT32_MAX`; never wrap.
4. **Make read-and-reset atomic against the ISR.** Between reading the count
   and setting it to zero, the ISR must not run, or an overrun arriving in
   between is lost. Mask the CAN interrupt (or use the RTOS's critical-section
   API) around those two steps only, and keep the section short.
5. **Report failure without losing the count.** If the callback cannot read
   the count, return an error and leave both `*out_count` and the stored count
   unchanged. The transport adds nothing and picks it up on the next call.
6. **Do not report overruns through `get_last_fault` only.** Latching
   `QSET_CAN_DRIVER_FAULT_RX_OVERRUN` there is still allowed for the latest
   fault query, but the latched fault is not a counter.

Chip-neutral example. `board_can_*` stands for the board's own register access
or vendor HAL calls:

```c
typedef struct {
    /* volatile: written by the ISR, read by task code. */
    volatile uint32_t rx_overruns;
    /* ...other adapter state... */
} board_can_context_t;

/* Called from the board's CAN receive/error interrupt handler. */
static void board_can_handle_rx_overrun(board_can_context_t *ctx)
{
    if (board_can_rx_overrun_flag_is_set()) {
        if (ctx->rx_overruns != UINT32_MAX) {
            ctx->rx_overruns++;
        }
        board_can_clear_rx_overrun_flag();
    }
}

/* Ops-table callback, called by the transport from task context. */
static qset_can_driver_result_t board_can_take_rx_overruns(void *context,
                                                           uint32_t *out_count)
{
    board_can_context_t *ctx = (board_can_context_t *)context;
    uint32_t count;

    if (ctx == 0 || out_count == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    board_can_mask_interrupt();
    count = ctx->rx_overruns;
    ctx->rx_overruns = 0u;
    board_can_unmask_interrupt();

    *out_count = count;
    return QSET_CAN_DRIVER_OK;
}
```

Register the callback as `.take_rx_overruns = board_can_take_rx_overruns` in
the adapter's `qset_can_driver_ops_t`. Init rejects an operations table that
leaves it out. `mock_take_rx_overruns` in `protocol/src/qset_can_mock.c` is the
host reference for the same contract; it has no interrupt to mask.

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
4. Test RX, TX, queue overflow, RX controller overrun counting, controller
   errors, and recovery with a real second CAN node before enabling mission
   messages.
