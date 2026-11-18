#include <deathboobs/database.h>
#include <deathboobs/record_codec.h>
#include <deathboobs/storage.h>
#include "fake_backend.h"

/* Use real PUNos types, without conflicting host libc typedefs. */
extern int puts(const char *text);

#define STR_INNER(value) #value
#define STR(value) STR_INNER(value)
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            puts("FAIL " __FILE__ ":" STR(__LINE__) ": " #condition); \
            return false; \
        } \
    } while (0)

static void fill(void *destination, u8 value, usize size) {
    u8 *bytes = destination;
    for (usize i = 0; i < size; ++i) bytes[i] = value;
}

static void copy(void *destination, const void *source, usize size) {
    u8 *out = destination;
    const u8 *in = source;
    for (usize i = 0; i < size; ++i) out[i] = in[i];
}

static bool equal(const void *left, const void *right, usize size) {
    const u8 *a = left;
    const u8 *b = right;
    for (usize i = 0; i < size; ++i) if (a[i] != b[i]) return false;
    return true;
}

static String test_path(void) {
    static char path[] = "database-test.db";
    String result = {path, sizeof(path) - 1};
    return result;
}

static ErrorCode open_database(DeathboobsDatabase **database) {
    String path = test_path();
    return deathboobs_database_open(&path, database);
}

static bool fresh_database(DeathboobsDatabase **database) {
    CHECK(fake_memory_live_blocks() == 0);
    CHECK(fake_backend_reset() == CORE_OK);
    CHECK(open_database(database) == CORE_OK);
    return true;
}

static bool close_database(DeathboobsDatabase **database) {
    CHECK(deathboobs_database_close(database) == CORE_OK);
    CHECK(*database == NULL);
    CHECK(fake_memory_live_blocks() == 0);
    return true;
}

static DeathboobsDetectionInput detection_input(DeathboobsSessionId session,
                                                DeathboobsTimestamp timestamp) {
    DeathboobsDetectionInput input = {0};
    input.session_id = session;
    input.timestamp = timestamp;
    input.x = -1234;
    input.y = 5678;
    input.azimuth = 12345;
    input.distance = 23456;
    input.confidence = 9876;
    input.has_target_id = true;
    input.target_id = 123;
    input.has_frame_reference = true;
    input.frame_reference_size = 3;
    input.frame_reference[0] = 'a';
    input.frame_reference[1] = 0;
    input.frame_reference[2] = 'z';
    return input;
}

static bool matches_input(const DeathboobsDetection *record,
                          const DeathboobsDetectionInput *input) {
    return record->session_id == input->session_id &&
           record->timestamp == input->timestamp &&
           record->x == input->x && record->y == input->y &&
           record->azimuth == input->azimuth &&
           record->distance == input->distance &&
           record->confidence == input->confidence &&
           record->has_target_id == input->has_target_id &&
           record->target_id == input->target_id &&
           record->has_frame_reference == input->has_frame_reference &&
           record->frame_reference_size == input->frame_reference_size &&
           equal(record->frame_reference, input->frame_reference,
                 sizeof(record->frame_reference));
}

typedef struct Seen {
    DeathboobsDetectionId ids[32];
    DeathboobsTimestamp timestamps[32];
    usize count;
    ErrorCode stop;
} Seen;

static ErrorCode collect(const DeathboobsDetection *record, void *context) {
    Seen *seen = context;
    if (seen->count >= 32) return CORE_OVERFLOW;
    seen->ids[seen->count] = record->id;
    seen->timestamps[seen->count] = record->timestamp;
    ++seen->count;
    return seen->stop;
}

static ErrorCode no_op(const DeathboobsDetection *record, void *context) {
    (void)record;
    (void)context;
    return CORE_OK;
}

static bool seed_session(DeathboobsSessionId id, DeathboobsTimestamp start,
                         bool finished, DeathboobsTimestamp end) {
    DeathboobsGameSession record = {0};
    DeathboobsPage page = {0};
    usize size = 0;
    record.id = id;
    record.started_at = start;
    record.has_finished_at = finished;
    record.finished_at = finished ? end : 0;
    record.status = finished ? DEATHBOOBS_SESSION_STATUS_FINISHED :
                               DEATHBOOBS_SESSION_STATUS_ACTIVE;
    page.type = DEATHBOOBS_PAGE_TYPE_SESSIONS;
    CHECK(deathboobs_game_session_encode(&record, page.payload,
          sizeof(page.payload), &size) == CORE_OK);
    page.payload_size = (u32)size;
    CHECK(fake_storage_seed_page(&page) == CORE_OK);
    return true;
}

static bool seed_detection(DeathboobsDetectionId id, DeathboobsSessionId session,
                           DeathboobsTimestamp timestamp) {
    DeathboobsDetection record = {0};
    DeathboobsPage page = {0};
    usize size = 0;
    record.id = id;
    record.session_id = session;
    record.timestamp = timestamp;
    page.type = DEATHBOOBS_PAGE_TYPE_DETECTIONS;
    CHECK(deathboobs_detection_encode(&record, page.payload,
          sizeof(page.payload), &size) == CORE_OK);
    page.payload_size = (u32)size;
    CHECK(fake_storage_seed_page(&page) == CORE_OK);
    return true;
}

