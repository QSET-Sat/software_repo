#include "qset_can_transport.h"

static int channel_is_valid(qset_can_bus_channel_t channel)
{
    return channel == QSET_CAN_BUS_PRIMARY || channel == QSET_CAN_BUS_BACKUP;
}

static int state_is_valid(qset_can_controller_state_t state)
{
    return state >= QSET_CAN_CONTROLLER_UNINITIALIZED &&
           state <= QSET_CAN_CONTROLLER_RECOVERING;
}

static int fault_is_valid(qset_can_driver_fault_t fault)
{
    return fault >= QSET_CAN_DRIVER_FAULT_NONE && fault <= QSET_CAN_DRIVER_FAULT_INTERNAL;
}

static int result_is_valid(qset_can_driver_result_t result)
{
    return result <= QSET_CAN_DRIVER_OK && result >= QSET_CAN_DRIVER_FAILURE;
}

static int frame_is_valid(const qset_can_frame_t *frame)
{
    return frame->id <= QSET_CAN_MAX_STANDARD_ID && frame->dlc <= QSET_CAN_MAX_DLC;
}

static int ops_are_complete(const qset_can_driver_ops_t *ops)
{
    return ops->initialize != 0 && ops->start != 0 && ops->stop != 0 &&
           ops->receive != 0 && ops->transmit != 0 && ops->get_state != 0 &&
           ops->get_last_fault != 0 && ops->recover_bus_off != 0 &&
           ops->take_rx_overruns != 0;
}

static qset_can_driver_result_t normalize_result(qset_can_driver_result_t result)
{
    return result_is_valid(result) ? result : QSET_CAN_DRIVER_FAILURE;
}

static void saturating_add(uint32_t *counter, uint32_t amount)
{
    if (amount > UINT32_MAX - *counter) {
        *counter = UINT32_MAX;
    } else {
        *counter += amount;
    }
}

/* A failed take adds nothing; the driver keeps its count for the next call. */
static void collect_rx_overruns(qset_can_controller_t *controller)
{
    uint32_t count = 0u;
    qset_can_driver_result_t result = normalize_result(
        controller->ops->take_rx_overruns(controller->driver_context, &count));

    if (result == QSET_CAN_DRIVER_OK) {
        saturating_add(&controller->diagnostics.rx_controller_overruns, count);
    }
}

static qset_can_driver_result_t read_state(const qset_can_controller_t *controller,
                                           qset_can_controller_state_t *out_state)
{
    qset_can_controller_state_t state = QSET_CAN_CONTROLLER_UNINITIALIZED;
    qset_can_driver_result_t result =
        normalize_result(controller->ops->get_state(controller->driver_context, &state));

    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    if (!state_is_valid(state) || state == QSET_CAN_CONTROLLER_UNINITIALIZED) {
        return QSET_CAN_DRIVER_FAILURE;
    }
    *out_state = state;
    return QSET_CAN_DRIVER_OK;
}

qset_can_driver_result_t qset_can_controller_init(
    qset_can_controller_t *controller,
    const qset_can_driver_ops_t *ops,
    void *driver_context,
    const qset_can_controller_config_t *config)
{
    qset_can_driver_result_t result;

    if (controller == 0 || ops == 0 || config == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (controller->ops != 0) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    if (!ops_are_complete(ops) || !channel_is_valid(config->channel) ||
        config->nominal_bitrate == 0u) {
        return QSET_CAN_DRIVER_INVALID_ARGUMENT;
    }

    result = normalize_result(ops->initialize(driver_context, config));
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }

    controller->ops = ops;
    controller->driver_context = driver_context;
    controller->channel = config->channel;
    controller->nominal_bitrate = config->nominal_bitrate;
    return QSET_CAN_DRIVER_OK;
}

