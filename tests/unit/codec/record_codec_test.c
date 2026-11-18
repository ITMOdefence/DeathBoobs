#include <deathboobs/record_codec.h>

/* PUNos freestanding types must not be mixed with host libc type headers. */
extern int puts(const char *text);

#define STRINGIFY_INNER(value) #value
#define STRINGIFY(value) STRINGIFY_INNER(value)
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            puts("FAIL " __FILE__ ":" STRINGIFY(__LINE__) ": " #condition); \
            return false;                                                  \
        }                                                                  \
    } while (0)

static void fill_bytes(void *destination, u8 value, usize size) {
    u8 *bytes = destination;
    for (usize i = 0; i < size; ++i) {
        bytes[i] = value;
    }
}

static void copy_bytes(void *destination, const void *source, usize size) {
    u8 *out = destination;
    const u8 *in = source;
    for (usize i = 0; i < size; ++i) {
        out[i] = in[i];
    }
}

static bool equal_bytes(const void *left, const void *right, usize size) {
    const u8 *a = left;
    const u8 *b = right;
    for (usize i = 0; i < size; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

static bool bytes_are(const void *data, usize size, u8 value) {
    const u8 *bytes = data;
    for (usize i = 0; i < size; ++i) {
        if (bytes[i] != value) {
            return false;
        }
    }
    return true;
}

/* Independent examples from the offset tables, not built by codec helpers. */
static const u8 session_golden[40] = {
    0x01, 0x01, 0x01, 0x00, 0x28, 0x00, 0x00, 0x00,
    0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
    0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
    0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21,
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const u8 detection_golden[63] = {
    0x02, 0x01, 0x03, 0x00, 0x3f, 0x00, 0x00, 0x00,
    0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
    0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
    0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21,
    0x38, 0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31,
    0x1e, 0xfb, 0xff, 0xff, 0x40, 0xe2, 0x01, 0x00,
    0x5a, 0x23, 0x00, 0x00, 0x39, 0x30, 0x00, 0x00,
    0x2e, 0x22, 0x03, 0x00, 0x00, 0xff, 0x41
};

static DeathboobsGameSession example_session(void) {
    DeathboobsGameSession result = {0};
    result.id = 0x0102030405060708ULL;
    result.started_at = 0x1112131415161718ULL;
    result.has_finished_at = true;
    result.finished_at = 0x2122232425262728ULL;
    result.status = DEATHBOOBS_SESSION_STATUS_FINISHED;
    return result;
}

static DeathboobsDetection example_detection(void) {
    DeathboobsDetection result = {0};
    result.id = 0x0102030405060708ULL;
    result.session_id = 0x1112131415161718ULL;
    result.timestamp = 0x2122232425262728ULL;
    result.has_target_id = true;
    result.target_id = 0x3132333435363738ULL;
    result.x = -1250;
    result.y = 123456;
    result.azimuth = 9050;
    result.distance = 12345;
    result.confidence = 8750;
    result.has_frame_reference = true;
    result.frame_reference_size = 3;
    result.frame_reference[0] = 0x00;
    result.frame_reference[1] = 0xff;
    result.frame_reference[2] = 0x41;
    return result;
}

static bool equal_session(const DeathboobsGameSession *a,
                          const DeathboobsGameSession *b) {
    return a->id == b->id && a->started_at == b->started_at &&
           a->has_finished_at == b->has_finished_at &&
           a->finished_at == b->finished_at && a->status == b->status;
}

static bool equal_detection(const DeathboobsDetection *a,
                            const DeathboobsDetection *b) {
    return a->id == b->id && a->session_id == b->session_id &&
           a->timestamp == b->timestamp &&
           a->has_target_id == b->has_target_id &&
           a->target_id == b->target_id && a->x == b->x && a->y == b->y &&
           a->azimuth == b->azimuth && a->distance == b->distance &&
           a->confidence == b->confidence &&
           a->has_frame_reference == b->has_frame_reference &&
           a->frame_reference_size == b->frame_reference_size &&
           equal_bytes(a->frame_reference, b->frame_reference,
                       DEATHBOOBS_FRAME_REFERENCE_MAX_BYTES);
}

static bool reject_session(const u8 *bytes, usize size, ErrorCode expected) {
    DeathboobsGameSession output;
    u8 original[sizeof(output)];
    fill_bytes(&output, 0xa5, sizeof(output));
    copy_bytes(original, &output, sizeof(output));
    CHECK(deathboobs_game_session_decode(bytes, size, &output) == expected);
    CHECK(equal_bytes(&output, original, sizeof(output)));
    return true;
}

static bool reject_detection(const u8 *bytes, usize size, ErrorCode expected) {
    DeathboobsDetection output;
    u8 original[sizeof(output)];
    fill_bytes(&output, 0xa5, sizeof(output));
    copy_bytes(original, &output, sizeof(output));
    CHECK(deathboobs_detection_decode(bytes, size, &output) == expected);
    CHECK(equal_bytes(&output, original, sizeof(output)));
    return true;
}

static bool reject_session_model(const DeathboobsGameSession *input,
                                 usize capacity) {
    u8 bytes[42];
    usize size = 0x12345678;
    fill_bytes(bytes, 0xa5, sizeof(bytes));
    CHECK(deathboobs_game_session_encode(input, bytes + 1, capacity, &size) ==
          CORE_INVALID_ARGUMENT);
    CHECK(size == 0x12345678);
    CHECK(bytes_are(bytes, sizeof(bytes), 0xa5));
    return true;
}

static bool reject_detection_model(const DeathboobsDetection *input,
                                   usize capacity) {
    u8 bytes[318];
    usize size = 0x12345678;
    fill_bytes(bytes, 0xa5, sizeof(bytes));
    CHECK(deathboobs_detection_encode(input, bytes + 1, capacity, &size) ==
          CORE_INVALID_ARGUMENT);
    CHECK(size == 0x12345678);
    CHECK(bytes_are(bytes, sizeof(bytes), 0xa5));
    return true;
}

static bool test_golden_vectors(void) {
    DeathboobsGameSession session = example_session();
    DeathboobsGameSession decoded_session;
    DeathboobsDetection detection = example_detection();
    DeathboobsDetection decoded_detection;
    u8 bytes[318];
    usize size = 0;

    fill_bytes(bytes, 0xa5, sizeof(bytes));
    CHECK(deathboobs_game_session_encode(&session, bytes + 1, 40, &size) ==
          CORE_OK);
    CHECK(size == sizeof(session_golden));
    CHECK(equal_bytes(bytes + 1, session_golden, size));
    CHECK(bytes[0] == 0xa5 && bytes_are(bytes + 41, sizeof(bytes) - 41, 0xa5));
    CHECK(deathboobs_game_session_decode(session_golden,
                                         sizeof(session_golden),
                                         &decoded_session) == CORE_OK);
    CHECK(equal_session(&session, &decoded_session));
    CHECK(deathboobs_game_session_decode(bytes + 1, size, &decoded_session) ==
          CORE_OK);
    CHECK(equal_session(&session, &decoded_session));

    fill_bytes(bytes, 0xa5, sizeof(bytes));
    CHECK(deathboobs_detection_encode(&detection, bytes + 1, 63, &size) ==
          CORE_OK);
    CHECK(size == sizeof(detection_golden));
    CHECK(equal_bytes(bytes + 1, detection_golden, size));
    CHECK(bytes[0] == 0xa5 && bytes_are(bytes + 64, sizeof(bytes) - 64, 0xa5));
    fill_bytes(&decoded_detection, 0xa5, sizeof(decoded_detection));
    CHECK(deathboobs_detection_decode(detection_golden,
                                      sizeof(detection_golden),
                                      &decoded_detection) == CORE_OK);
    CHECK(equal_detection(&detection, &decoded_detection));
    CHECK(deathboobs_detection_decode(bytes + 1, size, &decoded_detection) ==
          CORE_OK);
    CHECK(equal_detection(&detection, &decoded_detection));
    return true;
}

static bool test_session_boundaries(void) {
    DeathboobsGameSession input = {0};
    DeathboobsGameSession output;
    u8 bytes[40];
    usize size = 0;
    input.id = DEATHBOOBS_ID_MIN;
    input.status = DEATHBOOBS_SESSION_STATUS_ACTIVE;
    CHECK(deathboobs_game_session_encode(&input, bytes, sizeof(bytes), &size) ==
          CORE_OK);
    CHECK(size == 40 && bytes[2] == 0 && bytes[32] == 1);
    CHECK(deathboobs_game_session_decode(bytes, size, &output) == CORE_OK);
    CHECK(equal_session(&input, &output));

    /* A present zero timestamp is distinct from an absent timestamp. */
    input.status = DEATHBOOBS_SESSION_STATUS_FINISHED;
    input.has_finished_at = true;
    CHECK(deathboobs_game_session_encode(&input, bytes, sizeof(bytes), &size) ==
          CORE_OK);
    CHECK(bytes[2] == 1 && bytes[32] == 2);
    CHECK(deathboobs_game_session_decode(bytes, size, &output) == CORE_OK);
    CHECK(equal_session(&input, &output));

    input.id = DEATHBOOBS_ID_MAX;
    input.started_at = DEATHBOOBS_TIMESTAMP_MAX;
    input.finished_at = DEATHBOOBS_TIMESTAMP_MAX;
    CHECK(deathboobs_game_session_encode(&input, bytes, sizeof(bytes), &size) ==
          CORE_OK);
    CHECK(deathboobs_game_session_decode(bytes, size, &output) == CORE_OK);
    CHECK(equal_session(&input, &output));
    return true;
}

static bool test_detection_optional_and_boundaries(void) {
    for (u8 flags = 0; flags < 4; ++flags) {
        DeathboobsDetection input = {0};
        DeathboobsDetection output;
        u8 bytes[318];
        usize size = 0;
        input.id = DEATHBOOBS_ID_MAX;
        input.session_id = DEATHBOOBS_ID_MAX;
        input.timestamp = DEATHBOOBS_TIMESTAMP_MAX;
        input.x = DEATHBOOBS_COORDINATE_MIN;
        input.y = DEATHBOOBS_COORDINATE_MAX;
        input.azimuth = DEATHBOOBS_AZIMUTH_MAX;
        input.distance = DEATHBOOBS_DISTANCE_MAX;
        input.confidence = DEATHBOOBS_CONFIDENCE_MAX;
        input.has_target_id = (flags & 1) != 0;
        input.target_id = input.has_target_id ? DEATHBOOBS_ID_MAX : 0;
        input.has_frame_reference = (flags & 2) != 0;
        if (input.has_frame_reference) {
            input.frame_reference_size = DEATHBOOBS_FRAME_REFERENCE_MAX_BYTES;
            for (usize i = 0; i < input.frame_reference_size; ++i) {
                input.frame_reference[i] = (u8)i;
            }
        }
        fill_bytes(bytes, 0xa5, sizeof(bytes));
        CHECK(deathboobs_detection_encode(&input, bytes + 1,
                                          sizeof(bytes) - 2, &size) == CORE_OK);
        CHECK(size == (usize)60 + input.frame_reference_size);
        CHECK(bytes[3] == flags && bytes[4] == 0);
        CHECK(bytes[0] == 0xa5 &&
              bytes_are(bytes + size + 1, sizeof(bytes) - size - 1, 0xa5));
        fill_bytes(&output, 0xa5, sizeof(output));
        CHECK(deathboobs_detection_decode(bytes + 1, size, &output) == CORE_OK);
        CHECK(equal_detection(&input, &output));
    }

    DeathboobsDetection minimum = {0};
    DeathboobsDetection output;
    u8 bytes[61];
    usize size = 0;
    minimum.id = DEATHBOOBS_ID_MIN;
    minimum.session_id = DEATHBOOBS_ID_MIN;
    minimum.has_target_id = true;
    minimum.target_id = DEATHBOOBS_ID_MIN;
    minimum.has_frame_reference = true;
    minimum.frame_reference_size = DEATHBOOBS_FRAME_REFERENCE_MIN_BYTES;
    CHECK(deathboobs_detection_encode(&minimum, bytes, sizeof(bytes), &size) ==
          CORE_OK);
    CHECK(size == 61);
    CHECK(deathboobs_detection_decode(bytes, size, &output) == CORE_OK);
    CHECK(equal_detection(&minimum, &output));
    return true;
}

static bool test_every_truncated_prefix(void) {
    for (usize size = 0; size < sizeof(session_golden); ++size) {
        u8 bytes[size == 0 ? 1 : size];
        copy_bytes(bytes, session_golden, size);
        CHECK(reject_session(bytes, size, CORE_INVALID_ARGUMENT));
    }
    for (usize size = 0; size < sizeof(detection_golden); ++size) {
        u8 bytes[size == 0 ? 1 : size];
        copy_bytes(bytes, detection_golden, size);
        CHECK(reject_detection(bytes, size, CORE_INVALID_ARGUMENT));
    }

    DeathboobsDetection input = example_detection();
    u8 maximum[316];
    usize encoded_size = 0;
    input.frame_reference_size = DEATHBOOBS_FRAME_REFERENCE_MAX_BYTES;
    fill_bytes(input.frame_reference, 0xff, sizeof(input.frame_reference));
    CHECK(deathboobs_detection_encode(&input, maximum, sizeof(maximum),
                                      &encoded_size) == CORE_OK);
    CHECK(encoded_size == sizeof(maximum));
    for (usize size = 0; size < sizeof(maximum); ++size) {
        u8 bytes[size == 0 ? 1 : size];
        copy_bytes(bytes, maximum, size);
        CHECK(reject_detection(bytes, size, CORE_INVALID_ARGUMENT));
    }
    return true;
}

static bool test_unsupported_and_wrong_kind(void) {
    const u8 unsupported[] = {0, 2, 255};
    for (usize i = 0; i < sizeof(unsupported); ++i) {
        u8 session[40];
        u8 detection[63];
        copy_bytes(session, session_golden, sizeof(session));
        copy_bytes(detection, detection_golden, sizeof(detection));
        session[1] = unsupported[i];
        detection[1] = unsupported[i];
        CHECK(reject_session(session, sizeof(session), CORE_UNSUPPORTED));
        CHECK(reject_detection(detection, sizeof(detection), CORE_UNSUPPORTED));
        CHECK(reject_session(session, 8, CORE_UNSUPPORTED));
        CHECK(reject_detection(detection, 8, CORE_UNSUPPORTED));
        CHECK(reject_session(session, 7, CORE_INVALID_ARGUMENT));
        CHECK(reject_detection(detection, 7, CORE_INVALID_ARGUMENT));
    }
    const u8 kinds[] = {0, 3, 255};
    for (usize i = 0; i < sizeof(kinds); ++i) {
        u8 session[40];
        u8 detection[63];
        copy_bytes(session, session_golden, sizeof(session));
        copy_bytes(detection, detection_golden, sizeof(detection));
        session[0] = kinds[i];
        detection[0] = kinds[i];
        CHECK(reject_session(session, sizeof(session), CORE_UNSUPPORTED));
        CHECK(reject_detection(detection, sizeof(detection), CORE_UNSUPPORTED));
    }
    CHECK(reject_session(detection_golden, sizeof(detection_golden),
                         CORE_INVALID_ARGUMENT));
    CHECK(reject_detection(session_golden, sizeof(session_golden),
                           CORE_INVALID_ARGUMENT));
    return true;
}

static bool test_corrupt_session(void) {
    const struct { usize offset; u8 value; } mutations[] = {
        {2, 0}, {2, 3}, {3, 1}, {4, 0}, {4, 39}, {4, 41},
        {5, 1}, {6, 1}, {7, 255}, {32, 0}, {32, 1}, {32, 3}, {32, 255}
    };
    for (usize i = 0; i < sizeof(mutations) / sizeof(mutations[0]); ++i) {
        u8 bytes[40];
        copy_bytes(bytes, session_golden, sizeof(bytes));
        bytes[mutations[i].offset] = mutations[i].value;
        CHECK(reject_session(bytes, sizeof(bytes), CORE_INVALID_ARGUMENT));
    }
    for (usize offset = 33; offset < 40; ++offset) {
        u8 bytes[40];
        copy_bytes(bytes, session_golden, sizeof(bytes));
        bytes[offset] = 1;
        CHECK(reject_session(bytes, sizeof(bytes), CORE_INVALID_ARGUMENT));
    }
    u8 bytes[41];
    copy_bytes(bytes, session_golden, sizeof(session_golden));
    fill_bytes(bytes + 8, 0, 8);
    CHECK(reject_session(bytes, 40, CORE_INVALID_ARGUMENT));
    copy_bytes(bytes, session_golden, sizeof(session_golden));
    fill_bytes(bytes + 24, 0, 8); /* End before start. */
    CHECK(reject_session(bytes, 40, CORE_INVALID_ARGUMENT));
    copy_bytes(bytes, session_golden, sizeof(session_golden));
    bytes[2] = 0;
    bytes[32] = 1; /* Absent finished_at must be canonical zero. */
    CHECK(reject_session(bytes, 40, CORE_INVALID_ARGUMENT));
    copy_bytes(bytes, session_golden, sizeof(session_golden));
    bytes[40] = 0;
    CHECK(reject_session(bytes, sizeof(bytes), CORE_INVALID_ARGUMENT));
    bytes[4] = 41;
    CHECK(reject_session(bytes, sizeof(bytes), CORE_INVALID_ARGUMENT));
    return true;
}

static bool test_corrupt_detection(void) {
    const struct { usize offset; u8 value; } mutations[] = {
        {2, 0}, {2, 1}, {2, 2}, {2, 7}, {3, 1}, {4, 0}, {4, 62}, {4, 64},
        {5, 1}, {6, 1}, {7, 255}, {58, 0}, {58, 2}, {58, 4}, {59, 1},
        {49, 0xff}, {54, 0xff}, {57, 0xff}
    };
    for (usize i = 0; i < sizeof(mutations) / sizeof(mutations[0]); ++i) {
        u8 bytes[63];
        copy_bytes(bytes, detection_golden, sizeof(bytes));
        bytes[mutations[i].offset] = mutations[i].value;
        CHECK(reject_detection(bytes, sizeof(bytes), CORE_INVALID_ARGUMENT));
    }
    const usize id_offsets[] = {8, 16, 32};
    for (usize i = 0; i < sizeof(id_offsets) / sizeof(id_offsets[0]); ++i) {
        u8 bytes[63];
        copy_bytes(bytes, detection_golden, sizeof(bytes));
        fill_bytes(bytes + id_offsets[i], 0, 8);
        CHECK(reject_detection(bytes, sizeof(bytes), CORE_INVALID_ARGUMENT));
    }
    /* Signed bounds, INT32_MIN/MAX, distance/azimuth/confidence just too large. */
    const struct { usize offset; u8 value[4]; usize width; } values[] = {
        {40, {0xbf, 0xbd, 0xf0, 0xff}, 4}, /* -1000001 */
        {40, {0x41, 0x42, 0x0f, 0x00}, 4}, /*  1000001 */
        {44, {0xbf, 0xbd, 0xf0, 0xff}, 4},
        {44, {0x41, 0x42, 0x0f, 0x00}, 4},
        {40, {0x00, 0x00, 0x00, 0x80}, 4},
        {44, {0xff, 0xff, 0xff, 0x7f}, 4},
        {48, {0xa0, 0x8c, 0x00, 0x00}, 4}, /* 36000 */
        {52, {0x41, 0x42, 0x0f, 0x00}, 4}, /* 1000001 */
        {56, {0x11, 0x27, 0x00, 0x00}, 2}, /* 10001 */
        {58, {0x01, 0x01, 0x00, 0x00}, 2}  /* 257 */
    };
    for (usize i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        u8 bytes[63];
        copy_bytes(bytes, detection_golden, sizeof(bytes));
        copy_bytes(bytes + values[i].offset, values[i].value, values[i].width);
        CHECK(reject_detection(bytes, sizeof(bytes), CORE_INVALID_ARGUMENT));
    }
    u8 bytes[64];
    copy_bytes(bytes, detection_golden, sizeof(detection_golden));
    bytes[63] = 0;
    CHECK(reject_detection(bytes, sizeof(bytes), CORE_INVALID_ARGUMENT));
    bytes[4] = 64;
    CHECK(reject_detection(bytes, sizeof(bytes), CORE_INVALID_ARGUMENT));

    /* Exact lengths do not make inconsistent optional fields valid. */
    copy_bytes(bytes, detection_golden, sizeof(detection_golden));
    bytes[4] = 60;
    bytes[58] = 0;
    CHECK(reject_detection(bytes, 60, CORE_INVALID_ARGUMENT));
    bytes[2] = 0;
    CHECK(reject_detection(bytes, 60, CORE_INVALID_ARGUMENT));
    return true;
}

static bool test_invalid_session_models(void) {
    DeathboobsGameSession input = example_session();
    CHECK(reject_session_model(NULL, 40));
    input.id = 0;
    CHECK(reject_session_model(&input, 40));
    input = example_session();
    input.status = (DeathboobsSessionStatus)0;
    CHECK(reject_session_model(&input, 40));
    input.status = (DeathboobsSessionStatus)3;
    CHECK(reject_session_model(&input, 40));
    input.status = (DeathboobsSessionStatus)-1;
    CHECK(reject_session_model(&input, 40));
    input = example_session();
    input.has_finished_at = false;
    CHECK(reject_session_model(&input, 40));
    input.finished_at = 0;
    CHECK(reject_session_model(&input, 40));
    input = example_session();
    input.finished_at = input.started_at - 1;
    CHECK(reject_session_model(&input, 40));
    input = example_session();
    input.status = DEATHBOOBS_SESSION_STATUS_ACTIVE;
    CHECK(reject_session_model(&input, 40));
    input.has_finished_at = false;
    CHECK(reject_session_model(&input, 40));
    input = example_session();
    for (usize capacity = 0; capacity < 40; ++capacity) {
        CHECK(reject_session_model(&input, capacity));
    }
    return true;
}

static bool test_invalid_detection_models(void) {
    DeathboobsDetection input = example_detection();
    CHECK(reject_detection_model(NULL, 316));
#define REJECT_FIELD(field, value)                         \
    do {                                                   \
        input = example_detection();                       \
        input.field = (value);                             \
        CHECK(reject_detection_model(&input, 316));         \
    } while (0)
    REJECT_FIELD(id, 0);
    REJECT_FIELD(session_id, 0);
    REJECT_FIELD(target_id, 0);
    REJECT_FIELD(has_target_id, false);
    REJECT_FIELD(x, DEATHBOOBS_COORDINATE_MIN - 1);
    REJECT_FIELD(x, DEATHBOOBS_COORDINATE_MAX + 1);
    REJECT_FIELD(y, DEATHBOOBS_COORDINATE_MIN - 1);
    REJECT_FIELD(y, DEATHBOOBS_COORDINATE_MAX + 1);
    REJECT_FIELD(azimuth, DEATHBOOBS_AZIMUTH_MAX + 1);
    REJECT_FIELD(distance, DEATHBOOBS_DISTANCE_MAX + 1);
    REJECT_FIELD(confidence, DEATHBOOBS_CONFIDENCE_MAX + 1);
    REJECT_FIELD(frame_reference_size, 0);
    REJECT_FIELD(frame_reference_size, 257);
    REJECT_FIELD(frame_reference_size, 65535);
    REJECT_FIELD(has_frame_reference, false);
#undef REJECT_FIELD
    input = example_detection();
    input.frame_reference[3] = 1;
    CHECK(reject_detection_model(&input, 316));
    input = example_detection();
    input.frame_reference[255] = 1;
    CHECK(reject_detection_model(&input, 316));
    input = example_detection();
    input.has_frame_reference = false;
    input.frame_reference_size = 0;
    CHECK(reject_detection_model(&input, 316));
    input = example_detection();
    for (usize capacity = 0; capacity < 63; ++capacity) {
        CHECK(reject_detection_model(&input, capacity));
    }
    input.frame_reference_size = DEATHBOOBS_FRAME_REFERENCE_MAX_BYTES;
    fill_bytes(input.frame_reference, 0x5a, sizeof(input.frame_reference));
    for (usize capacity = 0; capacity < 316; ++capacity) {
        CHECK(reject_detection_model(&input, capacity));
    }
    return true;
}

static bool test_null_arguments(void) {
    DeathboobsGameSession session = example_session();
    DeathboobsDetection detection = example_detection();
    u8 bytes[316];
    usize size = 123;
    fill_bytes(bytes, 0xa5, sizeof(bytes));
    CHECK(deathboobs_game_session_encode(&session, NULL, 40, &size) ==
          CORE_INVALID_ARGUMENT);
    CHECK(size == 123);
    CHECK(deathboobs_game_session_encode(&session, bytes, sizeof(bytes), NULL) ==
          CORE_INVALID_ARGUMENT);
    CHECK(bytes_are(bytes, sizeof(bytes), 0xa5));
    CHECK(deathboobs_detection_encode(&detection, NULL, 63, &size) ==
          CORE_INVALID_ARGUMENT);
    CHECK(size == 123);
    CHECK(deathboobs_detection_encode(&detection, bytes, sizeof(bytes), NULL) ==
          CORE_INVALID_ARGUMENT);
    CHECK(bytes_are(bytes, sizeof(bytes), 0xa5));
    CHECK(reject_session(NULL, 40, CORE_INVALID_ARGUMENT));
    CHECK(reject_session(NULL, 0, CORE_INVALID_ARGUMENT));
    CHECK(reject_detection(NULL, 63, CORE_INVALID_ARGUMENT));
    CHECK(reject_detection(NULL, 0, CORE_INVALID_ARGUMENT));
    CHECK(deathboobs_game_session_decode(session_golden,
                                         sizeof(session_golden), NULL) ==
          CORE_INVALID_ARGUMENT);
    CHECK(deathboobs_detection_decode(detection_golden,
                                      sizeof(detection_golden), NULL) ==
          CORE_INVALID_ARGUMENT);
    return true;
}

static bool test_overlapping_buffers(void) {
    union {
        DeathboobsGameSession session;
        DeathboobsDetection detection;
        u8 bytes[sizeof(DeathboobsDetection) + 316];
    } storage;
    DeathboobsGameSession session = example_session();
    DeathboobsDetection detection = example_detection();
    for (usize offset = 0; offset < 2; ++offset) {
        usize size = 0;
        storage.session = session;
        CHECK(deathboobs_game_session_encode(&storage.session,
                                             storage.bytes + offset,
                                             sizeof(storage.bytes) - offset,
                                             &size) == CORE_OK);
        CHECK(size == sizeof(session_golden));
        CHECK(equal_bytes(storage.bytes + offset, session_golden, size));
        CHECK(deathboobs_game_session_decode(storage.bytes + offset, size,
                                             &storage.session) == CORE_OK);
        CHECK(equal_session(&session, &storage.session));

        storage.detection = detection;
        CHECK(deathboobs_detection_encode(&storage.detection,
                                          storage.bytes + offset,
                                          sizeof(storage.bytes) - offset,
                                          &size) == CORE_OK);
        CHECK(size == sizeof(detection_golden));
        CHECK(equal_bytes(storage.bytes + offset, detection_golden, size));
        CHECK(deathboobs_detection_decode(storage.bytes + offset, size,
                                          &storage.detection) == CORE_OK);
        CHECK(equal_detection(&detection, &storage.detection));
    }
    return true;
}

int main(void) {
    const struct { const char *name; bool (*run)(void); } tests[] = {
        {"golden vectors and unaligned buffers", test_golden_vectors},
        {"session boundaries and present epoch", test_session_boundaries},
        {"detection optional fields and boundaries", test_detection_optional_and_boundaries},
        {"every truncated prefix", test_every_truncated_prefix},
        {"unsupported versions/kinds and wrong record kind", test_unsupported_and_wrong_kind},
        {"corrupt session records", test_corrupt_session},
        {"corrupt detection records", test_corrupt_detection},
        {"invalid session models and short buffers", test_invalid_session_models},
        {"invalid detection models and short buffers", test_invalid_detection_models},
        {"null arguments", test_null_arguments},
        {"overlapping input/output buffers", test_overlapping_buffers}
    };
    bool passed = true;
    for (usize i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        puts(tests[i].name);
        if (!tests[i].run()) {
            passed = false;
        }
    }
    puts(passed ? "PASS: 11 codec test groups" : "FAIL: codec tests");
    return passed ? 0 : 1;
}