static bool test_sessions_and_detections(void) {
    DeathboobsDatabase *database = NULL;
    DeathboobsGameSession session, found;
    DeathboobsDetection detection, loaded;
    CHECK(fresh_database(&database));
    CHECK(deathboobs_database_start_session(database, 100, &session) == CORE_OK);
    CHECK(session.id == 1 && session.started_at == 100);
    CHECK(!session.has_finished_at && session.finished_at == 0);
    CHECK(session.status == DEATHBOOBS_SESSION_STATUS_ACTIVE);
    CHECK(deathboobs_database_get_session(database, 1, &found) == CORE_OK);
    CHECK(found.id == 1 && found.started_at == 100 && !found.has_finished_at);
    DeathboobsDetectionInput input = detection_input(session.id, 150);
    CHECK(deathboobs_database_save_detection(database, &input, &detection) == CORE_OK);
    CHECK(detection.id == 1 && matches_input(&detection, &input));
    CHECK(deathboobs_database_get_detection(database, 1, &loaded) == CORE_OK);
    CHECK(loaded.id == 1 && matches_input(&loaded, &input));
    CHECK(deathboobs_database_finish_session(database, 1, 149) == CORE_INVALID_ARGUMENT);
    CHECK(deathboobs_database_finish_session(database, 1, 150) == CORE_OK);
    CHECK(deathboobs_database_get_session(database, 1, &found) == CORE_OK);
    CHECK(found.has_finished_at && found.finished_at == 150);
    CHECK(found.status == DEATHBOOBS_SESSION_STATUS_FINISHED);
    CHECK(deathboobs_database_finish_session(database, 1, 160) == CORE_INVALID_STATE);
    input.timestamp = 160;
    CHECK(deathboobs_database_save_detection(database, &input, &loaded) == CORE_INVALID_STATE);
    CHECK(fake_storage_committed_page_count() == 3);
    CHECK(close_database(&database));
    CHECK(deathboobs_database_close(&database) == CORE_OK);
    CHECK(deathboobs_database_close(NULL) == CORE_INVALID_ARGUMENT);
    return true;
}

static bool test_invalid_input_and_missing_ids(void) {
    DeathboobsDatabase *database = NULL;
    DeathboobsGameSession session, session_out;
    DeathboobsDetection detection_out;
    u8 session_before[sizeof(session_out)], detection_before[sizeof(detection_out)];
    CHECK(fresh_database(&database));
    fill(&session_out, 0xa5, sizeof(session_out));
    fill(&detection_out, 0xa5, sizeof(detection_out));
    copy(session_before, &session_out, sizeof(session_out));
    copy(detection_before, &detection_out, sizeof(detection_out));
    CHECK(deathboobs_database_get_session(database, 9, &session_out) == CORE_NOT_FOUND);
    CHECK(deathboobs_database_get_session(database, 0, &session_out) == CORE_INVALID_ARGUMENT);
    CHECK(equal(&session_out, session_before, sizeof(session_out)));
    CHECK(deathboobs_database_get_detection(database, 9, &detection_out) == CORE_NOT_FOUND);
    CHECK(deathboobs_database_get_detection(database, 0, &detection_out) == CORE_INVALID_ARGUMENT);
    CHECK(equal(&detection_out, detection_before, sizeof(detection_out)));
    CHECK(deathboobs_database_finish_session(database, 9, 100) == CORE_NOT_FOUND);
    CHECK(deathboobs_database_finish_session(database, 0, 100) == CORE_INVALID_ARGUMENT);
    CHECK(deathboobs_database_start_session(NULL, 100, &session) == CORE_INVALID_ARGUMENT);
    CHECK(deathboobs_database_start_session(database, 100, NULL) == CORE_INVALID_ARGUMENT);
    CHECK(deathboobs_database_get_session(database, 1, NULL) == CORE_INVALID_ARGUMENT);
    CHECK(deathboobs_database_get_detection(database, 1, NULL) == CORE_INVALID_ARGUMENT);
    CHECK(deathboobs_database_finish_session(NULL, 1, 100) == CORE_INVALID_ARGUMENT);
    DeathboobsDetectionInput input = detection_input(9, 100);
    CHECK(deathboobs_database_save_detection(database, &input, &detection_out) == CORE_NOT_FOUND);
    CHECK(equal(&detection_out, detection_before, sizeof(detection_out)));
    input.session_id = 0;
    CHECK(deathboobs_database_save_detection(database, &input, &detection_out) == CORE_INVALID_ARGUMENT);
    CHECK(deathboobs_database_save_detection(database, NULL, &detection_out) == CORE_INVALID_ARGUMENT);
    CHECK(deathboobs_database_save_detection(database, &input, NULL) == CORE_INVALID_ARGUMENT);
    CHECK(deathboobs_database_save_detection(NULL, &input, &detection_out) == CORE_INVALID_ARGUMENT);
    CHECK(fake_storage_committed_page_count() == 0);
    CHECK(deathboobs_database_start_session(database, 100, &session) == CORE_OK);
    input = detection_input(session.id, 99);
    CHECK(deathboobs_database_save_detection(database, &input, &detection_out) == CORE_INVALID_ARGUMENT);
    input.timestamp = 100;
    input.distance = DEATHBOOBS_DISTANCE_MAX + 1;
    CHECK(deathboobs_database_save_detection(database, &input, &detection_out) == CORE_INVALID_ARGUMENT);
    input = detection_input(session.id, 100);
    input.frame_reference_size = 257;
    CHECK(deathboobs_database_save_detection(database, &input, &detection_out) == CORE_INVALID_ARGUMENT);
    input = detection_input(session.id, 100);
    input.frame_reference[255] = 1;
    CHECK(deathboobs_database_save_detection(database, &input, &detection_out) == CORE_INVALID_ARGUMENT);
    CHECK(equal(&detection_out, detection_before, sizeof(detection_out)));
    CHECK(deathboobs_database_finish_session(database, session.id, 99) == CORE_INVALID_ARGUMENT);
    CHECK(fake_storage_committed_page_count() == 1);
    CHECK(close_database(&database));
    return true;
}

