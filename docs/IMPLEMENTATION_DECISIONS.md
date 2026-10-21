# CAN implementation decision register

Status: working decisions approved for the common protocol and host-side
implementation. Hardware-dependent values marked **Deferred** remain open and
must be resolved before the affected target integration is released.

This register complements `protocol/SPEC.md`. The protocol specification remains
the wire-format source of truth; this document records implementation policy,
runtime behavior, and explicit deferrals.

## Node registry and configuration

| Node ID | Assignment |
| ------- | ---------- |
| `0x1` | OBC |
| `0x2` | ADCS |
| `0x3` | COMMS |
| `0x4` | PAYLOAD |
| `0x5` | BATT |
| `0x6` | MPPT |
| `0x7..0xF` | Unassigned and rejected |

- Node IDs are selected at build time and cannot change at runtime.
- Each firmware target receives a generated local-node configuration constant.
- The complete registry remains in the protocol library for receive validation.
- A build fails when its local ID is missing, outside `0x1..0x6`, or inconsistent
  with the selected firmware target.
- Registry changes use normal software-team review and update the specification,
  code, and tests together.

## Version 1 message scope

Version 1 contains only the common protocol foundation:

1. Node heartbeat.
2. Protocol-violation error.
3. Request-status command.
4. Node-status telemetry.

Subsystem-specific commands and telemetry are deferred until the common system
is proven on hardware.

### Heartbeat

- DLC is exactly 2.
- Byte 0 is protocol version, initially `0x01`.
- Byte 1 is node state.
- The version byte is retained for future compatibility even though firmware
  updates are not an early milestone.

Approved node-state values:

| Value | State |
| ----- | ----- |
| `0x00` | Starting |
| `0x01` | Nominal |
| `0x02` | Degraded |
| `0x03` | Safe |
| `0x04` | Stopping or resetting |
| `0x05..0xFE` | Reserved and rejected |
| `0xFF` | Invalid/unavailable and rejected |

### Protocol-violation error

- Error index is 0 and DLC is exactly 4.
- Byte 0 is the reason.
- Bytes 1 and 2 are the offending 11-bit CAN ID, unsigned little-endian.
- Byte 3 is the observed DLC.

Approved initial reasons:

| Value | Reason |
| ----- | ------ |
| `0x01` | Unassigned message index |
| `0x02` | Wrong DLC |
| `0x03` | Invalid or reserved field value |
| `0x04` | Unauthorized sender; defined but not emitted in version 1 |

All other reason values are reserved and rejected.

### Request status

- Command index is 0 and DLC is exactly 1.
- The CAN ID names the destination.
- Only the OBC application is permitted to transmit command-tier frames.
- Byte 0 is a request sequence in `1..255`.
- Sequence 0 is invalid for a request and is reserved for unsolicited status.

### Node status

- Telemetry index is 0 and DLC is exactly 7.
- Byte 0 is the echoed request sequence, or 0 for unsolicited status.
- Byte 1 is the node state.
- Bytes 2 through 5 are unsigned 32-bit uptime seconds, little-endian.
- Byte 6 is active fault count; `0..254` is a count and `0xFF` is unavailable.

Uptime is monotonic from processor/application runtime reset, wraps naturally
at `2^32`, and is not affected by wall-clock changes. Active fault count is the
number of distinct currently active faults, not occurrence count. Repeated
occurrences do not increase it, recovered faults leave the count after their
stability requirement is met, and values above 254 saturate at 254. Historical
occurrence counters remain separate diagnostics.

## Common codec rules

- Every defined message requires its exact DLC.
- Multi-byte integers are unsigned and little-endian unless a future message
  explicitly specifies otherwise.
- Reserved fields are transmitted as zero and rejected when nonzero.
- Unassigned enumeration values and message indexes are rejected.
- Structural ID parsing remains separate from message decoding.
- Receive bytes beyond DLC are ignored; senders initialize the complete data
  array to zero.

The specification keeps one readable canonical vector per message, using ADCS
consistently in the examples. Automated conformance tests additionally cover
every registered node, valid state and reason, boundary values, invalid DLCs,
reserved values, unknown indexes, invalid IDs, and Rust/C round trips.

## Existing API and codec extension

- The existing ID creation, ID parsing, and structural frame-validation calls
  retain their current behavior.
- `qset_can_validate_frame` remains structural and does not perform message
  payload validation.
- Payload codecs extend the existing Rust library where the target supports it.
- Each message has its own C-compatible structure and encode/decode functions.
- Functions use the existing pointer and negative `int32_t` status convention.
- Output values remain unchanged on failure.
- Existing index and DLC errors are reused; a payload-value error is added for
  invalid versions, states, reasons, sequences, and future field values.

## Request correlation and retry

