/* Host tests for the RX path (qset_can_rx). `make rx-test` builds this file
 * once per node with -Ibuild/node/<NODE>. Every run sends the same traffic
 * and checks what that node should do with it, using the rules and the
 * assumptions (A1 to A5) listed in qset_can_rx.h.
 *
 * Notes on the tests themselves:
 * - CHECK is used instead of assert(). assert() calls disappear when NDEBUG
 *   is defined, so the tests would "pass" without running (JPL Rule 5).
 * - Counters are read with qset_can_controller_get_diagnostics (issue 10).
 * - Frames the mock refuses to queue (ID above 0x7FF, DLC above 8) are either
 *   made with qset_can_mock_corrupt_next_receive or handed straight to
 *   qset_can_rx_process.
 */
#include "qset_can_mock.h"
#include "qset_can_rx.h"
#include "qset_local_node.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_BITRATE 1000000u
#define TEST_TIME_US UINT64_C(5000)

/* Always runs, even with NDEBUG. */
#define CHECK(condition) check((condition) != 0, #condition, __LINE__)

static const char *current_case = "setup";

static void check(int passed, const char *text, int line)
{
    if (!passed) {
        fprintf(stderr, "FAILED  %s (0x%x) case \"%s\", line %d: %s\n",
                QSET_LOCAL_NODE_NAME, (unsigned)QSET_LOCAL_NODE_ID, current_case, line, text);
        exit(1);
    }
}

static int local_is(uint8_t node)
{
    return QSET_LOCAL_NODE_ID == node;
}

static int local_is_obc(void)
{
    return local_is(QSET_CAN_NODE_OBC);
}

/* ---- Fake services: they just record what they were given ---- */

typedef struct {
    int handled; /* message handler calls of any kind */
    int heartbeats;
    int violations;
    int requests;
    int statuses;
    int reports;
    uint64_t last_timestamp_us;
    qset_can_heartbeat_t last_heartbeat;
    qset_can_protocol_violation_t last_violation;
    qset_can_request_status_t last_request;
    qset_can_node_status_t last_status;
    qset_can_protocol_violation_t last_report;
} recorder_t;

static void record_heartbeat(void *context, const qset_can_heartbeat_t *message,
                             uint64_t timestamp_us)
{
    recorder_t *rec = (recorder_t *)context;

    rec->handled++;
    rec->heartbeats++;
    rec->last_heartbeat = *message;
    rec->last_timestamp_us = timestamp_us;
}

static void record_violation(void *context, const qset_can_protocol_violation_t *message,
                             uint64_t timestamp_us)
{
    recorder_t *rec = (recorder_t *)context;

    rec->handled++;
    rec->violations++;
    rec->last_violation = *message;
    rec->last_timestamp_us = timestamp_us;
}

static void record_request(void *context, const qset_can_request_status_t *message,
                           uint64_t timestamp_us)
{
    recorder_t *rec = (recorder_t *)context;

    rec->handled++;
    rec->requests++;
    rec->last_request = *message;
    rec->last_timestamp_us = timestamp_us;
}

static void record_status(void *context, const qset_can_node_status_t *message,
                          uint64_t timestamp_us)
{
    recorder_t *rec = (recorder_t *)context;

    rec->handled++;
    rec->statuses++;
    rec->last_status = *message;
    rec->last_timestamp_us = timestamp_us;
}

static void record_report(void *context, const qset_can_protocol_violation_t *report)
{
    recorder_t *rec = (recorder_t *)context;

    rec->reports++;
    rec->last_report = *report;
}

static const qset_can_rx_handlers_t all_handlers = {
    .on_heartbeat = record_heartbeat,
    .on_protocol_violation = record_violation,
    .on_request_status = record_request,
    .on_node_status = record_status,
    .report_violation = record_report,
};

