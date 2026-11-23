#include "qset_can_mock.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define TEST_BITRATE 1000000u

static const qset_can_controller_config_t primary_config = {
    .channel = QSET_CAN_BUS_PRIMARY,
    .nominal_bitrate = TEST_BITRATE,
};

static void init_and_start(qset_can_controller_t *controller,
                           qset_can_mock_t *mock,
                           qset_can_bus_channel_t channel)
{
    qset_can_controller_config_t config = {
        .channel = channel,
        .nominal_bitrate = TEST_BITRATE,
    };

    assert(qset_can_controller_init(controller, &qset_can_mock_driver_ops, mock, &config) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_start(controller) == QSET_CAN_DRIVER_OK);
}

static qset_can_frame_t make_frame(uint16_t id, uint8_t value)
{
    qset_can_frame_t frame = {
        .id = id,
        .dlc = QSET_CAN_MAX_DLC,
        .data = {value, (uint8_t)(value + 1u), (uint8_t)(value + 2u),
                 (uint8_t)(value + 3u), (uint8_t)(value + 4u),
                 (uint8_t)(value + 5u), (uint8_t)(value + 6u),
                 (uint8_t)(value + 7u)},
    };
    return frame;
}

static void test_layout_and_lifecycle(void)
{
    qset_can_controller_t controller = {0};
    qset_can_mock_t mock = {0};
    qset_can_controller_state_t state = QSET_CAN_CONTROLLER_RECOVERING;
    qset_can_driver_fault_t fault = QSET_CAN_DRIVER_FAULT_INTERNAL;
    qset_can_controller_config_t invalid_config = primary_config;
    qset_can_driver_ops_t incomplete_ops = qset_can_mock_driver_ops;

    _Static_assert(sizeof(qset_can_frame_t) == 12, "C/Rust frame size changed");
    _Static_assert(offsetof(qset_can_frame_t, id) == 0, "id offset changed");
    _Static_assert(offsetof(qset_can_frame_t, dlc) == 2, "dlc offset changed");
    _Static_assert(offsetof(qset_can_frame_t, data) == 3, "data offset changed");

    assert(qset_can_controller_get_state(&controller, &state) == QSET_CAN_DRIVER_OK);
    assert(state == QSET_CAN_CONTROLLER_UNINITIALIZED);
    assert(qset_can_controller_start(&controller) == QSET_CAN_DRIVER_INVALID_STATE);
    assert(qset_can_controller_stop(&controller) == QSET_CAN_DRIVER_INVALID_STATE);
    assert(qset_can_controller_get_last_fault(&controller, &fault) ==
           QSET_CAN_DRIVER_INVALID_STATE);

    invalid_config.nominal_bitrate = 0u;
    assert(qset_can_controller_init(&controller, &qset_can_mock_driver_ops, &mock,
                                    &invalid_config) == QSET_CAN_DRIVER_INVALID_ARGUMENT);
    invalid_config = primary_config;
    invalid_config.channel = (qset_can_bus_channel_t)2;
    assert(qset_can_controller_init(&controller, &qset_can_mock_driver_ops, &mock,
                                    &invalid_config) == QSET_CAN_DRIVER_INVALID_ARGUMENT);
    incomplete_ops.transmit = 0;
    assert(qset_can_controller_init(&controller, &incomplete_ops, &mock, &primary_config) ==
           QSET_CAN_DRIVER_INVALID_ARGUMENT);
    incomplete_ops = qset_can_mock_driver_ops;
    incomplete_ops.take_events = 0;
    assert(qset_can_controller_init(&controller, &incomplete_ops, &mock, &primary_config) ==
           QSET_CAN_DRIVER_INVALID_ARGUMENT);

    assert(qset_can_controller_init(&controller, &qset_can_mock_driver_ops, &mock,
                                    &primary_config) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_init(&controller, &qset_can_mock_driver_ops, &mock,
                                    &primary_config) == QSET_CAN_DRIVER_INVALID_STATE);
    assert(qset_can_controller_get_state(&controller, &state) == QSET_CAN_DRIVER_OK);
    assert(state == QSET_CAN_CONTROLLER_STOPPED);
    assert(qset_can_controller_stop(&controller) == QSET_CAN_DRIVER_INVALID_STATE);
    assert(qset_can_controller_start(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_start(&controller) == QSET_CAN_DRIVER_INVALID_STATE);
    assert(qset_can_controller_get_state(&controller, &state) == QSET_CAN_DRIVER_OK);
    assert(state == QSET_CAN_CONTROLLER_ERROR_ACTIVE);
    assert(qset_can_controller_get_last_fault(&controller, &fault) == QSET_CAN_DRIVER_OK);
    assert(fault == QSET_CAN_DRIVER_FAULT_NONE);
    assert(qset_can_controller_stop(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_stop(&controller) == QSET_CAN_DRIVER_INVALID_STATE);
    assert(qset_can_controller_start(&controller) == QSET_CAN_DRIVER_OK);

    assert(qset_can_controller_init(0, &qset_can_mock_driver_ops, &mock, &primary_config) ==
           QSET_CAN_DRIVER_NULL_POINTER);
    assert(qset_can_controller_start(0) == QSET_CAN_DRIVER_NULL_POINTER);
    assert(qset_can_controller_stop(0) == QSET_CAN_DRIVER_NULL_POINTER);
    assert(qset_can_controller_receive(&controller, 0) == QSET_CAN_DRIVER_NULL_POINTER);
    assert(qset_can_controller_transmit(&controller, 0) == QSET_CAN_DRIVER_NULL_POINTER);
    assert(qset_can_controller_get_state(&controller, 0) == QSET_CAN_DRIVER_NULL_POINTER);
    assert(qset_can_controller_get_last_fault(&controller, 0) ==
           QSET_CAN_DRIVER_NULL_POINTER);
    assert(qset_can_controller_recover_bus_off(0) == QSET_CAN_DRIVER_NULL_POINTER);
}

static void test_round_trip_and_output_guarantees(void)
{
    qset_can_controller_t controller = {0};
    qset_can_mock_t mock = {0};
    qset_can_frame_t frame = make_frame(0x7aau, 0x10u);
    qset_can_rx_item_t item;
    qset_can_rx_item_t unchanged;

    init_and_start(&controller, &mock, QSET_CAN_BUS_PRIMARY);
    assert(qset_can_mock_set_time_us(&mock, UINT64_C(123456789)) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_transmit(&controller, &frame) == QSET_CAN_DRIVER_OK);

    memset(&item, 0xa5, sizeof(item));
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_OK);
    assert(item.frame.id == frame.id);
    assert(item.frame.dlc == frame.dlc);
    assert(memcmp(item.frame.data, frame.data, sizeof(frame.data)) == 0);
    assert(item.channel == QSET_CAN_BUS_PRIMARY);
    assert(item.timestamp_us == UINT64_C(123456789));

    memset(&item, 0x5a, sizeof(item));
    unchanged = item;
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_NO_DATA);
    assert(memcmp(&item, &unchanged, sizeof(item)) == 0);

    assert(qset_can_mock_set_time_us(&mock, 0u) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_inject_receive(&mock, &frame) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_OK);
    assert(item.timestamp_us == 0u);

    memset(&item, 0x3c, sizeof(item));
    unchanged = item;
    assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_RECEIVE,
                                   QSET_CAN_DRIVER_FAULT_RX_OVERRUN) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_FAILURE);
    assert(memcmp(&item, &unchanged, sizeof(item)) == 0);
}