- The OBC maintains a separate sequence counter per destination, from 1 through
  255 and wrapping to 1.
- Only one status request may be outstanding per destination. Requests to
  different nodes may be outstanding concurrently.
- Responses match on source node plus sequence. Sequence 0 is unsolicited.
- Request handling is nonblocking. A stuck node never blocks communication with
  other nodes.
- A valid duplicate request produces another response from the current health
  snapshot. The OBC accepts the first matching response and counts later ones.
- Late and unexpected responses are ignored for correlation but counted.
- A timeout is a local OBC communication fault, not a protocol violation.
- The provisional response timeout is 500 ms. The OBC makes two retries with
  the same sequence, for three total attempts, then fails the request and clears
  its outstanding slot.
- CAN hardware retransmission handles transient link errors. Heartbeats, errors,
  status responses, and unsolicited status are not retried by the application.

## Hardware and bus boundary

- Confirmed processor candidates:
  - OBC: `SPC56EL70L5CBOSR`.
  - ADCS: `STM32L496RGT6`.
- COMMS processing hardware may involve an STM32H7 and FPGA, but the exact
  arrangement is deferred.
- PAYLOAD, BATT, and MPPT processors are deferred.
- Built-in CAN controllers are preferred but remain subject to target review.
- CAN transceivers are deferred.
- Classic CAN nominal bitrate is 1 Mbit/s.
- The system has a primary linear bus and a backup linear bus. Each bus uses
  120-ohm end termination; exact physical implementation is owned by hardware.
- Version 1 actively uses only the primary bus. The C adapter includes a bus
  channel identifier from the beginning, but failover and simultaneous dual-bus
  transmission are deferred.
- Protocol frames do not encode physical bus identity; Rust codecs are
  bus-agnostic.

## Rust target and panic policy

- Whether Rust links into every flight target is deferred until toolchain
  feasibility is proven, especially for the SPC56.
- If a target cannot link the Rust library, an equivalent C implementation may
  be considered and must pass the same golden/conformance vectors.
- Invalid input returns status errors and must never panic.
- Rust never unwinds through C.
- Final firmware owns the panic/fault handler and records a diagnostic when
  possible before a controlled reset or watchdog reset.
- The current infinite-spin handler is only a temporary build fallback and is
  removed or feature-gated during board integration.

## C and FreeRTOS ownership

Approved data flow:

```text
CAN controller -> C ISR -> C RX queue -> C RX task -> protocol validation/decoding
protocol encoding -> C TX queue -> C TX task -> CAN controller
```

- C owns the hardware driver, ISR, FreeRTOS queues/tasks, and bus selection.
- Rust owns protocol IDs, validation, and codecs on supported targets.
- Rust does not call FreeRTOS, access hardware, allocate memory, or block.
- C preserves frame ID, DLC, and transmitted payload bytes exactly.
- Queue-full and driver failures are observable and never silently discarded.
- The exact FreeRTOS kernel, port, compiler, and HAL are selected after target
  hardware/SDK confirmation. Prefer one common, pinned FreeRTOS LTS version
  wherever supported; isolate target differences behind the C adapter.

## RX, TX, and filtering policy

RX:

- The ISR copies complete frames into a bounded queue with the appropriate
  ISR-safe FreeRTOS API.
- A full queue drops the newly received frame, increments a counter, and latches
  an observable diagnostic.
- Queue items carry C-only receive timestamp and bus-channel metadata without
  changing `qset_can_frame_t`.
- The RX task performs structural validation, message decoding, and routing.

TX:

- Callers enqueue complete frames without blocking. Successful enqueue means
  accepted for transmission, not delivered to the destination.
- Queue-full returns an immediate error and increments a counter.
- One TX task owns normal controller transmission.
- Pending frames select the lowest CAN ID first; equal IDs retain FIFO order.
- A request is delivered only when its correlated response is received.

Initial acceptance filters:

- Reject extended, remote, and controller-error frames before the Rust boundary.
- Accept standard data frames in `0x000..0x00F`, `0x100..0x1FF`,
  `0x200..0x2FF`, and `0x400..0x4FF`, including values that software may reject.
- Reject `0x010..0x0FF`, `0x300..0x3FF`, and `0x500..0x7FF`.
- Narrower target-specific filters require measured justification.

## Controller errors and recovery

- Warning, error-passive, controller overrun, and RTOS queue-overflow events are
  counted separately.
- Communication continues while the controller permits it.
- Bus-off stops TX submissions and places the node in Degraded state.
- The C adapter performs controlled reset and reinitialization after a
  configurable delay.
- A bounded set of fast attempts is followed by slower persistent attempts.
- Stable recovery clears the active communication fault but retains history.
- Hardware errors do not generate application protocol-violation frames.
- Backup-bus switching is deferred.

