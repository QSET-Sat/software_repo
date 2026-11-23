/* Receive path above the board adapter. The rules it follows, and the
 * assumptions still open (A1 to A5), are listed at the top of qset_can_rx.h.
 *
 * Build with -Ibuild/node/<NODE>, because this file uses QSET_LOCAL_NODE_ID. */
#include "qset_can_rx.h"

#include "qset_local_node.h"

#include <stdint.h>

static int local_is_obc(void)
{
    return QSET_LOCAL_NODE_ID == QSET_CAN_NODE_OBC;
}

/* Every node needs on_heartbeat and report_violation. The OBC also needs the
 * violation and node-status handlers. A subsystem also needs request status. */
static int handlers_are_complete(const qset_can_rx_handlers_t *handlers)
{
    if (handlers->on_heartbeat == 0 || handlers->report_violation == 0) {
        return 0;
    }
    if (local_is_obc()) {
        return handlers->on_protocol_violation != 0 && handlers->on_node_status != 0;
    }
    return handlers->on_request_status != 0;
}

/* Step 6: counts one rejected frame in the transport diagnostics (issue 10). */
static void count_invalid(qset_can_rx_t *rx)
{
    /* Cannot fail: init checked that the controller pointer is not null. */
    (void)qset_can_controller_record_invalid_frame(rx->controller);
}

/* Turns a decoder status into a violation reason. */
static uint8_t reason_for(int32_t status)
{
    switch (status) {
    case QSET_CAN_BAD_INDEX:
        return (uint8_t)QSET_CAN_VIOLATION_REASON_UNASSIGNED_MESSAGE_INDEX;
    case QSET_CAN_BAD_DLC:
        return (uint8_t)QSET_CAN_VIOLATION_REASON_WRONG_DLC;
    case QSET_CAN_BAD_VALUE:
        return (uint8_t)QSET_CAN_VIOLATION_REASON_INVALID_OR_RESERVED_FIELD_VALUE;
    default:
        /* Bad ID range or unregistered node: no reason code of its own (A4). */
        return (uint8_t)QSET_CAN_VIOLATION_REASON_UNASSIGNED_MESSAGE_INDEX;
    }
}

/* Steps 4 and 6 for an invalid frame: count it once, then send it to the
 * violation reporter only if this node owns the error. */
static qset_can_rx_result_t reject(qset_can_rx_t *rx,
                                   const qset_can_frame_t *frame,
                                   int32_t status,
                                   int this_node_owns_error)
{
    count_invalid(rx);
    if (this_node_owns_error) {
        const qset_can_protocol_violation_t report = {
            .source_node = (uint8_t)QSET_LOCAL_NODE_ID,
            .reason = reason_for(status),
            .offending_id = frame->id,
            .observed_dlc = frame->dlc,
        };

        rx->handlers.report_violation(rx->context, &report);
    }
    return QSET_CAN_RX_REJECTED;
}

/* Heartbeat tier. The ID names the source node. */
static qset_can_rx_result_t rx_heartbeat(qset_can_rx_t *rx,
                                         const qset_can_rx_item_t *item,
                                         uint8_t source)
{
    qset_can_heartbeat_t message;
    int32_t status = qset_can_decode_heartbeat(&item->frame, &message);

    if (status != QSET_CAN_OK) {
        /* Only the OBC reports a bad heartbeat. */
        return reject(rx, &item->frame, status, local_is_obc());
    }
    /* Every node may use every other node's heartbeat (A1 for our own ID). */
    if (source == QSET_LOCAL_NODE_ID) {
        return QSET_CAN_RX_IGNORED;
    }
    rx->handlers.on_heartbeat(rx->context, &message, item->timestamp_us);
    return QSET_CAN_RX_OK;
}

/* Error tier. The ID names the source node. */
static qset_can_rx_result_t rx_error(qset_can_rx_t *rx,
                                     const qset_can_rx_item_t *item,
                                     uint8_t source,
                                     uint8_t index)
{
    qset_can_protocol_violation_t message;
    int32_t status = qset_can_decode_protocol_violation(&item->frame, &message);

    if (status != QSET_CAN_OK) {
        /* Index 0 is the protocol-violation frame itself: never report it, so
         * errors cannot cause more errors. Other indexes: the OBC (A3). */
        return reject(rx, &item->frame, status, index != 0u && local_is_obc());
    }
    /* Only the OBC uses these. It ignores one that claims to be its own (A1). */
    if (!local_is_obc() || source == QSET_LOCAL_NODE_ID) {
        return QSET_CAN_RX_IGNORED;
    }
    rx->handlers.on_protocol_violation(rx->context, &message, item->timestamp_us);
    return QSET_CAN_RX_OK;
}