static void test_validation(void)
{
    qset_can_controller_t controller = {0};
    qset_can_mock_t mock = {0};
    qset_can_frame_t frame = make_frame(0x321u, 0x20u);

    init_and_start(&controller, &mock, QSET_CAN_BUS_PRIMARY);

    /* 0x321 is not an assigned application-protocol ID, but it is a valid raw
     * standard CAN frame and therefore belongs at this boundary. */
    assert(qset_can_controller_transmit(&controller, &frame) == QSET_CAN_DRIVER_OK);
    frame.id = 0x800u;
    assert(qset_can_controller_transmit(&controller, &frame) ==
           QSET_CAN_DRIVER_INVALID_ARGUMENT);
    assert(qset_can_mock_inject_receive(&mock, &frame) ==
           QSET_CAN_DRIVER_INVALID_ARGUMENT);
    frame.id = 0x100u;
    frame.dlc = 9u;
    assert(qset_can_controller_transmit(&controller, &frame) ==
           QSET_CAN_DRIVER_INVALID_ARGUMENT);
    assert(qset_can_mock_inject_receive(&mock, &frame) ==
           QSET_CAN_DRIVER_INVALID_ARGUMENT);
}

static void test_fifo_and_channel_isolation(void)
{
    qset_can_controller_t primary = {0};
    qset_can_controller_t backup = {0};
    qset_can_mock_t primary_mock = {0};
    qset_can_mock_t backup_mock = {0};
    qset_can_rx_item_t item;
    qset_can_frame_t frame;
    unsigned int index;

    init_and_start(&primary, &primary_mock, QSET_CAN_BUS_PRIMARY);
    init_and_start(&backup, &backup_mock, QSET_CAN_BUS_BACKUP);

    for (index = 0u; index < QSET_CAN_MOCK_QUEUE_CAPACITY; ++index) {
        frame = make_frame((uint16_t)(0x100u + index), (uint8_t)index);
        assert(qset_can_mock_set_time_us(&primary_mock, (uint64_t)(1000u + index)) ==
               QSET_CAN_DRIVER_OK);
        assert(qset_can_controller_transmit(&primary, &frame) == QSET_CAN_DRIVER_OK);
    }
    frame = make_frame(0x200u, 0xffu);
    assert(qset_can_controller_transmit(&primary, &frame) == QSET_CAN_DRIVER_QUEUE_FULL);

    assert(qset_can_mock_set_time_us(&backup_mock, 77u) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_transmit(&backup, &frame) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_receive(&backup, &item) == QSET_CAN_DRIVER_OK);
    assert(item.channel == QSET_CAN_BUS_BACKUP);
    assert(item.frame.id == 0x200u);
    assert(item.timestamp_us == 77u);
    assert(qset_can_controller_receive(&backup, &item) == QSET_CAN_DRIVER_NO_DATA);

    for (index = 0u; index < QSET_CAN_MOCK_QUEUE_CAPACITY; ++index) {
        assert(qset_can_controller_receive(&primary, &item) == QSET_CAN_DRIVER_OK);
        assert(item.channel == QSET_CAN_BUS_PRIMARY);
        assert(item.frame.id == (uint16_t)(0x100u + index));
        assert(item.frame.data[0] == (uint8_t)index);
        assert(item.timestamp_us == (uint64_t)(1000u + index));
    }
    assert(qset_can_controller_receive(&primary, &item) == QSET_CAN_DRIVER_NO_DATA);
}

static void test_states_faults_and_recovery(void)
{
    qset_can_controller_t controller = {0};
    qset_can_mock_t mock = {0};
    qset_can_frame_t frame = make_frame(0x401u, 0x30u);
    qset_can_rx_item_t item;
    qset_can_controller_state_t state;
    qset_can_driver_fault_t fault;
    const qset_can_driver_fault_t injected_faults[] = {
        QSET_CAN_DRIVER_FAULT_RX_OVERRUN,
        QSET_CAN_DRIVER_FAULT_TX_FAILURE,
        QSET_CAN_DRIVER_FAULT_BUS_OFF,
        QSET_CAN_DRIVER_FAULT_HARDWARE,
        QSET_CAN_DRIVER_FAULT_INTERNAL,
    };
    size_t index;

    init_and_start(&controller, &mock, QSET_CAN_BUS_PRIMARY);
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_ERROR_WARNING) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_transmit(&controller, &frame) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_ERROR_PASSIVE) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_OK);

    assert(qset_can_controller_transmit(&controller, &frame) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_BUS_OFF) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_transmit(&controller, &frame) ==
           QSET_CAN_DRIVER_INVALID_STATE);
    /* A queued frame remains drainable after bus-off. */
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_last_fault(&controller, &fault) == QSET_CAN_DRIVER_OK);
    assert(fault == QSET_CAN_DRIVER_FAULT_BUS_OFF);

    assert(qset_can_controller_recover_bus_off(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_state(&controller, &state) == QSET_CAN_DRIVER_OK);
    assert(state == QSET_CAN_CONTROLLER_RECOVERING);
    assert(qset_can_controller_transmit(&controller, &frame) ==
           QSET_CAN_DRIVER_INVALID_STATE);
    assert(qset_can_controller_recover_bus_off(&controller) ==
           QSET_CAN_DRIVER_INVALID_STATE);
    assert(qset_can_mock_complete_recovery(&mock, QSET_CAN_DRIVER_FAULT_NONE) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_state(&controller, &state) == QSET_CAN_DRIVER_OK);
    assert(state == QSET_CAN_CONTROLLER_ERROR_ACTIVE);
    /* Successful operations and recovery do not clear the latest fault. */
    assert(qset_can_controller_get_last_fault(&controller, &fault) == QSET_CAN_DRIVER_OK);
    assert(fault == QSET_CAN_DRIVER_FAULT_BUS_OFF);

    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_BUS_OFF) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_recover_bus_off(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_complete_recovery(&mock, QSET_CAN_DRIVER_FAULT_HARDWARE) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_state(&controller, &state) == QSET_CAN_DRIVER_OK);
    assert(state == QSET_CAN_CONTROLLER_BUS_OFF);
    assert(qset_can_controller_get_last_fault(&controller, &fault) == QSET_CAN_DRIVER_OK);
    assert(fault == QSET_CAN_DRIVER_FAULT_HARDWARE);

    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_ERROR_ACTIVE) ==
           QSET_CAN_DRIVER_OK);
    for (index = 0u; index < sizeof(injected_faults) / sizeof(injected_faults[0]); ++index) {
        assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_TRANSMIT,
                                       injected_faults[index]) == QSET_CAN_DRIVER_OK);
        assert(qset_can_controller_transmit(&controller, &frame) == QSET_CAN_DRIVER_FAILURE);
        assert(qset_can_controller_get_last_fault(&controller, &fault) ==
               QSET_CAN_DRIVER_OK);
        assert(fault == injected_faults[index]);
        if (injected_faults[index] == QSET_CAN_DRIVER_FAULT_BUS_OFF) {
            assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_ERROR_ACTIVE) ==
                   QSET_CAN_DRIVER_OK);
        }
    }
}

