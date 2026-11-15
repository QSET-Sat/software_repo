#ifndef QSET_CAN_TRANSPORT_H
#define QSET_CAN_TRANSPORT_H

#include "qset_can.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    QSET_CAN_BUS_PRIMARY = 0,
    QSET_CAN_BUS_BACKUP = 1
} qset_can_bus_channel_t;

typedef enum {
    QSET_CAN_CONTROLLER_UNINITIALIZED = 0,
    QSET_CAN_CONTROLLER_STOPPED = 1,
    QSET_CAN_CONTROLLER_ERROR_ACTIVE = 2,
    QSET_CAN_CONTROLLER_ERROR_WARNING = 3,
    QSET_CAN_CONTROLLER_ERROR_PASSIVE = 4,
    QSET_CAN_CONTROLLER_BUS_OFF = 5,
    QSET_CAN_CONTROLLER_RECOVERING = 6
} qset_can_controller_state_t;

typedef enum {
    QSET_CAN_DRIVER_OK = 0,
    QSET_CAN_DRIVER_NULL_POINTER = -1,
    QSET_CAN_DRIVER_INVALID_ARGUMENT = -2,
    QSET_CAN_DRIVER_INVALID_STATE = -3,
    QSET_CAN_DRIVER_NO_DATA = -4,
    QSET_CAN_DRIVER_BUSY = -5,
    QSET_CAN_DRIVER_QUEUE_FULL = -6,
    QSET_CAN_DRIVER_FAILURE = -7
} qset_can_driver_result_t;

typedef enum {
    QSET_CAN_DRIVER_FAULT_NONE = 0,
    QSET_CAN_DRIVER_FAULT_RX_OVERRUN = 1,
    QSET_CAN_DRIVER_FAULT_TX_FAILURE = 2,
    QSET_CAN_DRIVER_FAULT_BUS_OFF = 3,
    QSET_CAN_DRIVER_FAULT_HARDWARE = 4,
    QSET_CAN_DRIVER_FAULT_INTERNAL = 5
} qset_can_driver_fault_t;

typedef struct {
    qset_can_bus_channel_t channel;
    uint32_t nominal_bitrate;
} qset_can_controller_config_t;

/* The timestamp is monotonic microseconds from an adapter-defined epoch.
 * Zero is a valid timestamp and has no wall-clock meaning. */
typedef struct {
    qset_can_frame_t frame;
    qset_can_bus_channel_t channel;
    uint64_t timestamp_us;
} qset_can_rx_item_t;

/* Every callback is required. Implementations map vendor-specific failures to
 * the portable result/fault enums before returning through this interface. */
typedef struct qset_can_driver_ops {
    qset_can_driver_result_t (*initialize)(void *context,
                                           const qset_can_controller_config_t *config);
    qset_can_driver_result_t (*start)(void *context);
    qset_can_driver_result_t (*stop)(void *context);
    qset_can_driver_result_t (*receive)(void *context,
                                        qset_can_frame_t *out_frame,
                                        uint64_t *out_timestamp_us);
    qset_can_driver_result_t (*transmit)(void *context, const qset_can_frame_t *frame);
    qset_can_driver_result_t (*get_state)(void *context,
                                          qset_can_controller_state_t *out_state);
    qset_can_driver_result_t (*get_last_fault)(void *context,
                                               qset_can_driver_fault_t *out_fault);
    qset_can_driver_result_t (*recover_bus_off)(void *context);
} qset_can_driver_ops_t;

/* Statically allocate and zero-initialize this handle before calling init.
 * Callers serialize all access; the transport does not provide locking. */
typedef struct {
    const qset_can_driver_ops_t *ops;
    void *driver_context;
    qset_can_bus_channel_t channel;
    uint32_t nominal_bitrate;
} qset_can_controller_t;

/* All calls are nonblocking and leave output objects unchanged on failure.
 * Init is valid once on a zeroed handle; start requires STOPPED; stop requires
 * any started state; TX requires ERROR_ACTIVE/WARNING/PASSIVE; RX may also
 * drain data during BUS_OFF/RECOVERING; recovery requires BUS_OFF. */
qset_can_driver_result_t qset_can_controller_init(
    qset_can_controller_t *controller,
    const qset_can_driver_ops_t *ops,
    void *driver_context,
    const qset_can_controller_config_t *config);
qset_can_driver_result_t qset_can_controller_start(qset_can_controller_t *controller);
qset_can_driver_result_t qset_can_controller_stop(qset_can_controller_t *controller);
qset_can_driver_result_t qset_can_controller_receive(qset_can_controller_t *controller,
                                                      qset_can_rx_item_t *out_item);
qset_can_driver_result_t qset_can_controller_transmit(qset_can_controller_t *controller,
                                                       const qset_can_frame_t *frame);
qset_can_driver_result_t qset_can_controller_get_state(
    const qset_can_controller_t *controller,
    qset_can_controller_state_t *out_state);
qset_can_driver_result_t qset_can_controller_get_last_fault(
    const qset_can_controller_t *controller,
    qset_can_driver_fault_t *out_fault);
qset_can_driver_result_t qset_can_controller_recover_bus_off(
    qset_can_controller_t *controller);

#ifdef __cplusplus
}
#endif

#endif
