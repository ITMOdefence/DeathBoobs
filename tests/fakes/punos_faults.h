#ifndef DEATHBOOBS_TEST_PUNOS_FAULTS_H
#define DEATHBOOBS_TEST_PUNOS_FAULTS_H

#include <sys/core.h>

typedef enum FakeOperation {
    FAKE_OPEN,
    FAKE_READ,
    FAKE_WRITE,
    FAKE_FLUSH,
    FAKE_CLOSE,
    FAKE_REMOVE,
    FAKE_RENAME,
    FAKE_FREE
} FakeOperation;

/* Fail the nth matching call. A write may transfer a prefix before failing. */
void fake_fail(FakeOperation operation, const char *path, usize nth,
               ErrorCode error, usize partial);
void fake_capture_disk(bool durable);
void fake_restore_disk(void);
void fake_truncate(const char *path, usize size);
bool fake_no_resources(void);

#endif
