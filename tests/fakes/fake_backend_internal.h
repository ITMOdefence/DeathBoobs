#ifndef DEATHBOOBS_TEST_FAKE_BACKEND_INTERNAL_H
#define DEATHBOOBS_TEST_FAKE_BACKEND_INTERNAL_H

#include "fake_backend.h"

/* Host ABI bridge used only by tests; never include host libc type headers. */
extern void *malloc(usize size);
extern void free(void *pointer);

bool fake_storage_is_active(const DeathboobsStorage *storage);
bool fake_storage_has_transaction(void);
void fake_storage_set_transaction(bool active);
ErrorCode fake_storage_validate_page(const DeathboobsPage *page);
ErrorCode fake_storage_commit_append(DeathboobsStorage *storage,
                                    const DeathboobsPage *page);
void fake_transaction_reset_failures(void);

#endif
