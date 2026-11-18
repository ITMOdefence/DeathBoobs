#include <deathboobs/record_codec.h>
#include "../internal/bytes.h"

#define SESSION_FLAG_FINISHED_AT ((u16)0x0001)
#define DETECTION_FLAG_TARGET_ID ((u16)0x0001)
#define DETECTION_FLAG_FRAME_REFERENCE ((u16)0x0002)

_Static_assert(DEATHBOOBS_DETECTION_ENCODED_MAX_SIZE <= 0xffffffffULL,
               "record size must fit in its u32 wire field");

static void write_u16(u8 *bytes, u16 value) {
    bytes[0] = (u8)value;
    bytes[1] = (u8)(value >> 8);
}

static void write_u32(u8 *bytes, u32 value) {
    for (usize i = 0; i < 4; ++i) {
        bytes[i] = (u8)(value >> (i * 8));
    }
}

static void write_u64(u8 *bytes, u64 value) {
    for (usize i = 0; i < 8; ++i) {
        bytes[i] = (u8)(value >> (i * 8));
    }
}

static u16 read_u16(const u8 *bytes) {
    return (u16)((u16)bytes[0] | ((u16)bytes[1] << 8));
}

static u32 read_u32(const u8 *bytes) {
    u32 value = 0;
    for (usize i = 0; i < 4; ++i) {
        value |= (u32)bytes[i] << (i * 8);
    }
    return value;
}

static u64 read_u64(const u8 *bytes) {
    u64 value = 0;
    for (usize i = 0; i < 8; ++i) {
        value |= (u64)bytes[i] << (i * 8);
    }
    return value;
}

static i32 read_i32(const u8 *bytes) {
    u32 raw = read_u32(bytes);
    /* Widen first: converting a high-bit u32 directly to i32 is not portable. */
    i64 value = (i64)raw;
    if (raw > (u32)0x7fffffffU) {
        value -= 0x100000000LL;
    }
    return (i32)value;
}

