#ifndef DEATHBOOBS_DATABASE_H
#define DEATHBOOBS_DATABASE_H

#include <deathboobs/records.h>
#include <sys/string.h>

typedef struct DeathboobsDatabase DeathboobsDatabase;

/* Creation input: Database assigns id; all other rules match Detection. */
typedef struct DeathboobsDetectionInput {
    DeathboobsSessionId session_id;
    DeathboobsTimestamp timestamp;
    bool has_target_id;
    DeathboobsTargetId target_id;
    i32 x;
    i32 y;
    u32 azimuth;
    u32 distance;
    u16 confidence;
    bool has_frame_reference;
    u16 frame_reference_size;
    u8 frame_reference[DEATHBOOBS_FRAME_REFERENCE_MAX_BYTES];
} DeathboobsDetectionInput;

/*
 * Called in ascending (timestamp, detection_id) order. record is a borrowed
 * snapshot valid only for this call; copy it to retain it. A non-CORE_OK return
 * stops iteration and is propagated unchanged. context may be NULL.
 * Lookup calls are allowed from a visitor; mutation, close and nested visits
 * return CORE_INVALID_STATE. Calls are synchronous, single-threaded and must
 * be serialized by the application. A failed visit may have delivered a prefix.
 */
typedef ErrorCode (*DeathboobsDetectionVisitor)(
    const DeathboobsDetection *record, void *context);

/*
 * Opens/creates through Storage and rebuilds RAM indexes in one page pass.
 * Uses PUNos libmem for memory; links to Storage and Transaction public APIs.
 * No fake backend is linked implicitly by production code.
 * Normally out_database is written only on complete success. If cleanup after
 * a failed open itself fails and retains resources, the cleanup error is
 * returned and *out_database receives a CLOSE-ONLY handle for explicit retry;
 * queries/mutations on that handle return CORE_INVALID_STATE. Initialize the
 * output pointer to NULL and close any returned handle even after an error.
 * This exception prevents losing ownership when libmem cannot free a block.
 * Malformed records/page mappings or cross-record invariants return
 * CORE_INVALID_ARGUMENT;
 * unsupported record/page kinds return CORE_UNSUPPORTED. Dependency errors
 * propagate. No usable partially initialized Database is published.
 */
ErrorCode deathboobs_database_open(const String *path,
                                 DeathboobsDatabase **out_database);

/*
 * Closes Storage and releases indexes. NULL *database is a successful no-op;
 * database itself must not be NULL. A dependency failure may leave a handle
 * for retry: always inspect *database. After close has started, only another
 * close is permitted. A pending failed rollback is retried before closing.
 */
ErrorCode deathboobs_database_close(DeathboobsDatabase **database);

/*
 * Mutations validate input, reserve index memory, then perform one append via
 * Transaction. IDs and records become visible only after successful commit.
 * On error outputs are unchanged and no ID is consumed. Failed rollback puts
 * the handle into an error state; only close is allowed. Errors from rollback
 * take precedence over the original error. Independent ID sequences exhaust
 * at DEATHBOOBS_ID_MAX and return CORE_OVERFLOW without wrapping.
 */
ErrorCode deathboobs_database_start_session(DeathboobsDatabase *database,
    DeathboobsTimestamp started_at, DeathboobsGameSession *out_session);

/* End must be >= start and >= every stored timestamp in this session. */
ErrorCode deathboobs_database_finish_session(DeathboobsDatabase *database,
    DeathboobsSessionId session_id, DeathboobsTimestamp finished_at);

/* Only existing ACTIVE sessions accept detections with timestamp >= start. */
ErrorCode deathboobs_database_save_detection(DeathboobsDatabase *database,
    const DeathboobsDetectionInput *input, DeathboobsDetection *out_detection);

/* Lookups read/validate the indexed page; outputs are unchanged on error. */
ErrorCode deathboobs_database_get_session(DeathboobsDatabase *database,
    DeathboobsSessionId session_id, DeathboobsGameSession *out_session);
ErrorCode deathboobs_database_get_detection(DeathboobsDatabase *database,
    DeathboobsDetectionId detection_id, DeathboobsDetection *out_detection);

/* Unknown session returns CORE_NOT_FOUND; a known empty session visits none. */
ErrorCode deathboobs_database_visit_session_history(DeathboobsDatabase *database,
    DeathboobsSessionId session_id, DeathboobsDetectionVisitor visitor,
    void *context);

/* Global closed interval [from, to], both endpoints included; from <= to. */
ErrorCode deathboobs_database_visit_detections_between(DeathboobsDatabase *database,
    DeathboobsTimestamp from, DeathboobsTimestamp to,
    DeathboobsDetectionVisitor visitor, void *context);

/*
 * Unless stated otherwise: required NULL arguments or ID zero return
 * CORE_INVALID_ARGUMENT, missing IDs return CORE_NOT_FOUND, and forbidden
 * lifecycle/reentrant operations return CORE_INVALID_STATE. Input buffers and
 * output objects must be valid, aligned and disjoint from one another.
 */

#endif