static void test_injected_control_failures(void)
{
    qset_can_controller_t controller = {0};
    qset_can_mock_t mock = {0};
    qset_can_controller_state_t state = QSET_CAN_CONTROLLER_RECOVERING;
    qset_can_driver_fault_t fault = QSET_CAN_DRIVER_FAULT_TX_FAILURE;

    assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_INITIALIZE,
                                   QSET_CAN_DRIVER_FAULT_HARDWARE) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_init(&controller, &qset_can_mock_driver_ops, &mock,
                                    &primary_config) == QSET_CAN_DRIVER_FAILURE);
    assert(controller.ops == 0);
    assert(qset_can_controller_init(&controller, &qset_can_mock_driver_ops, &mock,
                                    &primary_config) == QSET_CAN_DRIVER_OK);

    assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_START,
                                   QSET_CAN_DRIVER_FAULT_INTERNAL) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_STOP,
                                   QSET_CAN_DRIVER_FAULT_INTERNAL) == QSET_CAN_DRIVER_BUSY);
    assert(qset_can_controller_start(&controller) == QSET_CAN_DRIVER_FAILURE);
    assert(qset_can_controller_start(&controller) == QSET_CAN_DRIVER_OK);

    assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_GET_STATE,
                                   QSET_CAN_DRIVER_FAULT_INTERNAL) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_state(&controller, &state) == QSET_CAN_DRIVER_FAILURE);
    assert(state == QSET_CAN_CONTROLLER_RECOVERING);

    assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_GET_LAST_FAULT,
                                   QSET_CAN_DRIVER_FAULT_INTERNAL) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_last_fault(&controller, &fault) ==
           QSET_CAN_DRIVER_FAILURE);
    assert(fault == QSET_CAN_DRIVER_FAULT_TX_FAILURE);

    assert(qset_can_mock_latch_fault(&mock, QSET_CAN_DRIVER_FAULT_RX_OVERRUN) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_last_fault(&controller, &fault) == QSET_CAN_DRIVER_OK);
    assert(fault == QSET_CAN_DRIVER_FAULT_RX_OVERRUN);
}

static qset_can_driver_result_t inject_overruns(qset_can_mock_t *mock, uint32_t count)
{
    qset_can_driver_events_t events = {0};

    events.rx_overruns = count;
    return qset_can_mock_inject_events(mock, &events);
}

static void test_rx_controller_overruns(void)
{
    qset_can_controller_t controller = {0};
    qset_can_mock_t mock = {0};
    qset_can_frame_t frame = make_frame(0x402u, 0x40u);
    qset_can_rx_item_t item;
    qset_can_diagnostics_t diagnostics = {.rx_controller_overruns = 99u};
    qset_can_diagnostics_t unchanged;

    /* Readable before init, and starts at zero. */
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_controller_overruns == 0u);
    assert(inject_overruns(&mock, 1u) == QSET_CAN_DRIVER_INVALID_STATE);

    init_and_start(&controller, &mock, QSET_CAN_BUS_PRIMARY);

    /* Overruns are collected on receive, even when no frame is waiting. */
    assert(inject_overruns(&mock, 3u) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_controller_overruns == 0u);
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_NO_DATA);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_controller_overruns == 3u);

    /* Later events add to the total, and a frame is still delivered. */
    assert(qset_can_controller_transmit(&controller, &frame) == QSET_CAN_DRIVER_OK);
    assert(inject_overruns(&mock, 2u) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_OK);
    assert(item.frame.id == frame.id);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_controller_overruns == 5u);

    /* The driver's count was reset by the take, so nothing is double counted. */
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_NO_DATA);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_controller_overruns == 5u);

    /* Reading does not clear. */
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_controller_overruns == 5u);

    /* A failed take adds nothing and loses nothing. */
    assert(inject_overruns(&mock, 4u) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_TAKE_EVENTS,
                                   QSET_CAN_DRIVER_FAULT_INTERNAL) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_NO_DATA);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_controller_overruns == 5u);
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_NO_DATA);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_controller_overruns == 9u);

    /* Saturates instead of wrapping. */
    assert(inject_overruns(&mock, UINT32_MAX) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_NO_DATA);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_controller_overruns == UINT32_MAX);
    assert(inject_overruns(&mock, 1u) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_NO_DATA);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_controller_overruns == UINT32_MAX);

    /* Null pointers leave the output unchanged. */
    unchanged = diagnostics;
    assert(qset_can_controller_get_diagnostics(0, &diagnostics) ==
           QSET_CAN_DRIVER_NULL_POINTER);
    assert(qset_can_controller_get_diagnostics(&controller, 0) ==
           QSET_CAN_DRIVER_NULL_POINTER);
    assert(diagnostics.rx_controller_overruns == unchanged.rx_controller_overruns);
    assert(inject_overruns(0, 1u) == QSET_CAN_DRIVER_NULL_POINTER);
}

