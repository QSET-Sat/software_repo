#ifndef QSET_CAN_RX_H
#define QSET_CAN_RX_H

/* Receive path above the board adapter. For every frame it:
 *   1. receives the raw frame and its metadata
 *   2. checks the frame's structure
 *   3. decodes the assigned message
 *   4. decides whether this node owns any protocol error
 *   5. routes a valid message to the right handler
 *   6. counts failures in the transport diagnostics
 * The same code runs on every node. It does not use FreeRTOS.
 *
 * WHERE THE RULES COME FROM
 * - This node's ID: QSET_LOCAL_NODE_ID, fixed at build time (SPEC.md, "Local
 *   node selection"). So this code is compiled once per node.
 * - Who gets which message: SPEC.md "Sender / consumers" lines.
 *     Heartbeat: every node may use every other node's heartbeat.
 *     Protocol violation and node status: only the OBC uses them.
 *     Request status: only the node the command is addressed to.
 * - Who reports a bad frame: IMPLEMENTATION_DECISIONS.md, "Protocol-error
 *   ownership". A bad command is reported by its destination. Anything else
 *   is reported by the OBC. A bad protocol-violation frame is never reported.
 * - Counting (issue 10): each frame that protocol checks reject is counted
 *   once with qset_can_controller_record_invalid_frame. Receive itself counts
 *   impossible frames and controller events (overruns, bus-off, ...), so
 *   hardware problems never become protocol violations.
 * - Sending reports: the violation reporter (not built yet) does the rate
 *   limiting and sending, and counts suppressed reports (FREERTOS_DESIGN.md).
 *   This code only decides whether this node should report.
 *
 * ASSUMPTIONS STILL OPEN
 * A1. A frame that claims to come from this node is ignored. In normal mode a
 *     CAN controller does not receive its own frames, so it would be another
 *     board built with our node ID.
 * A2. Only the OBC sends commands, so a command the OBC hears came from
 *     another node. It is rejected and counted as invalid. No report is sent,
 *     because reason 0x04 (unauthorized sender) is not emitted in version 1.
 * A3. "Protocol-error frame" means error index 0 only. A bad error index 0
 *     frame is never reported. An unassigned error index (1 to 15) is
 *     reported by the OBC, like other bad non-command traffic.
 * A4. The spec has no reason code for a bad ID range or an unregistered
 *     node, so the OBC reports those as 0x01 (unassigned message index).
 * A5. Items given to qset_can_rx_process come from
 *     qset_can_controller_receive (through the future ISR and RX queue), which
 *     already drops impossible frames. If one gets here anyway, it is counted
 *     once as invalid, dropped, and never reported.
 *
 * NOTE FOR THE TEAM (existing code, not an assumption)
 * SPEC.md says a wrong-DLC report may carry an observed DLC of 9 to 15, and
 * its golden vector uses 9. Receive (issues 9 and 10) drops every frame with
 * DLC above 8 first, so this node can never send that report.
 */

#include "qset_can_transport.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result of every qset_can_rx call. Negative values are caller mistakes. */
typedef enum {
    QSET_CAN_RX_OK = 0,           /* Success. For a frame: handler called. */
    QSET_CAN_RX_IGNORED = 1,      /* Valid frame, but not for this node. */
    QSET_CAN_RX_REJECTED = 2,     /* Invalid or unauthorized frame. Counted as
                                     invalid, and reported if this node owns it. */
    QSET_CAN_RX_NO_FRAME = 3,     /* Poll only: nothing was waiting. */
    QSET_CAN_RX_DRIVER_ERROR = 4, /* Receive failed or the frame was impossible.
                                     The transport counts it; never a violation. */
    QSET_CAN_RX_NULL_POINTER = -1,
    QSET_CAN_RX_INVALID_ARGUMENT = -2, /* A handler this node needs is missing. */
    QSET_CAN_RX_INVALID_STATE = -3     /* Not initialized, or initialized twice. */
} qset_can_rx_result_t;

/* The services a node plugs in. Like the driver ops table, every handler this
 * node calls is required (marked OBC / subsystem / all below), and init
 * rejects a table that is missing one. Unused handlers may be NULL.
 *
 * Handlers run inside the RX call, in the RX task: keep them short, never
 * block, and never call qset_can_rx functions from inside one. context is the
 * pointer given to qset_can_rx_init. timestamp_us is the receive time from
 * the adapter. The bus channel is not passed on, because version 1 only uses
 * the primary bus (IMPLEMENTATION_DECISIONS.md). */
typedef struct {
    /* All nodes: heartbeats from every other node. Check source_node, e.g. a
     * subsystem only has to watch the OBC. */
    void (*on_heartbeat)(void *context,
                         const qset_can_heartbeat_t *message,
                         uint64_t timestamp_us);

    /* OBC: protocol-violation reports sent by other nodes. */
    void (*on_protocol_violation)(void *context,
                                  const qset_can_protocol_violation_t *message,
                                  uint64_t timestamp_us);

    /* Subsystem: a status request addressed to this node. */
    void (*on_request_status)(void *context,
                              const qset_can_request_status_t *message,
                              uint64_t timestamp_us);

    /* OBC: node-status telemetry from other nodes. Request correlation (and
     * its late/duplicate/unexpected counters) happens in this handler. */
    void (*on_node_status)(void *context,
                           const qset_can_node_status_t *message,
                           uint64_t timestamp_us);

    /* All nodes: this node owns a protocol error and should report it. This
     * is the violation reporter's entry point. It rate-limits, encodes and
     * transmits the report, and calls
     * qset_can_controller_record_suppressed_violation_report when it skips
     * one. */
    void (*report_violation)(void *context,
                             const qset_can_protocol_violation_t *report);
} qset_can_rx_handlers_t;

/* Statically allocate and zero-initialize this before qset_can_rx_init.
 * Treat the fields as private. */
typedef struct {
    qset_can_controller_t *controller;
    qset_can_rx_handlers_t handlers;
    void *context;
    uint8_t initialized;
} qset_can_rx_t;

/* Sets up the RX path once for one controller (one bus). The handler table
 * is copied, so it does not need to outlive this call. The controller may be
 * initialized before or after this call. */
qset_can_rx_result_t qset_can_rx_init(qset_can_rx_t *rx,
                                      qset_can_controller_t *controller,
                                      const qset_can_rx_handlers_t *handlers,
                                      void *context);

/* Takes one frame from the controller and processes it. Returns NO_FRAME if
 * nothing was waiting, DRIVER_ERROR if receive failed (receive has already
 * counted any impossible frame or controller event), otherwise the result of
 * qset_can_rx_process. Call from the RX task only. */
qset_can_rx_result_t qset_can_rx_poll(qset_can_rx_t *rx);

/* Processes one frame that qset_can_controller_receive already returned, for
 * example one taken from the RX queue (A5). Call from the RX task only. */
qset_can_rx_result_t qset_can_rx_process(qset_can_rx_t *rx,
                                         const qset_can_rx_item_t *item);

#ifdef __cplusplus
}
#endif

#endif