/* Only the handlers this node calls; the rest are NULL. */
static qset_can_rx_handlers_t needed_handlers(void)
{
    qset_can_rx_handlers_t handlers = all_handlers;

    if (local_is_obc()) {
        handlers.on_request_status = 0;
    } else {
        handlers.on_protocol_violation = 0;
        handlers.on_node_status = 0;
    }
    return handlers;
}

/* ---- Test frames ---- */

/* Golden vectors from protocol/SPEC.md. */
static const qset_can_frame_t heartbeat_adcs = {.id = 0x002u, .dlc = 2u, .data = {0x01u, 0x01u}};
static const qset_can_frame_t violation_adcs = {
    .id = 0x120u, .dlc = 4u, .data = {0x02u, 0x20u, 0x02u, 0x09u}};
static const qset_can_frame_t request_adcs = {.id = 0x220u, .dlc = 1u, .data = {0x2au}};
static const qset_can_frame_t status_adcs = {
    .id = 0x420u, .dlc = 7u, .data = {0x2au, 0x01u, 0x80u, 0x51u, 0x01u, 0x00u, 0x00u}};

/* The OBC's own heartbeat, built the same way. */
static const qset_can_frame_t heartbeat_obc = {.id = 0x001u, .dlc = 2u, .data = {0x01u, 0x01u}};

/* ---- Helpers ---- */

typedef struct {
    qset_can_controller_t controller;
    qset_can_mock_t mock;
    qset_can_rx_t rx;
    recorder_t rec;
} fixture_t;

typedef struct {
    qset_can_rx_result_t result;
    qset_can_diagnostics_t diag;
    recorder_t rec;
} outcome_t;

static void setup_controller(fixture_t *f)
{
    static const qset_can_controller_config_t config = {
        .channel = QSET_CAN_BUS_PRIMARY,
        .nominal_bitrate = TEST_BITRATE,
    };

    memset(f, 0, sizeof(*f));
    CHECK(qset_can_controller_init(&f->controller, &qset_can_mock_driver_ops, &f->mock,
                                   &config) == QSET_CAN_DRIVER_OK);
    CHECK(qset_can_controller_start(&f->controller) == QSET_CAN_DRIVER_OK);
    CHECK(qset_can_mock_set_time_us(&f->mock, TEST_TIME_US) == QSET_CAN_DRIVER_OK);
}

static void setup(fixture_t *f)
{
    setup_controller(f);
    CHECK(qset_can_rx_init(&f->rx, &f->controller, &all_handlers, &f->rec) == QSET_CAN_RX_OK);
}

static qset_can_diagnostics_t diag_of(const fixture_t *f)
{
    qset_can_diagnostics_t diag;

    CHECK(qset_can_controller_get_diagnostics(&f->controller, &diag) == QSET_CAN_DRIVER_OK);
    return diag;
}

/* Sends one frame through the mock adapter on a fresh RX path. */
static outcome_t send_one(const char *name, const qset_can_frame_t *frame)
{
    fixture_t f;
    outcome_t out;

    current_case = name;
    setup(&f);
    CHECK(qset_can_mock_inject_receive(&f.mock, frame) == QSET_CAN_DRIVER_OK);
    out.result = qset_can_rx_poll(&f.rx);
    CHECK(qset_can_rx_poll(&f.rx) == QSET_CAN_RX_NO_FRAME);
    out.diag = diag_of(&f);
    out.rec = f.rec;
    return out;
}

/* Every counter is zero except invalid_frames, which must equal invalid. */
static void check_only_invalid(const qset_can_diagnostics_t *d, uint32_t invalid)
{
    CHECK(d->invalid_frames == invalid);
    CHECK(d->rx_controller_overruns == 0u && d->controller_warning_events == 0u);
    CHECK(d->controller_passive_events == 0u && d->bus_off_events == 0u);
    CHECK(d->recovery_attempts == 0u && d->recovery_failures == 0u);
    CHECK(d->rx_queue_overflows == 0u && d->tx_queue_full_failures == 0u);
    CHECK(d->late_responses == 0u && d->duplicate_responses == 0u);
    CHECK(d->unexpected_sequences == 0u && d->suppressed_violation_reports == 0u);
}

