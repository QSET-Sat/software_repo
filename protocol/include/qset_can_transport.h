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

/* Controller events counted by the driver since the previous successful
 * take_events call. Each field saturates at UINT32_MAX in the driver.
 * - rx_overruns: frames the controller dropped because its hardware receive
 *   buffer was full; one per overrun indication seen.
 * - warning_events: entries into ERROR_WARNING from ERROR_ACTIVE.
 * - passive_events: entries into ERROR_PASSIVE from ERROR_ACTIVE or
 *   ERROR_WARNING.
 * - bus_off_events: entries into BUS_OFF from ERROR_ACTIVE, ERROR_WARNING,
 *   or ERROR_PASSIVE. A recovery attempt that ends back in BUS_OFF is a
 *   recovery failure, not a new bus-off event.
 * - recovery_failures: accepted recovery attempts that ended back in BUS_OFF
 *   (RECOVERING to BUS_OFF). Do not count a recover_bus_off call the driver
 *   rejected with an error; the transport counts that one itself.
 * One transition counts once, for the state entered. Moves toward a healthier
 * state are not counted. */
typedef struct {
    uint32_t rx_overruns;
    uint32_t warning_events;
    uint32_t passive_events;
    uint32_t bus_off_events;
    uint32_t recovery_failures;
} qset_can_driver_events_t;

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
    /* Reports controller events. The driver counts every event in its own
     * storage (normally from its ISR) as defined by qset_can_driver_events_t.
     * This call returns all counts accumulated since the previous successful
     * call and resets them to zero. The read and reset of all fields must be
     * one atomic step with respect to the driver's ISR, for example by masking
     * the CAN interrupt around them, so no event is lost. On failure,
     * out_events is unchanged and the driver keeps its counts for the next
     * call. See docs/FREERTOS_DESIGN.md, "Implementing controller event
     * reporting". */
    qset_can_driver_result_t (*take_events)(void *context,
                                            qset_can_driver_events_t *out_events);
} qset_can_driver_ops_t;

/* Local transport diagnostics. Counters saturate at UINT32_MAX and never wrap.
 * They reset only when the controller handle is re-zeroed. */
typedef struct {
    /* Controller events collected from the driver; see
     * qset_can_driver_events_t for what each one counts. */
    uint32_t rx_controller_overruns;
    uint32_t controller_warning_events;
    uint32_t controller_passive_events;
    uint32_t bus_off_events;
    /* Bus-off recovery. Attempts: recover_bus_off calls that reached the
     * driver (the controller was BUS_OFF). Failures: attempts the driver
     * rejected immediately, counted by the transport, plus accepted attempts
     * that later ended back in BUS_OFF, counted by the driver. Calls rejected
     * before reaching the driver (wrong state, state read error) count as
     * neither. */
    uint32_t recovery_attempts;
    uint32_t recovery_failures;
    /* Received frames dropped because the RX queue was full. One per frame. */
    uint32_t rx_queue_overflows;
    /* Send requests refused because the TX queue was full. One per refusal. */
    uint32_t tx_queue_full_failures;
    /* Received frames discarded as invalid, one per frame. Counts frames with
     * an impossible Classic CAN shape (ID above 0x7FF or DLC above 8), which
     * receive drops automatically, and frames the protocol layer rejects,
     * reported through qset_can_controller_record_invalid_frame. A frame is
     * counted at only one of those levels. Frames rejected by hardware
     * acceptance filters and invalid frames passed to transmit are not counted. */
    uint32_t invalid_frames;
    /* Request correlation results for status responses (nonzero sequence).
     * Only the OBC issues requests in version 1, so these stay zero on other
     * nodes. Each ignored response is counted in exactly one of them:
     * late: matches a request already failed after its final timeout;
     * duplicate: matches a request that already received its response;
     * unexpected: matches no outstanding, completed, or failed request. */
    uint32_t late_responses;
    uint32_t duplicate_responses;
    uint32_t unexpected_sequences;
    /* Protocol violation reports this node decided to send but skipped
     * because the per reason/offending-ID limit (1 per second) or the global
     * limit (5 per second) was reached. One per skipped report. Reports lost
     * to bus-off or a full TX queue are not counted here. */
    uint32_t suppressed_violation_reports;
    /* Filled by qset_can_controller_get_diagnostics with live values read
     * from the driver at the time of the call; not stored by the transport.
     * Before init they read UNINITIALIZED and FAULT_NONE. */
    qset_can_controller_state_t bus_state;
    qset_can_driver_fault_t latest_driver_fault;
} qset_can_diagnostics_t;

/* Enters or exits a short critical section supplied by the platform, such as a
 * wrapper around FreeRTOS taskENTER_CRITICAL/taskEXIT_CRITICAL. */
typedef void (*qset_can_critical_section_fn_t)(void *context);

