# Next steps for QSET C-CAN

For assignable team work, see [the issue backlog](docs/ISSUE_BACKLOG.md).

The host foundation is complete when `make test` passes and the spec and C
boundary match. The items below are ordered gates for later work; they do not
silently define missing spacecraft behavior.

1. **Approve the first message catalog.** For each tier/index and node, record
   purpose, source/destination, required DLC, every byte/bit, endianness,
   units/scaling, reserved bits, and invalid-value handling in `protocol/SPEC.md`.
   Resolve whether heartbeat carries a payload. Add fixed golden wire vectors.
2. **Implement production payload codecs in Rust.** Add typed messages,
   encode/decode and validation functions, then C-facing calls only where the
   FreeRTOS application needs them. Reject undefined indexes at the message
   decoding layer while keeping structural ID parsing separate. Run golden
   vectors through both Rust and C-linked tests.
3. **Finalize hardware and node registry.** Record the MCU, CAN controller,
   transceiver, clock, bitrate, physical topology, assigned boards/nodes,
   toolchain, and FreeRTOS port. Update the versioned node table and firmware
   build targets. Node ID changes require spec and test updates in the same PR.
4. **Implement the C FreeRTOS transport.** Follow `docs/FREERTOS_DESIGN.md`:
   platform adapter, RX ISR/queue/task, TX queue/task, bounded error reporting,
   and hardware filters. Link the Rust static library for the chosen target.
   Prove raw frame preservation and real two-node transmit/receive behavior.
5. **Add protocol services with approved policies.** Define and implement
   heartbeat period/node states/timeouts, error reporting, command routing
   and authorization, telemetry scheduling/deadbands, and any retries. Set
   queue sizes and priorities from measured bus load and task timing.

The July 2026 external DOCX is historical input. `protocol/SPEC.md`, code,
and tests should move together in reviewed changes. The onboarding lab's
generic sensor packet remains separate from the flight protocol.