/* A valid frame: delivered if this node wants it, otherwise ignored. Nothing
 * is counted and nothing is reported either way. */
static void check_valid(const outcome_t *out, int wanted)
{
    CHECK(out->result == (wanted ? QSET_CAN_RX_OK : QSET_CAN_RX_IGNORED));
    CHECK(out->rec.handled == (wanted ? 1 : 0));
    CHECK(out->rec.reports == 0);
    check_only_invalid(&out->diag, 0u);
    if (wanted) {
        CHECK(out->rec.last_timestamp_us == TEST_TIME_US);
    }
}

/* An invalid frame: counted once as invalid and nothing else, never
 * delivered, and reported only if this node owns the error. */
static void check_rejected(const outcome_t *out, int owner, uint8_t reason,
                           const qset_can_frame_t *frame)
{
    CHECK(out->result == QSET_CAN_RX_REJECTED);
    CHECK(out->rec.handled == 0);
    check_only_invalid(&out->diag, 1u);
    if (!owner) {
        CHECK(out->rec.reports == 0);
        return;
    }
    CHECK(out->rec.reports == 1);
    CHECK(out->rec.last_report.source_node == QSET_LOCAL_NODE_ID);
    CHECK(out->rec.last_report.reason == reason);
    CHECK(out->rec.last_report.offending_id == frame->id);
    CHECK(out->rec.last_report.observed_dlc == frame->dlc);
}

#define REASON_UNASSIGNED ((uint8_t)QSET_CAN_VIOLATION_REASON_UNASSIGNED_MESSAGE_INDEX)
#define REASON_WRONG_DLC ((uint8_t)QSET_CAN_VIOLATION_REASON_WRONG_DLC)
#define REASON_BAD_VALUE ((uint8_t)QSET_CAN_VIOLATION_REASON_INVALID_OR_RESERVED_FIELD_VALUE)

/* ---- Tests ---- */

static void test_api_errors(void)
{
    qset_can_rx_t rx = {0};
    qset_can_controller_t controller = {0};
    qset_can_rx_item_t item;
    qset_can_diagnostics_t diag;
    recorder_t rec = {0};

    current_case = "API errors";
    memset(&item, 0, sizeof(item));
    CHECK(qset_can_rx_poll(&rx) == QSET_CAN_RX_INVALID_STATE);
    CHECK(qset_can_rx_process(&rx, &item) == QSET_CAN_RX_INVALID_STATE);

    CHECK(qset_can_rx_init(0, &controller, &all_handlers, &rec) == QSET_CAN_RX_NULL_POINTER);
    CHECK(qset_can_rx_init(&rx, 0, &all_handlers, &rec) == QSET_CAN_RX_NULL_POINTER);
    CHECK(qset_can_rx_init(&rx, &controller, 0, &rec) == QSET_CAN_RX_NULL_POINTER);
    CHECK(qset_can_rx_init(&rx, &controller, &all_handlers, &rec) == QSET_CAN_RX_OK);
    CHECK(qset_can_rx_init(&rx, &controller, &all_handlers, &rec) == QSET_CAN_RX_INVALID_STATE);

    CHECK(qset_can_rx_poll(0) == QSET_CAN_RX_NULL_POINTER);
    CHECK(qset_can_rx_process(0, &item) == QSET_CAN_RX_NULL_POINTER);
    CHECK(qset_can_rx_process(&rx, 0) == QSET_CAN_RX_NULL_POINTER);

    /* The controller was never set up: a driver error, not a violation. */
    CHECK(qset_can_rx_poll(&rx) == QSET_CAN_RX_DRIVER_ERROR);
    CHECK(qset_can_controller_get_diagnostics(&controller, &diag) == QSET_CAN_DRIVER_OK);
    check_only_invalid(&diag, 0u);
    CHECK(rec.reports == 0 && rec.handled == 0);
}

