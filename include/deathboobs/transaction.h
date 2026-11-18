#ifndef DEATHBOOBS_TRANSACTION_H
#define DEATHBOOBS_TRANSACTION_H

#include <deathboobs/storage.h>

typedef struct DeathboobsTransaction DeathboobsTransaction;

/* Opens locked Storage and completes recovery before returning it. */
ErrorCode deathboobs_transaction_open(const String *database_path,
                                      const String *journal_path,
                                      DeathboobsStorage **out_storage);

/* Begins one transaction. journal_path may be NULL for <database>.journal. */
ErrorCode deathboobs_transaction_begin(DeathboobsStorage *storage,
                                       const String *journal_path,
                                       DeathboobsTransaction **out_transaction);

/* Stages replacement of an existing page. Last write for an ID wins. */
ErrorCode deathboobs_transaction_page_write(DeathboobsTransaction *transaction,
                                            const DeathboobsPage *page);

/* Stages an appended page and returns its final physical ID. */
ErrorCode deathboobs_transaction_page_append(DeathboobsTransaction *transaction,
                                             const DeathboobsPage *page,
                                             DeathboobsPageId *out_page_id);

/* On success consumes the handle. On error it remains valid. */
ErrorCode deathboobs_transaction_commit(DeathboobsTransaction **transaction);

/* Rolls back unless commit has crossed the durable cleanup phase. */
ErrorCode deathboobs_transaction_rollback(DeathboobsTransaction **transaction);

/* Reports whether rollback remains valid after a commit error. */
ErrorCode deathboobs_transaction_can_rollback(
    const DeathboobsTransaction *transaction, bool *out_can_rollback);

/* Recovers a published journal and removes abandoned staging files. */
ErrorCode deathboobs_transaction_recover(DeathboobsStorage *storage,
                                         const String *journal_path);

#endif