static bool test_history_time_and_reopen(void) {
    DeathboobsDatabase *database = NULL;
    DeathboobsGameSession a, b, empty, found;
    DeathboobsDetection record;
    Seen seen = {0};
    CHECK(fresh_database(&database));
    CHECK(deathboobs_database_start_session(database, 100, &a) == CORE_OK);
    CHECK(deathboobs_database_start_session(database, 50, &b) == CORE_OK);
    CHECK(deathboobs_database_start_session(database, 0, &empty) == CORE_OK);
    const DeathboobsSessionId sessions[] = {1, 2, 1, 1, 2};
    const DeathboobsTimestamp times[] = {300, 200, 200, 200, 400};
    for (usize i = 0; i < 5; ++i) {
        DeathboobsDetectionInput input = detection_input(sessions[i], times[i]);
        CHECK(deathboobs_database_save_detection(database, &input, &record) == CORE_OK);
        CHECK(record.id == i + 1);
    }
    CHECK(deathboobs_database_finish_session(database, a.id, 300) == CORE_OK);
    for (usize pass = 0; pass < 2; ++pass) {
        if (pass != 0) {
            CHECK(close_database(&database));
            CHECK(open_database(&database) == CORE_OK);
        }
        seen = (Seen){0};
        CHECK(deathboobs_database_visit_session_history(database, a.id, collect, &seen) == CORE_OK);
        CHECK(seen.count == 3 && seen.ids[0] == 3 && seen.ids[1] == 4 && seen.ids[2] == 1);
        CHECK(seen.timestamps[0] == 200 && seen.timestamps[2] == 300);
        seen = (Seen){0};
        CHECK(deathboobs_database_visit_session_history(database, b.id, collect, &seen) == CORE_OK);
        CHECK(seen.count == 2 && seen.ids[0] == 2 && seen.ids[1] == 5);
        seen = (Seen){0};
        CHECK(deathboobs_database_visit_session_history(database, empty.id, collect, &seen) == CORE_OK);
        CHECK(seen.count == 0);
        CHECK(deathboobs_database_visit_session_history(database, 99, collect, &seen) == CORE_NOT_FOUND);
        CHECK(deathboobs_database_visit_session_history(database, 0, collect, &seen) == CORE_INVALID_ARGUMENT);
        CHECK(deathboobs_database_visit_session_history(database, a.id, NULL, &seen) == CORE_INVALID_ARGUMENT);
        seen = (Seen){0};
        CHECK(deathboobs_database_visit_detections_between(database, 200, 300, collect, &seen) == CORE_OK);
        CHECK(seen.count == 4 && seen.ids[0] == 2 && seen.ids[1] == 3 && seen.ids[2] == 4 && seen.ids[3] == 1);
        seen = (Seen){0};
        CHECK(deathboobs_database_visit_detections_between(database, 200, 200, collect, &seen) == CORE_OK);
        CHECK(seen.count == 3 && seen.ids[0] == 2 && seen.ids[2] == 4);
        seen = (Seen){0};
        CHECK(deathboobs_database_visit_detections_between(database, 0, 199, collect, &seen) == CORE_OK);
        CHECK(seen.count == 0);
        CHECK(deathboobs_database_visit_detections_between(database, 300, 200, collect, &seen) == CORE_INVALID_ARGUMENT);
        CHECK(deathboobs_database_visit_detections_between(database, 0, DEATHBOOBS_TIMESTAMP_MAX, no_op, NULL) == CORE_OK);
        CHECK(deathboobs_database_get_session(database, a.id, &found) == CORE_OK);
        CHECK(found.status == DEATHBOOBS_SESSION_STATUS_FINISHED && found.finished_at == 300);
        CHECK(deathboobs_database_get_detection(database, 5, &record) == CORE_OK);
        CHECK(record.timestamp == 400 && record.session_id == b.id);
    }
    CHECK(deathboobs_database_start_session(database, 500, &found) == CORE_OK);
    CHECK(found.id == 4);
    DeathboobsDetectionInput next = detection_input(b.id, 500);
    CHECK(deathboobs_database_save_detection(database, &next, &record) == CORE_OK);
    CHECK(record.id == 6);
    CHECK(close_database(&database));
    return true;
}

typedef struct Reenter {
    DeathboobsDatabase *database;
    usize calls;
    bool valid;
} Reenter;

static ErrorCode reenter_visitor(const DeathboobsDetection *record, void *context) {
    Reenter *state = context;
    DeathboobsGameSession session;
    DeathboobsDetection detection;
    DeathboobsDetectionInput input = detection_input(record->session_id, record->timestamp);
    ++state->calls;
    if (deathboobs_database_get_session(state->database, record->session_id, &session) != CORE_OK ||
        deathboobs_database_get_detection(state->database, record->id, &detection) != CORE_OK ||
        deathboobs_database_start_session(state->database, 0, &session) != CORE_INVALID_STATE ||
        deathboobs_database_finish_session(state->database, record->session_id, record->timestamp) != CORE_INVALID_STATE ||
        deathboobs_database_save_detection(state->database, &input, &detection) != CORE_INVALID_STATE ||
        deathboobs_database_visit_session_history(state->database, record->session_id, no_op, NULL) != CORE_INVALID_STATE ||
        deathboobs_database_visit_detections_between(state->database, 0, DEATHBOOBS_TIMESTAMP_MAX, no_op, NULL) != CORE_INVALID_STATE ||
        deathboobs_database_close(&state->database) != CORE_INVALID_STATE) state->valid = false;
    return CORE_OK;
}

static bool test_visitors_stop_and_reentrancy(void) {
    DeathboobsDatabase *database = NULL;
    DeathboobsGameSession session;
    DeathboobsDetection detection;
    CHECK(fresh_database(&database));
    CHECK(deathboobs_database_start_session(database, 0, &session) == CORE_OK);
    for (usize i = 0; i < 3; ++i) {
        DeathboobsDetectionInput input = detection_input(session.id, i);
        CHECK(deathboobs_database_save_detection(database, &input, &detection) == CORE_OK);
    }
    Seen seen = {0};
    seen.stop = CORE_INTERRUPTED;
    CHECK(deathboobs_database_visit_session_history(database, 1, collect, &seen) == CORE_INTERRUPTED);
    CHECK(seen.count == 1);
    seen.count = 0;
    CHECK(deathboobs_database_visit_detections_between(database, 0, 2, collect, &seen) == CORE_INTERRUPTED);
    CHECK(seen.count == 1);
    Reenter state = {database, 0, true};
    CHECK(deathboobs_database_visit_session_history(database, 1, reenter_visitor, &state) == CORE_OK);
    CHECK(state.valid && state.calls == 3 && state.database == database);
    CHECK(deathboobs_database_finish_session(database, 1, 2) == CORE_OK);
    CHECK(close_database(&database));
    return true;
}