static void check_init_rejects(qset_can_rx_handlers_t handlers)
{
    qset_can_rx_t rx = {0};
    qset_can_controller_t controller = {0};
    recorder_t rec = {0};

    CHECK(qset_can_rx_init(&rx, &controller, &handlers, &rec) == QSET_CAN_RX_INVALID_ARGUMENT);
    CHECK(qset_can_rx_poll(&rx) == QSET_CAN_RX_INVALID_STATE);
}

/* Init rejects a table missing any handler this node calls, accepts one with
 * exactly those, and keeps its own copy of the table. */
static void test_init_requires_needed_handlers(void)
{
    const qset_can_frame_t *heartbeat_from_other =
        local_is_obc() ? &heartbeat_adcs : &heartbeat_obc;
    qset_can_rx_handlers_t handlers;
    fixture_t f;

    current_case = "required handlers";
    handlers = needed_handlers();
    handlers.on_heartbeat = 0;
    check_init_rejects(handlers);
    handlers = needed_handlers();
    handlers.report_violation = 0;
    check_init_rejects(handlers);
    if (local_is_obc()) {
        handlers = needed_handlers();
        handlers.on_protocol_violation = 0;
        check_init_rejects(handlers);
        handlers = needed_handlers();
        handlers.on_node_status = 0;
        check_init_rejects(handlers);
    } else {
        handlers = needed_handlers();
        handlers.on_request_status = 0;
        check_init_rejects(handlers);
    }

    setup_controller(&f);
    handlers = needed_handlers();
    CHECK(qset_can_rx_init(&f.rx, &f.controller, &handlers, &f.rec) == QSET_CAN_RX_OK);
    memset(&handlers, 0, sizeof(handlers)); /* the RX path kept its own copy */
    CHECK(qset_can_mock_inject_receive(&f.mock, heartbeat_from_other) == QSET_CAN_DRIVER_OK);
    CHECK(qset_can_rx_poll(&f.rx) == QSET_CAN_RX_OK);
    CHECK(f.rec.heartbeats == 1);
}

/* Acceptance: valid frames reach the correct handler. */
static void test_valid_frames_reach_the_right_handler(void)
{
    outcome_t out;

    /* Every node uses every other node's heartbeat, but not its own (A1). */
    out = send_one("OBC heartbeat", &heartbeat_obc);
    check_valid(&out, !local_is_obc());
    CHECK(out.rec.heartbeats == (local_is_obc() ? 0 : 1));
    if (!local_is_obc()) {
        CHECK(out.rec.last_heartbeat.source_node == QSET_CAN_NODE_OBC);
        CHECK(out.rec.last_heartbeat.state == QSET_CAN_NODE_STATE_NOMINAL);
    }

    out = send_one("ADCS heartbeat", &heartbeat_adcs);
    check_valid(&out, !local_is(QSET_CAN_NODE_ADCS));
    CHECK(out.rec.heartbeats == (local_is(QSET_CAN_NODE_ADCS) ? 0 : 1));
    if (!local_is(QSET_CAN_NODE_ADCS)) {
        CHECK(out.rec.last_heartbeat.source_node == QSET_CAN_NODE_ADCS);
    }

    /* Only the OBC uses violation reports. */
    out = send_one("ADCS violation report", &violation_adcs);
    check_valid(&out, local_is_obc());
    CHECK(out.rec.violations == (local_is_obc() ? 1 : 0));
    if (local_is_obc()) {
        CHECK(out.rec.last_violation.source_node == QSET_CAN_NODE_ADCS);
        CHECK(out.rec.last_violation.reason == REASON_WRONG_DLC);
        CHECK(out.rec.last_violation.offending_id == 0x220u);
        CHECK(out.rec.last_violation.observed_dlc == 9u);
    }

    /* Only ADCS gets a request addressed to ADCS. */
    out = send_one("request status to ADCS", &request_adcs);
    check_valid(&out, local_is(QSET_CAN_NODE_ADCS));
    CHECK(out.rec.requests == (local_is(QSET_CAN_NODE_ADCS) ? 1 : 0));
    if (local_is(QSET_CAN_NODE_ADCS)) {
        CHECK(out.rec.last_request.destination_node == QSET_CAN_NODE_ADCS);
        CHECK(out.rec.last_request.sequence == 42u);
    }

    /* Only the OBC uses node status. */
    out = send_one("ADCS node status", &status_adcs);
    check_valid(&out, local_is_obc());
    CHECK(out.rec.statuses == (local_is_obc() ? 1 : 0));
    if (local_is_obc()) {
        CHECK(out.rec.last_status.source_node == QSET_CAN_NODE_ADCS);
        CHECK(out.rec.last_status.sequence == 42u);
        CHECK(out.rec.last_status.state == QSET_CAN_NODE_STATE_NOMINAL);
        CHECK(out.rec.last_status.uptime_s == 86400u);
        CHECK(out.rec.last_status.active_faults == 0u);
    }
}

