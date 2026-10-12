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
    QSET_CAN_BAD_DLC = -6
} qset_can_status_t;

/* Standard 11-bit Classic CAN data frame. The first dlc bytes are on wire;
 * data[dlc..8] is ignored on receive. Hardware-specific flags stay in C. */
typedef struct {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[QSET_CAN_MAX_DLC];
} qset_can_frame_t;

/* The Rust implementation uses int32_t status codes. Outputs are unchanged
 * on error. Non-null pointers must point to valid, aligned storage.
 * Pass the numeric QSET_CAN_TIER_* value as tier. */
int32_t qset_can_make_id(uint8_t tier, uint8_t node, uint8_t index, uint16_t *out_id);
int32_t qset_can_parse_id(uint16_t id, uint8_t *out_tier, uint8_t *out_node, uint8_t *out_index);
int32_t qset_can_validate_frame(const qset_can_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif
