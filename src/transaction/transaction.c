#include <deathboobs/transaction.h>
#include <sys/memory.h>

#include "journal.h"
#include "../internal/page_codec.h"
#include "../internal/storage_internal.h"

typedef struct PendingPage {
    struct PendingPage *next;
    MemoryBlock self;
    DeathboobsPage new_page;
    bool appended;
} PendingPage;

typedef enum TransactionPhase {
    TX_STAGING,
    TX_COMMITTING,
    TX_COMMIT_CLEANUP,
    TX_ROLLING_BACK,
    TX_RELEASE_COMMIT,
    TX_RELEASE_ROLLBACK
} TransactionPhase;

struct DeathboobsTransaction {
    MemoryBlock self;
    DeathboobsStorage *storage;
    Journal *journal;
    PendingPage *pages;
    u64 count;
    u64 original_count;
    u64 append_count;
    TransactionPhase phase;
    bool stage_owned;
    bool published;
    bool publish_synced;
    bool storage_owned;
};

/* The project exposes one active transaction or recovery per process.
 * Storage separately holds an OS lock on the database inode.
 */
static bool writer_active;
/* Path buffers have no allocation to release when open or recovery fails. */
static Journal writer_journal;

static bool acquire_writer(void)
{
    return !__atomic_test_and_set(&writer_active, __ATOMIC_ACQUIRE);
}

static void release_writer(void)
{
    __atomic_clear(&writer_active, __ATOMIC_RELEASE);
}

static ErrorCode release_transaction(DeathboobsTransaction **transaction)
{
    DeathboobsTransaction *tx = *transaction;
    while (tx->pages) {
        PendingPage *next = tx->pages->next;
        MemoryBlock block = tx->pages->self;
        ErrorCode error = memory_free(&block);
        if (error != CORE_OK) {
            return error;
        }
        tx->pages = next;
    }
    if (tx->storage_owned) {
        ErrorCode end = deathboobs_storage_tx_end(tx->storage);
        if (end != CORE_OK) {
            return end;
        }
        tx->storage_owned = false;
    }
    MemoryBlock block = tx->self;
    ErrorCode error = memory_free(&block);
    if (error == CORE_OK) {
        *transaction = NULL;
        release_writer();
    }
    return error;
}

ErrorCode deathboobs_transaction_open(const String *database_path,
                                      const String *journal_path,
                                      DeathboobsStorage **out_storage)
{
    DeathboobsStorage *storage = NULL;
    if (!database_path || !out_storage) {
        return CORE_INVALID_ARGUMENT;
    }
    ErrorCode error = deathboobs_storage_open(database_path, &storage);
    if (error != CORE_OK) {
        return error;
    }
    error = deathboobs_transaction_recover(storage, journal_path);
    if (error != CORE_OK) {
        (void)deathboobs_storage_close(&storage);
        return error;
    }
    *out_storage = storage;
    return CORE_OK;
}

ErrorCode deathboobs_transaction_begin(DeathboobsStorage *storage,
                                       const String *journal_path,
                                       DeathboobsTransaction **out_transaction)
{
    MemoryBlock block = {0};
    Journal *journal = &writer_journal;
    if (!storage || !out_transaction) {
        return CORE_INVALID_ARGUMENT;
    }
    if (!acquire_writer()) {
        return CORE_WOULD_BLOCK;
    }
    ErrorCode error = journal_init(journal, storage, journal_path);
    if (error == CORE_OK) {
        error = journal_check_absent(&journal->path);
    }
    if (error == CORE_OK) {
        error = journal_check_absent(&journal->stage);
    }
    if (error == CORE_OK) {
        error = deathboobs_storage_flush(storage);
    }
    if (error == CORE_OK) {
        error = memory_allocate(sizeof(DeathboobsTransaction), &block);
    }
    if (error == CORE_OK) {
        error = deathboobs_storage_tx_begin(storage);
    }
    if (error != CORE_OK) {
        if (block.data) {
            (void)memory_free(&block);
        }
        release_writer();
        return error;
    }

    DeathboobsTransaction *tx = (DeathboobsTransaction *)block.data;
    *tx = (DeathboobsTransaction){
        .self = block, .storage = storage, .journal = journal,
        .storage_owned = true
    };
    *out_transaction = tx;
    return CORE_OK;
}

