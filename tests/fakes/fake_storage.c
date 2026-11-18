#include "fake_backend_internal.h"

struct DeathboobsStorage {
    u64 marker;
};

static DeathboobsStorage *active_storage;
static bool transaction_active;
static DeathboobsPage *pages;
static usize page_count;
static usize page_capacity;
static ErrorCode failures[7];

void fake_storage_fail_next(FakeStorageOperation operation, ErrorCode error) {
    if ((usize)operation < 7) {
        failures[(usize)operation] = error;
    }
}

static ErrorCode take_failure(FakeStorageOperation operation) {
    ErrorCode error = failures[(usize)operation];
    failures[(usize)operation] = CORE_OK;
    return error;
}

bool fake_storage_is_active(const DeathboobsStorage *storage) {
    return storage != NULL && storage == active_storage;
}

bool fake_storage_has_transaction(void) {
    return transaction_active;
}

void fake_storage_set_transaction(bool active) {
    transaction_active = active;
}

ErrorCode fake_backend_reset(void) {
    if (active_storage != NULL || transaction_active ||
        fake_memory_live_blocks() != 0) {
        return CORE_INVALID_STATE;
    }
    free(pages);
    pages = NULL;
    page_count = 0;
    page_capacity = 0;
    for (usize i = 0; i < 7; ++i) {
        failures[i] = CORE_OK;
    }
    fake_transaction_reset_failures();
    fake_memory_clear_failure();
    return CORE_OK;
}

u64 fake_storage_committed_page_count(void) {
    return (u64)page_count;
}

static ErrorCode reserve_page(void) {
    if (page_count < page_capacity) {
        return CORE_OK;
    }
    if (page_count == USIZE_MAX) {
        return CORE_OVERFLOW;
    }
    usize capacity = page_capacity == 0 ? 8 : page_capacity;
    if (capacity <= page_count) {
        if (capacity > USIZE_MAX / 2) {
            return CORE_OVERFLOW;
        }
        capacity *= 2;
    }
    if (capacity > USIZE_MAX / sizeof(*pages)) {
        return CORE_OVERFLOW;
    }
    DeathboobsPage *replacement = malloc(capacity * sizeof(*pages));
    if (replacement == NULL) {
        return CORE_OUT_OF_MEMORY;
    }
    for (usize i = 0; i < page_count; ++i) {
        replacement[i] = pages[i];
    }
    free(pages);
    pages = replacement;
    page_capacity = capacity;
    return CORE_OK;
}

