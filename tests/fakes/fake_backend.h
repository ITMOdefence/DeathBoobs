#ifndef DEATHBOOBS_TEST_FAKE_BACKEND_H
#define DEATHBOOBS_TEST_FAKE_BACKEND_H

#include <deathboobs/transaction.h>
#include <sys/memory.h>

/*
 * Test-only backend: one persistent in-memory store, one open handle and one
 * staged append. All nonempty paths identify that same store. Closing and
 * reopening preserves pages; reset clears them and requires every handle and
 * MemoryBlock to have been released. It provides no crash durability or CRC.
 */
typedef enum FakeStorageOperation {
    FAKE_STORAGE_OPEN,
    FAKE_STORAGE_CLOSE,
    FAKE_STORAGE_COUNT,
    FAKE_STORAGE_READ,
    FAKE_STORAGE_WRITE,
    FAKE_STORAGE_APPEND,
    FAKE_STORAGE_FLUSH
} FakeStorageOperation;

typedef enum FakeTransactionOperation {
    FAKE_TRANSACTION_BEGIN,
    FAKE_TRANSACTION_APPEND,
    FAKE_TRANSACTION_COMMIT,
    FAKE_TRANSACTION_ROLLBACK
} FakeTransactionOperation;

/* One-shot failures, independently stored for each operation. */
void fake_storage_fail_next(FakeStorageOperation operation, ErrorCode error);
void fake_transaction_fail_next(FakeTransactionOperation operation,
                                ErrorCode error);
/*
 * Overrides only the reported ID of the next successful staged append.
 * The privately staged page keeps its real physical ID; reset clears this
 * one-shot fault. This tests Database's validation before commit.
 */
void fake_transaction_override_next_page_id(DeathboobsPageId page_id);

ErrorCode fake_backend_reset(void);
u64 fake_storage_committed_page_count(void);

/*
 * Closed-store fixture helper. Assigns physical ID but deliberately bypasses
 * content validation, permitting malformed records/page metadata in tests.
 */
ErrorCode fake_storage_seed_page(const DeathboobsPage *page);

/*
 * Fail the allocation after this many successful allocate/reallocate calls.
 * Failure is one-shot. Zero-size/no-op calls do not consume the countdown.
 * The counter covers libmem MemoryBlocks only; fake backend/handle allocations
 * use the host allocator separately. Free succeeds for valid blocks unless
 * an explicit free failure has been armed below.
 */
void fake_memory_fail_after(usize successful_allocations);
/* Fails the next valid, nonempty free once; block and counter stay unchanged. */
void fake_memory_fail_next_free(ErrorCode error);
void fake_memory_clear_failure(void);
usize fake_memory_live_blocks(void);

/* Storage close always consumes a valid handle, including injected errors. */

#endif
