#include "qset_can_mock.h"

static int mock_state_is_valid(qset_can_controller_state_t state)
{
    return state >= QSET_CAN_CONTROLLER_STOPPED && state <= QSET_CAN_CONTROLLER_RECOVERING;
}

static int mock_fault_is_valid(qset_can_driver_fault_t fault)
{
    return fault >= QSET_CAN_DRIVER_FAULT_NONE && fault <= QSET_CAN_DRIVER_FAULT_INTERNAL;
}

static int mock_operation_is_valid(qset_can_mock_operation_t operation)
{
    return operation >= QSET_CAN_MOCK_OP_INITIALIZE &&
           operation <= QSET_CAN_MOCK_OP_TAKE_RX_OVERRUNS;
}

static int mock_frame_is_valid(const qset_can_frame_t *frame)
{
    return frame->id <= QSET_CAN_MAX_STANDARD_ID && frame->dlc <= QSET_CAN_MAX_DLC;
}

static int mock_is_running(const qset_can_mock_t *mock)
{
    return mock->state == QSET_CAN_CONTROLLER_ERROR_ACTIVE ||
           mock->state == QSET_CAN_CONTROLLER_ERROR_WARNING ||
           mock->state == QSET_CAN_CONTROLLER_ERROR_PASSIVE;
}

static qset_can_driver_result_t mock_take_failure(qset_can_mock_t *mock,
                                                  qset_can_mock_operation_t operation)
{
    if (mock->failure_pending == 0u || mock->failure_operation != operation) {
        return QSET_CAN_DRIVER_OK;
    }

    mock->failure_pending = 0u;
    mock->last_fault = mock->failure_fault;
    if (mock->initialized != 0u && mock->failure_fault == QSET_CAN_DRIVER_FAULT_BUS_OFF) {
        mock->state = QSET_CAN_CONTROLLER_BUS_OFF;
    }
    return QSET_CAN_DRIVER_FAILURE;
}

static qset_can_driver_result_t mock_push(qset_can_mock_t *mock,
                                          const qset_can_frame_t *frame)
{
    uint8_t tail;

    if (mock->queue_count >= QSET_CAN_MOCK_QUEUE_CAPACITY) {
        return QSET_CAN_DRIVER_QUEUE_FULL;
    }
    tail = (uint8_t)((mock->queue_head + mock->queue_count) % QSET_CAN_MOCK_QUEUE_CAPACITY);
    mock->queue[tail].frame = *frame;
    mock->queue[tail].timestamp_us = mock->current_time_us;
    mock->queue_count++;
    return QSET_CAN_DRIVER_OK;
}

