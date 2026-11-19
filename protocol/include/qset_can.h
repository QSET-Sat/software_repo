#ifndef QSET_CAN_H
#define QSET_CAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QSET_CAN_MAX_DLC 8u
#define QSET_CAN_MAX_STANDARD_ID 0x7ffu

/* Stable values shared with the Rust protocol library. */
typedef enum {
    QSET_CAN_TIER_HEARTBEAT = 0,
    QSET_CAN_TIER_ERROR = 1,
    QSET_CAN_TIER_COMMAND = 2,
    QSET_CAN_TIER_TELEMETRY = 3
} qset_can_tier_t;

typedef enum {
    QSET_CAN_OK = 0,
    QSET_CAN_NULL_POINTER = -1,
    QSET_CAN_BAD_TIER = -2,
    QSET_CAN_BAD_NODE = -3,
    QSET_CAN_BAD_INDEX = -4,
    QSET_CAN_BAD_ID = -5,
    QSET_CAN_BAD_DLC = -6,
    QSET_CAN_BAD_VALUE = -7
} qset_can_status_t;

/* Registered node IDs. Must match the NODE_* constants and REGISTERED_NODES
 * in protocol/rust/src/common.rs and the node table in protocol/SPEC.md. */
typedef enum {
    QSET_CAN_NODE_OBC = 0x1,
    QSET_CAN_NODE_ADCS = 0x2,
    QSET_CAN_NODE_COMMS = 0x3,
    QSET_CAN_NODE_PAYLOAD = 0x4,
    QSET_CAN_NODE_BATT = 0x5,
    QSET_CAN_NODE_MPPT = 0x6
} qset_can_node_t;

/* Raw uint8_t state fields use these wire values. Other values are invalid. */
typedef enum {
    QSET_CAN_NODE_STATE_STARTING = 0,
    QSET_CAN_NODE_STATE_NOMINAL = 1,
    QSET_CAN_NODE_STATE_DEGRADED = 2,
    QSET_CAN_NODE_STATE_SAFE = 3,
    QSET_CAN_NODE_STATE_STOPPING = 4
} qset_can_node_state_t;

/* Raw uint8_t protocol-violation reason fields use these wire values. */
typedef enum {
    QSET_CAN_VIOLATION_REASON_UNASSIGNED_MESSAGE_INDEX = 1,
    QSET_CAN_VIOLATION_REASON_WRONG_DLC = 2,
    QSET_CAN_VIOLATION_REASON_INVALID_OR_RESERVED_FIELD_VALUE = 3,
    QSET_CAN_VIOLATION_REASON_UNAUTHORIZED_SENDER = 4
} qset_can_violation_reason_t;

/* Standard 11-bit Classic CAN data frame. The first dlc bytes are on wire;
 * data[dlc..8] is ignored on receive. Hardware-specific flags stay in C. */
typedef struct {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[QSET_CAN_MAX_DLC];
} qset_can_frame_t;

/* Message fields use raw fixed-width integers across the C/Rust ABI. */
typedef struct {
    uint8_t source_node;
    uint8_t state;
} qset_can_heartbeat_t;

typedef struct {
    uint8_t source_node;
    uint8_t reason;
    uint16_t offending_id;
    uint8_t observed_dlc;
} qset_can_protocol_violation_t;

typedef struct {
    uint8_t destination_node;
    uint8_t sequence;
} qset_can_request_status_t;

typedef struct {
    uint8_t source_node;
    uint8_t sequence;
    uint8_t state;
    uint32_t uptime_s;
    uint8_t active_faults; /* 0..254, or 0xFF when unavailable. */
} qset_can_node_status_t;

/* All functions return QSET_CAN_OK or a negative QSET_CAN_* status. A null
 * pointer returns QSET_CAN_NULL_POINTER before other validation. Other
 * pointers must address valid, correctly aligned objects. Every output is
 * unchanged on failure, including padding bytes in message structs. */

/* Create a standard ID. tier is a numeric QSET_CAN_TIER_* value; node must be
 * registered and index must fit its tier. Writes out_id only on success. */
int32_t qset_can_make_id(uint8_t tier, uint8_t node, uint8_t index, uint16_t *out_id);

/* Parse a standard ID into tier, node, and index. All three outputs are
 * required and are written together only on success. */
int32_t qset_can_parse_id(uint16_t id, uint8_t *out_tier, uint8_t *out_node, uint8_t *out_index);

/* Check only structural ID, registered-node, and DLC validity. Payloads and
 * assigned message indexes are checked by the message-specific decoders. */
int32_t qset_can_validate_frame(const qset_can_frame_t *frame);

/* Encode a version-1 heartbeat from a registered source and valid state.
 * Writes a DLC-2 frame with zeroed unused data bytes only on success. */
int32_t qset_can_encode_heartbeat(const qset_can_heartbeat_t *message, qset_can_frame_t *out_frame);

/* Decode a version-1 heartbeat with exact DLC 2, version 1, and valid state.
 * Writes out_message only on success; bytes beyond DLC are ignored. */
int32_t qset_can_decode_heartbeat(const qset_can_frame_t *frame, qset_can_heartbeat_t *out_message);

/* Encode error index 0 from a registered source. reason must be 1..4,
 * offending_id <= 0x7FF, and observed_dlc <= 15. Writes only on success. */
int32_t qset_can_encode_protocol_violation(const qset_can_protocol_violation_t *message,
                                           qset_can_frame_t *out_frame);

/* Decode error index 0 with exact DLC 4 and valid reason, offending ID, and
 * observed DLC. Ignores bytes beyond DLC; writes only on success. */
int32_t qset_can_decode_protocol_violation(const qset_can_frame_t *frame,
                                           qset_can_protocol_violation_t *out_message);

/* Encode command index 0 for a registered destination with sequence 1..255.
 * Writes a DLC-1 frame with zeroed unused data bytes only on success. */
int32_t qset_can_encode_request_status(const qset_can_request_status_t *message,
                                       qset_can_frame_t *out_frame);

/* Decode command index 0 with exact DLC 1 and nonzero sequence. Ignores bytes
 * beyond DLC and writes out_message only on success. */
int32_t qset_can_decode_request_status(const qset_can_frame_t *frame,
                                       qset_can_request_status_t *out_message);

/* Encode telemetry index 0 from a registered source and valid state. Sequence
 * 0 is unsolicited; active_faults 0xFF means unavailable. Writes only on success. */
int32_t qset_can_encode_node_status(const qset_can_node_status_t *message,
                                    qset_can_frame_t *out_frame);

/* Decode telemetry index 0 with exact DLC 7 and valid state. Sequence 0 and
 * active_faults 0xFF are accepted. Ignores bytes beyond DLC; writes only on success. */
int32_t qset_can_decode_node_status(const qset_can_frame_t *frame,
                                    qset_can_node_status_t *out_message);

#ifdef __cplusplus
}
#endif

#endif