static void test_rx_queue_overflows(void)
{
    qset_can_controller_t controller = {0};
    qset_can_mock_t mock = {0};
    qset_can_rx_item_t item;
    qset_can_diagnostics_t diagnostics = {.rx_queue_overflows = 99u};
    uint32_t index;

    /* Starts at zero, and recording is valid before init. */
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_queue_overflows == 0u);
    assert(qset_can_controller_record_rx_queue_overflow(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_queue_overflows == 1u);

    init_and_start(&controller, &mock, QSET_CAN_BUS_PRIMARY);

    /* Each call counts exactly one dropped frame. */
    for (index = 2u; index <= 4u; ++index) {
        assert(qset_can_controller_record_rx_queue_overflow(&controller) ==
               QSET_CAN_DRIVER_OK);
        assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
               QSET_CAN_DRIVER_OK);
        assert(diagnostics.rx_queue_overflows == index);
    }

    /* Reading does not clear. */
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_queue_overflows == 4u);

    /* Independent of the RX controller overrun counter, in both directions. */
    assert(diagnostics.rx_controller_overruns == 0u);
    assert(inject_overruns(&mock, 7u) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_NO_DATA);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_controller_overruns == 7u);
    assert(diagnostics.rx_queue_overflows == 4u);

    /* Saturates instead of wrapping. Seeded directly because reaching the
     * limit one call at a time is impractical. */
    controller.diagnostics.rx_queue_overflows = UINT32_MAX - 1u;
    assert(qset_can_controller_record_rx_queue_overflow(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_queue_overflows == UINT32_MAX);
    assert(qset_can_controller_record_rx_queue_overflow(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_queue_overflows == UINT32_MAX);
    assert(diagnostics.rx_controller_overruns == 7u);

    assert(qset_can_controller_record_rx_queue_overflow(0) == QSET_CAN_DRIVER_NULL_POINTER);
}

static void test_tx_queue_full_failures(void)
{
    qset_can_controller_t controller = {0};
    qset_can_mock_t mock = {0};
    qset_can_frame_t frame = make_frame(0x403u, 0x50u);
    qset_can_diagnostics_t diagnostics = {.tx_queue_full_failures = 99u};
    uint32_t index;

    /* Starts at zero, and recording is valid before init. */
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.tx_queue_full_failures == 0u);
    assert(qset_can_controller_record_tx_queue_full(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.tx_queue_full_failures == 1u);

    init_and_start(&controller, &mock, QSET_CAN_BUS_PRIMARY);

    /* Each call counts exactly one refused send. */
    for (index = 2u; index <= 4u; ++index) {
        assert(qset_can_controller_record_tx_queue_full(&controller) == QSET_CAN_DRIVER_OK);
        assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
               QSET_CAN_DRIVER_OK);
        assert(diagnostics.tx_queue_full_failures == index);
    }

    /* Reading does not clear. */
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.tx_queue_full_failures == 4u);

    /* A full driver buffer is a normal TX-task wait, not a refused send, so
     * controller transmit returning QUEUE_FULL does not count here. */
    for (index = 0u; index < QSET_CAN_MOCK_QUEUE_CAPACITY; ++index) {
        assert(qset_can_controller_transmit(&controller, &frame) == QSET_CAN_DRIVER_OK);
    }
    assert(qset_can_controller_transmit(&controller, &frame) == QSET_CAN_DRIVER_QUEUE_FULL);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.tx_queue_full_failures == 4u);

    /* Independent of the RX counters, in both directions. */
    assert(diagnostics.rx_controller_overruns == 0u);
    assert(diagnostics.rx_queue_overflows == 0u);
    assert(qset_can_controller_record_rx_queue_overflow(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_queue_overflows == 1u);
    assert(diagnostics.tx_queue_full_failures == 4u);

    /* Saturates instead of wrapping. Seeded directly because reaching the
     * limit one call at a time is impractical. */
    controller.diagnostics.tx_queue_full_failures = UINT32_MAX - 1u;
    assert(qset_can_controller_record_tx_queue_full(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.tx_queue_full_failures == UINT32_MAX);
    assert(qset_can_controller_record_tx_queue_full(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.tx_queue_full_failures == UINT32_MAX);
    assert(diagnostics.rx_queue_overflows == 1u);

    assert(qset_can_controller_record_tx_queue_full(0) == QSET_CAN_DRIVER_NULL_POINTER);
}

static void test_invalid_frames(void)
{
    qset_can_controller_t controller = {0};
    qset_can_mock_t mock = {0};
    qset_can_frame_t frame = make_frame(0x404u, 0x60u);
    qset_can_frame_t bad_frame = frame;
    qset_can_rx_item_t item;
    qset_can_rx_item_t unchanged;
    qset_can_diagnostics_t diagnostics = {.invalid_frames = 99u};

    /* Starts at zero; recording is valid before init; mock needs init. */
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.invalid_frames == 0u);
    assert(qset_can_controller_record_invalid_frame(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.invalid_frames == 1u);
    assert(qset_can_mock_corrupt_next_receive(&mock, 0x800u, QSET_CAN_MAX_DLC) ==
           QSET_CAN_DRIVER_INVALID_STATE);

    init_and_start(&controller, &mock, QSET_CAN_BUS_PRIMARY);

    /* A driver frame with an impossible ID is discarded and counted. */
    assert(qset_can_controller_transmit(&controller, &frame) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_corrupt_next_receive(&mock, 0x800u, QSET_CAN_MAX_DLC) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_corrupt_next_receive(&mock, 0x800u, QSET_CAN_MAX_DLC) ==
           QSET_CAN_DRIVER_BUSY);
    memset(&item, 0x6b, sizeof(item));
    unchanged = item;
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_FAILURE);
    assert(memcmp(&item, &unchanged, sizeof(item)) == 0);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.invalid_frames == 2u);
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_NO_DATA);

    /* A driver frame with an impossible DLC is discarded and counted. */
    assert(qset_can_controller_transmit(&controller, &frame) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_corrupt_next_receive(&mock, frame.id, 9u) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_FAILURE);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.invalid_frames == 3u);

    /* Valid frames, including the highest valid ID and DLC, are not counted. */
    frame.id = QSET_CAN_MAX_STANDARD_ID;
    assert(qset_can_controller_transmit(&controller, &frame) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_OK);
    assert(item.frame.id == QSET_CAN_MAX_STANDARD_ID);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.invalid_frames == 3u);

    /* Invalid frames passed to transmit are a caller error, not counted. */
    bad_frame.id = 0x800u;
    assert(qset_can_controller_transmit(&controller, &bad_frame) ==
           QSET_CAN_DRIVER_INVALID_ARGUMENT);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.invalid_frames == 3u);

    /* Protocol-level rejections reported by the RX task count one per call. */
    assert(qset_can_controller_record_invalid_frame(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.invalid_frames == 4u);

    /* Reading does not clear, and other counters are unaffected. */
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.invalid_frames == 4u);
    assert(diagnostics.rx_controller_overruns == 0u);
    assert(diagnostics.rx_queue_overflows == 0u);
    assert(diagnostics.tx_queue_full_failures == 0u);
    assert(qset_can_controller_record_tx_queue_full(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.invalid_frames == 4u);

    /* Saturates instead of wrapping, from both the record call and receive.
     * Seeded directly because reaching the limit one call at a time is
     * impractical. */
    controller.diagnostics.invalid_frames = UINT32_MAX - 1u;
    assert(qset_can_controller_record_invalid_frame(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.invalid_frames == UINT32_MAX);
    assert(qset_can_controller_record_invalid_frame(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_transmit(&controller, &frame) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_corrupt_next_receive(&mock, 0x800u, QSET_CAN_MAX_DLC) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_FAILURE);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.invalid_frames == UINT32_MAX);

    assert(qset_can_controller_record_invalid_frame(0) == QSET_CAN_DRIVER_NULL_POINTER);
    assert(qset_can_mock_corrupt_next_receive(0, 0x800u, 0u) ==
           QSET_CAN_DRIVER_NULL_POINTER);
}

typedef qset_can_driver_result_t (*record_fn_t)(qset_can_controller_t *controller);

static uint32_t *correlation_counter(qset_can_diagnostics_t *diagnostics, size_t which)
{
    switch (which) {
    case 0u:
        return &diagnostics->late_responses;
    case 1u:
        return &diagnostics->duplicate_responses;
    default:
        return &diagnostics->unexpected_sequences;
    }
}

static void read_diagnostics(const qset_can_controller_t *controller,
                             qset_can_diagnostics_t *out)
{
    assert(qset_can_controller_get_diagnostics(controller, out) == QSET_CAN_DRIVER_OK);
}

static void test_response_correlation_counters(void)
{
    const record_fn_t record[] = {
        qset_can_controller_record_late_response,
        qset_can_controller_record_duplicate_response,
        qset_can_controller_record_unexpected_sequence,
    };
    const size_t count = sizeof(record) / sizeof(record[0]);
    size_t which;
    size_t other;
    uint32_t index;

    for (which = 0u; which < count; ++which) {
        qset_can_controller_t controller = {0};
        qset_can_mock_t mock = {0};
        qset_can_diagnostics_t diagnostics;

        /* Starts at zero, and recording is valid before init. */
        read_diagnostics(&controller, &diagnostics);
        assert(*correlation_counter(&diagnostics, which) == 0u);
        assert(record[which](&controller) == QSET_CAN_DRIVER_OK);
        read_diagnostics(&controller, &diagnostics);
        assert(*correlation_counter(&diagnostics, which) == 1u);

        init_and_start(&controller, &mock, QSET_CAN_BUS_PRIMARY);

        /* Each call counts exactly one response. */
        for (index = 2u; index <= 4u; ++index) {
            assert(record[which](&controller) == QSET_CAN_DRIVER_OK);
            read_diagnostics(&controller, &diagnostics);
            assert(*correlation_counter(&diagnostics, which) == index);
        }

        /* Reading does not clear. */
        read_diagnostics(&controller, &diagnostics);
        assert(*correlation_counter(&diagnostics, which) == 4u);

        /* Only this counter moved: the other two correlation counters and
         * all earlier counters are still zero. */
        for (other = 0u; other < count; ++other) {
            if (other != which) {
                assert(*correlation_counter(&diagnostics, other) == 0u);
            }
        }
        assert(diagnostics.rx_controller_overruns == 0u);
        assert(diagnostics.rx_queue_overflows == 0u);
        assert(diagnostics.tx_queue_full_failures == 0u);
        assert(diagnostics.invalid_frames == 0u);
        assert(diagnostics.suppressed_violation_reports == 0u);

        /* Saturates instead of wrapping. Seeded directly because reaching the
         * limit one call at a time is impractical. */
        *correlation_counter(&controller.diagnostics, which) = UINT32_MAX - 1u;
        assert(record[which](&controller) == QSET_CAN_DRIVER_OK);
        read_diagnostics(&controller, &diagnostics);
        assert(*correlation_counter(&diagnostics, which) == UINT32_MAX);
        assert(record[which](&controller) == QSET_CAN_DRIVER_OK);
        read_diagnostics(&controller, &diagnostics);
        assert(*correlation_counter(&diagnostics, which) == UINT32_MAX);

        assert(record[which](0) == QSET_CAN_DRIVER_NULL_POINTER);
    }
}

static void test_suppressed_violation_reports(void)
{
    qset_can_controller_t controller = {0};
    qset_can_mock_t mock = {0};
    qset_can_diagnostics_t diagnostics;
    uint32_t index;

    /* Starts at zero, and recording is valid before init. */
    read_diagnostics(&controller, &diagnostics);
    assert(diagnostics.suppressed_violation_reports == 0u);
    assert(qset_can_controller_record_suppressed_violation_report(&controller) ==
           QSET_CAN_DRIVER_OK);
    read_diagnostics(&controller, &diagnostics);
    assert(diagnostics.suppressed_violation_reports == 1u);

    init_and_start(&controller, &mock, QSET_CAN_BUS_PRIMARY);

    /* Each call counts exactly one skipped report. */
    for (index = 2u; index <= 4u; ++index) {
        assert(qset_can_controller_record_suppressed_violation_report(&controller) ==
               QSET_CAN_DRIVER_OK);
        read_diagnostics(&controller, &diagnostics);
        assert(diagnostics.suppressed_violation_reports == index);
    }

    /* Reading does not clear. */
    read_diagnostics(&controller, &diagnostics);
    assert(diagnostics.suppressed_violation_reports == 4u);

    /* Only this counter moved. */
    assert(diagnostics.rx_controller_overruns == 0u);
    assert(diagnostics.rx_queue_overflows == 0u);
    assert(diagnostics.tx_queue_full_failures == 0u);
    assert(diagnostics.invalid_frames == 0u);
    assert(diagnostics.late_responses == 0u);
    assert(diagnostics.duplicate_responses == 0u);
    assert(diagnostics.unexpected_sequences == 0u);

    /* Recording an invalid frame does not count as a suppressed report. */
    assert(qset_can_controller_record_invalid_frame(&controller) == QSET_CAN_DRIVER_OK);
    read_diagnostics(&controller, &diagnostics);
    assert(diagnostics.invalid_frames == 1u);
    assert(diagnostics.suppressed_violation_reports == 4u);

    /* Saturates instead of wrapping. Seeded directly because reaching the
     * limit one call at a time is impractical. */
    controller.diagnostics.suppressed_violation_reports = UINT32_MAX - 1u;
    assert(qset_can_controller_record_suppressed_violation_report(&controller) ==
           QSET_CAN_DRIVER_OK);
    read_diagnostics(&controller, &diagnostics);
    assert(diagnostics.suppressed_violation_reports == UINT32_MAX);
    assert(qset_can_controller_record_suppressed_violation_report(&controller) ==
           QSET_CAN_DRIVER_OK);
    read_diagnostics(&controller, &diagnostics);
    assert(diagnostics.suppressed_violation_reports == UINT32_MAX);
    assert(diagnostics.invalid_frames == 1u);

    assert(qset_can_controller_record_suppressed_violation_report(0) ==
           QSET_CAN_DRIVER_NULL_POINTER);
}

/* Runs one receive so pending driver events are collected, then checks all
 * four controller event counters. */
static void expect_controller_events(qset_can_controller_t *controller,
                                     uint32_t overruns,
                                     uint32_t warning,
                                     uint32_t passive,
                                     uint32_t bus_off)
{
    qset_can_rx_item_t item;
    qset_can_diagnostics_t diagnostics;

    assert(qset_can_controller_receive(controller, &item) == QSET_CAN_DRIVER_NO_DATA);
    read_diagnostics(controller, &diagnostics);
    assert(diagnostics.rx_controller_overruns == overruns);
    assert(diagnostics.controller_warning_events == warning);
    assert(diagnostics.controller_passive_events == passive);
    assert(diagnostics.bus_off_events == bus_off);
}

static void test_controller_error_events(void)
{
    qset_can_controller_t controller = {0};
    qset_can_mock_t mock = {0};
    qset_can_frame_t frame = make_frame(0x405u, 0x70u);
    qset_can_diagnostics_t diagnostics;
    qset_can_driver_events_t events = {0};
    const qset_can_driver_events_t one_each = {1u, 1u, 1u, 1u, 0u};
    const qset_can_driver_events_t max_each = {0u, UINT32_MAX, UINT32_MAX, UINT32_MAX, 0u};

    read_diagnostics(&controller, &diagnostics);
    assert(diagnostics.controller_warning_events == 0u);
    assert(diagnostics.controller_passive_events == 0u);
    assert(diagnostics.bus_off_events == 0u);
    assert(qset_can_mock_inject_events(&mock, &one_each) == QSET_CAN_DRIVER_INVALID_STATE);

    init_and_start(&controller, &mock, QSET_CAN_BUS_PRIMARY);

    /* Entering warning is counted, once collected by receive. */
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_ERROR_WARNING) ==
           QSET_CAN_DRIVER_OK);
    read_diagnostics(&controller, &diagnostics);
    assert(diagnostics.controller_warning_events == 0u);
    expect_controller_events(&controller, 0u, 1u, 0u, 0u);

    /* Warning to passive counts passive. */
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_ERROR_PASSIVE) ==
           QSET_CAN_DRIVER_OK);
    expect_controller_events(&controller, 0u, 1u, 1u, 0u);

    /* Improving from passive to warning is not counted. */
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_ERROR_WARNING) ==
           QSET_CAN_DRIVER_OK);
    expect_controller_events(&controller, 0u, 1u, 1u, 0u);

    /* A second warning episode counts again. */
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_ERROR_ACTIVE) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_ERROR_WARNING) ==
           QSET_CAN_DRIVER_OK);
    expect_controller_events(&controller, 0u, 2u, 1u, 0u);

    /* Active straight to passive counts passive only. */
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_ERROR_ACTIVE) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_ERROR_PASSIVE) ==
           QSET_CAN_DRIVER_OK);
    expect_controller_events(&controller, 0u, 2u, 2u, 0u);

    /* Passive to bus-off counts bus-off; receive still collects in bus-off. */
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_BUS_OFF) ==
           QSET_CAN_DRIVER_OK);
    expect_controller_events(&controller, 0u, 2u, 2u, 1u);

    /* A failed recovery returns to bus-off without a new bus-off event. */
    assert(qset_can_controller_recover_bus_off(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_complete_recovery(&mock, QSET_CAN_DRIVER_FAULT_HARDWARE) ==
           QSET_CAN_DRIVER_OK);
    expect_controller_events(&controller, 0u, 2u, 2u, 1u);

    /* Bus-off from a latched fault and from a failed transmit both count. */
    assert(qset_can_controller_recover_bus_off(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_complete_recovery(&mock, QSET_CAN_DRIVER_FAULT_NONE) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_latch_fault(&mock, QSET_CAN_DRIVER_FAULT_BUS_OFF) ==
           QSET_CAN_DRIVER_OK);
    expect_controller_events(&controller, 0u, 2u, 2u, 2u);
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_ERROR_ACTIVE) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_TRANSMIT,
                                   QSET_CAN_DRIVER_FAULT_BUS_OFF) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_transmit(&controller, &frame) == QSET_CAN_DRIVER_FAILURE);
    expect_controller_events(&controller, 0u, 2u, 2u, 3u);
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_ERROR_ACTIVE) ==
           QSET_CAN_DRIVER_OK);

    /* Episodes too short for the state to show are still counted exactly,
     * because the driver counts them, not the transport polling state. */
    events.warning_events = 3u;
    assert(qset_can_mock_inject_events(&mock, &events) == QSET_CAN_DRIVER_OK);
    expect_controller_events(&controller, 0u, 5u, 2u, 3u);

    /* A failed take adds nothing and loses nothing, for every field. */
    assert(qset_can_mock_inject_events(&mock, &one_each) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_TAKE_EVENTS,
                                   QSET_CAN_DRIVER_FAULT_INTERNAL) == QSET_CAN_DRIVER_OK);
    expect_controller_events(&controller, 0u, 5u, 2u, 3u);
    expect_controller_events(&controller, 1u, 6u, 3u, 4u);

    /* Reading does not clear, and non-controller counters are unaffected. */
    read_diagnostics(&controller, &diagnostics);
    assert(diagnostics.controller_warning_events == 6u);
    assert(diagnostics.controller_passive_events == 3u);
    assert(diagnostics.bus_off_events == 4u);
    assert(diagnostics.rx_queue_overflows == 0u);
    assert(diagnostics.tx_queue_full_failures == 0u);
    assert(diagnostics.invalid_frames == 0u);
    assert(diagnostics.late_responses == 0u);
    assert(diagnostics.duplicate_responses == 0u);
    assert(diagnostics.unexpected_sequences == 0u);
    assert(diagnostics.suppressed_violation_reports == 0u);

    /* Each counter saturates instead of wrapping. */
    assert(qset_can_mock_inject_events(&mock, &max_each) == QSET_CAN_DRIVER_OK);
    expect_controller_events(&controller, 1u, UINT32_MAX, UINT32_MAX, UINT32_MAX);
    assert(qset_can_mock_inject_events(&mock, &one_each) == QSET_CAN_DRIVER_OK);
    expect_controller_events(&controller, 2u, UINT32_MAX, UINT32_MAX, UINT32_MAX);

    assert(qset_can_mock_inject_events(0, &one_each) == QSET_CAN_DRIVER_NULL_POINTER);
    assert(qset_can_mock_inject_events(&mock, 0) == QSET_CAN_DRIVER_NULL_POINTER);
}