static qset_can_driver_result_t mock_initialize(
    void *context,
    const qset_can_controller_config_t *config)
{
    qset_can_mock_t *mock = (qset_can_mock_t *)context;
    qset_can_driver_result_t result;

    if (mock == 0 || config == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (mock->initialized != 0u) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    result = mock_take_failure(mock, QSET_CAN_MOCK_OP_INITIALIZE);
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    if ((config->channel != QSET_CAN_BUS_PRIMARY && config->channel != QSET_CAN_BUS_BACKUP) ||
        config->nominal_bitrate == 0u) {
        return QSET_CAN_DRIVER_INVALID_ARGUMENT;
    }

    mock->current_time_us = 0u;
    mock->nominal_bitrate = config->nominal_bitrate;
    mock->state = QSET_CAN_CONTROLLER_STOPPED;
    mock->last_fault = QSET_CAN_DRIVER_FAULT_NONE;
    mock->channel = config->channel;
    mock->queue_head = 0u;
    mock->queue_count = 0u;
    mock->initialized = 1u;
    return QSET_CAN_DRIVER_OK;
}

static qset_can_driver_result_t mock_start(void *context)
{
    qset_can_mock_t *mock = (qset_can_mock_t *)context;
    qset_can_driver_result_t result;

    if (mock == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (mock->initialized == 0u || mock->state != QSET_CAN_CONTROLLER_STOPPED) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    result = mock_take_failure(mock, QSET_CAN_MOCK_OP_START);
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    mock->state = QSET_CAN_CONTROLLER_ERROR_ACTIVE;
    return QSET_CAN_DRIVER_OK;
}

static qset_can_driver_result_t mock_stop(void *context)
{
    qset_can_mock_t *mock = (qset_can_mock_t *)context;
    qset_can_driver_result_t result;

    if (mock == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (mock->initialized == 0u || mock->state == QSET_CAN_CONTROLLER_STOPPED) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    result = mock_take_failure(mock, QSET_CAN_MOCK_OP_STOP);
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    mock->state = QSET_CAN_CONTROLLER_STOPPED;
    return QSET_CAN_DRIVER_OK;
}

static qset_can_driver_result_t mock_receive(void *context,
                                             qset_can_frame_t *out_frame,
                                             uint64_t *out_timestamp_us)
{
    qset_can_mock_t *mock = (qset_can_mock_t *)context;
    qset_can_driver_result_t result;

    if (mock == 0 || out_frame == 0 || out_timestamp_us == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (mock->initialized == 0u || mock->state == QSET_CAN_CONTROLLER_STOPPED) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    result = mock_take_failure(mock, QSET_CAN_MOCK_OP_RECEIVE);
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    if (mock->queue_count == 0u) {
        return QSET_CAN_DRIVER_NO_DATA;
    }

    *out_frame = mock->queue[mock->queue_head].frame;
    *out_timestamp_us = mock->queue[mock->queue_head].timestamp_us;
    mock->queue_head = (uint8_t)((mock->queue_head + 1u) % QSET_CAN_MOCK_QUEUE_CAPACITY);
    mock->queue_count--;
    return QSET_CAN_DRIVER_OK;
}

static qset_can_driver_result_t mock_transmit(void *context, const qset_can_frame_t *frame)
{
    qset_can_mock_t *mock = (qset_can_mock_t *)context;
    qset_can_driver_result_t result;

    if (mock == 0 || frame == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (mock->initialized == 0u || !mock_is_running(mock)) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    if (!mock_frame_is_valid(frame)) {
        return QSET_CAN_DRIVER_INVALID_ARGUMENT;
    }
    result = mock_take_failure(mock, QSET_CAN_MOCK_OP_TRANSMIT);
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    return mock_push(mock, frame);
}

static qset_can_driver_result_t mock_get_state(void *context,
                                               qset_can_controller_state_t *out_state)
{
    qset_can_mock_t *mock = (qset_can_mock_t *)context;
    qset_can_driver_result_t result;

    if (mock == 0 || out_state == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (mock->initialized == 0u) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    result = mock_take_failure(mock, QSET_CAN_MOCK_OP_GET_STATE);
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    *out_state = mock->state;
    return QSET_CAN_DRIVER_OK;
}

static qset_can_driver_result_t mock_get_last_fault(void *context,
                                                    qset_can_driver_fault_t *out_fault)
{
    qset_can_mock_t *mock = (qset_can_mock_t *)context;
    qset_can_driver_result_t result;

    if (mock == 0 || out_fault == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (mock->initialized == 0u) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    result = mock_take_failure(mock, QSET_CAN_MOCK_OP_GET_LAST_FAULT);
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    *out_fault = mock->last_fault;
    return QSET_CAN_DRIVER_OK;
}

static qset_can_driver_result_t mock_recover_bus_off(void *context)
{
    qset_can_mock_t *mock = (qset_can_mock_t *)context;
    qset_can_driver_result_t result;

    if (mock == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (mock->initialized == 0u || mock->state != QSET_CAN_CONTROLLER_BUS_OFF) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    result = mock_take_failure(mock, QSET_CAN_MOCK_OP_RECOVER_BUS_OFF);
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    mock->state = QSET_CAN_CONTROLLER_RECOVERING;
    return QSET_CAN_DRIVER_OK;
}

static qset_can_driver_result_t mock_take_rx_overruns(void *context, uint32_t *out_count)
{
    qset_can_mock_t *mock = (qset_can_mock_t *)context;
    qset_can_driver_result_t result;

    if (mock == 0 || out_count == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (mock->initialized == 0u) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    result = mock_take_failure(mock, QSET_CAN_MOCK_OP_TAKE_RX_OVERRUNS);
    if (result != QSET_CAN_DRIVER_OK) {
        return result;
    }
    /* A real driver masks its CAN interrupt around this read and reset. */
    *out_count = mock->pending_rx_overruns;
    mock->pending_rx_overruns = 0u;
    return QSET_CAN_DRIVER_OK;
}

const qset_can_driver_ops_t qset_can_mock_driver_ops = {
    .initialize = mock_initialize,
    .start = mock_start,
    .stop = mock_stop,
    .receive = mock_receive,
    .transmit = mock_transmit,
    .get_state = mock_get_state,
    .get_last_fault = mock_get_last_fault,
    .recover_bus_off = mock_recover_bus_off,
    .take_rx_overruns = mock_take_rx_overruns,
};

qset_can_driver_result_t qset_can_mock_set_time_us(qset_can_mock_t *mock,
                                                    uint64_t timestamp_us)
{
    if (mock == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (mock->initialized == 0u) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    mock->current_time_us = timestamp_us;
    return QSET_CAN_DRIVER_OK;
}

qset_can_driver_result_t qset_can_mock_inject_receive(qset_can_mock_t *mock,
                                                      const qset_can_frame_t *frame)
{
    if (mock == 0 || frame == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (mock->initialized == 0u || !mock_is_running(mock)) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    if (!mock_frame_is_valid(frame)) {
        return QSET_CAN_DRIVER_INVALID_ARGUMENT;
    }
    return mock_push(mock, frame);
}

qset_can_driver_result_t qset_can_mock_force_state(qset_can_mock_t *mock,
                                                   qset_can_controller_state_t state)
{
    if (mock == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (mock->initialized == 0u) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    if (!mock_state_is_valid(state)) {
        return QSET_CAN_DRIVER_INVALID_ARGUMENT;
    }
    mock->state = state;
    if (state == QSET_CAN_CONTROLLER_BUS_OFF) {
        mock->last_fault = QSET_CAN_DRIVER_FAULT_BUS_OFF;
    }
    return QSET_CAN_DRIVER_OK;
}

qset_can_driver_result_t qset_can_mock_latch_fault(qset_can_mock_t *mock,
                                                   qset_can_driver_fault_t fault)
{
    if (mock == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (mock->initialized == 0u) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    if (!mock_fault_is_valid(fault) || fault == QSET_CAN_DRIVER_FAULT_NONE) {
        return QSET_CAN_DRIVER_INVALID_ARGUMENT;
    }
    mock->last_fault = fault;
    if (fault == QSET_CAN_DRIVER_FAULT_BUS_OFF) {
        mock->state = QSET_CAN_CONTROLLER_BUS_OFF;
    }
    return QSET_CAN_DRIVER_OK;
}

qset_can_driver_result_t qset_can_mock_inject_rx_overruns(qset_can_mock_t *mock,
                                                          uint32_t count)
{
    if (mock == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (mock->initialized == 0u) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    if (count > UINT32_MAX - mock->pending_rx_overruns) {
        mock->pending_rx_overruns = UINT32_MAX;
    } else {
        mock->pending_rx_overruns += count;
    }
    return QSET_CAN_DRIVER_OK;
}

qset_can_driver_result_t qset_can_mock_fail_next(qset_can_mock_t *mock,
                                                 qset_can_mock_operation_t operation,
                                                 qset_can_driver_fault_t fault)
{
    if (mock == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (!mock_operation_is_valid(operation) || !mock_fault_is_valid(fault) ||
        fault == QSET_CAN_DRIVER_FAULT_NONE) {
        return QSET_CAN_DRIVER_INVALID_ARGUMENT;
    }
    if (mock->failure_pending != 0u) {
        return QSET_CAN_DRIVER_BUSY;
    }
    mock->failure_operation = operation;
    mock->failure_fault = fault;
    mock->failure_pending = 1u;
    return QSET_CAN_DRIVER_OK;
}

qset_can_driver_result_t qset_can_mock_complete_recovery(
    qset_can_mock_t *mock,
    qset_can_driver_fault_t outcome_fault)
{
    if (mock == 0) {
        return QSET_CAN_DRIVER_NULL_POINTER;
    }
    if (mock->initialized == 0u || mock->state != QSET_CAN_CONTROLLER_RECOVERING) {
        return QSET_CAN_DRIVER_INVALID_STATE;
    }
    if (!mock_fault_is_valid(outcome_fault)) {
        return QSET_CAN_DRIVER_INVALID_ARGUMENT;
    }

    if (outcome_fault == QSET_CAN_DRIVER_FAULT_NONE) {
        mock->state = QSET_CAN_CONTROLLER_ERROR_ACTIVE;
    } else {
        mock->last_fault = outcome_fault;
        mock->state = QSET_CAN_CONTROLLER_BUS_OFF;
    }
    return QSET_CAN_DRIVER_OK;
}