/* Acceptance: commands are delivered only to their destination. */
static void test_commands_only_reach_their_destination(void)
{
    static const uint8_t nodes[] = {QSET_CAN_NODE_OBC,   QSET_CAN_NODE_ADCS,
                                    QSET_CAN_NODE_COMMS, QSET_CAN_NODE_PAYLOAD,
                                    QSET_CAN_NODE_BATT,  QSET_CAN_NODE_MPPT};
    size_t i;

    for (i = 0u; i < sizeof(nodes) / sizeof(nodes[0]); i++) {
        qset_can_frame_t frame = {.dlc = 1u, .data = {0x07u}};
        outcome_t out;

        CHECK(qset_can_make_id(QSET_CAN_TIER_COMMAND, nodes[i], 0u, &frame.id) == QSET_CAN_OK);
        out = send_one("request status to each node", &frame);

        if (!local_is(nodes[i])) {
            check_valid(&out, 0);
        } else if (local_is_obc()) {
            /* A command the OBC hears came from someone else (A2). */
            CHECK(out.result == QSET_CAN_RX_REJECTED);
            check_only_invalid(&out.diag, 1u);
            CHECK(out.rec.handled == 0 && out.rec.reports == 0);
        } else {
            check_valid(&out, 1);
            CHECK(out.rec.requests == 1);
            CHECK(out.rec.last_request.destination_node == nodes[i]);
            CHECK(out.rec.last_request.sequence == 7u);
        }
    }
}

/* Acceptance: unknown indexes are rejected at the message layer. */
static void test_unknown_indexes_are_rejected(void)
{
    static const qset_can_frame_t command_index_1 = {.id = 0x221u, .dlc = 1u, .data = {0x2au}};
    static const qset_can_frame_t command_index_15 = {.id = 0x22fu, .dlc = 0u};
    static const qset_can_frame_t telemetry_index_2 = {.id = 0x432u, .dlc = 7u};
    static const qset_can_frame_t error_index_15 = {
        .id = 0x12fu, .dlc = 4u, .data = {0x02u, 0x20u, 0x02u, 0x01u}};
    outcome_t out;

    /* A bad command is reported by its destination. */
    out = send_one("command index 1 to ADCS", &command_index_1);
    check_rejected(&out, local_is(QSET_CAN_NODE_ADCS), REASON_UNASSIGNED, &command_index_1);

    out = send_one("command index 15 to ADCS", &command_index_15);
    check_rejected(&out, local_is(QSET_CAN_NODE_ADCS), REASON_UNASSIGNED, &command_index_15);

    /* Bad telemetry is reported by the OBC only. */
    out = send_one("COMMS telemetry index 2", &telemetry_index_2);
    check_rejected(&out, local_is_obc(), REASON_UNASSIGNED, &telemetry_index_2);

    /* Not a protocol-violation frame, so the OBC reports it (A3). */
    out = send_one("ADCS error index 15", &error_index_15);
    check_rejected(&out, local_is_obc(), REASON_UNASSIGNED, &error_index_15);
}