static void expect_recovery(const qset_can_controller_t *controller,
                            uint32_t attempts,
                            uint32_t failures)
{
    qset_can_diagnostics_t diagnostics;

    read_diagnostics(controller, &diagnostics);
    assert(diagnostics.recovery_attempts == attempts);
    assert(diagnostics.recovery_failures == failures);
}

/* Receive with nothing waiting, so pending driver events are collected. */
static void collect_pending(qset_can_controller_t *controller)
{
    qset_can_rx_item_t item;

    assert(qset_can_controller_receive(controller, &item) == QSET_CAN_DRIVER_NO_DATA);
}

static void test_recovery_attempts_and_failures(void)
{
    qset_can_controller_t controller = {0};
    qset_can_mock_t mock = {0};
    qset_can_diagnostics_t diagnostics;
    qset_can_controller_state_t state;
    qset_can_driver_events_t events = {0};

    /* Starts at zero; a request before init is not an attempt. */
    expect_recovery(&controller, 0u, 0u);
    assert(qset_can_controller_recover_bus_off(&controller) == QSET_CAN_DRIVER_INVALID_STATE);
    expect_recovery(&controller, 0u, 0u);

    init_and_start(&controller, &mock, QSET_CAN_BUS_PRIMARY);

    /* Requests outside bus-off never reach the driver and are not attempts. */
    assert(qset_can_controller_recover_bus_off(&controller) == QSET_CAN_DRIVER_INVALID_STATE);
    expect_recovery(&controller, 0u, 0u);

    /* A successful recovery is one attempt and no failure. */
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_BUS_OFF) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_recover_bus_off(&controller) == QSET_CAN_DRIVER_OK);
    expect_recovery(&controller, 1u, 0u);
    /* A second request while recovering is not an attempt. */
    assert(qset_can_controller_recover_bus_off(&controller) == QSET_CAN_DRIVER_INVALID_STATE);
    expect_recovery(&controller, 1u, 0u);
    assert(qset_can_mock_complete_recovery(&mock, QSET_CAN_DRIVER_FAULT_NONE) ==
           QSET_CAN_DRIVER_OK);
    collect_pending(&controller);
    expect_recovery(&controller, 1u, 0u);

    /* An accepted attempt that ends back in bus-off is a failure, counted by
     * the driver and collected on receive, and is not a new bus-off event. */
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_BUS_OFF) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_recover_bus_off(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_complete_recovery(&mock, QSET_CAN_DRIVER_FAULT_HARDWARE) ==
           QSET_CAN_DRIVER_OK);
    expect_recovery(&controller, 2u, 0u);
    collect_pending(&controller);
    expect_recovery(&controller, 2u, 1u);
    read_diagnostics(&controller, &diagnostics);
    assert(diagnostics.bus_off_events == 2u);

    /* A request the driver rejects immediately is an attempt and a failure,
     * counted by the transport at once; the controller stays in bus-off. */
    assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_RECOVER_BUS_OFF,
                                   QSET_CAN_DRIVER_FAULT_INTERNAL) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_recover_bus_off(&controller) == QSET_CAN_DRIVER_FAILURE);
    expect_recovery(&controller, 3u, 2u);
    assert(qset_can_controller_get_state(&controller, &state) == QSET_CAN_DRIVER_OK);
    assert(state == QSET_CAN_CONTROLLER_BUS_OFF);
    collect_pending(&controller);
    expect_recovery(&controller, 3u, 2u);

    /* A state read failure stops the request before the driver: not an attempt. */
    assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_GET_STATE,
                                   QSET_CAN_DRIVER_FAULT_INTERNAL) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_recover_bus_off(&controller) == QSET_CAN_DRIVER_FAILURE);
    expect_recovery(&controller, 3u, 2u);

    /* Repeated failed attempts keep counting, then a success adds no failure. */
    assert(qset_can_controller_recover_bus_off(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_complete_recovery(&mock, QSET_CAN_DRIVER_FAULT_HARDWARE) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_recover_bus_off(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_complete_recovery(&mock, QSET_CAN_DRIVER_FAULT_NONE) ==
           QSET_CAN_DRIVER_OK);
    collect_pending(&controller);
    expect_recovery(&controller, 5u, 3u);

    /* Reading does not clear, and unrelated counters are unaffected. */
    expect_recovery(&controller, 5u, 3u);
    read_diagnostics(&controller, &diagnostics);
    assert(diagnostics.rx_controller_overruns == 0u);
    assert(diagnostics.controller_warning_events == 0u);
    assert(diagnostics.controller_passive_events == 0u);
    assert(diagnostics.bus_off_events == 2u);
    assert(diagnostics.rx_queue_overflows == 0u);
    assert(diagnostics.tx_queue_full_failures == 0u);
    assert(diagnostics.invalid_frames == 0u);
    assert(diagnostics.late_responses == 0u);
    assert(diagnostics.duplicate_responses == 0u);
    assert(diagnostics.unexpected_sequences == 0u);
    assert(diagnostics.suppressed_violation_reports == 0u);

    /* Both counters saturate: attempts from the transport (seeded directly,
     * since counting to the limit is impractical) and failures from both the
     * driver-reported and the transport-counted paths. */
    controller.diagnostics.recovery_attempts = UINT32_MAX - 1u;
    events.recovery_failures = UINT32_MAX;
    assert(qset_can_mock_inject_events(&mock, &events) == QSET_CAN_DRIVER_OK);
    collect_pending(&controller);
    expect_recovery(&controller, UINT32_MAX - 1u, UINT32_MAX);
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_BUS_OFF) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_RECOVER_BUS_OFF,
                                   QSET_CAN_DRIVER_FAULT_INTERNAL) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_recover_bus_off(&controller) == QSET_CAN_DRIVER_FAILURE);
    expect_recovery(&controller, UINT32_MAX, UINT32_MAX);
    assert(qset_can_controller_recover_bus_off(&controller) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_complete_recovery(&mock, QSET_CAN_DRIVER_FAULT_HARDWARE) ==
           QSET_CAN_DRIVER_OK);
    collect_pending(&controller);
    expect_recovery(&controller, UINT32_MAX, UINT32_MAX);
}

