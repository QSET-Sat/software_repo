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
    incomplete_ops.take_rx_overruns = 0;
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
    assert(qset_can_mock_inject_rx_overruns(&mock, 1u) == QSET_CAN_DRIVER_INVALID_STATE);

    init_and_start(&controller, &mock, QSET_CAN_BUS_PRIMARY);

    /* Overruns are collected on receive, even when no frame is waiting. */
    assert(qset_can_mock_inject_rx_overruns(&mock, 3u) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_controller_overruns == 0u);
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_NO_DATA);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_controller_overruns == 3u);

    /* Later events add to the total, and a frame is still delivered. */
    assert(qset_can_controller_transmit(&controller, &frame) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_inject_rx_overruns(&mock, 2u) == QSET_CAN_DRIVER_OK);
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
    assert(qset_can_mock_inject_rx_overruns(&mock, 4u) == QSET_CAN_DRIVER_OK);
    assert(qset_can_mock_fail_next(&mock, QSET_CAN_MOCK_OP_TAKE_RX_OVERRUNS,
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
    assert(qset_can_mock_inject_rx_overruns(&mock, UINT32_MAX) == QSET_CAN_DRIVER_OK);
    assert(qset_can_controller_receive(&controller, &item) == QSET_CAN_DRIVER_NO_DATA);
    assert(qset_can_controller_get_diagnostics(&controller, &diagnostics) ==
           QSET_CAN_DRIVER_OK);
    assert(diagnostics.rx_controller_overruns == UINT32_MAX);
    assert(qset_can_mock_inject_rx_overruns(&mock, 1u) == QSET_CAN_DRIVER_OK);
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
    assert(qset_can_mock_inject_rx_overruns(0, 1u) == QSET_CAN_DRIVER_NULL_POINTER);
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
    puts("Portable C CAN transport: passed");
    return 0;
}