static ErrorCode collect_then_fail_next_read(const DeathboobsDetection *record,
                                            void *context) {
    Seen *seen = context;
    ErrorCode error = collect(record, seen);
    if (error == CORE_OK && seen->count == 1) {
        fake_storage_fail_next(FAKE_STORAGE_READ, CORE_IO_ERROR);
    }
    return error;
}

static bool test_visitor_read_failure_after_prefix(void) {
    DeathboobsDatabase *database = NULL;
    DeathboobsGameSession session;
    DeathboobsDetection detection;
    CHECK(fresh_database(&database));
    CHECK(deathboobs_database_start_session(database, 0, &session) == CORE_OK);
    for (usize i = 0; i < 3; ++i) {
        DeathboobsDetectionInput input = detection_input(session.id, i + 10);
        CHECK(deathboobs_database_save_detection(database, &input, &detection) == CORE_OK);
    }
    for (usize pass = 0; pass < 2; ++pass) {
        Seen prefix = {0};
        ErrorCode error = pass == 0 ?
            deathboobs_database_visit_session_history(database, session.id,
                collect_then_fail_next_read, &prefix) :
            deathboobs_database_visit_detections_between(database, 0,
                DEATHBOOBS_TIMESTAMP_MAX, collect_then_fail_next_read, &prefix);
        CHECK(error == CORE_IO_ERROR);
        CHECK(prefix.count == 1 && prefix.ids[0] == 1 && prefix.timestamps[0] == 10);
        Seen retried = {0};
        error = pass == 0 ?
            deathboobs_database_visit_session_history(database, session.id,
                collect, &retried) :
            deathboobs_database_visit_detections_between(database, 0,
                DEATHBOOBS_TIMESTAMP_MAX, collect, &retried);
        CHECK(error == CORE_OK);
        CHECK(retried.count == 3 && retried.ids[0] == 1 && retried.ids[2] == 3);
    }
    CHECK(fake_storage_committed_page_count() == 4);
    CHECK(deathboobs_database_start_session(database, 20, &session) == CORE_OK);
    CHECK(session.id == 2); /* A failed visit must clear its reentrancy guard. */
    CHECK(close_database(&database));
    return true;
}

static bool test_id_exhaustion_and_independence(void) {
    DeathboobsDatabase *database = NULL;
    DeathboobsGameSession session, untouched;
    DeathboobsDetection detection;
    u8 original[sizeof(untouched)];
    CHECK(fake_backend_reset() == CORE_OK);
    CHECK(seed_session(DEATHBOOBS_ID_MAX - 1, 100, false, 0));
    CHECK(open_database(&database) == CORE_OK);
    CHECK(deathboobs_database_start_session(database, 100, &session) == CORE_OK);
    CHECK(session.id == DEATHBOOBS_ID_MAX);
    fill(&untouched, 0xa5, sizeof(untouched));
    copy(original, &untouched, sizeof(untouched));
    CHECK(deathboobs_database_start_session(database, 100, &untouched) == CORE_OVERFLOW);
    CHECK(equal(original, &untouched, sizeof(untouched)));
    DeathboobsDetectionInput input = detection_input(session.id, 100);
    CHECK(deathboobs_database_save_detection(database, &input, &detection) == CORE_OK);
    CHECK(detection.id == 1);
    CHECK(close_database(&database));
    CHECK(open_database(&database) == CORE_OK);
    CHECK(deathboobs_database_start_session(database, 100, &untouched) == CORE_OVERFLOW);
    CHECK(close_database(&database));

    CHECK(fake_backend_reset() == CORE_OK);
    CHECK(seed_session(1, 100, false, 0));
    CHECK(seed_detection(DEATHBOOBS_ID_MAX - 1, 1, 100));
    CHECK(open_database(&database) == CORE_OK);
    input = detection_input(1, 100);
    CHECK(deathboobs_database_save_detection(database, &input, &detection) == CORE_OK);
    CHECK(detection.id == DEATHBOOBS_ID_MAX);
    u8 detection_original[sizeof(detection)];
    copy(detection_original, &detection, sizeof(detection));
    CHECK(deathboobs_database_save_detection(database, &input, &detection) == CORE_OVERFLOW);
    CHECK(equal(detection_original, &detection, sizeof(detection)));
    CHECK(deathboobs_database_start_session(database, 100, &session) == CORE_OK);
    CHECK(session.id == 2);
    CHECK(close_database(&database));
    CHECK(open_database(&database) == CORE_OK);
    CHECK(deathboobs_database_save_detection(database, &input, &detection) == CORE_OVERFLOW);
    CHECK(deathboobs_database_get_detection(database, DEATHBOOBS_ID_MAX, &detection) == CORE_OK);
    CHECK(close_database(&database));
    return true;
}

static void fail_write(usize fault) {
    if (fault < 3) {
        const FakeTransactionOperation operations[] = {
            FAKE_TRANSACTION_BEGIN, FAKE_TRANSACTION_APPEND, FAKE_TRANSACTION_COMMIT
        };
        fake_transaction_fail_next(operations[fault], CORE_IO_ERROR);
    } else {
        const FakeStorageOperation operations[] = {
            FAKE_STORAGE_APPEND, FAKE_STORAGE_FLUSH, FAKE_STORAGE_COUNT
        };
        fake_storage_fail_next(operations[fault - 3], CORE_IO_ERROR);
    }
}