static ErrorCode validate_game_session(const DeathboobsGameSession *record) {
    if (record->id < DEATHBOOBS_ID_MIN) {
        return CORE_INVALID_ARGUMENT;
    }
    if (record->status == DEATHBOOBS_SESSION_STATUS_ACTIVE) {
        if (record->has_finished_at || record->finished_at != 0) {
            return CORE_INVALID_ARGUMENT;
        }
    } else if (record->status == DEATHBOOBS_SESSION_STATUS_FINISHED) {
        if (!record->has_finished_at ||
            record->finished_at < record->started_at) {
            return CORE_INVALID_ARGUMENT;
        }
    } else {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

static ErrorCode validate_detection(const DeathboobsDetection *record) {
    if (record->id < DEATHBOOBS_ID_MIN ||
        record->session_id < DEATHBOOBS_ID_MIN ||
        record->x < DEATHBOOBS_COORDINATE_MIN ||
        record->x > DEATHBOOBS_COORDINATE_MAX ||
        record->y < DEATHBOOBS_COORDINATE_MIN ||
        record->y > DEATHBOOBS_COORDINATE_MAX ||
        record->azimuth > DEATHBOOBS_AZIMUTH_MAX ||
        record->distance > DEATHBOOBS_DISTANCE_MAX ||
        record->confidence > DEATHBOOBS_CONFIDENCE_MAX) {
        return CORE_INVALID_ARGUMENT;
    }
    if ((record->has_target_id && record->target_id < DEATHBOOBS_ID_MIN) ||
        (!record->has_target_id && record->target_id != 0)) {
        return CORE_INVALID_ARGUMENT;
    }
    if (record->has_frame_reference) {
        if (record->frame_reference_size <
                DEATHBOOBS_FRAME_REFERENCE_MIN_BYTES ||
            record->frame_reference_size >
                DEATHBOOBS_FRAME_REFERENCE_MAX_BYTES) {
            return CORE_INVALID_ARGUMENT;
        }
    } else if (record->frame_reference_size != 0) {
        return CORE_INVALID_ARGUMENT;
    }
    /* Length is checked before indexing the fixed-size model buffer. */
    for (usize i = record->frame_reference_size;
         i < DEATHBOOBS_FRAME_REFERENCE_MAX_BYTES; ++i) {
        if (record->frame_reference[i] != 0) {
            return CORE_INVALID_ARGUMENT;
        }
    }
    return CORE_OK;
}

static void write_header(u8 *buffer, u8 kind, u16 flags, usize size) {
    buffer[0] = kind;
    buffer[1] = DEATHBOOBS_RECORD_VERSION;
    write_u16(buffer + 2, flags);
    write_u32(buffer + 4, (u32)size);
}

static ErrorCode validate_header(const u8 *buffer, usize size, u8 kind,
                                 usize min_size, usize max_size) {
    if (size < DEATHBOOBS_RECORD_HEADER_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    if (buffer[1] != DEATHBOOBS_RECORD_VERSION ||
        (buffer[0] != DEATHBOOBS_RECORD_KIND_GAME_SESSION &&
         buffer[0] != DEATHBOOBS_RECORD_KIND_DETECTION)) {
        return CORE_UNSUPPORTED;
    }
    if (buffer[0] != kind || size < min_size || size > max_size ||
        (usize)read_u32(buffer + 4) != size) {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

ErrorCode deathboobs_game_session_encode(const DeathboobsGameSession *record,
                                       u8 *buffer, usize buffer_size,
                                       usize *out_size) {
    if (record == NULL || buffer == NULL || out_size == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    ErrorCode error = validate_game_session(record);
    if (error != CORE_OK) {
        return error;
    }
    if (buffer_size < DEATHBOOBS_GAME_SESSION_ENCODED_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    /* Stage all bytes, including reserved zeros, before touching any output. */
    u8 encoded[DEATHBOOBS_GAME_SESSION_ENCODED_SIZE];
    deathboobs_bytes_zero(encoded, sizeof(encoded));
    u16 flags = record->has_finished_at ? SESSION_FLAG_FINISHED_AT : 0;
    write_header(encoded, DEATHBOOBS_RECORD_KIND_GAME_SESSION, flags,
                 DEATHBOOBS_GAME_SESSION_ENCODED_SIZE);
    write_u64(encoded + 8, record->id);
    write_u64(encoded + 16, record->started_at);
    write_u64(encoded + 24, record->finished_at);
    encoded[32] = (u8)record->status;

    deathboobs_bytes_copy(buffer, encoded, DEATHBOOBS_GAME_SESSION_ENCODED_SIZE);
    *out_size = DEATHBOOBS_GAME_SESSION_ENCODED_SIZE;
    return CORE_OK;
}

ErrorCode deathboobs_game_session_decode(const u8 *buffer, usize size,
                                       DeathboobsGameSession *out_record) {
    if (buffer == NULL || out_record == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    ErrorCode error = validate_header(buffer, size,
        DEATHBOOBS_RECORD_KIND_GAME_SESSION,
        DEATHBOOBS_GAME_SESSION_ENCODED_SIZE,
        DEATHBOOBS_GAME_SESSION_ENCODED_SIZE);
    if (error != CORE_OK) {
        return error;
    }
    u16 flags = read_u16(buffer + 2);
    if ((flags & (u16)~SESSION_FLAG_FINISHED_AT) != 0) {
        return CORE_INVALID_ARGUMENT;
    }
    for (usize i = 33; i < DEATHBOOBS_GAME_SESSION_ENCODED_SIZE; ++i) {
        if (buffer[i] != 0) {
            return CORE_INVALID_ARGUMENT;
        }
    }

    DeathboobsGameSession decoded;
    deathboobs_bytes_zero(&decoded, sizeof(decoded));
    decoded.id = read_u64(buffer + 8);
    decoded.started_at = read_u64(buffer + 16);
    decoded.has_finished_at = (flags & SESSION_FLAG_FINISHED_AT) != 0;
    decoded.finished_at = read_u64(buffer + 24);
    decoded.status = (DeathboobsSessionStatus)buffer[32];
    error = validate_game_session(&decoded);
    if (error != CORE_OK) {
        return error;
    }
    deathboobs_bytes_copy(out_record, &decoded, sizeof(decoded));
    return CORE_OK;
}

ErrorCode deathboobs_detection_encode(const DeathboobsDetection *record,
                                    u8 *buffer, usize buffer_size,
                                    usize *out_size) {
    if (record == NULL || buffer == NULL || out_size == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    ErrorCode error = validate_detection(record);
    if (error != CORE_OK) {
        return error;
    }
    /* Validated N <= 256; 60 + N <= 316 fits usize and the u32 wire size. */
    usize size = DEATHBOOBS_DETECTION_ENCODED_BASE_SIZE +
                 (usize)record->frame_reference_size;
    if (buffer_size < size) {
        return CORE_INVALID_ARGUMENT;
    }
    u8 encoded[DEATHBOOBS_DETECTION_ENCODED_MAX_SIZE];
    deathboobs_bytes_zero(encoded, sizeof(encoded));
    u16 flags = 0;
    if (record->has_target_id) {
        flags |= DETECTION_FLAG_TARGET_ID;
    }
    if (record->has_frame_reference) {
        flags |= DETECTION_FLAG_FRAME_REFERENCE;
    }
    write_header(encoded, DEATHBOOBS_RECORD_KIND_DETECTION, flags, size);
    write_u64(encoded + 8, record->id);
    write_u64(encoded + 16, record->session_id);
    write_u64(encoded + 24, record->timestamp);
    write_u64(encoded + 32, record->target_id);
    /* Conversion to u32 gives the required two's-complement bit pattern. */
    write_u32(encoded + 40, (u32)record->x);
    write_u32(encoded + 44, (u32)record->y);
    write_u32(encoded + 48, record->azimuth);
    write_u32(encoded + 52, record->distance);
    write_u16(encoded + 56, record->confidence);
    write_u16(encoded + 58, record->frame_reference_size);
    deathboobs_bytes_copy(encoded + DEATHBOOBS_DETECTION_ENCODED_BASE_SIZE,
                          record->frame_reference, record->frame_reference_size);

    deathboobs_bytes_copy(buffer, encoded, size);
    *out_size = size;
    return CORE_OK;
}

ErrorCode deathboobs_detection_decode(const u8 *buffer, usize size,
                                    DeathboobsDetection *out_record) {
    if (buffer == NULL || out_record == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    ErrorCode error = validate_header(buffer, size,
        DEATHBOOBS_RECORD_KIND_DETECTION,
        DEATHBOOBS_DETECTION_ENCODED_BASE_SIZE,
        DEATHBOOBS_DETECTION_ENCODED_MAX_SIZE);
    if (error != CORE_OK) {
        return error;
    }
    u16 flags = read_u16(buffer + 2);
    if ((flags & (u16)~(DETECTION_FLAG_TARGET_ID |
                       DETECTION_FLAG_FRAME_REFERENCE)) != 0) {
        return CORE_INVALID_ARGUMENT;
    }
    u16 frame_size = read_u16(buffer + 58);
    if (frame_size > DEATHBOOBS_FRAME_REFERENCE_MAX_BYTES ||
        size - DEATHBOOBS_DETECTION_ENCODED_BASE_SIZE != (usize)frame_size) {
        return CORE_INVALID_ARGUMENT;
    }
    /* frame_size is bounded and fully available before any frame copy. */
    DeathboobsDetection decoded;
    deathboobs_bytes_zero(&decoded, sizeof(decoded));
    decoded.id = read_u64(buffer + 8);
    decoded.session_id = read_u64(buffer + 16);
    decoded.timestamp = read_u64(buffer + 24);
    decoded.has_target_id = (flags & DETECTION_FLAG_TARGET_ID) != 0;
    decoded.target_id = read_u64(buffer + 32);
    decoded.x = read_i32(buffer + 40);
    decoded.y = read_i32(buffer + 44);
    decoded.azimuth = read_u32(buffer + 48);
    decoded.distance = read_u32(buffer + 52);
    decoded.confidence = read_u16(buffer + 56);
    decoded.has_frame_reference =
        (flags & DETECTION_FLAG_FRAME_REFERENCE) != 0;
    decoded.frame_reference_size = frame_size;
    error = validate_detection(&decoded);
    if (error != CORE_OK) {
        return error;
    }
    deathboobs_bytes_copy(decoded.frame_reference,
                          buffer + DEATHBOOBS_DETECTION_ENCODED_BASE_SIZE,
                          frame_size);
    deathboobs_bytes_copy(out_record, &decoded, sizeof(decoded));
    return CORE_OK;
}