ErrorCode fake_storage_seed_page(const DeathboobsPage *page) {
    if (page == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (active_storage != NULL || transaction_active) {
        return CORE_INVALID_STATE;
    }
    ErrorCode error = reserve_page();
    if (error != CORE_OK) {
        return error;
    }
    pages[page_count] = *page;
    pages[page_count].id = (DeathboobsPageId)page_count + 1;
    ++page_count;
    return CORE_OK;
}

ErrorCode fake_storage_validate_page(const DeathboobsPage *page) {
    if (page == NULL || page->type < DEATHBOOBS_PAGE_TYPE_METADATA ||
        page->type > DEATHBOOBS_PAGE_TYPE_INDEX_RESERVED ||
        page->payload_size > DEATHBOOBS_PAGE_PAYLOAD_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    for (usize i = page->payload_size; i < DEATHBOOBS_PAGE_PAYLOAD_SIZE; ++i) {
        if (page->payload[i] != 0) {
            return CORE_INVALID_ARGUMENT;
        }
    }
    return CORE_OK;
}

ErrorCode deathboobs_storage_open(const String *path,
                                  DeathboobsStorage **out_storage) {
    if (path == NULL || path->data == NULL || path->length == 0 ||
        path->length > STRING_MAX_LENGTH || out_storage == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (active_storage != NULL) {
        return CORE_ALREADY_EXISTS;
    }
    ErrorCode error = take_failure(FAKE_STORAGE_OPEN);
    if (error != CORE_OK) {
        return error;
    }
    DeathboobsStorage *storage = malloc(sizeof(*storage));
    if (storage == NULL) {
        return CORE_OUT_OF_MEMORY;
    }
    storage->marker = 1;
    active_storage = storage;
    *out_storage = storage;
    return CORE_OK;
}

ErrorCode deathboobs_storage_close(DeathboobsStorage **storage) {
    if (storage == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (*storage == NULL) {
        return CORE_OK;
    }
    if (!fake_storage_is_active(*storage)) {
        return CORE_INVALID_ARGUMENT;
    }
    if (transaction_active) {
        return CORE_INVALID_STATE;
    }
    ErrorCode error = take_failure(FAKE_STORAGE_CLOSE);
    ErrorCode flush_error = deathboobs_storage_flush(*storage);
    if (error == CORE_OK) {
        error = flush_error;
    }
    free(*storage);
    *storage = NULL;
    active_storage = NULL;
    return error;
}

ErrorCode deathboobs_storage_page_count(const DeathboobsStorage *storage,
                                        u64 *out_count) {
    if (!fake_storage_is_active(storage) || out_count == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    ErrorCode error = take_failure(FAKE_STORAGE_COUNT);
    if (error != CORE_OK) {
        return error;
    }
    *out_count = (u64)page_count;
    return CORE_OK;
}

ErrorCode deathboobs_storage_page_read(DeathboobsStorage *storage,
                                       DeathboobsPageId page_id,
                                       DeathboobsPage *out_page) {
    if (!fake_storage_is_active(storage) || out_page == NULL || page_id == 0 ||
        page_id > (u64)page_count) {
        return CORE_INVALID_ARGUMENT;
    }
    ErrorCode error = take_failure(FAKE_STORAGE_READ);
    if (error != CORE_OK) {
        return error;
    }
    *out_page = pages[(usize)page_id - 1];
    return CORE_OK;
}

ErrorCode deathboobs_storage_page_write(DeathboobsStorage *storage,
                                        const DeathboobsPage *page) {
    if (!fake_storage_is_active(storage) || page == NULL || page->id == 0 ||
        page->id > (u64)page_count) {
        return CORE_INVALID_ARGUMENT;
    }
    if (transaction_active) {
        return CORE_INVALID_STATE;
    }
    ErrorCode error = fake_storage_validate_page(page);
    if (error != CORE_OK) {
        return error;
    }
    error = take_failure(FAKE_STORAGE_WRITE);
    if (error != CORE_OK) {
        return error;
    }
    pages[(usize)page->id - 1] = *page;
    return CORE_OK;
}

ErrorCode deathboobs_storage_page_append(DeathboobsStorage *storage,
                                         DeathboobsPage *page,
                                         DeathboobsPageId *out_page_id) {
    if (!fake_storage_is_active(storage) || page == NULL || out_page_id == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (transaction_active) {
        return CORE_INVALID_STATE;
    }
    ErrorCode error = fake_storage_validate_page(page);
    if (error != CORE_OK) {
        return error;
    }
    error = take_failure(FAKE_STORAGE_APPEND);
    if (error != CORE_OK) {
        return error;
    }
    error = reserve_page();
    if (error != CORE_OK) {
        return error;
    }
    DeathboobsPageId page_id = (DeathboobsPageId)page_count + 1;
    pages[page_count] = *page;
    pages[page_count].id = page_id;
    ++page_count;
    page->id = page_id;
    *out_page_id = page_id;
    return CORE_OK;
}

ErrorCode deathboobs_storage_flush(DeathboobsStorage *storage) {
    if (!fake_storage_is_active(storage)) {
        return CORE_INVALID_ARGUMENT;
    }
    return take_failure(FAKE_STORAGE_FLUSH);
}

/* Both injected failures and allocation are resolved before publication. */
ErrorCode fake_storage_commit_append(DeathboobsStorage *storage,
                                    const DeathboobsPage *page) {
    if (!fake_storage_is_active(storage) || !transaction_active ||
        page == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    ErrorCode error = fake_storage_validate_page(page);
    if (error != CORE_OK) {
        return error;
    }
    if (page_count == USIZE_MAX || page->id != (u64)page_count + 1) {
        return CORE_OVERFLOW;
    }
    error = take_failure(FAKE_STORAGE_APPEND);
    if (error != CORE_OK) {
        return error;
    }
    error = take_failure(FAKE_STORAGE_FLUSH);
    if (error != CORE_OK) {
        return error;
    }
    error = reserve_page();
    if (error != CORE_OK) {
        return error;
    }
    pages[page_count] = *page;
    ++page_count;
    return CORE_OK;
}
