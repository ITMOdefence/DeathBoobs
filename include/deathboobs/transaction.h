#ifndef DEATHBOOBS_TRANSACTION_H
#define DEATHBOOBS_TRANSACTION_H

#include <deathboobs/storage.h>

typedef struct DeathboobsTransaction DeathboobsTransaction;

/* All journal_path arguments may be NULL to use <database path>.journal.
 * If supplied, the path must match that name byte for byte.
 */

/* Opens locked Storage and runs recovery before returning it. */
ErrorCode deathboobs_transaction_open(const String *database_path,
                                      const String *journal_path,
                                      DeathboobsStorage **out_storage);

/* Begins against an open Storage. Flushes the baseline before staging writes. */
ErrorCode deathboobs_transaction_begin(DeathboobsStorage *storage,
                                       const String *journal_path,
                                       DeathboobsTransaction **out_transaction);

/* Stages a copy of an existing page. Repeated writes replace the staged copy. */
ErrorCode deathboobs_transaction_page_write(DeathboobsTransaction *transaction,
                                            const DeathboobsPage *page);

/* On error, retains the transaction. Retry commit, or use rollback before
 * commit has entered cleanup. On success, sets *transaction to NULL.
 */
ErrorCode deathboobs_transaction_commit(DeathboobsTransaction **transaction);

/* Cancels a transaction. If commit has entered cleanup, retry commit instead.
 * On error, retains the transaction for retry. On success, clears it.
 */
ErrorCode deathboobs_transaction_rollback(DeathboobsTransaction **transaction);

/* Restores a published journal and removes any unpublished staging file.
 * An absent journal is valid. No transaction may be active.
 */
ErrorCode deathboobs_transaction_recover(DeathboobsStorage *storage,
                                         const String *journal_path);

#endif