static bool test_atomic_session_creation_faults(void) {
    for (usize fault = 0; fault < 6; ++fault) {
        DeathboobsDatabase *database = NULL;
        DeathboobsGameSession session;
        u8 original[sizeof(session)];
        CHECK(fresh_database(&database));
        fill(&session, 0xa5, sizeof(session));
        copy(original, &session, sizeof(session));
        fail_write(fault);
        CHECK(deathboobs_database_start_session(database, 100, &session) == CORE_IO_ERROR);
        CHECK(equal(original, &session, sizeof(session)));
        CHECK(fake_storage_committed_page_count() == 0);
        CHECK(deathboobs_database_get_session(database, 1, &session) == CORE_NOT_FOUND);
        CHECK(close_database(&database));
        CHECK(open_database(&database) == CORE_OK);
        CHECK(deathboobs_database_get_session(database, 1, &session) == CORE_NOT_FOUND);
        CHECK(deathboobs_database_start_session(database, 100, &session) == CORE_OK);
        CHECK(session.id == 1);
        CHECK(close_database(&database));
    }
    return true;
}

static bool test_atomic_detection_faults(void) {
    for (usize fault = 0; fault < 6; ++fault) {
        DeathboobsDatabase *database = NULL;
        DeathboobsGameSession session;
        DeathboobsDetection detection;
        u8 original[sizeof(detection)];
        CHECK(fresh_database(&database));
        CHECK(deathboobs_database_start_session(database, 100, &session) == CORE_OK);
        DeathboobsDetectionInput input = detection_input(session.id, 999);
        fill(&detection, 0xa5, sizeof(detection));
        copy(original, &detection, sizeof(detection));
        fail_write(fault);
        CHECK(deathboobs_database_save_detection(database, &input, &detection) == CORE_IO_ERROR);
        CHECK(equal(original, &detection, sizeof(detection)));
        CHECK(fake_storage_committed_page_count() == 1);
        CHECK(deathboobs_database_get_detection(database, 1, &detection) == CORE_NOT_FOUND);
        Seen seen = {0};
        CHECK(deathboobs_database_visit_session_history(database, 1, collect, &seen) == CORE_OK);
        CHECK(seen.count == 0);
        CHECK(close_database(&database));
        CHECK(open_database(&database) == CORE_OK);
        CHECK(deathboobs_database_get_detection(database, 1, &detection) == CORE_NOT_FOUND);
        input.timestamp = 150;
        CHECK(deathboobs_database_save_detection(database, &input, &detection) == CORE_OK);
        CHECK(detection.id == 1);
        CHECK(deathboobs_database_finish_session(database, 1, 150) == CORE_OK);
        CHECK(close_database(&database));
    }
    return true;
}

static bool test_atomic_finish_and_rollback_failure(void) {
    DeathboobsDatabase *database = NULL;
    DeathboobsGameSession session;
    DeathboobsDetection detection;
    CHECK(fresh_database(&database));
    CHECK(deathboobs_database_start_session(database, 100, &session) == CORE_OK);
    fake_transaction_fail_next(FAKE_TRANSACTION_COMMIT, CORE_IO_ERROR);
    CHECK(deathboobs_database_finish_session(database, 1, 200) == CORE_IO_ERROR);
    CHECK(fake_storage_committed_page_count() == 1);
    CHECK(deathboobs_database_get_session(database, 1, &session) == CORE_OK);
    CHECK(session.status == DEATHBOOBS_SESSION_STATUS_ACTIVE && !session.has_finished_at);
    CHECK(close_database(&database));
    CHECK(open_database(&database) == CORE_OK);
    CHECK(deathboobs_database_get_session(database, 1, &session) == CORE_OK);
    CHECK(session.status == DEATHBOOBS_SESSION_STATUS_ACTIVE);
    DeathboobsDetectionInput input = detection_input(1, 300);
    CHECK(deathboobs_database_save_detection(database, &input, &detection) == CORE_OK);
    CHECK(deathboobs_database_finish_session(database, 1, 300) == CORE_OK);
    CHECK(close_database(&database));

    CHECK(fresh_database(&database));
    u8 session_before[sizeof(session)];
    copy(session_before, &session, sizeof(session));
    fake_transaction_fail_next(FAKE_TRANSACTION_APPEND, CORE_IO_ERROR);
    fake_transaction_fail_next(FAKE_TRANSACTION_ROLLBACK, CORE_PERMISSION_DENIED);
    CHECK(deathboobs_database_start_session(database, 0, &session) == CORE_PERMISSION_DENIED);
    CHECK(equal(session_before, &session, sizeof(session)));
    CHECK(fake_storage_committed_page_count() == 0);
    CHECK(deathboobs_database_start_session(database, 0, &session) == CORE_INVALID_STATE);
    CHECK(deathboobs_database_get_session(database, 1, &session) == CORE_INVALID_STATE);
    CHECK(deathboobs_database_get_detection(database, 1, &detection) == CORE_INVALID_STATE);
    CHECK(deathboobs_database_visit_detections_between(database, 0, 0, no_op, NULL) == CORE_INVALID_STATE);
    DeathboobsDatabase *retained = database;
    fake_transaction_fail_next(FAKE_TRANSACTION_ROLLBACK, CORE_IO_ERROR);
    CHECK(deathboobs_database_close(&database) == CORE_IO_ERROR);
    CHECK(database == retained);
    CHECK(deathboobs_database_get_session(database, 1, &session) == CORE_INVALID_STATE);
    CHECK(deathboobs_database_start_session(database, 0, &session) == CORE_INVALID_STATE);
    CHECK(fake_storage_committed_page_count() == 0);
    CHECK(close_database(&database));
    CHECK(open_database(&database) == CORE_OK);
    CHECK(deathboobs_database_start_session(database, 0, &session) == CORE_OK);
    CHECK(session.id == 1);
    CHECK(close_database(&database));
    return true;
}

