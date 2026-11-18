#ifndef DEATHBOOBS_INTERNAL_JOURNAL_H
#define DEATHBOOBS_INTERNAL_JOURNAL_H

#include <deathboobs/storage.h>
#include <sys/file.h>

/* Maximum pathname size including NUL on the PUNos Linux target. */
#define JOURNAL_PATH_CAPACITY ((usize)4096)
#define JOURNAL_HEADER_SIZE ((usize)56)
#define JOURNAL_MAX_RECORDS (((u64)ISIZE_MAX - JOURNAL_HEADER_SIZE) / DEATHBOOBS_PAGE_SIZE)

typedef struct Journal {
    char paths[3 * JOURNAL_PATH_CAPACITY];
    String path;
    String stage;
    String directory;
    u64 device;
    u64 inode;
    u64 page_count;
} Journal;

ErrorCode journal_init(Journal *journal, const DeathboobsStorage *storage,
                       const String *requested_path);
ErrorCode journal_check_absent(const String *path);
ErrorCode journal_remove(const String *path);
ErrorCode journal_sync_directory(const Journal *journal);
ErrorCode journal_close(FileHandle *file, ErrorCode primary);
ErrorCode journal_write_header(FileHandle file, const Journal *journal, u64 count);
ErrorCode journal_write_page(FileHandle file, const DeathboobsPage *page);
ErrorCode journal_restore(const Journal *journal, DeathboobsStorage *storage,
                          bool *out_found);

#endif
