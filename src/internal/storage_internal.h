#ifndef DEATHBOOBS_INTERNAL_STORAGE_INTERNAL_H
#define DEATHBOOBS_INTERNAL_STORAGE_INTERNAL_H

#include <deathboobs/storage.h>

ErrorCode deathboobs_storage_tx_begin(DeathboobsStorage *storage);
ErrorCode deathboobs_storage_tx_end(DeathboobsStorage *storage);
ErrorCode deathboobs_storage_tx_page_write(DeathboobsStorage *storage,
                                           const DeathboobsPage *page);
ErrorCode deathboobs_storage_tx_resize(DeathboobsStorage *storage,
                                       u64 page_count);

#endif
