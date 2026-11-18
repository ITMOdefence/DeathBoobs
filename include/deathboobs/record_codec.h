#ifndef DEATHBOOBS_RECORD_CODEC_H
#define DEATHBOOBS_RECORD_CODEC_H

#include <deathboobs/records.h>

/* Record schema constants; independent of the Storage format version. */
#define DEATHBOOBS_RECORD_VERSION ((u8)1)
#define DEATHBOOBS_RECORD_KIND_GAME_SESSION ((u8)1)
#define DEATHBOOBS_RECORD_KIND_DETECTION ((u8)2)
#define DEATHBOOBS_RECORD_HEADER_SIZE ((usize)8)
#define DEATHBOOBS_GAME_SESSION_ENCODED_SIZE ((usize)40)
#define DEATHBOOBS_DETECTION_ENCODED_BASE_SIZE ((usize)60)
#define DEATHBOOBS_DETECTION_ENCODED_MAX_SIZE \
    (DEATHBOOBS_DETECTION_ENCODED_BASE_SIZE + \
     (usize)DEATHBOOBS_FRAME_REFERENCE_MAX_BYTES)

/*
 * Encodes one canonical record into buffer using the little-endian layout in
 * docs/records.md. On success, *out_size is the number of bytes written; bytes
 * after that size are untouched. No allocation or Storage access is performed.
 *
 * All pointers are required. A null pointer, invalid model or insufficient
 * buffer_size returns CORE_INVALID_ARGUMENT. On every error both buffer and
 * *out_size remain unchanged. The input record is never modified except when
 * the caller deliberately overlaps it with the output buffer.
 *
 * Input and output byte storage may overlap: encoding is staged before copying
 * to buffer. out_size must refer to a separate usize object, disjoint from the
 * input record and buffer. The caller provides valid memory for each declared
 * length; buffer need not be aligned.
 */
ErrorCode deathboobs_game_session_encode(const DeathboobsGameSession *record,
                                       u8 *buffer, usize buffer_size,
                                       usize *out_size);

/*
 * Decodes exactly one complete record; size must match its declared size.
 * Rejects truncated records, trailing bytes, noncanonical fields and values
 * outside the model's ranges. Only record-local validation is performed:
 * session existence, ID uniqueness and cross-record timing belong to Database.
 *
 * All pointers are required. Invalid pointers, lengths, values, flags/reserved
 * or a known kind passed to the wrong typed decoder return
 * CORE_INVALID_ARGUMENT. An unknown kind/version returns CORE_UNSUPPORTED
 * once a full common header is available; an incomplete header is always
 * CORE_INVALID_ARGUMENT. On error *out_record remains byte-for-byte unchanged.
 * On success it owns its optional bytes and has a zeroed unused frame tail.
 *
 * Input bytes may be unaligned or overlap out_record: decoding is completed
 * into a temporary model before publishing it. out_record itself must be a
 * properly aligned object. No allocation or Storage access is performed.
 */
ErrorCode deathboobs_game_session_decode(const u8 *buffer, usize size,
                                       DeathboobsGameSession *out_record);

/* Same encoding contract as deathboobs_game_session_encode. */
ErrorCode deathboobs_detection_encode(const DeathboobsDetection *record,
                                    u8 *buffer, usize buffer_size,
                                    usize *out_size);

/* Same decoding contract as deathboobs_game_session_decode. */
ErrorCode deathboobs_detection_decode(const u8 *buffer, usize size,
                                    DeathboobsDetection *out_record);

#endif
