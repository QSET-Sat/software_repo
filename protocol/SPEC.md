# QSET Custom CAN protocol — implemented foundation

Status: **draft foundation**. This document is the source of truth for the
rules implemented in this repository. It incorporates the July 2026 QSET
Custom CAN Protocol draft, with the command and telemetry base constants
corrected to match their stated ranges. No message payload contract is yet
approved. Changes to IDs, node assignments, or payloads must update this
document, code, and tests in the same review.

## Frame contract

The application protocol uses **Classic CAN standard data frames**: an 11-bit
arbitration ID (`0x000..0x7ff`) and DLC `0..8`. The raw frame crossing the
C/Rust boundary has `uint16_t id`, `uint8_t dlc`, and `uint8_t data[8]`.
Only the first `dlc` data bytes are on the wire; other array bytes are ignored
on receive. A sender should initialize all eight array bytes to zero before
filling its payload. C drivers must filter extended, remote, and controller
error frames before passing a frame to this interface.

The current validator checks tier, registered node, 11-bit ID, and DLC. It
does **not** validate a message index assignment, payload length, data bytes,
sender identity, or timing. A structurally valid frame is not necessarily a
defined QSET message.

## Node registry

| Node ID | Assignment |
| --- | --- |
| `0x0` | Reserved; `0x000` is not an implemented broadcast message |
| `0x1` | OBC |
| `0x2` | EPS |
| `0x3` | ADCS |
| `0x4` | COMMS |
| `0x5` | THERMAL |
| `0x6..0xF` | Unassigned; rejected until added to the registry |

The active table is `REGISTERED_NODES` in the Rust library. Add or change a
node there, update this table, and rebuild all affected firmware. A registry
update must keep IDs unique and within `0x1..0xF`. The OBC is the master that
issues commands. The CAN ID format alone cannot prove which node sent a
command, since command IDs name their destination.

## Arbitration ID layout

Lower numeric IDs have higher CAN arbitration priority. Within the current
tiers, node ID occupies four bits and index occupies four bits where used.

| Tier | Range | Formula | Node means | Index |
| --- | --- | --- | --- | --- |
| Heartbeat | `0x000..0x00F` | `node` | Source | Must be zero |
| Error | `0x100..0x1FF` | `0x100 + node×0x10 + index` | Source | `0..15` |
| Command | `0x200..0x2FF` | `0x200 + node×0x10 + index` | Destination | `0..15` |
| Telemetry | `0x400..0x4FF` | `0x400 + node×0x10 + index` | Source | `0..15` |

The heartbeat tier has only one ID per node. For example, an ADCS telemetry
frame with index `2` has ID `0x432`. The ranges `0x010..0x0FF`,
`0x300..0x3FF`, and `0x500..0x7FF` are unassigned. Any ID with an
unregistered node is rejected, even if it lies inside a tier range. Error,
command, and telemetry indexes are accepted structurally, but no index has a
defined payload yet.

## C/Rust boundary

`protocol/include/qset_can.h` defines the shared ABI. The Rust static library
exports:

- `qset_can_make_id(tier, node, index, out_id)`
- `qset_can_parse_id(id, out_tier, out_node, out_index)`
- `qset_can_validate_frame(frame)`

All return `0` on success or a negative `QSET_CAN_*` status. Null pointers
return `QSET_CAN_NULL_POINTER`; other non-null pointers must address valid,
aligned objects. Output parameters remain unchanged on failure. C passes
numeric tier values from the header, not an enum object across the ABI.
Frame validation reports a bad ID or node before a bad DLC when both fail.
The API is independent of FreeRTOS and any CAN controller.

## Pending protocol definitions

No command, error, telemetry, or heartbeat **payload** is specified by this
foundation. The next approved message catalog must assign indexes, DLC,
field byte order, units/scaling, invalid values, and golden wire vectors.
Heartbeat timing, node states, timeout thresholds, retries, telemetry
deadbands, and command authorization are also future protocol decisions.
See `NEXT_STEPS.md` for the implementation sequence.