/* Acceptance: invalid traffic increments the correct diagnostic. */
static void test_wrong_dlc_and_bad_values(void)
{
    static const qset_can_frame_t request_dlc_2 = {.id = 0x220u, .dlc = 2u, .data = {0x2au}};
    static const qset_can_frame_t request_sequence_0 = {.id = 0x220u, .dlc = 1u, .data = {0x00u}};
    static const qset_can_frame_t heartbeat_dlc_3 = {
        .id = 0x002u, .dlc = 3u, .data = {0x01u, 0x01u}};
    static const qset_can_frame_t heartbeat_version_2 = {
        .id = 0x002u, .dlc = 2u, .data = {0x02u, 0x01u}};
    static const qset_can_frame_t heartbeat_state_5 = {
        .id = 0x002u, .dlc = 2u, .data = {0x01u, 0x05u}};
    static const qset_can_frame_t status_dlc_6 = {
        .id = 0x420u, .dlc = 6u, .data = {0x2au, 0x01u}};
    static const qset_can_frame_t status_state_ff = {
        .id = 0x420u, .dlc = 7u, .data = {0x2au, 0xffu}};
    outcome_t out;

    out = send_one("request status with DLC 2", &request_dlc_2);
    check_rejected(&out, local_is(QSET_CAN_NODE_ADCS), REASON_WRONG_DLC, &request_dlc_2);

    out = send_one("request status with sequence 0", &request_sequence_0);
    check_rejected(&out, local_is(QSET_CAN_NODE_ADCS), REASON_BAD_VALUE, &request_sequence_0);

    out = send_one("heartbeat with DLC 3", &heartbeat_dlc_3);
    check_rejected(&out, local_is_obc(), REASON_WRONG_DLC, &heartbeat_dlc_3);

    out = send_one("heartbeat with version 2", &heartbeat_version_2);
    check_rejected(&out, local_is_obc(), REASON_BAD_VALUE, &heartbeat_version_2);

    out = send_one("heartbeat with reserved state", &heartbeat_state_5);
    check_rejected(&out, local_is_obc(), REASON_BAD_VALUE, &heartbeat_state_5);

    out = send_one("node status with DLC 6", &status_dlc_6);
    check_rejected(&out, local_is_obc(), REASON_WRONG_DLC, &status_dlc_6);

    out = send_one("node status with invalid state", &status_state_ff);
    check_rejected(&out, local_is_obc(), REASON_BAD_VALUE, &status_state_ff);
}

/* ADCS receives request status with DLC 2. Its report must encode to a valid
 * protocol-violation frame: ID 0x120, DLC 4, data 02 20 02 02. */
static void test_report_encodes(void)
{
    static const qset_can_frame_t request_dlc_2 = {.id = 0x220u, .dlc = 2u, .data = {0x2au}};
    static const qset_can_frame_t expected = {
        .id = 0x120u, .dlc = 4u, .data = {0x02u, 0x20u, 0x02u, 0x02u}};
    qset_can_frame_t encoded;
    outcome_t out;

    out = send_one("report encodes", &request_dlc_2);
    if (local_is(QSET_CAN_NODE_ADCS)) {
        CHECK(out.rec.reports == 1);
        CHECK(qset_can_encode_protocol_violation(&out.rec.last_report, &encoded) ==
              QSET_CAN_OK);
        CHECK(encoded.id == expected.id && encoded.dlc == expected.dlc);
        CHECK(memcmp(encoded.data, expected.data, sizeof(encoded.data)) == 0);
    } else {
        CHECK(out.rec.reports == 0);
    }
}

/* Bad protocol-violation frames are counted but never reported, not even by
 * the OBC, so errors cannot cause more errors. */