static bool test_incorrect_transaction_page_ids(void) {
    const DeathboobsPageId incorrect_ids[] = {0, 2};
    for (usize i = 0; i < sizeof(incorrect_ids) / sizeof(incorrect_ids[0]); ++i) {
        DeathboobsDatabase *database = NULL;
        DeathboobsGameSession session;
        u8 original[sizeof(session)];
        CHECK(fresh_database(&database));
        fill(&session, 0xa5, sizeof(session));
        copy(original, &session, sizeof(session));
        fake_transaction_override_next_page_id(incorrect_ids[i]);
        CHECK(deathboobs_database_start_session(database, 100, &session) == CORE_INVALID_ARGUMENT);
        CHECK(equal(original, &session, sizeof(session)));
        CHECK(fake_storage_committed_page_count() == 0);
        CHECK(deathboobs_database_get_session(database, 1, &session) == CORE_NOT_FOUND);
        CHECK(close_database(&database));
        CHECK(open_database(&database) == CORE_OK);
        CHECK(deathboobs_database_get_session(database, 1, &session) == CORE_NOT_FOUND);
        CHECK(deathboobs_database_start_session(database, 100, &session) == CORE_OK);
        CHECK(session.id == 1 && fake_storage_committed_page_count() == 1);
        CHECK(close_database(&database));
    }
    /* An unused override belongs to its fixture and must not survive reset. */
    fake_transaction_override_next_page_id(0);
    DeathboobsDatabase *database = NULL;
    DeathboobsGameSession session;
    CHECK(fresh_database(&database));
    CHECK(deathboobs_database_start_session(database, 100, &session) == CORE_OK);
    CHECK(session.id == 1);
    CHECK(close_database(&database));
    return true;
}

static bool test_storage_errors_and_cleanup(void) {
    DeathboobsDatabase *database = NULL;
    DeathboobsGameSession session;
    DeathboobsDetection detection;
    CHECK(fake_backend_reset() == CORE_OK);
    const FakeStorageOperation open_faults[] = {FAKE_STORAGE_OPEN, FAKE_STORAGE_COUNT, FAKE_STORAGE_READ};
    CHECK(seed_session(1, 0, false, 0));
    for (usize i = 0; i < sizeof(open_faults) / sizeof(open_faults[0]); ++i) {
        u64 marker = 0;
        DeathboobsDatabase *untouched = (DeathboobsDatabase *)(void *)&marker;
        fake_storage_fail_next(open_faults[i], CORE_PERMISSION_DENIED);
        CHECK(open_database(&untouched) == CORE_PERMISSION_DENIED);
        CHECK(untouched == (DeathboobsDatabase *)(void *)&marker);
        CHECK(fake_memory_live_blocks() == 0);
        CHECK(open_database(&database) == CORE_OK);
        CHECK(close_database(&database));
    }
    CHECK(open_database(&database) == CORE_OK);
    DeathboobsDetectionInput input = detection_input(1, 10);
    CHECK(deathboobs_database_save_detection(database, &input, &detection) == CORE_OK);
    u8 session_before[sizeof(session)], detection_before[sizeof(detection)];
    fill(&session, 0xa5, sizeof(session));
    fill(&detection, 0xa5, sizeof(detection));
    copy(session_before, &session, sizeof(session));
    copy(detection_before, &detection, sizeof(detection));
    fake_storage_fail_next(FAKE_STORAGE_READ, CORE_IO_ERROR);
    CHECK(deathboobs_database_get_session(database, 1, &session) == CORE_IO_ERROR);
    CHECK(equal(session_before, &session, sizeof(session)));
    fake_storage_fail_next(FAKE_STORAGE_READ, CORE_IO_ERROR);
    CHECK(deathboobs_database_get_detection(database, 1, &detection) == CORE_IO_ERROR);
    CHECK(equal(detection_before, &detection, sizeof(detection)));
    Seen seen = {0};
    fake_storage_fail_next(FAKE_STORAGE_READ, CORE_IO_ERROR);
    CHECK(deathboobs_database_visit_session_history(database, 1, collect, &seen) == CORE_IO_ERROR);
    CHECK(seen.count == 0);
    CHECK(deathboobs_database_visit_session_history(database, 1, collect, &seen) == CORE_OK);
    CHECK(seen.count == 1);
    fake_storage_fail_next(FAKE_STORAGE_CLOSE, CORE_IO_ERROR);
    CHECK(deathboobs_database_close(&database) == CORE_IO_ERROR);
    if (database != NULL) {
        CHECK(deathboobs_database_get_session(database, 1, &session) == CORE_INVALID_STATE);
        CHECK(close_database(&database));
    }
    CHECK(fake_memory_live_blocks() == 0);
    CHECK(open_database(&database) == CORE_OK);
    CHECK(deathboobs_database_get_detection(database, 1, &detection) == CORE_OK);
    CHECK(close_database(&database));
    fake_storage_fail_next(FAKE_STORAGE_READ, CORE_PERMISSION_DENIED);
    fake_storage_fail_next(FAKE_STORAGE_CLOSE, CORE_IO_ERROR);
    CHECK(open_database(&database) == CORE_PERMISSION_DENIED);
    CHECK(database == NULL && fake_memory_live_blocks() == 0);
    CHECK(open_database(&database) == CORE_OK);
    CHECK(close_database(&database));
    String path = test_path();
    CHECK(deathboobs_database_open(NULL, &database) == CORE_INVALID_ARGUMENT);
    CHECK(deathboobs_database_open(&path, NULL) == CORE_INVALID_ARGUMENT);
    return true;
}

