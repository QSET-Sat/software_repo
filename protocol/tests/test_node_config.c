/* Host test for one generated node configuration. `make node-test` compiles
 * this file once per node, each time with -Ibuild/node/<NODE>. */
#include "qset_local_node.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    uint16_t id = 0xffff;
    uint8_t tier = 0xff, node = 0xff, index = 0xff;

    /* The Rust registry must accept the local ID, at the expected value. */
    assert(qset_can_make_id(QSET_CAN_TIER_HEARTBEAT, QSET_LOCAL_NODE_ID, 0, &id) == QSET_CAN_OK);
    assert(id == QSET_LOCAL_NODE_ID);

    /* Parsing the heartbeat back must name the same node. */
    assert(qset_can_parse_id(id, &tier, &node, &index) == QSET_CAN_OK);
    assert(tier == QSET_CAN_TIER_HEARTBEAT && node == QSET_LOCAL_NODE_ID && index == 0);

    printf("PASSED  %s (0x%x): builds and runs\n", QSET_LOCAL_NODE_NAME, (unsigned)QSET_LOCAL_NODE_ID);
    return 0;
}