static void expect_state_and_fault(const qset_can_controller_t *controller,
                                   qset_can_controller_state_t state,
                                   qset_can_driver_fault_t fault)
{
    qset_can_diagnostics_t diagnostics;

    read_diagnostics(controller, &diagnostics);
    assert(diagnostics.bus_state == state);
    assert(diagnostics.latest_driver_fault == fault);
}

static void test_diagnostics_bus_state_and_latest_fault(void)
{
    qset_can_controller_t controller = {0};
    qset_can_mock_t mock = {0};
    qset_can_frame_t frame = make_frame(0x406u, 0x80u);
    qset_can_diagnostics_t diagnostics;
    qset_can_diagnostics_t unchanged;

    /* Before init: no driver to ask. */
    expect_state_and_fault(&controller, QSET_CAN_CONTROLLER_UNINITIALIZED,
                           QSET_CAN_DRIVER_FAULT_NONE);

    assert(qset_can_controller_init(&controller, &qset_can_mock_driver_ops, &mock,
                                    &primary_config) == QSET_CAN_DRIVER_OK);
    expect_state_and_fault(&controller, QSET_CAN_CONTROLLER_STOPPED,
                           QSET_CAN_DRIVER_FAULT_NONE);

    /* The state is live: every change shows on the next read, with no
     * receive needed in between. */
    assert(qset_can_controller_start(&controller) == QSET_CAN_DRIVER_OK);
    expect_state_and_fault(&controller, QSET_CAN_CONTROLLER_ERROR_ACTIVE,
                           QSET_CAN_DRIVER_FAULT_NONE);
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_ERROR_WARNING) ==
           QSET_CAN_DRIVER_OK);
    expect_state_and_fault(&controller, QSET_CAN_CONTROLLER_ERROR_WARNING,
                           QSET_CAN_DRIVER_FAULT_NONE);
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_ERROR_PASSIVE) ==
           QSET_CAN_DRIVER_OK);
    expect_state_and_fault(&controller, QSET_CAN_CONTROLLER_ERROR_PASSIVE,
                           QSET_CAN_DRIVER_FAULT_NONE);
    assert(qset_can_mock_force_state(&mock, QSET_CAN_CONTROLLER_BUS_OFF) ==
           QSET_CAN_DRIVER_OK);
    expect_state_and_fault(&controller, QSET_CAN_CONTROLLER_BUS_OFF,
                           QSET_CAN_DRIVER_FAULT_BUS_OFF);
    assert(qset_can_controller_recover_bus_off(&controller) == QSET_CAN_DRIVER_OK);
    expect_state_and_fault(&controller, QSET_CAN_CONTROLLER_RECOVERING,
                           QSET_CAN_DRIVER_FAULT_BUS_OFF);

    /* Recovery success does not clear the latest fault. */
    assert(qset_can_mock_complete_recovery(&mock, QSET_CAN_DRIVER_FAULT_NONE) ==
           QSET_CAN_DRIVER_OK);
    expect_state_and_fault(&controller, QSET_CAN_CONTROLLER_ERROR_ACTIVE,
                           QSET_CAN_DRIVER_FAULT_BUS_OFF);

    /* A newer fault replaces it, and successful operations keep it. */
    assert(qset_can_mock_latch_fault(&mock, QSET_CAN_DRIVER_FAULT_HARDWARE) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_transmit(&controller, &frame) == QSET_CAN_DRIVER_OK);
    expect_state_and_fault(&controller, QSET_CAN_CONTROLLER_ERROR_ACTIVE,
                           QSET_CAN_DRIVER_FAULT_HARDWARE);

    /* The snapshot matches the standalone getters. */
    {
        qset_can_controller_state_t state;
        qset_can_driver_fault_t fault;

        assert(qset_can_controller_get_state(&controller, &state) == QSET_CAN_DRIVER_OK);
        assert(qset_can_controller_get_last_fault(&controller, &fault) ==
               QSET_CAN_DRIVER_OK);
        read_diagnostics(&controller, &diagnostics);
        assert(diagnostics.bus_state == state);
        assert(diagnostics.latest_driver_fault == fault);
    }

    /* Counters and the live values come back together, and reading clears
     * neither. */
    assert(qset_can_controller_record_invalid_frame(&controller) == QSET_CAN_DRIVER_OK);
    read_diagnostics(&controller, &diagnostics);
    assert(diagnostics.invalid_frames == 1u);
    assert(diagnostics.bus_state == QSET_CAN_CONTROLLER_ERROR_ACTIVE);
    assert(diagnostics.latest_driver_fault == QSET_CAN_DRIVER_FAULT_HARDWARE);
    read_diagnostics(&controller, &diagnostics);
    assert(diagnostics.invalid_frames == 1u);
    assert(diagnostics.latest_driver_fault == QSET_CAN_DRIVER_FAULT_HARDWARE);

    /* A failed state read returns the error and leaves the output unchanged;
     * the counters are still there on the next call. */
    memset(&diagnostics, 0x5c, sizeof(diagnostics));
    unchanged = diagnostics;
    assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_GET_STATE,
                                   QSET_CAN_DRIVER_FAULT_INTERNAL) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_FAILURE);
    assert(memcmp(&diagnostics, &unchanged, sizeof(diagnostics)) == 0);

    /* The same for a failed fault read. The injected failure itself latches
     * INTERNAL as the latest fault, which the next read then reports. */
    assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_GET_LAST_FAULT,
                                   QSET_CAN_DRIVER_FAULT_INTERNAL) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_FAILURE);
    assert(memcmp(&diagnostics, &unchanged, sizeof(diagnostics)) == 0);
    read_diagnostics(&controller, &diagnostics);
    assert(diagnostics.invalid_frames == 1u);
    assert(diagnostics.latest_driver_fault == QSET_CAN_DRIVER_FAULT_INTERNAL);
}