static bool test_allocation_failures(void) {
    DeathboobsDatabase *database = NULL;
    DeathboobsGameSession session;
    DeathboobsDetection detection;
    CHECK(fake_backend_reset() == CORE_OK);
    fake_memory_fail_after(0);
    CHECK(open_database(&database) == CORE_OUT_OF_MEMORY);
    CHECK(database == NULL && fake_memory_live_blocks() == 0);
    fake_memory_clear_failure();
    CHECK(open_database(&database) == CORE_OK);
    fake_memory_fail_after(0);
    CHECK(deathboobs_database_start_session(database, 0, &session) == CORE_OUT_OF_MEMORY);
    CHECK(fake_storage_committed_page_count() == 0);
    fake_memory_clear_failure();
    CHECK(deathboobs_database_start_session(database, 0, &session) == CORE_OK);
    CHECK(session.id == 1);
    CHECK(close_database(&database));
    for (usize allocations = 0; allocations < 3; ++allocations) {
        CHECK(fresh_database(&database));
        CHECK(deathboobs_database_start_session(database, 0, &session) == CORE_OK);
        DeathboobsDetectionInput input = detection_input(1, 1);
        fake_memory_fail_after(allocations);
        CHECK(deathboobs_database_save_detection(database, &input, &detection) == CORE_OUT_OF_MEMORY);
        CHECK(fake_storage_committed_page_count() == 1);
        fake_memory_clear_failure();
        CHECK(deathboobs_database_get_detection(database, 1, &detection) == CORE_NOT_FOUND);
        CHECK(deathboobs_database_save_detection(database, &input, &detection) == CORE_OK);
        CHECK(detection.id == 1);
        CHECK(close_database(&database));
    }
    CHECK(fake_backend_reset() == CORE_OK);
    CHECK(seed_session(1, 0, false, 0));
    CHECK(seed_detection(1, 1, 1));
    for (usize allocations = 0; allocations < 5; ++allocations) {
        fake_memory_fail_after(allocations);
        CHECK(open_database(&database) == CORE_OUT_OF_MEMORY);
        CHECK(database == NULL && fake_memory_live_blocks() == 0);
        fake_memory_clear_failure();
    }
    CHECK(open_database(&database) == CORE_OK);
    CHECK(close_database(&database));
    return true;
}

static bool test_malformed_replay(void) {
    for (usize scenario = 0; scenario < 17; ++scenario) {
        DeathboobsDatabase *database = NULL;
        ErrorCode expected = CORE_INVALID_ARGUMENT;
        CHECK(fake_backend_reset() == CORE_OK);
        if (scenario != 1) CHECK(seed_session(1, 100, false, 0));
        if (scenario == 0) CHECK(seed_session(1, 100, false, 0));
        if (scenario == 1) CHECK(seed_session(1, 100, true, 200));
        if (scenario == 2) CHECK(seed_session(1, 101, true, 200));
        if (scenario == 3) {
            CHECK(seed_session(1, 100, true, 200));
            CHECK(seed_session(1, 100, false, 0));
        }
        if (scenario == 4) {
            CHECK(seed_session(1, 100, true, 200));
            CHECK(seed_session(1, 100, true, 200));
        }
        if (scenario == 5) CHECK(seed_detection(1, 999, 150));
        if (scenario == 6) CHECK(seed_detection(1, 1, 99));
        if (scenario == 7) {
            CHECK(seed_session(1, 100, true, 200));
            CHECK(seed_detection(1, 1, 150));
        }
        if (scenario == 8) {
            CHECK(seed_detection(1, 1, 200));
            CHECK(seed_session(1, 100, true, 199));
        }
        if (scenario == 9) {
            CHECK(seed_detection(1, 1, 150));
            CHECK(seed_detection(1, 1, 160));
        }
        if (scenario >= 10) {
            DeathboobsGameSession session = {0};
            DeathboobsPage page = {0};
            usize size = 0;
            session.id = 2;
            session.started_at = 100;
            session.status = DEATHBOOBS_SESSION_STATUS_ACTIVE;
            page.type = DEATHBOOBS_PAGE_TYPE_SESSIONS;
            CHECK(deathboobs_game_session_encode(&session, page.payload,
                  sizeof(page.payload), &size) == CORE_OK);
            page.payload_size = (u32)size;
            if (scenario == 10) page.type = DEATHBOOBS_PAGE_TYPE_DETECTIONS;
            if (scenario == 11) page.flags = 1;
            if (scenario == 12) page.generation = 1;
            if (scenario == 13) page.payload_size = 39;
            if (scenario == 14) { page.payload[1] = 2; expected = CORE_UNSUPPORTED; }
            if (scenario == 15) { page.type = DEATHBOOBS_PAGE_TYPE_METADATA; expected = CORE_UNSUPPORTED; }
            if (scenario == 16) { page.payload[0] = 255; expected = CORE_UNSUPPORTED; }
            CHECK(fake_storage_seed_page(&page) == CORE_OK);
        }
        CHECK(open_database(&database) == expected);
        CHECK(database == NULL && fake_memory_live_blocks() == 0);
    }
    CHECK(fake_backend_reset() == CORE_OK);
    return true;
}

static bool test_nonmonotonic_replay_ids(void) {
    DeathboobsDatabase *database = NULL;
    DeathboobsGameSession session;
    DeathboobsDetection detection;
    Seen seen = {0};
    CHECK(fake_backend_reset() == CORE_OK);
    CHECK(seed_session(10, 0, false, 0));
    CHECK(seed_session(2, 0, false, 0));
    CHECK(seed_detection(50, 10, 20));
    CHECK(seed_detection(3, 2, 10));
    CHECK(seed_detection(4, 10, 10));
    CHECK(open_database(&database) == CORE_OK);
    CHECK(deathboobs_database_get_session(database, 2, &session) == CORE_OK && session.id == 2);
    CHECK(deathboobs_database_get_detection(database, 3, &detection) == CORE_OK && detection.session_id == 2);
    CHECK(deathboobs_database_visit_session_history(database, 10, collect, &seen) == CORE_OK);
    CHECK(seen.count == 2 && seen.ids[0] == 4 && seen.ids[1] == 50);
    CHECK(deathboobs_database_start_session(database, 0, &session) == CORE_OK && session.id == 11);
    DeathboobsDetectionInput input = detection_input(2, 30);
    CHECK(deathboobs_database_save_detection(database, &input, &detection) == CORE_OK && detection.id == 51);
    CHECK(close_database(&database));
    return true;
}

typedef struct OrderCheck {
    DeathboobsSessionId session;
    DeathboobsTimestamp previous_time;
    DeathboobsDetectionId previous_id;
    usize count;
} OrderCheck;

