#include <deathboobs/database.h>
#include <deathboobs/record_codec.h>
#include <deathboobs/transaction.h>
#include <sys/memory.h>
#include "../internal/bytes.h"

typedef struct SessionEntry {
    DeathboobsGameSession record;
    DeathboobsPageId page_id;
    bool has_detections;
    DeathboobsTimestamp latest_detection;
} SessionEntry;

typedef struct DetectionEntry {
    DeathboobsDetectionId id;
    DeathboobsSessionId session_id;
    DeathboobsTimestamp timestamp;
    DeathboobsPageId page_id;
} DetectionEntry;

typedef struct Index {
    MemoryBlock memory;
    usize capacity;
} Index;

typedef enum DetectionOrder {
    ORDER_ID,
    ORDER_SESSION_TIME,
    ORDER_TIME
} DetectionOrder;

struct DeathboobsDatabase {
    MemoryBlock owned;
    DeathboobsStorage *storage;
    DeathboobsTransaction *pending;
    Index sessions;
    Index detections;
    Index session_history;
    Index time_history;
    usize session_count;
    usize detection_count;
    u64 page_count;
    u64 max_session_id;
    u64 max_detection_id;
    bool visiting;
    bool poisoned;
    bool closing;
};

static SessionEntry *sessions(DeathboobsDatabase *database) {
    return (SessionEntry *)database->sessions.memory.data;
}

static DetectionEntry *detections(Index *index) {
    return (DetectionEntry *)index->memory.data;
}

