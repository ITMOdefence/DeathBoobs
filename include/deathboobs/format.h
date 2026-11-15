#ifndef DEATHBOOBS_FORMAT_H
#define DEATHBOOBS_FORMAT_H

#include <sys/core.h>

#define DEATHBOOBS_PAGE_SIZE ((usize)4096)
#define DEATHBOOBS_PAGE_HEADER_SIZE ((usize)32)
#define DEATHBOOBS_PAGE_PAYLOAD_SIZE \
    (DEATHBOOBS_PAGE_SIZE - DEATHBOOBS_PAGE_HEADER_SIZE)

#define DEATHBOOBS_FORMAT_MAJOR ((u16)1)
#define DEATHBOOBS_FORMAT_MINOR ((u16)0)

typedef u64 DeathboobsPageId;

typedef enum DeathboobsPageType {
    DEATHBOOBS_PAGE_TYPE_INVALID = 0,
    DEATHBOOBS_PAGE_TYPE_METADATA = 1,
    DEATHBOOBS_PAGE_TYPE_SESSIONS = 2,
    DEATHBOOBS_PAGE_TYPE_DETECTIONS = 3,
    DEATHBOOBS_PAGE_TYPE_INDEX_RESERVED = 4
} DeathboobsPageType;

typedef struct DeathboobsPage {
    DeathboobsPageId id;
    DeathboobsPageType type;
    u16 flags;
    u32 payload_size;
    u64 generation;
    u8 payload[DEATHBOOBS_PAGE_PAYLOAD_SIZE];
} DeathboobsPage;

_Static_assert(DEATHBOOBS_PAGE_PAYLOAD_SIZE == 4064,
               "DeathBoobs page payload size mismatch");
_Static_assert(sizeof(DeathboobsPageType) == 4,
               "DeathBoobs page type ABI mismatch");

#endif