static ErrorCode check_order(const DeathboobsDetection *record, void *context) {
    OrderCheck *state = context;
    if (state->session != 0 && record->session_id != state->session)
        return CORE_INVALID_STATE;
    if (state->count != 0 &&
        (record->timestamp < state->previous_time ||
         (record->timestamp == state->previous_time && record->id <= state->previous_id)))
        return CORE_INVALID_STATE;
    state->previous_time = record->timestamp;
    state->previous_id = record->id;
    ++state->count;
    return CORE_OK;
}

static bool test_index_growth(void) {
    DeathboobsDatabase *database = NULL;
    DeathboobsGameSession session;
    DeathboobsDetection detection;
    CHECK(fresh_database(&database));
    for (usize i = 0; i < 33; ++i) {
        CHECK(deathboobs_database_start_session(database, 0, &session) == CORE_OK);
        CHECK(session.id == i + 1);
    }
    for (usize i = 0; i < 97; ++i) {
        DeathboobsDetectionInput input = detection_input((i * 5) % 33 + 1,
                                                         100 + (i * 37) % 97);
        CHECK(deathboobs_database_save_detection(database, &input, &detection) == CORE_OK);
        CHECK(detection.id == i + 1 && matches_input(&detection, &input));
    }
    for (usize pass = 0; pass < 2; ++pass) {
        if (pass != 0) {
            CHECK(close_database(&database));
            CHECK(open_database(&database) == CORE_OK);
        }
        for (usize i = 0; i < 97; ++i) {
            CHECK(deathboobs_database_get_detection(database, i + 1, &detection) == CORE_OK);
            CHECK(detection.session_id == (i * 5) % 33 + 1);
            CHECK(detection.timestamp == 100 + (i * 37) % 97);
        }
        for (usize id = 1; id <= 33; ++id) {
            usize expected = 0;
            for (usize i = 0; i < 97; ++i) if ((i * 5) % 33 + 1 == id) ++expected;
            OrderCheck order = {0};
            order.session = id;
            CHECK(deathboobs_database_visit_session_history(database, id, check_order, &order) == CORE_OK);
            CHECK(order.count == expected);
        }
        OrderCheck order = {0};
        CHECK(deathboobs_database_visit_detections_between(database, 0,
              DEATHBOOBS_TIMESTAMP_MAX, check_order, &order) == CORE_OK);
        CHECK(order.count == 97);
        order = (OrderCheck){0};
        CHECK(deathboobs_database_visit_detections_between(database, 120, 129,
                                                          check_order, &order) == CORE_OK);
        CHECK(order.count == 10);
    }
    CHECK(close_database(&database));
    return true;
}

static bool test_retryable_memory_cleanup(void) {
    DeathboobsDatabase *database = NULL;
    DeathboobsGameSession session;
    DeathboobsDetection detection;
    for (usize scenario = 0; scenario < 2; ++scenario) {
        CHECK(fake_backend_reset() == CORE_OK);
        CHECK(seed_session(1, 0, false, 0));
        fake_storage_fail_next(scenario == 0 ? FAKE_STORAGE_COUNT : FAKE_STORAGE_READ,
                               CORE_IO_ERROR);
        fake_memory_fail_next_free(CORE_INTERRUPTED);
        CHECK(open_database(&database) == CORE_INTERRUPTED);
        CHECK(database != NULL && fake_memory_live_blocks() != 0);
        CHECK(deathboobs_database_get_session(database, 1, &session) == CORE_INVALID_STATE);
        CHECK(deathboobs_database_get_detection(database, 1, &detection) == CORE_INVALID_STATE);
        CHECK(deathboobs_database_start_session(database, 0, &session) == CORE_INVALID_STATE);
        CHECK(close_database(&database));
    }
    CHECK(fresh_database(&database));
    CHECK(deathboobs_database_start_session(database, 0, &session) == CORE_OK);
    DeathboobsDetectionInput input = detection_input(1, 1);
    CHECK(deathboobs_database_save_detection(database, &input, &detection) == CORE_OK);
    fake_memory_fail_next_free(CORE_INTERRUPTED);
    CHECK(deathboobs_database_close(&database) == CORE_INTERRUPTED);
    CHECK(database != NULL && fake_memory_live_blocks() != 0);
    CHECK(deathboobs_database_get_session(database, 1, &session) == CORE_INVALID_STATE);
    CHECK(close_database(&database));
    CHECK(open_database(&database) == CORE_OK);
    CHECK(deathboobs_database_get_detection(database, 1, &detection) == CORE_OK);
    CHECK(close_database(&database));
    return true;
}

int main(void) {
    const struct { const char *name; bool (*run)(void); } tests[] = {
        {"sessions, detections and lifecycle", test_sessions_and_detections},
        {"invalid input, missing IDs and unchanged outputs", test_invalid_input_and_missing_ids},
        {"history, time ranges and rebuilt indexes", test_history_time_and_reopen},
        {"visitor stop and reentrancy", test_visitors_stop_and_reentrancy},
        {"visitor read errors after a delivered prefix", test_visitor_read_failure_after_prefix},
        {"ID exhaustion and independent sequences", test_id_exhaustion_and_independence},
        {"atomic session creation faults", test_atomic_session_creation_faults},
        {"atomic detection faults", test_atomic_detection_faults},
        {"atomic finish and failed rollback", test_atomic_finish_and_rollback_failure},
        {"incorrect physical IDs from Transaction", test_incorrect_transaction_page_ids},
        {"Storage errors and resource cleanup", test_storage_errors_and_cleanup},
        {"allocation failures before publication", test_allocation_failures},
        {"malformed replay invariants", test_malformed_replay},
        {"nonmonotonic replay IDs", test_nonmonotonic_replay_ids},
        {"RAM index capacity growth", test_index_growth},
        {"retryable memory cleanup failures", test_retryable_memory_cleanup}
    };
    for (usize i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        puts(tests[i].name);
        if (!tests[i].run()) return 1;
    }
    if (fake_backend_reset() != CORE_OK) return 1;
    puts("PASS: 16 database test groups");
    return 0;
}
