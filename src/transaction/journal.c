#include "journal.h"

#include "../internal/page_codec.h"

#define JOURNAL_MAJOR ((u16)1)
#define JOURNAL_MINOR ((u16)0)
#define HEADER_CRC_OFFSET ((usize)48)

static const u8 MAGIC[8] = {'D', 'B', 'J', 'R', 'N', 'L', 0, 0};
static const char JOURNAL_SUFFIX[] = ".journal";
static const char STAGING_SUFFIX[] = ".tmp";

static void copy_chars(char *dest, const char *src, usize size)
{
    for (usize i = 0; i < size; ++i) {
        dest[i] = src[i];
    }
}

ErrorCode journal_init(Journal *journal, const DeathboobsStorage *storage,
                       const String *requested_path)
{
    const String *database;
    usize journal_size;
    usize stage_size;
    usize directory_length = 0;
    char *buffer;
    ErrorCode error = deathboobs_storage_file_identity(storage, &journal->device,
                                                        &journal->inode, &database);
    if (error != CORE_OK) {
        return error;
    }
    error = deathboobs_storage_page_count(storage, &journal->page_count);
    if (error != CORE_OK) {
        return error;
    }
    if (database->length > JOURNAL_PATH_CAPACITY + 1 -
                            sizeof(JOURNAL_SUFFIX) - sizeof(STAGING_SUFFIX)) {
        return CORE_OVERFLOW;
    }
    journal_size = database->length + sizeof(JOURNAL_SUFFIX);
    stage_size = journal_size - 1 + sizeof(STAGING_SUFFIX);
    if (requested_path) {
        if (!requested_path->data || requested_path->length != journal_size - 1) {
            return CORE_INVALID_ARGUMENT;
        }
        for (usize i = 0; i < requested_path->length; ++i) {
            char expected = i < database->length
                                ? database->data[i]
                                : JOURNAL_SUFFIX[i - database->length];
            if (requested_path->data[i] != expected) {
                return CORE_INVALID_ARGUMENT;
            }
        }
    }
    for (usize i = 0; i < database->length; ++i) {
        if (database->data[i] == '/') {
            directory_length = i + 1;
        }
    }
    const char *directory = database->data;
    if (directory_length == 0) {
        directory = ".";
        directory_length = 1;
    }

    /* Each of the three terminated paths fits in JOURNAL_PATH_CAPACITY. */
    buffer = journal->paths;
    journal->path = (String){buffer, journal_size - 1};
    copy_chars(buffer, database->data, database->length);
    copy_chars(buffer + database->length, JOURNAL_SUFFIX, sizeof(JOURNAL_SUFFIX));

    buffer += journal_size;
    journal->stage = (String){buffer, stage_size - 1};
    copy_chars(buffer, journal->path.data, journal->path.length);
    copy_chars(buffer + journal->path.length, STAGING_SUFFIX, sizeof(STAGING_SUFFIX));

    buffer += stage_size;
    journal->directory = (String){buffer, directory_length};
    copy_chars(buffer, directory, directory_length);
    buffer[directory_length] = 0;
    return CORE_OK;
}

ErrorCode journal_close(FileHandle *file, ErrorCode primary)
{
    ErrorCode cleanup = file_close(file);
    return primary == CORE_OK ? cleanup : primary;
}

ErrorCode journal_check_absent(const String *path)
{
    FileHandle file = NULL;
    ErrorCode error = file_open(path, FILE_OPEN_MODE_READ, 0, &file);
    if (error == CORE_NOT_FOUND) {
        return CORE_OK;
    }
    if (error != CORE_OK) {
        return error;
    }
    error = file_close(&file);
    return error == CORE_OK ? CORE_ALREADY_EXISTS : error;
}

ErrorCode journal_remove(const String *path)
{
    ErrorCode error = file_remove(path);
    return error == CORE_NOT_FOUND ? CORE_OK : error;
}

ErrorCode journal_sync_directory(const Journal *journal)
{
    FileHandle directory = NULL;
    ErrorCode error = file_open(&journal->directory, FILE_OPEN_MODE_READ, 0,
                                &directory);
    if (error != CORE_OK) {
        return error;
    }
    error = file_flush(directory);
    return journal_close(&directory, error);
}

ErrorCode journal_write_header(FileHandle file, const Journal *journal, u64 count)
{
    u8 header[JOURNAL_HEADER_SIZE] = {0};
    for (usize i = 0; i < sizeof(MAGIC); ++i) {
        header[i] = MAGIC[i];
    }
    put_u16_le(header + 8, JOURNAL_MAJOR);
    put_u16_le(header + 10, JOURNAL_MINOR);
    put_u32_le(header + 12, (u32)DEATHBOOBS_PAGE_SIZE);
    put_u64_le(header + 16, journal->device);
    put_u64_le(header + 24, journal->inode);
    put_u64_le(header + 32, journal->page_count);
    put_u64_le(header + 40, count);
    put_u32_le(header + HEADER_CRC_OFFSET, crc32(header, sizeof(header)));
    return file_write_all(file, (Bytes){header, sizeof(header)});
}

