#ifndef DEATHBOOBS_TRANSACTION_H
#define DEATHBOOBS_TRANSACTION_H

#include <deathboobs/storage.h>

typedef struct DeathboobsTransaction DeathboobsTransaction;

/*
 * Boundary for part 3; this checkout supplies only a test fake.
 * One writer, append-only operations. begin publishes a handle only on success.
 * No call may publish a partially appended page. Output arguments remain
 * unchanged on error. The Storage handle remains owned by the caller.
 */
ErrorCode deathboobs_transaction_begin(DeathboobsStorage *storage,
                                     DeathboobsTransaction **out_transaction);

/*
 * Stages one data page and assigns its final physical ID, without changing
 * the caller's page. The assigned ID is not committed until commit succeeds.
 * Only the first staged append is required by the initial Database API.
 */
ErrorCode deathboobs_transaction_page_append(DeathboobsTransaction *transaction,
                                           const DeathboobsPage *page,
                                           DeathboobsPageId *out_page_id);

/*
 * On success, all staged writes are durable/visible and *transaction is NULL.
 * On failure, the handle remains valid and rollback MUST still be possible;
 * the caller must rollback before using Storage again. No successful commit
 * may be reported as an error (an ambiguous commit needs a separate contract).
 */
ErrorCode deathboobs_transaction_commit(DeathboobsTransaction **transaction);

/*
 * Restores pre-transaction Storage state, releases the handle and sets it to
 * NULL. On failure the handle remains valid for retry; Database becomes
 * unusable except for close/recovery. A NULL handle is a successful no-op.
 */
ErrorCode deathboobs_transaction_rollback(DeathboobsTransaction **transaction);

#endif
