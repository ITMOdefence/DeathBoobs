#ifndef DEATHBOOBS_INTERNAL_BYTES_H
#define DEATHBOOBS_INTERNAL_BYTES_H

#include <sys/core.h>

/*
 * PUNos has no libc memcpy/memset. Even -ffreestanding compilers may lower
 * aggregate initialization/assignment to those symbols. Volatile byte accesses
 * keep these RAM-only operations as explicit loops at every optimization level.
 * These helpers never encode a C structure into persistent storage.
 * Copy ranges must not overlap; callers use local snapshots or disjoint index
 * entries (adjacent entries are copied individually while shifting an array).
 */
static inline void deathboobs_bytes_zero(void *destination, usize size) {
    volatile u8 *bytes = (volatile u8 *)destination;
    for (usize i = 0; i < size; ++i) {
        bytes[i] = 0;
    }
}

static inline void deathboobs_bytes_copy(void *destination, const void *source,
                                       usize size) {
    volatile u8 *out = (volatile u8 *)destination;
    const volatile u8 *in = (const volatile u8 *)source;
    for (usize i = 0; i < size; ++i) {
        out[i] = in[i];
    }
}

#endif