/* Stands in for a platform critical section. A write made in enter lands
 * before the copy; a write made in exit simulates an ISR that was held off by
 * the masked interrupt and runs right after it is unmasked. */
typedef struct {
    qset_can_controller_t *controller;
    unsigned int enters;
    unsigned int exits;
    int inside;
    int write_on_enter;
    int write_on_exit;
} critical_probe_t;

static critical_probe_t *active_probe;

static void probe_enter(void *context)
{
    critical_probe_t *probe = (critical_probe_t *)context;

    assert(probe == active_probe);
    assert(probe->inside == 0);
    probe->inside = 1;
    probe->enters++;
    if (probe->write_on_enter != 0) {
        assert(qset_can_controller_record_rx_queue_overflow(probe->controller) ==
               QSET_CAN_DRIVER_OK);
    }
}

static void probe_exit(void *context)
{
    critical_probe_t *probe = (critical_probe_t *)context;

    assert(probe == active_probe);
    assert(probe->inside == 1);
    probe->inside = 0;
    probe->exits++;
    if (probe->write_on_exit != 0) {
        assert(qset_can_controller_record_rx_queue_overflow(probe->controller) ==
               QSET_CAN_DRIVER_OK);
    }
}

/* Driver calls must never happen inside the critical section. */
static qset_can_driver_result_t checked_get_state(void *context,
                                                  qset_can_controller_state_t *out_state)
{
    assert(active_probe == 0 || active_probe->inside == 0);
    return qset_can_mock_driver_ops.get_state(context, out_state);
}