ErrorCode deathboobs_transaction_page_write(DeathboobsTransaction *tx,
                                            const DeathboobsPage *page)
{
    if (!tx || !page) {
        return CORE_INVALID_ARGUMENT;
    }
    if (tx->phase != TX_STAGING) {
        return CORE_INVALID_STATE;
    }
    if (page->id == 0 || page->id > tx->journal->page_count) {
        return CORE_NOT_FOUND;
    }
    if (!page_type_valid(page->type) || page->payload_size > DEATHBOOBS_PAGE_PAYLOAD_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }

    PendingPage **slot = &tx->pages;
    while (*slot && (*slot)->new_page.id < page->id) {
        slot = &(*slot)->next;
    }
    if (*slot && (*slot)->new_page.id == page->id) {
        (*slot)->new_page = *page;
        return CORE_OK;
    }
    if (tx->count == JOURNAL_MAX_RECORDS) {
        return CORE_OVERFLOW;
    }
    MemoryBlock block = {0};
    ErrorCode error = memory_allocate(sizeof(PendingPage), &block);
    if (error != CORE_OK) {
        return error;
    }
    PendingPage *entry = (PendingPage *)block.data;
    *entry = (PendingPage){ .next = *slot, .self = block, .new_page = *page };
    *slot = entry;
    ++tx->count;
    ++tx->original_count;
    return CORE_OK;
}