qset_can_driver_result_t qset_can_controller_start(qset_can_controller_t *controller)
{
    qset_can_controller_state_t state;
    qset_can_driver_result_t result;

    if (controller == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (controller->ops == 0) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    result = read_state(controller, &state);
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    if (state != QSET_CAN_CONTROLLER_STOPPED) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    return normalize_result(controller->ops->start(controller->driver_context));
}

qset_can_driver_result_t qset_can_controller_stop(qset_can_controller_t *controller)
{
    qset_can_controller_state_t state;
    qset_can_driver_result_t result;

    if (controller == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (controller->ops == 0) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    result = read_state(controller, &state);
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    if (state == QSET_CAN_CONTROLLER_STOPPED) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    return normalize_result(controller->ops->stop(controller->driver_context));
}

qset_can_driver_result_t qset_can_controller_receive(qset_can_controller_t *controller,
                                                      qset_can_rx_item_t *out_item)
{
    qset_can_controller_state_t state;
    qset_can_driver_result_t result;
    qset_can_frame_t frame;
    uint64_t timestamp_us;
    qset_can_rx_item_t item;

    if (controller == 0 || out_item == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (controller->ops == 0) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    result = read_state(controller, &state);
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    if (state == QSET_CAN_CONTROLLER_STOPPED) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }

    collect_rx_overruns(controller);
    result = normalize_result(
        controller->ops->receive(controller->driver_context, &frame, &timestamp_us));
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    if (!frame_is_valid(&frame)) {
        /* Impossible Classic CAN shape from the driver: discard and count. */
        saturating_add(&controller->diagnostics.invalid_frames, 1u);
        return QSET_CAN_DRIVER_FAILURE;
    }

    item.frame = frame;
    item.channel = controller->channel;
    item.timestamp_us = timestamp_us;
    *out_item = item;
    return QSET_CAN_DRIVER_OK;
}

qset_can_driver_result_t qset_can_controller_transmit(qset_can_controller_t *controller,
                                                       const qset_can_frame_t *frame)
{
    qset_can_controller_state_t state;
    qset_can_driver_result_t result;

    if (controller == 0 || frame == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (controller->ops == 0) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    if (!frame_is_valid(frame)) {
        return QSET_CAN_DRIVER_INVALID_ARGUMENT;
    }
    result = read_state(controller, &state);
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    if (state != QSET_CAN_CONTROLLER_ERROR_ACTIVE &&
        state != QSET_CAN_CONTROLLER_ERROR_WARNING &&
        state != QSET_CAN_CONTROLLER_ERROR_PASSIVE) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    return normalize_result(controller->ops->transmit(controller->driver_context, frame));
}

qset_can_driver_result_t qset_can_controller_get_state(
    const qset_can_controller_t *controller,
    qset_can_controller_state_t *out_state)
{
    qset_can_controller_state_t state;
    qset_can_driver_result_t result;

    if (controller == 0 || out_state == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (controller->ops == 0) {
        *out_state = QSET_CAN_CONTROLLER_UNINITIALIZED;
        return QSET_CAN_DRIVER_OK;
    }
    result = read_state(controller, &state);
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    *out_state = state;
    return QSET_CAN_DRIVER_OK;
}

qset_can_driver_result_t qset_can_controller_get_last_fault(
    const qset_can_controller_t *controller,
    qset_can_driver_fault_t *out_fault)
{
    qset_can_driver_fault_t fault;
    qset_can_driver_result_t result;

    if (controller == 0 || out_fault == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (controller->ops == 0) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    result = normalize_result(
        controller->ops->get_last_fault(controller->driver_context, &fault));
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    if (!fault_is_valid(fault)) {
        return QSET_CAN_DRIVER_FAILURE;
    }
    *out_fault = fault;
    return QSET_CAN_DRIVER_OK;
}

qset_can_driver_result_t qset_can_controller_recover_bus_off(
    qset_can_controller_t *controller)
{
    qset_can_controller_state_t state;
    qset_can_driver_result_t result;

    if (controller == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (controller->ops == 0) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    result = read_state(controller, &state);
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    if (state != QSET_CAN_CONTROLLER_BUS_OFF) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    return normalize_result(controller->ops->recover_bus_off(controller->driver_context));
}

qset_can_driver_result_t qset_can_controller_get_diagnostics(
    const qset_can_controller_t *controller,
    qset_can_diagnostics_t *out_diagnostics)
{
    if (controller == 0 || out_diagnostics == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    *out_diagnostics = controller->diagnostics;
    return QSET_CAN_DRIVER_OK;
}

qset_can_driver_result_t qset_can_controller_record_rx_queue_overflow(
    qset_can_controller_t *controller)
{
    if (controller == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    saturating_add(&controller->diagnostics.rx_queue_overflows, 1u);
    return QSET_CAN_DRIVER_OK;
}

qset_can_driver_result_t qset_can_controller_record_tx_queue_full(
    qset_can_controller_t *controller)
{
    if (controller == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    saturating_add(&controller->diagnostics.tx_queue_full_failures, 1u);
    return QSET_CAN_DRIVER_OK;
}

qset_can_driver_result_t qset_can_controller_record_invalid_frame(
    qset_can_controller_t *controller)
{
    if (controller == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    saturating_add(&controller->diagnostics.invalid_frames, 1u);
    return QSET_CAN_DRIVER_OK;
}