static qset_can_driver_result_t checked_get_last_fault(void *context,
                                                       qset_can_driver_fault_t *out_fault)
{
    assert(active_probe == 0 || active_probe->inside == 0);
    return qset_can_mock_driver_ops.get_last_fault(context, out_fault);
}

static void test_consistent_snapshot(void)
{
    qset_can_controller_t controller = {0};
    qset_can_mock_t mock = {0};
    qset_can_driver_ops_t checked_ops = qset_can_mock_driver_ops;
    critical_probe_t probe = {0};
    qset_can_diagnostics_t diagnostics;

    checked_ops.get_state = checked_get_state;
    checked_ops.get_last_fault = checked_get_last_fault;
    probe.controller = &controller;
    active_probe = &probe;

    /* Installing requires both functions or neither. */
    assert(qset_can_controller_set_critical_section(0, probe_enter, probe_exit, &probe) ==
           QSET_CAN_DRIVER_NULL_POINTER);
    assert(qset_can_controller_set_critical_section(&controller, probe_enter, 0, &probe) ==
           QSET_CAN_DRIVER_INVALID_ARGUMENT);
    assert(qset_can_controller_set_critical_section(&controller, 0, probe_exit, &probe) ==
           QSET_CAN_DRIVER_INVALID_ARGUMENT);
    assert(controller.critical_enter == 0 && controller.critical_exit == 0);

    /* Valid before init: one balanced enter/exit per read. */
    assert(qset_can_controller_set_critical_section(&controller, probe_enter, probe_exit,
                                                    &probe) == QSET_CAN_DRIVER_OK);
    read_diagnostics(&controller, &diagnostics);
    assert(probe.enters == 1u && probe.exits == 1u && probe.inside == 0);

    /* Init keeps the installed section. */
    assert(qset_can_controller_init(&controller, &checked_ops, &mock, &primary_config) ==
           QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_start(&controller) == QSET_CAN_DRIVER_OK);
    read_diagnostics(&controller, &diagnostics);
    assert(probe.enters == 2u && probe.exits == 2u);
    assert(diagnostics.bus_state == QSET_CAN_CONTROLLER_ERROR_ACTIVE);

    /* A write held off until after the copy is not in this snapshot, but it
     * is not lost: the next snapshot has it. */
    probe.write_on_exit = 1;
    read_diagnostics(&controller, &diagnostics);
    probe.write_on_exit = 0;
    assert(diagnostics.rx_queue_overflows == 0u);
    read_diagnostics(&controller, &diagnostics);
    assert(diagnostics.rx_queue_overflows == 1u);

    /* A write that lands before the copy is in the snapshot. Together with the
     * case above, this shows the copy happens between enter and exit. */
    probe.write_on_enter = 1;
    read_diagnostics(&controller, &diagnostics);
    probe.write_on_enter = 0;
    assert(diagnostics.rx_queue_overflows == 2u);

    /* Reading does not clear, and the section stays balanced. */
    read_diagnostics(&controller, &diagnostics);
    assert(diagnostics.rx_queue_overflows == 2u);
    assert(probe.enters == probe.exits && probe.inside == 0);

    /* A failed live read still leaves the section balanced. */
    assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_GET_STATE,
                                   QSET_CAN_DRIVER_FAULT_INTERNAL) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_FAILURE);
    assert(probe.enters == probe.exits && probe.inside == 0);

    /* Rejected arguments do not touch the section. */
    {
        unsigned int enters = probe.enters;

        assert(qset_can_controller_get_diagnostics(&controller, 0) ==
               QSET_CAN_DRIVER_NULL_POINTER);
        assert(probe.enters == enters && probe.exits == enters);
    }

    /* Removing the section: reads work without it and the probe is unused. */
    assert(qset_can_controller_set_critical_section(&controller, 0, 0, 0) ==
           QSET_CAN_DRIVER_OK);
    {
        unsigned int enters = probe.enters;

        read_diagnostics(&controller, &diagnostics);
        assert(diagnostics.rx_queue_overflows == 2u);
        assert(probe.enters == enters);
    }

    active_probe = 0;
}

int main(void)
{
    test_layout_and_lifecycle();
    test_round_trip_and_output_guarantees();
    test_validation();
    test_fifo_and_channel_isolation();
    test_states_faults_and_recovery();
    test_injected_control_failures();
    test_rx_controller_overruns();
    test_rx_queue_overflows();
    test_tx_queue_full_failures();
    test_invalid_frames();
    test_response_correlation_counters();
    test_suppressed_violation_reports();
    test_controller_error_events();
    test_recovery_attempts_and_failures();
    test_diagnostics_bus_state_and_latest_fault();
    test_consistent_snapshot();
    puts("Portable C CAN transport: passed");
    return 0;
}
