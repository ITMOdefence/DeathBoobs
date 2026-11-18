#ifndef DEATHBOOBS_RECORDS_H
#define DEATHBOOBS_RECORDS_H

#include <sys/core.h>

/*
 * Application records, not on-disk structures. Never persist sizeof(record)
 * bytes or depend on C member offsets, padding, enum size or bool encoding.
 * The versioned, little-endian wire contract is documented in docs/records.md.
 */
typedef u64 DeathboobsSessionId;
typedef u64 DeathboobsDetectionId;
typedef u64 DeathboobsTargetId;
typedef u64 DeathboobsTimestamp;

/* All three ID types use this range. Zero is invalid, not an absence marker. */
#define DEATHBOOBS_ID_MIN ((u64)1)
#define DEATHBOOBS_ID_MAX ((u64)0xffffffffffffffffULL)

/* Unsigned milliseconds since 1970-01-01 00:00:00 UTC; zero is valid. */
#define DEATHBOOBS_TIME_UNITS_PER_SECOND ((u64)1000)
#define DEATHBOOBS_TIMESTAMP_MIN ((DeathboobsTimestamp)0)
#define DEATHBOOBS_TIMESTAMP_MAX \
    ((DeathboobsTimestamp)0xffffffffffffffffULL)

/*
 * Local Cartesian target coordinates in millimetres: x east, y north.
 * The application chooses one origin per session and keeps it unchanged.
 * Each axis is limited to [-1000, 1000] metres.
 */
#define DEATHBOOBS_COORDINATE_UNITS_PER_METRE ((i32)1000)
#define DEATHBOOBS_COORDINATE_MIN ((i32)-1000000)
#define DEATHBOOBS_COORDINATE_MAX ((i32)1000000)

/* Clockwise bearing from north in hundredths of a degree: [0, 360). */
#define DEATHBOOBS_AZIMUTH_UNITS_PER_DEGREE ((u32)100)
#define DEATHBOOBS_AZIMUTH_MIN ((u32)0)
#define DEATHBOOBS_AZIMUTH_MAX ((u32)35999)

/* Non-negative target distance in millimetres, at most 1000 metres. */
#define DEATHBOOBS_DISTANCE_UNITS_PER_METRE ((u32)1000)
#define DEATHBOOBS_DISTANCE_MIN ((u32)0)
#define DEATHBOOBS_DISTANCE_MAX ((u32)1000000)

/* Probability = confidence / scale; 10000 represents 100 percent. */
#define DEATHBOOBS_CONFIDENCE_SCALE ((u16)10000)
#define DEATHBOOBS_CONFIDENCE_MIN ((u16)0)
#define DEATHBOOBS_CONFIDENCE_MAX DEATHBOOBS_CONFIDENCE_SCALE

/* Owned, opaque bytes referencing an external frame; no NUL terminator. */
#define DEATHBOOBS_FRAME_REFERENCE_MIN_BYTES ((u16)1)
#define DEATHBOOBS_FRAME_REFERENCE_MAX_BYTES ((u16)256)

/* The codec writes the documented one-byte value, never a native C enum. */
typedef enum DeathboobsSessionStatus {
    DEATHBOOBS_SESSION_STATUS_ACTIVE = 1,
    DEATHBOOBS_SESSION_STATUS_FINISHED = 2
} DeathboobsSessionStatus;

/*
 * A session snapshot. ID and started_at are immutable after creation.
 * ACTIVE requires has_finished_at == false; FINISHED requires it to be true
 * and finished_at >= started_at. An absent finished_at is canonically zero.
 * The presence flag alone determines whether the value exists.
 */
typedef struct DeathboobsGameSession {
    DeathboobsSessionId id;
    DeathboobsTimestamp started_at;
    bool has_finished_at;
    DeathboobsTimestamp finished_at;
    DeathboobsSessionStatus status;
} DeathboobsGameSession;

/*
 * An immutable detection in an existing active session. Database validates
 * timestamp >= session.started_at; the standalone codec validates only this
 * record's values. Detections may arrive out of timestamp order.
 *
 * Optional fields have explicit presence flags. If has_target_id is false,
 * target_id must be zero. If has_frame_reference is false, its size and all
 * bytes must be zero. Otherwise size is in [1, MAX_BYTES]; bytes at and after
 * size must be zero. The presence flags, not those zero values, mean absence.
 *
 * frame_reference owns its bytes, may contain zero bytes, and is neither a
 * C string nor an image. Its application-defined encoding is not interpreted
 * by the database. All numeric measurements are required fixed-point values.
 */
typedef struct DeathboobsDetection {
    DeathboobsDetectionId id;
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
} DeathboobsDetection;

#endif
