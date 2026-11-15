#ifndef DEATHBOOBS_STORAGE_H
#define DEATHBOOBS_STORAGE_H

#include <deathboobs/format.h>
#include <sys/string.h>

typedef struct DeathboobsStorage DeathboobsStorage;

/* Opens an existing database or creates a new one when the file is absent. */
ErrorCode deathboobs_storage_open(const String *path,
                                  DeathboobsStorage **out_storage);

/* Flushes pending data, closes the file and invalidates *storage. */
ErrorCode deathboobs_storage_close(DeathboobsStorage **storage);

/* Returns the number of data pages. The file header page is not included. */
ErrorCode deathboobs_storage_page_count(const DeathboobsStorage *storage,
                                        u64 *out_count);

/* Reads and validates one existing data page. */
ErrorCode deathboobs_storage_page_read(DeathboobsStorage *storage,
                                       DeathboobsPageId page_id,
                                       DeathboobsPage *out_page);

/* Replaces one existing data page in full. */
ErrorCode deathboobs_storage_page_write(DeathboobsStorage *storage,
                                        const DeathboobsPage *page);

/* Appends a page and assigns its physical identifier. */
ErrorCode deathboobs_storage_page_append(DeathboobsStorage *storage,
                                         DeathboobsPage *page,
                                         DeathboobsPageId *out_page_id);

/* Pushes all previous writes to the storage device. */
ErrorCode deathboobs_storage_flush(DeathboobsStorage *storage);

#endif