/* Command tier. The ID names the destination node. */
static qset_can_rx_result_t rx_command(qset_can_rx_t *rx,
                                       const qset_can_rx_item_t *item,
                                       uint8_t destination)
{
    qset_can_request_status_t message;
    int32_t status = qset_can_decode_request_status(&item->frame, &message);
    int for_this_node = destination == QSET_LOCAL_NODE_ID;

    if (status != QSET_CAN_OK) {
        /* Only the destination reports a bad command. */
        return reject(rx, &item->frame, status, for_this_node);
    }
    /* Commands are delivered only to their destination. */
    if (!for_this_node) {
        return QSET_CAN_RX_IGNORED;
    }
    if (local_is_obc()) {
        /* Only the OBC sends commands, so this one came from someone else (A2). */
        count_invalid(rx);
        return QSET_CAN_RX_REJECTED;
    }
    rx->handlers.on_request_status(rx->context, &message, item->timestamp_us);
    return QSET_CAN_RX_OK;
}

/* Telemetry tier. The ID names the source node. */
static qset_can_rx_result_t rx_telemetry(qset_can_rx_t *rx,
                                         const qset_can_rx_item_t *item,
                                         uint8_t source)
{
    qset_can_node_status_t message;
    int32_t status = qset_can_decode_node_status(&item->frame, &message);

    if (status != QSET_CAN_OK) {
        /* Only the OBC reports bad telemetry. */
        return reject(rx, &item->frame, status, local_is_obc());
    }
    /* Only the OBC uses node status. It ignores one that claims to be its own (A1). */
    if (!local_is_obc() || source == QSET_LOCAL_NODE_ID) {
        return QSET_CAN_RX_IGNORED;
    }
    rx->handlers.on_node_status(rx->context, &message, item->timestamp_us);
    return QSET_CAN_RX_OK;
}

qset_can_rx_result_t qset_can_rx_init(qset_can_rx_t *rx,
                                      qset_can_controller_t *controller,
                                      const qset_can_rx_handlers_t *handlers,
                                      void *context)
{
    if (rx == 0 || controller == 0 || handlers == 0) {
        return QSET_CAN_RX_NULL_POINTER;
    }
    if (rx->initialized != 0u) {
        return QSET_CAN_RX_INVALID_STATE;
    }
    if (!handlers_are_complete(handlers)) {
        return QSET_CAN_RX_INVALID_ARGUMENT;
    }

    rx->controller = controller;
    rx->handlers = *handlers;
    rx->context = context;
    rx->initialized = 1u;
    return QSET_CAN_RX_OK;
}

qset_can_rx_result_t qset_can_rx_poll(qset_can_rx_t *rx)
{
    qset_can_rx_item_t item;
    qset_can_driver_result_t result;

    if (rx == 0) {
        return QSET_CAN_RX_NULL_POINTER;
    }
    if (rx->initialized == 0u) {
        return QSET_CAN_RX_INVALID_STATE;
    }

    /* Step 1. Receive also collects controller events and drops impossible
     * frames, and counts both in the diagnostics (issue 10). */
    result = qset_can_controller_receive(rx->controller, &item);
    if (result == QSET_CAN_DRIVER_NO_DATA) {
        return QSET_CAN_RX_NO_FRAME;
    }
    if (result != QSET_CAN_DRIVER_OK) {
        /* Already counted by receive where needed. Never a protocol violation. */
        return QSET_CAN_RX_DRIVER_ERROR;
    }
    return qset_can_rx_process(rx, &item);
}

qset_can_rx_result_t qset_can_rx_process(qset_can_rx_t *rx, const qset_can_rx_item_t *item)
{
    const qset_can_frame_t *frame;
    uint8_t tier = 0u;
    uint8_t node = 0u;
    uint8_t index = 0u;
    int32_t status;

    if (rx == 0 || item == 0) {
        return QSET_CAN_RX_NULL_POINTER;
    }
    if (rx->initialized == 0u) {
        return QSET_CAN_RX_INVALID_STATE;
    }
    frame = &item->frame;

    /* Step 1 check, the same one receive makes. An impossible frame is
     * counted once and dropped, never reported (A5). */
    if (frame->id > QSET_CAN_MAX_STANDARD_ID || frame->dlc > QSET_CAN_MAX_DLC) {
        count_invalid(rx);
        return QSET_CAN_RX_DRIVER_ERROR;
    }

    /* Step 2: structural check. A failure here means a bad ID range or an
     * unregistered node, so the frame has no destination: the OBC reports it. */
    status = qset_can_validate_frame(frame);
    if (status == QSET_CAN_OK) {
        status = qset_can_parse_id(frame->id, &tier, &node, &index);
    }
    if (status != QSET_CAN_OK) {
        return reject(rx, frame, status, local_is_obc());
    }

    /* Steps 3 to 6, per tier: decode, check ownership, route, count. */
    switch (tier) {
    case QSET_CAN_TIER_HEARTBEAT:
        return rx_heartbeat(rx, item, node);
    case QSET_CAN_TIER_ERROR:
        return rx_error(rx, item, node, index);
    case QSET_CAN_TIER_COMMAND:
        return rx_command(rx, item, node);
    case QSET_CAN_TIER_TELEMETRY:
        return rx_telemetry(rx, item, node);
    default:
        /* parse_id only returns the four tiers above; kept for safety. */
        return reject(rx, frame, QSET_CAN_BAD_TIER, local_is_obc());
    }
}
