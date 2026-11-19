#include "qset_can.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(qset_can_heartbeat_t) == 2, "heartbeat size changed");
_Static_assert(offsetof(qset_can_heartbeat_t, source_node) == 0, "heartbeat source offset changed");
_Static_assert(offsetof(qset_can_heartbeat_t, state) == 1, "heartbeat state offset changed");
_Static_assert(sizeof(qset_can_protocol_violation_t) == 6, "violation size changed");
_Static_assert(offsetof(qset_can_protocol_violation_t, source_node) == 0, "violation source offset changed");
_Static_assert(offsetof(qset_can_protocol_violation_t, reason) == 1, "violation reason offset changed");
_Static_assert(offsetof(qset_can_protocol_violation_t, offending_id) == 2, "violation ID offset changed");
_Static_assert(offsetof(qset_can_protocol_violation_t, observed_dlc) == 4, "violation DLC offset changed");
_Static_assert(sizeof(qset_can_request_status_t) == 2, "request size changed");
_Static_assert(offsetof(qset_can_request_status_t, destination_node) == 0, "request destination offset changed");
_Static_assert(offsetof(qset_can_request_status_t, sequence) == 1, "request sequence offset changed");
_Static_assert(sizeof(qset_can_node_status_t) == 12, "status size changed");
_Static_assert(offsetof(qset_can_node_status_t, source_node) == 0, "status source offset changed");
_Static_assert(offsetof(qset_can_node_status_t, sequence) == 1, "status sequence offset changed");
_Static_assert(offsetof(qset_can_node_status_t, state) == 2, "status state offset changed");
_Static_assert(offsetof(qset_can_node_status_t, uptime_s) == 4, "status uptime offset changed");
_Static_assert(offsetof(qset_can_node_status_t, active_faults) == 8, "status fault offset changed");

_Static_assert(QSET_CAN_NODE_STATE_STARTING == 0 && QSET_CAN_NODE_STATE_NOMINAL == 1 &&
               QSET_CAN_NODE_STATE_DEGRADED == 2 && QSET_CAN_NODE_STATE_SAFE == 3 &&
               QSET_CAN_NODE_STATE_STOPPING == 4, "state values changed");
_Static_assert(QSET_CAN_VIOLATION_REASON_UNASSIGNED_MESSAGE_INDEX == 1 &&
               QSET_CAN_VIOLATION_REASON_WRONG_DLC == 2 &&
               QSET_CAN_VIOLATION_REASON_INVALID_OR_RESERVED_FIELD_VALUE == 3 &&
               QSET_CAN_VIOLATION_REASON_UNAUTHORIZED_SENDER == 4, "reason values changed");

static void assert_frame(const qset_can_frame_t *actual, const qset_can_frame_t *expected)
{
    assert(actual->id == expected->id);
    assert(actual->dlc == expected->dlc);
    assert(memcmp(actual->data, expected->data, sizeof(actual->data)) == 0);
}

/* Check both pointer positions and compare every output byte, including padding,
 * after failures. The valid and invalid cases are supplied by each codec test. */