## Heartbeat, health, and state policy

- Every node sends a heartbeat once per second from successful CAN
  initialization onward and continues in every state.
- The initial heartbeat state is Starting.
- Node-ID-based phase offsets avoid synchronized transmission.
- The OBC monitors every subsystem; every subsystem monitors the OBC.
- Subsystems need not monitor one another in version 1.
- Three missed heartbeats declare a node missing. Startup has a ten-second grace
  period. Recovery waits for multiple good heartbeats to avoid flapping.
- A single health-manager task owns published state and the active-fault registry.
- Starting becomes Nominal after required initialization succeeds.
- Recoverable capability loss changes Nominal to Degraded. Recovery to Nominal
  requires a stable clear interval.
- A locally detected critical fault may enter Safe from any state.
- Exact Safe behavior is subsystem-specific and deferred.
- Stopping is published before controlled reset or shutdown when communication
  remains available.
- Critical tasks report liveness. The hardware watchdog is serviced only while
  all required critical tasks are healthy. Noncritical failure may instead cause
  Degraded state.
- After reset, the node returns to Starting and retains reset reason when the
  hardware permits.

Status telemetry is sent for every valid status request and once, with sequence
0, when node state changes. Rapid state changes are coalesced. There is no
periodic status telemetry in version 1 because heartbeat already supplies
periodic state.

## Protocol-error ownership and authorization

- Only a command's addressed destination reports a malformed command.
- Only the OBC reports malformed heartbeat, error, telemetry, or otherwise
  unaddressable traffic.
- Malformed protocol-error frames are counted locally and never cause another
  protocol-error frame.
- Duplicate violations are limited to one report per reason/offending-ID pair
  per second, with a global maximum of five reports per second per node.
- Suppressed counts remain diagnostic data. Errors are not queued without bound
  while the bus is unavailable.
- Version 1 uses a trusted-bus command policy. Only OBC application code emits
  commands, but receivers cannot authenticate the sender from the current ID.
  Wire-level authentication is deferred.

## Local diagnostics

- The C transport maintains a fixed-size, statically allocated diagnostic
  structure with saturating counters, current bus state, and latest driver error.
- A nonblocking getter returns a consistent snapshot.
- Reading does not clear counters. Counters reset only with the processor or a
  future explicit maintenance operation.
- Version 1 transmits only aggregate active-fault count; detailed diagnostic
  telemetry is deferred.

## Verification and release criteria

- `make test` passes on macOS and Linux.
- Rust format, lint, and `no_std` checks pass; C warnings are errors.
- Rust tests cover every approved valid and invalid conformance vector.
- Canonical vectors cross the linked C/Rust boundary.
- Boundary-value encode/decode round trips pass.
- Fuzz/property tests show arbitrary input cannot panic or violate memory safety.
- Every selected target compiles and links.
- At least two real CAN nodes communicate at 1 Mbit/s and preserve exact ID, DLC,
  and data.
- Hardware tests cover concurrent requests, timeouts, duplicates, late frames,
  overflow, malformed traffic, rate limiting, bus-off, recovery, and watchdog.
- Sustained-load testing finalizes queue depths, task priorities, stack sizes,
  and timing constants.
- Mission-specific messages remain disabled until the common system passes.

## Deferred decision register

The following items are explicitly unresolved:

1. Exact COMMS processor/FPGA architecture and CAN controller.
2. PAYLOAD, BATT, and MPPT processors/controllers.
3. CAN transceiver for each board.
4. MCU clocks and exact CAN bit timing for 1 Mbit/s.
5. Exact compiler, linker, vendor HAL/SDK, FreeRTOS version, and FreeRTOS port
   for each target. Selection waits for target hardware/SDK confirmation and
   prefers a common pinned LTS version wherever supported.
6. Rust static-library feasibility on every target, especially SPC56.
7. Final RX/TX queue depths, task priorities, and stack sizes.
8. Bus-off recovery delays, attempt counts, slow-retry interval, and recovery
   stability duration.
9. Exact heartbeat phase offsets and good-heartbeat recovery count.
10. Health-fault recovery stability interval.
11. Exact Safe-mode behavior per subsystem.
12. Hardware-watchdog timeout and persistent reset-reason storage.
13. Primary/backup physical channel mapping per board.
14. Backup-bus failure detection and switching policy.
15. Electrical integration: termination placement, wiring, connectors, lengths,
    stubs, and grounding.
16. Wire-level command authentication.
17. Firmware-update and mixed-protocol-version compatibility policy.
18. All subsystem-specific commands and telemetry.

These deferrals do not block message-codec, C ABI, host-test, mock-transport,
request-correlation, health-state, or local-diagnostics implementation. They do
block the relevant target-specific integration and final flight release.
