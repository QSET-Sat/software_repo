# QSET Custom CAN protocol — implemented foundation

Status: **draft foundation**. This document is the source of truth for the
rules implemented in this repository. It incorporates the July 2026 QSET
Custom CAN Protocol draft, with the command and telemetry base constants
corrected to match their stated ranges. The first message catalog below is a
review candidate; it is not implemented or approved yet. Changes to IDs, node
assignments, or approved payloads must update this document, code, and tests in
the same review.

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
unregistered node is rejected, even if it lies inside a tier range. The
foundation parser accepts error, command, and telemetry indexes structurally.
The proposed catalog below assigns index `0` in each of those tiers; a future
message decoder will reject all other indexes until they are assigned.

## Proposed message catalog

Status: **candidate for subteam review**. These definitions are complete
enough to implement, but they do not become the production contract until the
OBC and affected subsystem teams approve them. Issue 4 will add codecs only
after that approval.

Every catalog decoder must require the exact DLC shown below. Bytes outside
the DLC are not part of the frame and remain ignored. All multi-byte integers
are unsigned and little-endian. Fields marked reserved must be transmitted as
zero and rejected when nonzero. Enumerated values not explicitly assigned are
rejected unless the field definition says otherwise.

### Common node-state field

The heartbeat and status telemetry use the same one-byte node state:

| Value | Meaning |
| --- | --- |
| `0x00` | Starting: initialization is not complete |
| `0x01` | Nominal: normal application work is enabled |
| `0x02` | Degraded: operating with a known loss of capability |
| `0x03` | Safe: only functions allowed by the safe-state policy are enabled |
| `0x04` | Stopping: an orderly shutdown or reset is in progress |
| `0x05..0xFE` | Reserved; reject |
| `0xFF` | Invalid or unavailable; reject |

This table defines the wire values, not the transitions, authority, or action
associated with each state. Those service policies remain a separate review.

### Heartbeat: node heartbeat

- **ID:** the registered source node ID (`0x001..0x005` currently); heartbeat
  has no separate message index.
- **Sender / consumers:** every registered node sends its own heartbeat; all
  nodes and diagnostic tools may consume it.
- **Purpose:** identify a live node and expose its protocol version and current
  state without carrying node-specific telemetry.
- **DLC:** exactly `2`.

| Byte | Field | Encoding, units, and validity |
| --- | --- | --- |
| `0` | Protocol version | `0x01` for this catalog; `0x00` is invalid and `0x02..0xFF` are reserved until assigned |
| `1` | Node state | Common node-state value above |

Golden vector: an EPS heartbeat in nominal state is ID `0x002`, DLC `2`, data
`01 01`.

### Error index 0: protocol violation

- **ID:** `0x100 + source_node*0x10 + 0`.
- **Sender / consumers:** any registered node that detects the violation sends
  it; the OBC and diagnostic listeners consume it.
- **Purpose:** report a rejected application-protocol frame without defining
  subsystem-specific fault codes.
- **DLC:** exactly `4`.

| Byte(s) | Field | Encoding, units, and validity |
| --- | --- | --- |
| `0` | Reason | `0x01` unassigned message index; `0x02` wrong DLC; `0x03` invalid or reserved field value; `0x04` unauthorized sender; `0x00` and `0x05..0xFF` are reserved and rejected |
| `1..2` | Offending arbitration ID | Unsigned 11-bit standard ID, little-endian; values above `0x07FF` are invalid |
| `3` | Observed DLC | Raw DLC value `0..15`; `9..15` explicitly report a non-Classic-CAN length and `16..255` are invalid |

Golden vector: EPS reporting wrong DLC (`0x02`) for command ID `0x220` with
observed DLC `9` is ID `0x120`, DLC `4`, data `02 20 02 09`.

### Command index 0: request status

- **ID:** `0x200 + destination_node*0x10 + 0`.
- **Sender / destination:** the OBC sends the command to the node named by the
  ID. Other senders are unauthorized.
- **Purpose:** request one status telemetry response and correlate that response
  without prescribing periodic telemetry behavior.
- **DLC:** exactly `1`.

| Byte | Field | Encoding, units, and validity |
| --- | --- | --- |
| `0` | Request sequence | `1..255`; `0` is reserved for unsolicited status telemetry and is invalid in this command |

Golden vector: OBC requesting ADCS status with sequence `42` is ID `0x230`,
DLC `1`, data `2A`.

### Telemetry index 0: node status

- **ID:** `0x400 + source_node*0x10 + 0`.
- **Sender / consumers:** every registered node sends its own status; the OBC
  and diagnostic listeners consume it.
- **Purpose:** answer a request-status command or provide an unsolicited basic
  health snapshot. The transmission schedule is not defined here.
- **DLC:** exactly `7`.

| Byte(s) | Field | Encoding, units, and validity |
| --- | --- | --- |
| `0` | Request sequence | Echo `1..255` from the triggering request; `0` means unsolicited status |
| `1` | Node state | Common node-state value above |
| `2..5` | Uptime | Seconds since the node application started, unsigned 32-bit little-endian; wraps modulo `2^32` and has no invalid sentinel |
| `6` | Active fault count | `0..254`; `0xFF` means unavailable |

Golden vector: ADCS replying to sequence `42`, nominal after `86400` seconds,
with no active faults is ID `0x430`, DLC `7`, data
`2A 01 80 51 01 00 00`.

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

## Approval and remaining protocol definitions

Before this catalog is approved, the OBC, EPS, ADCS, COMMS, and THERMAL teams
must confirm that these four messages are the minimum common set they need and
that every field can be produced and consumed as written. Approval should be
recorded in the catalog review; until then, deployed software must not rely on
these payloads.

Heartbeat period, timeout thresholds, state-transition policy, error response,
command authentication/enforcement, telemetry schedules/deadbands, and retry
behavior remain future protocol decisions. See `NEXT_STEPS.md` for the
implementation sequence.