#define CHECK_FAILURES(name, type, good, bad, good_frame, bad_frame, encode_error, decode_error) do { \
    qset_can_frame_t output_frame; \
    type output_message; \
    unsigned char frame_before[sizeof(output_frame)]; \
    unsigned char message_before[sizeof(output_message)]; \
    memset(&output_frame, 0xa5, sizeof(output_frame)); \
    memcpy(frame_before, &output_frame, sizeof(output_frame)); \
    assert(qset_can_encode_##name(NULL, &output_frame) == QSET_CAN_NULL_POINTER); \
    assert(memcmp(&output_frame, frame_before, sizeof(output_frame)) == 0); \
    assert(qset_can_encode_##name(&(good), NULL) == QSET_CAN_NULL_POINTER); \
    assert(qset_can_encode_##name(NULL, NULL) == QSET_CAN_NULL_POINTER); \
    assert(qset_can_encode_##name(&(bad), &output_frame) == (encode_error)); \
    assert(memcmp(&output_frame, frame_before, sizeof(output_frame)) == 0); \
    memset(&output_message, 0xa5, sizeof(output_message)); \
    memcpy(message_before, &output_message, sizeof(output_message)); \
    assert(qset_can_decode_##name(NULL, &output_message) == QSET_CAN_NULL_POINTER); \
    assert(memcmp(&output_message, message_before, sizeof(output_message)) == 0); \
    assert(qset_can_decode_##name(&(good_frame), NULL) == QSET_CAN_NULL_POINTER); \
    assert(qset_can_decode_##name(NULL, NULL) == QSET_CAN_NULL_POINTER); \
    assert(qset_can_decode_##name(&(bad_frame), &output_message) == (decode_error)); \
    assert(memcmp(&output_message, message_before, sizeof(output_message)) == 0); \
} while (0)

static void test_message_codecs(void)
{
    qset_can_frame_t frame;

    qset_can_heartbeat_t hb = {QSET_CAN_NODE_ADCS, QSET_CAN_NODE_STATE_NOMINAL};
    qset_can_frame_t hb_golden = {.id = 0x002, .dlc = 2, .data = {0x01, 0x01}};
    assert(qset_can_encode_heartbeat(&hb, &frame) == QSET_CAN_OK);
    assert_frame(&frame, &hb_golden);
    qset_can_heartbeat_t hb_out = {0};
    assert(qset_can_decode_heartbeat(&hb_golden, &hb_out) == QSET_CAN_OK);
    assert(hb_out.source_node == hb.source_node && hb_out.state == hb.state);
    qset_can_heartbeat_t hb_bad = hb;
    hb_bad.state = 5;
    qset_can_frame_t hb_bad_frame = hb_golden;
    hb_bad_frame.data[0] = 2;
    CHECK_FAILURES(heartbeat, qset_can_heartbeat_t, hb, hb_bad, hb_golden, hb_bad_frame,
                   QSET_CAN_BAD_VALUE, QSET_CAN_BAD_VALUE);

    qset_can_protocol_violation_t violation = {
        .source_node = QSET_CAN_NODE_ADCS,
        .reason = QSET_CAN_VIOLATION_REASON_WRONG_DLC,
        .offending_id = 0x220,
        .observed_dlc = 9
    };
    qset_can_frame_t violation_golden = {.id = 0x120, .dlc = 4, .data = {0x02, 0x20, 0x02, 0x09}};
    assert(qset_can_encode_protocol_violation(&violation, &frame) == QSET_CAN_OK);
    assert_frame(&frame, &violation_golden);
    qset_can_protocol_violation_t violation_out = {0};
    assert(qset_can_decode_protocol_violation(&violation_golden, &violation_out) == QSET_CAN_OK);
    assert(violation_out.source_node == violation.source_node && violation_out.reason == violation.reason);
    assert(violation_out.offending_id == violation.offending_id && violation_out.observed_dlc == violation.observed_dlc);
    qset_can_protocol_violation_t violation_bad = violation;
    violation_bad.reason = 0;
    qset_can_frame_t violation_bad_frame = violation_golden;
    violation_bad_frame.data[3] = 16;
    CHECK_FAILURES(protocol_violation, qset_can_protocol_violation_t, violation, violation_bad,
                   violation_golden, violation_bad_frame, QSET_CAN_BAD_VALUE, QSET_CAN_BAD_VALUE);
    violation_bad = violation;
    violation_bad.offending_id = 0x800;
    assert(qset_can_encode_protocol_violation(&violation_bad, &frame) == QSET_CAN_BAD_VALUE);
    violation_bad = violation;
    violation_bad.observed_dlc = 16;
    assert(qset_can_encode_protocol_violation(&violation_bad, &frame) == QSET_CAN_BAD_VALUE);

    qset_can_request_status_t request = {QSET_CAN_NODE_ADCS, 42};
    qset_can_frame_t request_golden = {.id = 0x220, .dlc = 1, .data = {0x2a}};
    assert(qset_can_encode_request_status(&request, &frame) == QSET_CAN_OK);
    assert_frame(&frame, &request_golden);
    qset_can_request_status_t request_out = {0};
    assert(qset_can_decode_request_status(&request_golden, &request_out) == QSET_CAN_OK);
    assert(request_out.destination_node == request.destination_node && request_out.sequence == request.sequence);
    qset_can_request_status_t request_bad = request;
    request_bad.sequence = 0;
    qset_can_frame_t request_bad_frame = request_golden;
    request_bad_frame.id = 0x221;
    CHECK_FAILURES(request_status, qset_can_request_status_t, request, request_bad,
                   request_golden, request_bad_frame, QSET_CAN_BAD_VALUE, QSET_CAN_BAD_INDEX);

    qset_can_node_status_t status = {
        .source_node = QSET_CAN_NODE_ADCS,
        .sequence = 42,
        .state = QSET_CAN_NODE_STATE_NOMINAL,
        .uptime_s = 86400,
        .active_faults = 0
    };
    qset_can_frame_t status_golden = {.id = 0x420, .dlc = 7,
                                      .data = {0x2a, 0x01, 0x80, 0x51, 0x01, 0x00, 0x00}};
    assert(qset_can_encode_node_status(&status, &frame) == QSET_CAN_OK);
    assert_frame(&frame, &status_golden);
    qset_can_node_status_t status_out = {0};
    assert(qset_can_decode_node_status(&status_golden, &status_out) == QSET_CAN_OK);
    assert(status_out.source_node == status.source_node && status_out.sequence == status.sequence);
    assert(status_out.state == status.state && status_out.uptime_s == status.uptime_s);
    assert(status_out.active_faults == status.active_faults);
    qset_can_node_status_t status_bad = status;
    status_bad.state = 0xff;
    qset_can_frame_t status_bad_frame = status_golden;
    status_bad_frame.dlc = 8;
    CHECK_FAILURES(node_status, qset_can_node_status_t, status, status_bad,
                   status_golden, status_bad_frame, QSET_CAN_BAD_VALUE, QSET_CAN_BAD_DLC);

    status.active_faults = 0xff;
    status.sequence = 0;
    assert(qset_can_encode_node_status(&status, &frame) == QSET_CAN_OK);
    assert(frame.data[0] == 0 && frame.data[6] == 0xff);
    assert(qset_can_decode_node_status(&frame, &status_out) == QSET_CAN_OK);
    assert(status_out.sequence == 0 && status_out.active_faults == 0xff);
}

int main(void)
{
    _Static_assert(sizeof(qset_can_frame_t) == 12, "C/Rust frame size changed");
    _Static_assert(offsetof(qset_can_frame_t, id) == 0, "id offset changed");
    _Static_assert(offsetof(qset_can_frame_t, dlc) == 2, "dlc offset changed");
    _Static_assert(offsetof(qset_can_frame_t, data) == 3, "data offset changed");
    _Static_assert(QSET_CAN_BAD_VALUE == -7, "payload-value status changed");

    uint16_t id = 0xffff;
    uint8_t tier = 0xff, node = 0xff, index = 0xff;

    assert(qset_can_make_id(QSET_CAN_TIER_HEARTBEAT, QSET_CAN_NODE_OBC, 0, &id) == QSET_CAN_OK && id == 0x001);
    assert(qset_can_make_id(QSET_CAN_TIER_ERROR, QSET_CAN_NODE_ADCS, 15, &id) == QSET_CAN_OK && id == 0x12f);
    assert(qset_can_make_id(QSET_CAN_TIER_COMMAND, QSET_CAN_NODE_COMMS, 0, &id) == QSET_CAN_OK && id == 0x230);
    assert(qset_can_make_id(QSET_CAN_TIER_TELEMETRY, QSET_CAN_NODE_COMMS, 2, &id) == QSET_CAN_OK && id == 0x432);
    assert(qset_can_parse_id(id, &tier, &node, &index) == QSET_CAN_OK);
    assert(tier == QSET_CAN_TIER_TELEMETRY && node == QSET_CAN_NODE_COMMS && index == 2);

    qset_can_frame_t frame = {.id = 0x432, .dlc = 8, .data = {0}};
    assert(qset_can_validate_frame(&frame) == QSET_CAN_OK);
    frame.dlc = 9;
    assert(qset_can_validate_frame(&frame) == QSET_CAN_BAD_DLC);
    frame.dlc = 0;
    assert(qset_can_validate_frame(&frame) == QSET_CAN_OK);
    frame.id = 0x300;
    assert(qset_can_validate_frame(&frame) == QSET_CAN_BAD_ID);

    id = 0xffff;
    assert(qset_can_make_id(QSET_CAN_TIER_HEARTBEAT, QSET_CAN_NODE_OBC, 1, &id) == QSET_CAN_BAD_INDEX);
    assert(id == 0xffff);
    /* 7 is unassigned: never a registered node. */
    assert(qset_can_make_id(QSET_CAN_TIER_COMMAND, 7, 0, &id) == QSET_CAN_BAD_NODE);
    assert(id == 0xffff);
    assert(qset_can_make_id(99, QSET_CAN_NODE_OBC, 0, &id) == QSET_CAN_BAD_TIER);
    assert(qset_can_make_id(QSET_CAN_TIER_COMMAND, QSET_CAN_NODE_OBC, 0, NULL) == QSET_CAN_NULL_POINTER);

    tier = node = index = 0xff;
    assert(qset_can_parse_id(0x000, &tier, &node, &index) == QSET_CAN_BAD_NODE);
    assert(tier == 0xff && node == 0xff && index == 0xff);
    assert(qset_can_parse_id(0x432, NULL, &node, &index) == QSET_CAN_NULL_POINTER);
    assert(qset_can_validate_frame(NULL) == QSET_CAN_NULL_POINTER);

    test_message_codecs();

    puts("C/Rust protocol boundary: passed");
    return 0;
}