static ErrorCode ready(const DeathboobsDatabase *database, bool writing) {
    if (database == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (database->poisoned || database->closing ||
        (writing && database->visiting)) {
        return CORE_INVALID_STATE;
    }
    return CORE_OK;
}

/* Capacity may grow on a failed write, but logical index contents never do. */
static ErrorCode reserve(Index *index, usize needed, usize item_size) {
    if (needed <= index->capacity) {
        return CORE_OK;
    }
    usize capacity = index->capacity == 0 ? 8 : index->capacity;
    while (capacity < needed) {
        if (capacity > USIZE_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    if (capacity > USIZE_MAX / item_size) {
        return CORE_OVERFLOW;
    }
    ErrorCode error = memory_reallocate(&index->memory, capacity * item_size);
    if (error == CORE_OK) {
        index->capacity = capacity;
    }
    return error;
}

static usize session_position(DeathboobsDatabase *database, u64 id) {
    usize low = 0;
    usize high = database->session_count;
    SessionEntry *entries = sessions(database);
    while (low < high) {
        usize middle = low + (high - low) / 2;
        if (entries[middle].record.id < id) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

static bool find_session(DeathboobsDatabase *database, u64 id,
                         usize *out_position) {
    usize position = session_position(database, id);
    *out_position = position;
    return position < database->session_count &&
           sessions(database)[position].record.id == id;
}

static bool entry_less(const DetectionEntry *left, const DetectionEntry *right,
                       DetectionOrder order) {
    if (order == ORDER_ID) {
        return left->id < right->id;
    }
    if (order == ORDER_SESSION_TIME && left->session_id != right->session_id) {
        return left->session_id < right->session_id;
    }
    if (left->timestamp != right->timestamp) {
        return left->timestamp < right->timestamp;
    }
    return left->id < right->id;
}

static usize detection_position(Index *index, usize count,
    const DetectionEntry *key, DetectionOrder order) {
    DetectionEntry *entries = detections(index);
    usize low = 0;
    usize high = count;
    while (low < high) {
        usize middle = low + (high - low) / 2;
        if (entry_less(&entries[middle], key, order)) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

static bool find_detection(DeathboobsDatabase *database, u64 id,
                           usize *out_position) {
    DetectionEntry key;
    deathboobs_bytes_zero(&key, sizeof(key));
    key.id = id;
    usize position = detection_position(&database->detections,
        database->detection_count, &key, ORDER_ID);
    *out_position = position;
    return position < database->detection_count &&
           detections(&database->detections)[position].id == id;
}

static ErrorCode reserve_session(DeathboobsDatabase *database) {
    if (database->session_count == USIZE_MAX) {
        return CORE_OVERFLOW;
    }
    return reserve(&database->sessions, database->session_count + 1,
                   sizeof(SessionEntry));
}

static ErrorCode reserve_detection(DeathboobsDatabase *database) {
    if (database->detection_count == USIZE_MAX) {
        return CORE_OVERFLOW;
    }
    usize needed = database->detection_count + 1;
    ErrorCode error = reserve(&database->detections, needed,
                              sizeof(DetectionEntry));
    if (error != CORE_OK) {
        return error;
    }
    error = reserve(&database->session_history, needed, sizeof(DetectionEntry));
    if (error != CORE_OK) {
        return error;
    }
    return reserve(&database->time_history, needed, sizeof(DetectionEntry));
}

/* Publication helpers are infallible; all memory has already been reserved. */
static void insert_session(DeathboobsDatabase *database,
    const DeathboobsGameSession *record, DeathboobsPageId page_id) {
    usize position = session_position(database, record->id);
    SessionEntry *entries = sessions(database);
    for (usize i = database->session_count; i > position; --i) {
        deathboobs_bytes_copy(&entries[i], &entries[i - 1], sizeof(entries[i]));
    }
    SessionEntry entry;
    deathboobs_bytes_zero(&entry, sizeof(entry));
    deathboobs_bytes_copy(&entry.record, record, sizeof(entry.record));
    entry.page_id = page_id;
    deathboobs_bytes_copy(&entries[position], &entry, sizeof(entry));
    ++database->session_count;
    if (record->id > database->max_session_id) {
        database->max_session_id = record->id;
    }
}

static void insert_reference(Index *index, usize count,
    const DetectionEntry *entry, DetectionOrder order) {
    usize position = detection_position(index, count, entry, order);
    DetectionEntry *entries = detections(index);
    for (usize i = count; i > position; --i) {
        deathboobs_bytes_copy(&entries[i], &entries[i - 1], sizeof(entries[i]));
    }
    deathboobs_bytes_copy(&entries[position], entry, sizeof(*entry));
}

static void insert_detection(DeathboobsDatabase *database,
    const DeathboobsDetection *record, DeathboobsPageId page_id,
    usize session_index) {
    DetectionEntry entry;
    deathboobs_bytes_zero(&entry, sizeof(entry));
    entry.id = record->id;
    entry.session_id = record->session_id;
    entry.timestamp = record->timestamp;
    entry.page_id = page_id;
    usize count = database->detection_count;
    insert_reference(&database->detections, count, &entry, ORDER_ID);
    insert_reference(&database->session_history, count, &entry,
                     ORDER_SESSION_TIME);
    insert_reference(&database->time_history, count, &entry, ORDER_TIME);
    ++database->detection_count;
    if (record->id > database->max_detection_id) {
        database->max_detection_id = record->id;
    }
    SessionEntry *session = &sessions(database)[session_index];
    if (!session->has_detections || record->timestamp > session->latest_detection) {
        session->latest_detection = record->timestamp;
    }
    session->has_detections = true;
}

static ErrorCode page_shape(const DeathboobsPage *page, u64 expected_id) {
    if (page->id != expected_id || page->flags != 0 || page->generation != 0 ||
        page->payload_size > DEATHBOOBS_PAGE_PAYLOAD_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

static ErrorCode load_session(DeathboobsDatabase *database, u64 page_id,
                              DeathboobsGameSession *out_record) {
    DeathboobsPage page;
    ErrorCode error = deathboobs_storage_page_read(database->storage,
                                                  page_id, &page);
    if (error != CORE_OK) {
        return error;
    }
    error = page_shape(&page, page_id);
    if (error != CORE_OK || page.type != DEATHBOOBS_PAGE_TYPE_SESSIONS) {
        return CORE_INVALID_ARGUMENT;
    }
    return deathboobs_game_session_decode(page.payload, page.payload_size,
                                          out_record);
}

static ErrorCode load_detection(DeathboobsDatabase *database,
    const DetectionEntry *reference, DeathboobsDetection *out_record) {
    DeathboobsPage page;
    ErrorCode error = deathboobs_storage_page_read(database->storage,
                                                  reference->page_id, &page);
    if (error != CORE_OK) {
        return error;
    }
    error = page_shape(&page, reference->page_id);
    if (error != CORE_OK || page.type != DEATHBOOBS_PAGE_TYPE_DETECTIONS) {
        return CORE_INVALID_ARGUMENT;
    }
    DeathboobsDetection record;
    error = deathboobs_detection_decode(page.payload, page.payload_size, &record);
    if (error != CORE_OK) {
        return error;
    }
    if (record.id != reference->id || record.session_id != reference->session_id ||
        record.timestamp != reference->timestamp) {
        return CORE_INVALID_ARGUMENT;
    }
    deathboobs_bytes_copy(out_record, &record, sizeof(record));
    return CORE_OK;
}

static ErrorCode replay_session(DeathboobsDatabase *database,
    const DeathboobsGameSession *record, u64 page_id) {
    usize position;
    if (!find_session(database, record->id, &position)) {
        if (record->status != DEATHBOOBS_SESSION_STATUS_ACTIVE) {
            return CORE_INVALID_ARGUMENT;
        }
        ErrorCode error = reserve_session(database);
        if (error != CORE_OK) {
            return error;
        }
        insert_session(database, record, page_id);
        return CORE_OK;
    }
    SessionEntry *existing = &sessions(database)[position];
    if (existing->record.status != DEATHBOOBS_SESSION_STATUS_ACTIVE ||
        record->status != DEATHBOOBS_SESSION_STATUS_FINISHED ||
        existing->record.started_at != record->started_at ||
        (existing->has_detections &&
         record->finished_at < existing->latest_detection)) {
        return CORE_INVALID_ARGUMENT;
    }
    deathboobs_bytes_copy(&existing->record, record, sizeof(existing->record));
    existing->page_id = page_id;
    return CORE_OK;
}

static ErrorCode replay_detection(DeathboobsDatabase *database,
    const DeathboobsDetection *record, u64 page_id) {
    usize session_index;
    usize detection_index;
    if (!find_session(database, record->session_id, &session_index) ||
        sessions(database)[session_index].record.status !=
            DEATHBOOBS_SESSION_STATUS_ACTIVE ||
        record->timestamp < sessions(database)[session_index].record.started_at ||
        find_detection(database, record->id, &detection_index)) {
        return CORE_INVALID_ARGUMENT;
    }
    ErrorCode error = reserve_detection(database);
    if (error != CORE_OK) {
        return error;
    }
    insert_detection(database, record, page_id, session_index);
    return CORE_OK;
}

static ErrorCode rebuild(DeathboobsDatabase *database) {
    u64 count;
    ErrorCode error = deathboobs_storage_page_count(database->storage, &count);
    if (error != CORE_OK) {
        return error;
    }
    for (u64 index = 0; index < count; ++index) {
        u64 page_id = index + 1; /* index < count: no wrap, even for UINT64_MAX. */
        DeathboobsPage page;
        error = deathboobs_storage_page_read(database->storage, page_id, &page);
        if (error != CORE_OK) {
            return error;
        }
        error = page_shape(&page, page_id);
        if (error != CORE_OK) {
            return error;
        }
        if (page.type == DEATHBOOBS_PAGE_TYPE_SESSIONS) {
            DeathboobsGameSession record;
            error = deathboobs_game_session_decode(page.payload,
                                                    page.payload_size, &record);
            if (error == CORE_OK) {
                error = replay_session(database, &record, page_id);
            }
        } else if (page.type == DEATHBOOBS_PAGE_TYPE_DETECTIONS) {
            DeathboobsDetection record;
            error = deathboobs_detection_decode(page.payload,
                                                 page.payload_size, &record);
            if (error == CORE_OK) {
                error = replay_detection(database, &record, page_id);
            }
        } else {
            error = page.type == DEATHBOOBS_PAGE_TYPE_INVALID ?
                    CORE_INVALID_ARGUMENT : CORE_UNSUPPORTED;
        }
        if (error != CORE_OK) {
            return error;
        }
    }
    database->page_count = count;
    return CORE_OK;
}

ErrorCode deathboobs_database_open(const String *path,
                                 DeathboobsDatabase **out_database) {
    if (path == NULL || out_database == NULL || path->data == NULL ||
        path->length == 0 || path->length > STRING_MAX_LENGTH) {
        return CORE_INVALID_ARGUMENT;
    }
    MemoryBlock memory;
    deathboobs_bytes_zero(&memory, sizeof(memory));
    ErrorCode error = memory_allocate(sizeof(DeathboobsDatabase), &memory);
    if (error != CORE_OK) {
        return error;
    }
    DeathboobsDatabase *database = (DeathboobsDatabase *)memory.data;
    deathboobs_bytes_zero(database, sizeof(*database));
    database->owned.data = memory.data;
    database->owned.size = memory.size;
    error = deathboobs_transaction_open(path, NULL, &database->storage);
    if (error == CORE_OK) {
        error = rebuild(database);
    }
    if (error != CORE_OK) {
        /* Storage close consumes its handle even when reporting a flush error. */
        ErrorCode cleanup_error = deathboobs_database_close(&database);
        if (database != NULL) {
            /* Retain explicit ownership of a block whose release failed. */
            database->closing = true;
            *out_database = database;
            return cleanup_error;
        }
        return error;
    }
    *out_database = database;
    return CORE_OK;
}

ErrorCode deathboobs_database_close(DeathboobsDatabase **out_database) {
    if (out_database == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    DeathboobsDatabase *database = *out_database;
    if (database == NULL) {
        return CORE_OK;
    }
    if (database->visiting) {
        return CORE_INVALID_STATE;
    }
    if (database->pending != NULL) {
        bool can_rollback = false;
        ErrorCode error = deathboobs_transaction_can_rollback(
            database->pending, &can_rollback);
        if (error == CORE_OK) {
            error = can_rollback
                        ? deathboobs_transaction_rollback(&database->pending)
                        : deathboobs_transaction_commit(&database->pending);
        }
        if (error != CORE_OK) {
            database->poisoned = true;
            return error;
        }
    }
    database->closing = true;
    ErrorCode close_error = CORE_OK;
    if (database->storage != NULL) {
        close_error = deathboobs_storage_close(&database->storage);
        if (close_error != CORE_OK && database->storage != NULL) {
            return close_error;
        }
    }
    Index *indexes[4];
    indexes[0] = &database->sessions;
    indexes[1] = &database->detections;
    indexes[2] = &database->session_history;
    indexes[3] = &database->time_history;
    for (usize i = 0; i < sizeof(indexes) / sizeof(indexes[0]); ++i) {
        ErrorCode error = memory_free(&indexes[i]->memory);
        if (error != CORE_OK) {
            return error;
        }
    }
    MemoryBlock owned;
    owned.data = database->owned.data;
    owned.size = database->owned.size;
    ErrorCode error = memory_free(&owned);
    if (error != CORE_OK) {
        return error;
    }
    *out_database = NULL;
    return close_error;
}

static ErrorCode abort_write(DeathboobsDatabase *database, ErrorCode original) {
    ErrorCode error = deathboobs_transaction_rollback(&database->pending);
    if (error != CORE_OK) {
        database->poisoned = true;
        return error;
    }
    return original;
}

static ErrorCode handle_commit_error(DeathboobsDatabase *database,
                                     ErrorCode original) {
    bool can_rollback = false;
    ErrorCode error = deathboobs_transaction_can_rollback(
        database->pending, &can_rollback);
    if (error != CORE_OK) {
        database->poisoned = true;
        return error;
    }
    if (can_rollback) {
        return abort_write(database, original);
    }
    database->poisoned = true;
    return original;
}

static ErrorCode append_record(DeathboobsDatabase *database,
    const DeathboobsPage *page, DeathboobsPageId *out_page_id) {
    if (database->page_count == (u64)0xffffffffffffffffULL) {
        return CORE_OVERFLOW;
    }
    ErrorCode error = deathboobs_transaction_begin(database->storage, NULL,
                                                   &database->pending);
    if (error != CORE_OK) {
        return error;
    }
    DeathboobsPageId page_id;
    error = deathboobs_transaction_page_append(database->pending, page, &page_id);
    if (error != CORE_OK) {
        return abort_write(database, error);
    }
    if (page_id != database->page_count + 1) {
        return abort_write(database, CORE_INVALID_ARGUMENT);
    }
    error = deathboobs_transaction_commit(&database->pending);
    if (error != CORE_OK) {
        return handle_commit_error(database, error);
    }
    ++database->page_count;
    *out_page_id = page_id;
    return CORE_OK;
}

ErrorCode deathboobs_database_start_session(DeathboobsDatabase *database,
    DeathboobsTimestamp started_at, DeathboobsGameSession *out_session) {
    ErrorCode error = ready(database, true);
    if (error != CORE_OK) {
        return error;
    }
    if (out_session == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (database->max_session_id == DEATHBOOBS_ID_MAX) {
        return CORE_OVERFLOW;
    }
    DeathboobsGameSession record;
    deathboobs_bytes_zero(&record, sizeof(record));
    record.id = database->max_session_id + 1;
    record.started_at = started_at;
    record.status = DEATHBOOBS_SESSION_STATUS_ACTIVE;
    DeathboobsPage page;
    deathboobs_bytes_zero(&page, sizeof(page));
    page.type = DEATHBOOBS_PAGE_TYPE_SESSIONS;
    usize size;
    error = deathboobs_game_session_encode(&record, page.payload,
                                           sizeof(page.payload), &size);
    if (error != CORE_OK) {
        return error;
    }
    page.payload_size = (u32)size;
    error = reserve_session(database);
    if (error != CORE_OK) {
        return error;
    }
    DeathboobsPageId page_id;
    error = append_record(database, &page, &page_id);
    if (error != CORE_OK) {
        return error;
    }
    insert_session(database, &record, page_id);
    deathboobs_bytes_copy(out_session, &record, sizeof(record));
    return CORE_OK;
}

ErrorCode deathboobs_database_finish_session(DeathboobsDatabase *database,
    DeathboobsSessionId session_id, DeathboobsTimestamp finished_at) {
    ErrorCode error = ready(database, true);
    if (error != CORE_OK) {
        return error;
    }
    if (session_id < DEATHBOOBS_ID_MIN) {
        return CORE_INVALID_ARGUMENT;
    }
    usize position;
    if (!find_session(database, session_id, &position)) {
        return CORE_NOT_FOUND;
    }
    SessionEntry *entry = &sessions(database)[position];
    if (entry->record.status != DEATHBOOBS_SESSION_STATUS_ACTIVE) {
        return CORE_INVALID_STATE;
    }
    if (finished_at < entry->record.started_at ||
        (entry->has_detections && finished_at < entry->latest_detection)) {
        return CORE_INVALID_ARGUMENT;
    }
    DeathboobsGameSession record;
    deathboobs_bytes_copy(&record, &entry->record, sizeof(record));
    record.status = DEATHBOOBS_SESSION_STATUS_FINISHED;
    record.has_finished_at = true;
    record.finished_at = finished_at;
    DeathboobsPage page;
    deathboobs_bytes_zero(&page, sizeof(page));
    page.type = DEATHBOOBS_PAGE_TYPE_SESSIONS;
    usize size;
    error = deathboobs_game_session_encode(&record, page.payload,
                                           sizeof(page.payload), &size);
    if (error != CORE_OK) {
        return error;
    }
    page.payload_size = (u32)size;
    DeathboobsPageId page_id;
    error = append_record(database, &page, &page_id);
    if (error != CORE_OK) {
        return error;
    }
    deathboobs_bytes_copy(&entry->record, &record, sizeof(record));
    entry->page_id = page_id;
    return CORE_OK;
}

ErrorCode deathboobs_database_save_detection(DeathboobsDatabase *database,
    const DeathboobsDetectionInput *input, DeathboobsDetection *out_detection) {
    ErrorCode error = ready(database, true);
    if (error != CORE_OK) {
        return error;
    }
    if (input == NULL || out_detection == NULL ||
        input->session_id < DEATHBOOBS_ID_MIN) {
        return CORE_INVALID_ARGUMENT;
    }
    usize session_index;
    if (!find_session(database, input->session_id, &session_index)) {
        return CORE_NOT_FOUND;
    }
    SessionEntry *session = &sessions(database)[session_index];
    if (session->record.status != DEATHBOOBS_SESSION_STATUS_ACTIVE) {
        return CORE_INVALID_STATE;
    }
    if (input->timestamp < session->record.started_at) {
        return CORE_INVALID_ARGUMENT;
    }
    if (database->max_detection_id == DEATHBOOBS_ID_MAX) {
        return CORE_OVERFLOW;
    }
    DeathboobsDetection record;
    deathboobs_bytes_zero(&record, sizeof(record));
    record.id = database->max_detection_id + 1;
    record.session_id = input->session_id;
    record.timestamp = input->timestamp;
    record.has_target_id = input->has_target_id;
    record.target_id = input->target_id;
    record.x = input->x;
    record.y = input->y;
    record.azimuth = input->azimuth;
    record.distance = input->distance;
    record.confidence = input->confidence;
    record.has_frame_reference = input->has_frame_reference;
    record.frame_reference_size = input->frame_reference_size;
    /* Copy the full bounded model buffer so the codec can reject a dirty tail. */
    for (usize i = 0; i < DEATHBOOBS_FRAME_REFERENCE_MAX_BYTES; ++i) {
        record.frame_reference[i] = input->frame_reference[i];
    }
    DeathboobsPage page;
    deathboobs_bytes_zero(&page, sizeof(page));
    page.type = DEATHBOOBS_PAGE_TYPE_DETECTIONS;
    usize size;
    error = deathboobs_detection_encode(&record, page.payload,
                                        sizeof(page.payload), &size);
    if (error != CORE_OK) {
        return error;
    }
    page.payload_size = (u32)size;
    error = reserve_detection(database);
    if (error != CORE_OK) {
        return error;
    }
    DeathboobsPageId page_id;
    error = append_record(database, &page, &page_id);
    if (error != CORE_OK) {
        return error;
    }
    insert_detection(database, &record, page_id, session_index);
    deathboobs_bytes_copy(out_detection, &record, sizeof(record));
    return CORE_OK;
}

static bool same_session(const DeathboobsGameSession *left,
                          const DeathboobsGameSession *right) {
    return left->id == right->id && left->started_at == right->started_at &&
           left->has_finished_at == right->has_finished_at &&
           left->finished_at == right->finished_at && left->status == right->status;
}

ErrorCode deathboobs_database_get_session(DeathboobsDatabase *database,
    DeathboobsSessionId session_id, DeathboobsGameSession *out_session) {
    ErrorCode error = ready(database, false);
    if (error != CORE_OK) {
        return error;
    }
    if (out_session == NULL || session_id < DEATHBOOBS_ID_MIN) {
        return CORE_INVALID_ARGUMENT;
    }
    usize position;
    if (!find_session(database, session_id, &position)) {
        return CORE_NOT_FOUND;
    }
    SessionEntry *entry = &sessions(database)[position];
    DeathboobsGameSession record;
    error = load_session(database, entry->page_id, &record);
    if (error != CORE_OK) {
        return error;
    }
    if (!same_session(&record, &entry->record)) {
        return CORE_INVALID_ARGUMENT;
    }
    deathboobs_bytes_copy(out_session, &record, sizeof(record));
    return CORE_OK;
}

ErrorCode deathboobs_database_get_detection(DeathboobsDatabase *database,
    DeathboobsDetectionId detection_id, DeathboobsDetection *out_detection) {
    ErrorCode error = ready(database, false);
    if (error != CORE_OK) {
        return error;
    }
    if (out_detection == NULL || detection_id < DEATHBOOBS_ID_MIN) {
        return CORE_INVALID_ARGUMENT;
    }
    usize position;
    if (!find_detection(database, detection_id, &position)) {
        return CORE_NOT_FOUND;
    }
    return load_detection(database,
        &detections(&database->detections)[position], out_detection);
}

static ErrorCode visit(DeathboobsDatabase *database, Index *index, usize start,
    bool by_session, u64 session_id, u64 to,
    DeathboobsDetectionVisitor visitor, void *context) {
    database->visiting = true;
    ErrorCode error = CORE_OK;
    for (usize i = start; i < database->detection_count; ++i) {
        DetectionEntry reference;
        deathboobs_bytes_copy(&reference, &detections(index)[i], sizeof(reference));
        if ((by_session && reference.session_id != session_id) ||
            (!by_session && reference.timestamp > to)) {
            break;
        }
        DeathboobsDetection record;
        error = load_detection(database, &reference, &record);
        if (error != CORE_OK) {
            break;
        }
        error = visitor(&record, context);
        if (error != CORE_OK) {
            break;
        }
    }
    database->visiting = false;
    return error;
}

ErrorCode deathboobs_database_visit_session_history(DeathboobsDatabase *database,
    DeathboobsSessionId session_id, DeathboobsDetectionVisitor visitor,
    void *context) {
    ErrorCode error = ready(database, true);
    if (error != CORE_OK) {
        return error;
    }
    if (visitor == NULL || session_id < DEATHBOOBS_ID_MIN) {
        return CORE_INVALID_ARGUMENT;
    }
    usize position;
    if (!find_session(database, session_id, &position)) {
        return CORE_NOT_FOUND;
    }
    DetectionEntry key;
    deathboobs_bytes_zero(&key, sizeof(key));
    key.session_id = session_id;
    usize start = detection_position(&database->session_history,
        database->detection_count, &key, ORDER_SESSION_TIME);
    return visit(database, &database->session_history, start, true, session_id,
                  0, visitor, context);
}

ErrorCode deathboobs_database_visit_detections_between(DeathboobsDatabase *database,
    DeathboobsTimestamp from, DeathboobsTimestamp to,
    DeathboobsDetectionVisitor visitor, void *context) {
    ErrorCode error = ready(database, true);
    if (error != CORE_OK) {
        return error;
    }
    if (visitor == NULL || from > to) {
        return CORE_INVALID_ARGUMENT;
    }
    DetectionEntry key;
    deathboobs_bytes_zero(&key, sizeof(key));
    key.timestamp = from;
    usize start = detection_position(&database->time_history,
        database->detection_count, &key, ORDER_TIME);
    return visit(database, &database->time_history, start, false, 0, to,
                  visitor, context);
}
