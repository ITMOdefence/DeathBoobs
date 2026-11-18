#ifndef DEATHBOOBS_INTERNAL_WRITER_LOCK_H
#define DEATHBOOBS_INTERNAL_WRITER_LOCK_H

#include <sys/string.h>

typedef struct DeathboobsWriterLock {
    i32 fd;
    u64 device;
    u64 inode;
} DeathboobsWriterLock;

ErrorCode deathboobs_writer_lock(const String *path, DeathboobsWriterLock *out_lock);
ErrorCode deathboobs_writer_unlock(DeathboobsWriterLock *lock);
ErrorCode deathboobs_writer_resize(DeathboobsWriterLock *lock, u64 byte_size);

#endif