ErrorCode journal_write_page(FileHandle file, const DeathboobsPage *page)
{
    u8 record[DEATHBOOBS_PAGE_SIZE];
    ErrorCode error = encode_page(page, record);
    return error == CORE_OK
               ? file_write_all(file, (Bytes){record, sizeof(record)}) : error;
}

static ErrorCode read_exact(FileHandle file, u8 *data, usize size)
{
    usize total = 0;
    while (total < size) {
        usize count = 0;
        ErrorCode error =
            file_read(file, (Bytes){data + total, size - total}, &count);
        if (error == CORE_INTERRUPTED) {
            continue;
        }
        if (error != CORE_OK) {
            return error;
        }
        if (count == 0 || count > size - total) {
            return CORE_IO_ERROR;
        }
        total += count;
    }
    return CORE_OK;
}

static ErrorCode read_header(FileHandle file, const Journal *journal, u64 *out_count)
{
    FileInfo info;
    u8 header[JOURNAL_HEADER_SIZE];
    u32 stored_crc;
    u64 count;
    ErrorCode error = file_info(file, &info);
    if (error != CORE_OK) {
        return error;
    }
    if (info.kind != FILE_KIND_REGULAR || info.size < JOURNAL_HEADER_SIZE) {
        return CORE_IO_ERROR;
    }
    error = read_exact(file, header, sizeof(header));
    if (error != CORE_OK) {
        return error;
    }
    for (usize i = 0; i < sizeof(MAGIC); ++i) {
        if (header[i] != MAGIC[i]) {
            return CORE_IO_ERROR;
        }
    }
    stored_crc = get_u32_le(header + HEADER_CRC_OFFSET);
    put_u32_le(header + HEADER_CRC_OFFSET, 0);
    if (stored_crc != crc32(header, sizeof(header))) {
        return CORE_IO_ERROR;
    }
    if (get_u16_le(header + 8) != JOURNAL_MAJOR ||
        get_u16_le(header + 10) != JOURNAL_MINOR ||
        get_u32_le(header + 12) != DEATHBOOBS_PAGE_SIZE) {
        return CORE_UNSUPPORTED;
    }
    if (get_u64_le(header + 16) != journal->device ||
        get_u64_le(header + 24) != journal->inode) {
        return CORE_INVALID_STATE;
    }
    count = get_u64_le(header + 40);
    if (!bytes_are_zero(header + 52, 4) ||
        get_u64_le(header + 32) != journal->page_count ||
        count == 0 || count > journal->page_count || count > JOURNAL_MAX_RECORDS ||
        info.size != JOURNAL_HEADER_SIZE + count * DEATHBOOBS_PAGE_SIZE) {
        return CORE_IO_ERROR;
    }
    *out_count = count;
    return CORE_OK;
}

static ErrorCode visit_records(FileHandle file, u64 count, u64 page_count,
                               DeathboobsStorage *restore_to)
{
    u8 record[DEATHBOOBS_PAGE_SIZE];
    DeathboobsPage page;
    DeathboobsPageId previous = 0;
    for (u64 n = 0; n < count; ++n) {
        ErrorCode error = read_exact(file, record, sizeof(record));
        if (error != CORE_OK) {
            return error;
        }
        DeathboobsPageId id = get_u64_le(record);
        if (id <= previous || id > page_count) {
            return CORE_IO_ERROR;
        }
        error = decode_page(record, id, &page);
        if (error == CORE_OK && restore_to) {
            error = deathboobs_storage_page_write(restore_to, &page);
        }
        if (error != CORE_OK) {
            return error;
        }
        previous = id;
    }
    return CORE_OK;
}

ErrorCode journal_restore(const Journal *journal, DeathboobsStorage *storage,
                          bool *out_found)
{
    FileHandle file = NULL;
    u64 count;
    u64 position;
    ErrorCode error = file_open(&journal->path, FILE_OPEN_MODE_READ, 0, &file);
    if (error == CORE_NOT_FOUND) {
        *out_found = false;
        return CORE_OK;
    }
    if (error != CORE_OK) {
        return error;
    }
    *out_found = true;
    error = read_header(file, journal, &count);
    if (error == CORE_OK) {
        /* Validate the entire journal before restoring even the first page. */
        error = visit_records(file, count, journal->page_count, NULL);
    }
    if (error == CORE_OK) {
        /* A preceding process may have died between rename and directory fsync. */
        error = journal_sync_directory(journal);
    }
    if (error == CORE_OK) {
        error = file_seek(file, (i64)JOURNAL_HEADER_SIZE, FILE_SEEK_ORIGIN_START,
                          &position);
        if (error == CORE_OK && position != JOURNAL_HEADER_SIZE) {
            error = CORE_IO_ERROR;
        }
    }
    if (error == CORE_OK) {
        error = visit_records(file, count, journal->page_count, storage);
    }
    if (error == CORE_OK) {
        error = deathboobs_storage_flush(storage);
    }
    return journal_close(&file, error);
}
