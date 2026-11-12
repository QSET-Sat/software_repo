#ifndef QSET_LOCAL_NODE_H
#define QSET_LOCAL_NODE_H

/* The local node identity for one firmware target. Firmware includes this
 * header, never qset_node_config.h directly, so every target gets the checks
 * below.
 *
 * qset_node_config.h is generated per target by
 *     make node-config NODE=<name>
 * and found by compiling with -Ibuild/node/<name>. A "file not found" error
 * for it means the target was built without a node configuration.
 *
 * per-board consistency check
 * Each board's own firmware must also confirm it was built for the right
 * node, so it cannot be compiled against another node's folder, e.g. in ADCS
 * code:
 *     _Static_assert(QSET_LOCAL_NODE_ID == QSET_CAN_NODE_ADCS,
 *                    "ADCS firmware built with the wrong node config");
 */
#include "qset_node_config.h"

/* Justified conditional: reports a config header that lacks the ID as a clear
 * build error, instead of an undeclared-identifier error below. */
#ifndef QSET_LOCAL_NODE_ID
#error                                                                         \
    "QSET_LOCAL_NODE_ID is not defined; regenerate with make node-config NODE=<name>"
#endif

/* Must list every registered node. Update together with REGISTERED_NODES in
 * protocol/rust/src/common.rs and the node table in protocol/SPEC.md. */
_Static_assert(QSET_LOCAL_NODE_ID == QSET_CAN_NODE_OBC ||
                   QSET_LOCAL_NODE_ID == QSET_CAN_NODE_ADCS ||
                   QSET_LOCAL_NODE_ID == QSET_CAN_NODE_COMMS ||
                   QSET_LOCAL_NODE_ID == QSET_CAN_NODE_PAYLOAD ||
                   QSET_LOCAL_NODE_ID == QSET_CAN_NODE_BATT ||
                   QSET_LOCAL_NODE_ID == QSET_CAN_NODE_MPPT,
               "QSET_LOCAL_NODE_ID must be a registered node");

#endif