/* Statically allocate and zero-initialize this handle before calling init.
 * Callers serialize all access; the transport does not provide locking. The
 * exception is qset_can_controller_record_rx_queue_overflow, which is safe to
 * call from the ISR that feeds the RX queue. The TX queue calls
 * qset_can_controller_record_tx_queue_full inside its own serialization.
 * Diagnostics counters may be written by an ISR or by other tasks while
 * qset_can_controller_get_diagnostics runs; install a critical section with
 * qset_can_controller_set_critical_section so its copy is consistent. */
typedef struct {
    const qset_can_driver_ops_t *ops;
    void *driver_context;
    qset_can_bus_channel_t channel;
    uint32_t nominal_bitrate;
    qset_can_diagnostics_t diagnostics;
    qset_can_critical_section_fn_t critical_enter;
    qset_can_critical_section_fn_t critical_exit;
    void *critical_context;
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
/* Receive collects pending controller events from the driver before reading a
 * frame, including when it then returns NO_DATA or another error. This getter
 * copies the counters without clearing them and adds the current bus state and
 * latest driver fault, read live through get_state and get_last_fault. Before
 * init it returns zero counters, UNINITIALIZED, and FAULT_NONE. If either live
 * read fails, it returns that error and leaves out_diagnostics unchanged; the
 * counters are kept and can be read on a later call.
 * Consistency: when a critical section is installed, all counters are copied
 * inside one enter/exit pair, so the snapshot never mixes values from before
 * and after a concurrent write. The live state and fault reads happen after
 * the exit, so the driver is never called inside the critical section. Call
 * from task context, not from an ISR. */
qset_can_driver_result_t qset_can_controller_get_diagnostics(
    const qset_can_controller_t *controller,
    qset_can_diagnostics_t *out_diagnostics);
/* Installs the critical section the diagnostics getter uses around its counter
 * copy. Pass both functions to install, or both null to remove. The section
 * must be nonblocking and must stop every diagnostics writer, including ISRs
 * and higher-priority tasks (masking interrupts does both on a single core);
 * a mutex is not enough because ISRs cannot take one. It is held only for a
 * fixed-size copy. Valid before or after init. Host tests may leave it unset. */
qset_can_driver_result_t qset_can_controller_set_critical_section(
    qset_can_controller_t *controller,
    qset_can_critical_section_fn_t enter_fn,
    qset_can_critical_section_fn_t exit_fn,
    void *context);
/* Called by the RX queue push when the queue is full: the new frame is dropped,
 * already queued frames are kept, and this is called once for that frame.
 * Nonblocking and ISR-safe. Valid before init; the counter saturates. */
qset_can_driver_result_t qset_can_controller_record_rx_queue_overflow(
    qset_can_controller_t *controller);
/* Called by the TX queue enqueue when the queue is full: the frame is refused,
 * the caller gets an immediate error, and this is called once for that
 * refusal. Several tasks may send, so the TX queue must call this while it
 * holds the same lock or critical section it uses to check for full; the
 * increment is not atomic on its own. Do not call it when the TX task finds
 * the controller busy, because that is a normal wait, not a refused send.
 * Nonblocking. Valid before init; the counter saturates. */
qset_can_driver_result_t qset_can_controller_record_tx_queue_full(
    qset_can_controller_t *controller);
/* Called by the RX task once for each received frame that protocol validation
 * rejects (for example an unassigned ID, unregistered node, wrong DLC for the
 * message, or invalid payload value). Receive already counts frames with an
 * impossible shape, so do not call this for frames receive rejected. Call it
 * from the RX task only. Nonblocking. Valid before init; the counter
 * saturates. */
qset_can_driver_result_t qset_can_controller_record_invalid_frame(
    qset_can_controller_t *controller);
/* Request correlation reporting. For each status response with a nonzero
 * sequence from node N, the correlation logic accepts it if it matches N's
 * outstanding request; otherwise it ignores it and calls exactly one of:
 * - record_late_response: the sequence matches N's last request that failed
 *   after its final timeout;
 * - record_duplicate_response: the sequence matches N's last request that was
 *   already answered;
 * - record_unexpected_sequence: anything else.
 * A response to an earlier attempt that arrives while the same sequence is
 * still outstanding is accepted, not late. Call from the task that owns
 * correlation only. Nonblocking. Valid before init; counters saturate. */
qset_can_driver_result_t qset_can_controller_record_late_response(
    qset_can_controller_t *controller);
qset_can_driver_result_t qset_can_controller_record_duplicate_response(
    qset_can_controller_t *controller);
qset_can_driver_result_t qset_can_controller_record_unexpected_sequence(
    qset_can_controller_t *controller);
/* Called by the protocol violation reporter once for each report it would send
 * but skips because the per reason/offending-ID or global rate limit is
 * reached. Do not call it for violations this node is not responsible for
 * reporting, or for reports lost to bus-off or a full TX queue. Call from the
 * task that owns violation reporting only. Nonblocking. Valid before init;
 * the counter saturates. */
qset_can_driver_result_t qset_can_controller_record_suppressed_violation_report(
    qset_can_controller_t *controller);

#ifdef __cplusplus
}
#endif

#endif