static void test_error_frames_are_never_reported(void)
{
    static const qset_can_frame_t violation_dlc_3 = {
        .id = 0x120u, .dlc = 3u, .data = {0x02u, 0x20u, 0x02u}};
    static const qset_can_frame_t violation_reason_0 = {
        .id = 0x120u, .dlc = 4u, .data = {0x00u, 0x20u, 0x02u, 0x09u}};
    static const qset_can_frame_t violation_offending_id_0x800 = {
        .id = 0x130u, .dlc = 4u, .data = {0x01u, 0x00u, 0x08u, 0x01u}};
    outcome_t out;

    out = send_one("violation with DLC 3", &violation_dlc_3);
    check_rejected(&out, 0, REASON_WRONG_DLC, &violation_dlc_3);

    out = send_one("violation with reason 0", &violation_reason_0);
    check_rejected(&out, 0, REASON_BAD_VALUE, &violation_reason_0);

    out = send_one("violation with offending ID 0x800", &violation_offending_id_0x800);
    check_rejected(&out, 0, REASON_BAD_VALUE, &violation_offending_id_0x800);
}

/* Bad ID ranges and unregistered nodes: only the OBC reports them, with
 * reason 0x01 (A4). */
static void test_unaddressable_frames(void)
{
    static const qset_can_frame_t heartbeat_node_0 = {
        .id = 0x000u, .dlc = 2u, .data = {0x01u, 0x01u}};
    static const qset_can_frame_t heartbeat_node_7 = {
        .id = 0x007u, .dlc = 2u, .data = {0x01u, 0x01u}};
    static const qset_can_frame_t error_node_7 = {.id = 0x170u, .dlc = 4u};
    static const qset_can_frame_t unassigned_range = {.id = 0x300u, .dlc = 0u};
    outcome_t out;

    out = send_one("heartbeat from reserved node 0", &heartbeat_node_0);
    check_rejected(&out, local_is_obc(), REASON_UNASSIGNED, &heartbeat_node_0);

    out = send_one("heartbeat from unregistered node 7", &heartbeat_node_7);
    check_rejected(&out, local_is_obc(), REASON_UNASSIGNED, &heartbeat_node_7);

    out = send_one("error frame from unregistered node 7", &error_node_7);
    check_rejected(&out, local_is_obc(), REASON_UNASSIGNED, &error_node_7);

    out = send_one("ID in the unassigned 0x300 range", &unassigned_range);
    check_rejected(&out, local_is_obc(), REASON_UNASSIGNED, &unassigned_range);
}

/* Acceptance: hardware errors are not converted into protocol violations.
 * They land in the transport's own counters instead. */
