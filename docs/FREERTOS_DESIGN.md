# C transport and planned FreeRTOS architecture

The hardware-independent controller contract and host mock are implemented and
compiled by `make test`. The board/FreeRTOS integration remains a design for a
later milestone. The MCU, controller, vendor SDK, task sizes/priorities, queue
depths, and interrupt rules will be fixed after board selection.

## Implemented portable boundary

- `qset_can_transport.h` defines allocation-free controller instances bound to
  either the primary or backup bus. It exposes nonblocking initialization,
  start/stop, raw RX/TX, controller-state and latest-fault queries, and an
  asynchronous bus-off recovery request.
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
  synchronization; the portable wrapper and mock contain no locks.

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
4. Test RX, TX, queue overflow, controller errors, and recovery with a real
   second CAN node before enabling mission messages.