ErrorCode deathboobs_transaction_page_append(DeathboobsTransaction *tx,
                                             const DeathboobsPage *page,
                                             DeathboobsPageId *out_page_id)
{
    MemoryBlock block = {0};
    DeathboobsPage staged;
    PendingPage **slot;
    DeathboobsPageId id;
    ErrorCode error;
    if (!tx || !page || !out_page_id) {
        return CORE_INVALID_ARGUMENT;
    }
    if (tx->phase != TX_STAGING) {
        return CORE_INVALID_STATE;
    }
    if (!page_type_valid(page->type) ||
        page->payload_size > DEATHBOOBS_PAGE_PAYLOAD_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    if (tx->journal->page_count > (u64)0xffffffffffffffffULL - tx->append_count - 1) {
        return CORE_OVERFLOW;
    }
    id = tx->journal->page_count + tx->append_count + 1;
    staged = *page;
    staged.id = id;
    error = memory_allocate(sizeof(PendingPage), &block);
    if (error != CORE_OK) {
        return error;
    }
    slot = &tx->pages;
    while (*slot) {
        slot = &(*slot)->next;
    }
    PendingPage *entry = (PendingPage *)block.data;
    *entry = (PendingPage){.self = block, .new_page = staged, .appended = true};
    *slot = entry;
    ++tx->count;
    ++tx->append_count;
    *out_page_id = id;
    return CORE_OK;
}

static ErrorCode prepare_journal(DeathboobsTransaction *tx)
{
    FileHandle stage = NULL;
    DeathboobsPage original;

    /* libfile may create the file and then fail to allocate its handle. */
    tx->stage_owned = true;
    ErrorCode error = file_open(&tx->journal->stage, FILE_OPEN_MODE_READ_WRITE_CREATE,
                                0600, &stage);
    if (error != CORE_OK) {
        return error;
    }
    error = journal_write_header(stage, tx->journal, tx->original_count,
                                 tx->journal->page_count + tx->append_count);
    for (PendingPage *page = tx->pages; page && error == CORE_OK; page = page->next) {
        if (!page->appended) {
            error = deathboobs_storage_page_read(tx->storage, page->new_page.id,
                                                 &original);
            if (error == CORE_OK) {
                error = journal_write_page(stage, &original);
            }
        }
    }
    if (error == CORE_OK) {
        error = file_flush(stage);
    }
    error = journal_close(&stage, error);
    if (error != CORE_OK) {
        return error;
    }
    error = file_rename(&tx->journal->stage, &tx->journal->path);
    if (error == CORE_OK) {
        tx->stage_owned = false;
        tx->published = true;
    }
    return error;
}

static ErrorCode apply_changes(DeathboobsTransaction *tx)
{
    ErrorCode error = CORE_OK;
    if (!tx->published) {
        error = prepare_journal(tx);
    }
    if (error == CORE_OK && !tx->publish_synced) {
        error = journal_sync_directory(tx->journal);
        tx->publish_synced = error == CORE_OK;
    }
    if (error == CORE_OK && tx->append_count != 0) {
        error = deathboobs_storage_tx_resize(
            tx->storage, tx->journal->page_count + tx->append_count);
    }
    for (PendingPage *page = tx->pages; page && error == CORE_OK; page = page->next) {
        error = deathboobs_storage_tx_page_write(tx->storage, &page->new_page);
    }
    return error == CORE_OK ? deathboobs_storage_flush(tx->storage) : error;
}

static ErrorCode remove_and_sync(const Journal *journal, const String *path)
{
    ErrorCode error = journal_remove(path);
    return error == CORE_OK ? journal_sync_directory(journal) : error;
}

ErrorCode deathboobs_transaction_commit(DeathboobsTransaction **transaction)
{
    if (!transaction || !*transaction) {
        return CORE_INVALID_ARGUMENT;
    }
    DeathboobsTransaction *tx = *transaction;
    if (tx->phase == TX_ROLLING_BACK || tx->phase == TX_RELEASE_ROLLBACK) {
        return CORE_INVALID_STATE;
    }
    if (tx->phase == TX_STAGING || tx->phase == TX_COMMITTING) {
        if (tx->count != 0) {
            tx->phase = TX_COMMITTING;
            ErrorCode error = apply_changes(tx);
            if (error != CORE_OK) {
                return error;
            }
        }
        tx->phase = TX_COMMIT_CLEANUP;
    }
    if (tx->phase == TX_COMMIT_CLEANUP) {
        if (tx->published) {
            ErrorCode error = remove_and_sync(tx->journal, &tx->journal->path);
            if (error != CORE_OK) {
                return error;
            }
        }
        tx->phase = TX_RELEASE_COMMIT;
    }
    return release_transaction(transaction);
}

ErrorCode deathboobs_transaction_rollback(DeathboobsTransaction **transaction)
{
    if (!transaction || !*transaction) {
        return CORE_INVALID_ARGUMENT;
    }
    DeathboobsTransaction *tx = *transaction;
    if (tx->phase == TX_COMMIT_CLEANUP || tx->phase == TX_RELEASE_COMMIT) {
        return CORE_INVALID_STATE;
    }
    if (tx->phase != TX_RELEASE_ROLLBACK) {
        ErrorCode error = CORE_OK;
        tx->phase = TX_ROLLING_BACK;
        if (tx->published) {
            bool found;
            error = journal_restore(tx->journal, tx->storage, &found);
            if (error == CORE_OK) {
                /* If absent on retry, the previous attempt already unlinked it. */
                error = remove_and_sync(tx->journal, &tx->journal->path);
            }
        } else if (tx->stage_owned) {
            error = remove_and_sync(tx->journal, &tx->journal->stage);
        }
        if (error != CORE_OK) {
            return error;
        }
        tx->phase = TX_RELEASE_ROLLBACK;
    }
    return release_transaction(transaction);
}

ErrorCode deathboobs_transaction_recover(DeathboobsStorage *storage,
                                         const String *journal_path)
{
    Journal *journal = &writer_journal;
    bool found;
    if (!storage) {
        return CORE_INVALID_ARGUMENT;
    }
    if (!acquire_writer()) {
        return CORE_WOULD_BLOCK;
    }
    ErrorCode begin = deathboobs_storage_tx_begin(storage);
    if (begin != CORE_OK) {
        release_writer();
        return begin;
    }
    ErrorCode error = journal_init(journal, storage, journal_path);
    if (error == CORE_OK) {
        error = journal_restore(journal, storage, &found);
    }
    if (error == CORE_OK && found) {
        error = journal_remove(&journal->path);
    }
    if (error == CORE_OK) {
        error = remove_and_sync(journal, &journal->stage);
    }
    ErrorCode end = deathboobs_storage_tx_end(storage);
    if (error == CORE_OK) {
        error = end;
    }
    release_writer();
    return error;
}

ErrorCode deathboobs_transaction_can_rollback(
    const DeathboobsTransaction *transaction, bool *out_can_rollback)
{
    if (!transaction || !out_can_rollback) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_can_rollback = transaction->phase != TX_COMMIT_CLEANUP &&
                        transaction->phase != TX_RELEASE_COMMIT;
    return CORE_OK;
}
