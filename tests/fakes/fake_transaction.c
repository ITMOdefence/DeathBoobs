#include "fake_backend_internal.h"

struct DeathboobsTransaction {
    DeathboobsStorage *storage;
    bool has_page;
    DeathboobsPage page;
};

static DeathboobsTransaction *active_transaction;
static ErrorCode failures[4];
static bool override_page_id;
static DeathboobsPageId reported_page_id;

void fake_transaction_override_next_page_id(DeathboobsPageId page_id) {
    override_page_id = true;
    reported_page_id = page_id;
}

void fake_transaction_fail_next(FakeTransactionOperation operation,
                                ErrorCode error) {
    if ((usize)operation < 4) {
        failures[(usize)operation] = error;
    }
}

void fake_transaction_reset_failures(void) {
    for (usize i = 0; i < 4; ++i) {
        failures[i] = CORE_OK;
    }
    override_page_id = false;
    reported_page_id = 0;
}

static ErrorCode take_failure(FakeTransactionOperation operation) {
    ErrorCode error = failures[(usize)operation];
    failures[(usize)operation] = CORE_OK;
    return error;
}

ErrorCode deathboobs_transaction_begin(DeathboobsStorage *storage,
                                     DeathboobsTransaction **out_transaction) {
    if (!fake_storage_is_active(storage) || out_transaction == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (active_transaction != NULL || fake_storage_has_transaction()) {
        return CORE_INVALID_STATE;
    }
    ErrorCode error = take_failure(FAKE_TRANSACTION_BEGIN);
    if (error != CORE_OK) {
        return error;
    }
    DeathboobsTransaction *transaction = malloc(sizeof(*transaction));
    if (transaction == NULL) {
        return CORE_OUT_OF_MEMORY;
    }
    transaction->storage = storage;
    transaction->has_page = false;
    active_transaction = transaction;
    fake_storage_set_transaction(true);
    *out_transaction = transaction;
    return CORE_OK;
}

ErrorCode deathboobs_transaction_page_append(DeathboobsTransaction *transaction,
                                           const DeathboobsPage *page,
                                           DeathboobsPageId *out_page_id) {
    if (transaction == NULL || transaction != active_transaction ||
        page == NULL || out_page_id == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (transaction->has_page) {
        return CORE_INVALID_STATE;
    }
    ErrorCode error = fake_storage_validate_page(page);
    if (error != CORE_OK) {
        return error;
    }
    error = take_failure(FAKE_TRANSACTION_APPEND);
    if (error != CORE_OK) {
        return error;
    }
    u64 count = 0;
    error = deathboobs_storage_page_count(transaction->storage, &count);
    if (error != CORE_OK) {
        return error;
    }
    if (count == (u64)0xffffffffffffffffULL) {
        return CORE_OVERFLOW;
    }
    transaction->page = *page;
    transaction->page.id = count + 1;
    transaction->has_page = true;
    *out_page_id = override_page_id ? reported_page_id : count + 1;
    override_page_id = false;
    return CORE_OK;
}

static void release_transaction(DeathboobsTransaction **transaction) {
    free(*transaction);
    *transaction = NULL;
    active_transaction = NULL;
    fake_storage_set_transaction(false);
}

ErrorCode deathboobs_transaction_commit(DeathboobsTransaction **transaction) {
    if (transaction == NULL || *transaction == NULL ||
        *transaction != active_transaction) {
        return CORE_INVALID_ARGUMENT;
    }
    ErrorCode error = take_failure(FAKE_TRANSACTION_COMMIT);
    if (error != CORE_OK) {
        return error;
    }
    if ((*transaction)->has_page) {
        error = fake_storage_commit_append((*transaction)->storage,
                                           &(*transaction)->page);
    } else {
        error = deathboobs_storage_flush((*transaction)->storage);
    }
    if (error != CORE_OK) {
        return error;
    }
    release_transaction(transaction);
    return CORE_OK;
}

ErrorCode deathboobs_transaction_rollback(DeathboobsTransaction **transaction) {
    if (transaction == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (*transaction == NULL) {
        return CORE_OK;
    }
    if (*transaction != active_transaction) {
        return CORE_INVALID_ARGUMENT;
    }
    ErrorCode error = take_failure(FAKE_TRANSACTION_ROLLBACK);
    if (error != CORE_OK) {
        return error;
    }
    release_transaction(transaction);
    return CORE_OK;
}