static void test_hardware_errors_are_not_violations(void)
{
    const qset_can_driver_events_t events = {
        .rx_overruns = 2u, .warning_events = 1u, .passive_events = 0u,
        .bus_off_events = 0u, .recovery_failures = 0u};
    fixture_t f;
    qset_can_diagnostics_t diag;

    current_case = "hardware errors";
    setup(&f);
    CHECK(qset_can_mock_inject_receive(&f.mock, &heartbeat_obc) == QSET_CAN_DRIVER_OK);
    CHECK(qset_can_mock_inject_receive(&f.mock, &request_adcs) == QSET_CAN_DRIVER_OK);

    /* Controller overruns and a warning episode, then a failed receive. */
    CHECK(qset_can_mock_inject_events(&f.mock, &events) == QSET_CAN_DRIVER_OK);
    CHECK(qset_can_mock_fail_next(&f.mock, QSET_CAN_MOCK_OP_RECEIVE,
                                  QSET_CAN_DRIVER_FAULT_RX_OVERRUN) == QSET_CAN_DRIVER_OK);
    CHECK(qset_can_rx_poll(&f.rx) == QSET_CAN_RX_DRIVER_ERROR);

    /* The controller goes bus-off during a receive. */
    CHECK(qset_can_mock_fail_next(&f.mock, QSET_CAN_MOCK_OP_RECEIVE,
                                  QSET_CAN_DRIVER_FAULT_BUS_OFF) == QSET_CAN_DRIVER_OK);
    CHECK(qset_can_rx_poll(&f.rx) == QSET_CAN_RX_DRIVER_ERROR);

    /* Queued frames still drain during bus-off. */
    CHECK(qset_can_rx_poll(&f.rx) ==
          (local_is_obc() ? QSET_CAN_RX_IGNORED : QSET_CAN_RX_OK));

    /* The driver hands over an impossible frame (DLC 9). Receive counts it,
     * so the RX path must not count it again or report it. */
    CHECK(qset_can_mock_corrupt_next_receive(&f.mock, 0x220u, 9u) == QSET_CAN_DRIVER_OK);
    CHECK(qset_can_rx_poll(&f.rx) == QSET_CAN_RX_DRIVER_ERROR);
    CHECK(qset_can_rx_poll(&f.rx) == QSET_CAN_RX_NO_FRAME);

    diag = diag_of(&f);
    CHECK(diag.rx_controller_overruns == 2u);
    CHECK(diag.controller_warning_events == 1u);
    CHECK(diag.bus_off_events == 1u);
    CHECK(diag.invalid_frames == 1u);
    CHECK(diag.bus_state == QSET_CAN_CONTROLLER_BUS_OFF);
    CHECK(f.rec.reports == 0);
    CHECK(f.rec.handled == (local_is_obc() ? 0 : 1));
}

/* Impossible frames handed straight to process are counted once and dropped,
 * never reported (A5). A valid item still goes through normally. */
static void test_impossible_items_given_to_process(void)
{
    static const qset_can_frame_t id_too_big = {.id = 0x800u, .dlc = 0u};
    static const qset_can_frame_t dlc_9 = {.id = 0x220u, .dlc = 9u, .data = {0x2au}};
    static const qset_can_frame_t dlc_16 = {.id = 0x220u, .dlc = 16u};
    fixture_t f;
    qset_can_rx_item_t item;

    current_case = "impossible items";
    setup(&f);
    memset(&item, 0, sizeof(item));
    item.channel = QSET_CAN_BUS_PRIMARY;
    item.timestamp_us = UINT64_C(777);

    item.frame = id_too_big;
    CHECK(qset_can_rx_process(&f.rx, &item) == QSET_CAN_RX_DRIVER_ERROR);
    item.frame = dlc_9;
    CHECK(qset_can_rx_process(&f.rx, &item) == QSET_CAN_RX_DRIVER_ERROR);
    item.frame = dlc_16;
    CHECK(qset_can_rx_process(&f.rx, &item) == QSET_CAN_RX_DRIVER_ERROR);
    {
        qset_can_diagnostics_t diag = diag_of(&f);

        check_only_invalid(&diag, 3u);
    }
    CHECK(f.rec.reports == 0 && f.rec.handled == 0);

    item.frame = request_adcs;
    CHECK(qset_can_rx_process(&f.rx, &item) ==
          (local_is(QSET_CAN_NODE_ADCS) ? QSET_CAN_RX_OK : QSET_CAN_RX_IGNORED));
    if (local_is(QSET_CAN_NODE_ADCS)) {
        CHECK(f.rec.requests == 1 && f.rec.last_timestamp_us == UINT64_C(777));
    }
}

int main(void)
{
    test_api_errors();
    test_init_requires_needed_handlers();
    test_valid_frames_reach_the_right_handler();
    test_commands_only_reach_their_destination();
    test_unknown_indexes_are_rejected();
    test_wrong_dlc_and_bad_values();
    test_report_encodes();
    test_error_frames_are_never_reported();
    test_unaddressable_frames();
    test_hardware_errors_are_not_violations();
    test_impossible_items_given_to_process();

    printf("PASSED  %s (0x%x): RX path\n", QSET_LOCAL_NODE_NAME, (unsigned)QSET_LOCAL_NODE_ID);
    return 0;
}
