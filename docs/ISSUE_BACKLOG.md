# CAN project board: issue drafts

These are ready to copy into GitHub issues. Check existing issues before
posting to avoid duplicates. Put each issue on the team project board and let
members assign themselves. Use **Ready**, **In progress**, **In review**, and
**Done** as board states. Keep dependencies in the issue body rather than
assigning everyone one long checklist.

The foundation code and documents in this local checkout must be reviewed and
pushed before the team can follow repository links below.

## 1. Review and merge the host CAN protocol foundation

Suggested label: `can-protocol` · Ready now

Review the Rust ID/frame library, C ABI, `protocol/SPEC.md`, host tests, and
FreeRTOS design. Create a PR from the current local changes and merge after
the team verifies `make test` on macOS or Linux. Confirm that the spec agrees
with the July draft, especially the corrected `0x200` command and `0x400`
telemetry bases. This issue closes when CI passes and the foundation is on
the shared branch.

## 2. Approve the first CAN message catalog

Suggested label: `protocol-design` · Ready after issue 1

Choose the first actual heartbeat, error, command, and telemetry messages the
team needs. For every assigned index, document sender/destination, purpose,
DLC, each field/bit, byte order, units/scaling, reserved and invalid values,
and at least one golden wire vector in `protocol/SPEC.md`. Resolve whether
heartbeat carries a payload. Review the catalog with the affected subteams.
This issue closes when the catalog is approved and no production payload rule
is left implicit.

## 3. Confirm board target and node registry

Suggested label: `hardware-integration` · Ready after issue 1; can run alongside issue 2

Record the selected MCU, CAN peripheral, transceiver, clocks, intended bus
bitrate/topology, compiler target, vendor SDK, and FreeRTOS port. List the
actual boards and their node IDs; update the registry in Rust and
`protocol/SPEC.md` together. This issue closes when hardware and software
leads agree on the target and node table, and ID tests pass after any change.

## 4. Implement the approved payload codecs in Rust

Suggested label: `rust-implementation` · Blocked by issue 2

Add typed `no_std` encode/decode and payload validation for the approved
messages. Keep structural ID parsing separate from message assignment checks.
Add Rust golden-vector tests and C boundary calls/tests where the firmware
needs them. This issue closes when valid vectors round-trip exactly, malformed
payloads fail as specified, and `make test` passes.

## 5. Implement and test the C FreeRTOS CAN receive path

Suggested label: `rust-implementation` · Blocked by issue 3

Using `docs/FREERTOS_DESIGN.md`, add the board CAN initialization/filtering,
bounded RX ISR handoff, queue, and RX task. Pass standard data frames to the
Rust boundary without changing ID, DLC, or on-wire data. Expose queue-full,
bus, and driver errors. This issue closes with target build success and an
on-board or hardware-in-loop receive test, including malformed and filtered
frame cases.

## 6. Implement and test the C FreeRTOS CAN transmit path

Suggested label: `freertos-implementation` · Blocked by issue 3; can run alongside issue 5

Add a bounded TX request queue, task, and controller adapter. Define how
callers learn about queue-full, controller failure, and completion. Keep
retry/bus-off behavior limited to policy approved for the selected hardware.
This issue closes with target build success and a real two-node transmit test
that verifies ID, DLC, and payload bytes.

## 7. Specify heartbeat, error, command, and telemetry service policies

Suggested label: `protocol-design` · Ready after issue 1; can run alongside issues 2 and 3

Agree on heartbeat period and node states, monitor timeout, emergency/error
handling, command routing and sender authority, telemetry schedule/deadbands,
and any retry policy. Record timing and failure behavior in `protocol/SPEC.md`
with tests or scenarios that will prove each rule. This issue closes with
reviewed policy decisions. Implementation issues should be opened from this
spec once the relevant codec and transport work is ready.
