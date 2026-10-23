#include "qset_can.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

int main(void)
{
    _Static_assert(sizeof(qset_can_frame_t) == 12, "C/Rust frame size changed");
    _Static_assert(offsetof(qset_can_frame_t, id) == 0, "id offset changed");
    _Static_assert(offsetof(qset_can_frame_t, dlc) == 2, "dlc offset changed");
    _Static_assert(offsetof(qset_can_frame_t, data) == 3, "data offset changed");
    _Static_assert(QSET_CAN_BAD_VALUE == -7, "payload-value status changed");

    uint16_t id = 0xffff;
    uint8_t tier = 0xff, node = 0xff, index = 0xff;

    assert(qset_can_make_id(QSET_CAN_TIER_HEARTBEAT, 1, 0, &id) == QSET_CAN_OK && id == 0x001);
    assert(qset_can_make_id(QSET_CAN_TIER_ERROR, 2, 15, &id) == QSET_CAN_OK && id == 0x12f);
    assert(qset_can_make_id(QSET_CAN_TIER_COMMAND, 3, 0, &id) == QSET_CAN_OK && id == 0x230);
    assert(qset_can_make_id(QSET_CAN_TIER_TELEMETRY, 3, 2, &id) == QSET_CAN_OK && id == 0x432);
    assert(qset_can_parse_id(id, &tier, &node, &index) == QSET_CAN_OK);
    assert(tier == QSET_CAN_TIER_TELEMETRY && node == 3 && index == 2);

    qset_can_frame_t frame = {.id = 0x432, .dlc = 8, .data = {0}};
    assert(qset_can_validate_frame(&frame) == QSET_CAN_OK);
    frame.dlc = 9;
    assert(qset_can_validate_frame(&frame) == QSET_CAN_BAD_DLC);
    frame.dlc = 0;
    assert(qset_can_validate_frame(&frame) == QSET_CAN_OK);
    frame.id = 0x300;
    assert(qset_can_validate_frame(&frame) == QSET_CAN_BAD_ID);

    id = 0xffff;
    assert(qset_can_make_id(QSET_CAN_TIER_HEARTBEAT, 1, 1, &id) == QSET_CAN_BAD_INDEX);
    assert(id == 0xffff);
    assert(qset_can_make_id(QSET_CAN_TIER_COMMAND, 7, 0, &id) == QSET_CAN_BAD_NODE);
    assert(id == 0xffff);
    assert(qset_can_make_id(99, 1, 0, &id) == QSET_CAN_BAD_TIER);
    assert(qset_can_make_id(QSET_CAN_TIER_COMMAND, 1, 0, NULL) == QSET_CAN_NULL_POINTER);

    tier = node = index = 0xff;
    assert(qset_can_parse_id(0x000, &tier, &node, &index) == QSET_CAN_BAD_NODE);
    assert(tier == 0xff && node == 0xff && index == 0xff);
    assert(qset_can_parse_id(0x432, NULL, &node, &index) == QSET_CAN_NULL_POINTER);
    assert(qset_can_validate_frame(NULL) == QSET_CAN_NULL_POINTER);

    puts("C/Rust protocol boundary: passed");
    return 0;
}
