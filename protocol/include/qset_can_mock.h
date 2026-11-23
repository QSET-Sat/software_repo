#ifndef QSET_CAN_MOCK_H
#define QSET_CAN_MOCK_H

#include "qset_can_transport.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QSET_CAN_MOCK_QUEUE_CAPACITY 16u

typedef enum {
    QSET_CAN_MOCK_OP_INITIALIZE = 0,
    QSET_CAN_MOCK_OP_START = 1,
    QSET_CAN_MOCK_OP_STOP = 2,
    QSET_CAN_MOCK_OP_RECEIVE = 3,
    QSET_CAN_MOCK_OP_TRANSMIT = 4,
    QSET_CAN_MOCK_OP_GET_STATE = 5,
    QSET_CAN_MOCK_OP_GET_LAST_FAULT = 6,
    QSET_CAN_MOCK_OP_RECOVER_BUS_OFF = 7,
    QSET_CAN_MOCK_OP_TAKE_EVENTS = 8
} qset_can_mock_operation_t;

typedef struct {
    qset_can_frame_t frame;
    uint64_t timestamp_us;
} qset_can_mock_queue_entry_t;

/* Public storage keeps the mock allocation-free. Tests should treat fields as
 * private and use the controls below. Zero-initialize before controller init. */
typedef struct {
    qset_can_mock_queue_entry_t queue[QSET_CAN_MOCK_QUEUE_CAPACITY];
    uint64_t current_time_us;
    uint32_t nominal_bitrate;
    qset_can_driver_events_t pending_events;
    qset_can_controller_state_t state;
    qset_can_driver_fault_t last_fault;
    qset_can_bus_channel_t channel;
    qset_can_mock_operation_t failure_operation;
    qset_can_driver_fault_t failure_fault;
    uint8_t queue_head;
    uint8_t queue_count;
    uint16_t corrupt_id;
    uint8_t corrupt_dlc;
    uint8_t corrupt_pending;
    uint8_t initialized;
    uint8_t failure_pending;
} qset_can_mock_t;

extern const qset_can_driver_ops_t qset_can_mock_driver_ops;

qset_can_driver_result_t qset_can_mock_set_time_us(qset_can_mock_t *mock,
                                                    uint64_t timestamp_us);
qset_can_driver_result_t qset_can_mock_inject_receive(qset_can_mock_t *mock,
                                                      const qset_can_frame_t *frame);
qset_can_driver_result_t qset_can_mock_force_state(qset_can_mock_t *mock,
                                                   qset_can_controller_state_t state);
qset_can_driver_result_t qset_can_mock_latch_fault(qset_can_mock_t *mock,
                                                   qset_can_driver_fault_t fault);
/* State changes made by force_state, latch_fault, and injected bus-off
 * failures count warning, error-passive, and bus-off events the way a real
 * driver's ISR would (see qset_can_driver_events_t). This control adds
 * arbitrary extra counts, as if the ISR saw events the state does not show,
 * such as an overrun or a short warning episode. Pending counts saturate at
 * UINT32_MAX until the transport takes them. */
qset_can_driver_result_t qset_can_mock_inject_events(qset_can_mock_t *mock,
                                                     const qset_can_driver_events_t *events);
/* Test-only: the next frame the mock returns from receive has its ID and DLC
 * replaced with these raw values, without validation, so tests can simulate a
 * faulty driver delivering an impossible frame. One shot. */
qset_can_driver_result_t qset_can_mock_corrupt_next_receive(qset_can_mock_t *mock,
                                                            uint16_t id,
                                                            uint8_t dlc);
qset_can_driver_result_t qset_can_mock_fail_next(qset_can_mock_t *mock,
                                                 qset_can_mock_operation_t operation,
                                                 qset_can_driver_fault_t fault);
/* NONE completes successfully into ERROR_ACTIVE. Any other valid fault records
 * a failed recovery, returns the mock to BUS_OFF, and counts one
 * recovery_failures event (not a bus-off event). */
qset_can_driver_result_t qset_can_mock_complete_recovery(
    qset_can_mock_t *mock,
    qset_can_driver_fault_t outcome_fault);

#ifdef __cplusplus
}
#endif

#endif
