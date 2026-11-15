#include <deathboobs/storage.h>

#include <sys/file.h>
#include <sys/memory.h>

#define FILE_HEADER_SIZE ((u32)32)
#define FILE_HEADER_CRC_OFFSET ((usize)28)
#define PAGE_CRC_OFFSET ((usize)24)
#define PAGE_RESERVED_OFFSET ((usize)28)
#define MAX_FILE_OFFSET ((u64)0x7fffffffffffffffULL)

struct DeathboobsStorage {
    MemoryBlock self;
    FileHandle file;
    u64 page_count;
    u8 scratch[DEATHBOOBS_PAGE_SIZE];
};

static const u8 FILE_MAGIC[8] = {'D', 'E', 'A', 'T', 'H', 'D', 'B', 0};

static void put_u16_le(u8 *out, u16 value)
{
    out[0] = (u8)value;
    out[1] = (u8)(value >> 8);
}

static void put_u32_le(u8 *out, u32 value)
{
    out[0] = (u8)value;
    out[1] = (u8)(value >> 8);
    out[2] = (u8)(value >> 16);
    out[3] = (u8)(value >> 24);
}

static void put_u64_le(u8 *out, u64 value)
{
    usize i;
    for (i = 0; i < 8; ++i) {
        out[i] = (u8)(value >> (i * 8));
    }
}

static u16 get_u16_le(const u8 *in)
{
    return (u16)((u16)in[0] | ((u16)in[1] << 8));
}

static u32 get_u32_le(const u8 *in)
{
    return (u32)in[0] | ((u32)in[1] << 8) | ((u32)in[2] << 16) |
           ((u32)in[3] << 24);
}

static u64 get_u64_le(const u8 *in)
{
    u64 value = 0;
    usize i;
    for (i = 0; i < 8; ++i) {
        value |= (u64)in[i] << (i * 8);
    }
    return value;
}

static bool bytes_are_zero(const u8 *data, usize size)
{
    usize i;
    for (i = 0; i < size; ++i) {
        if (data[i] != 0) {
            return false;
        }
    }
    return true;
}

static u32 crc32(const u8 *data, usize size)
{
    u32 crc = 0xffffffffU;
    usize i;
    u8 bit;
    for (i = 0; i < size; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit) {
            u32 mask = (u32)-(i32)(crc & 1U);
            crc = (crc >> 1) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

static ErrorCode seek_page(FileHandle file, DeathboobsPageId page_id)
{
    u64 offset;
    u64 position;
    ErrorCode error;
    if (page_id > MAX_FILE_OFFSET / DEATHBOOBS_PAGE_SIZE) {
        return CORE_OVERFLOW;
    }
    offset = page_id * DEATHBOOBS_PAGE_SIZE;
    error = file_seek(file, (i64)offset, FILE_SEEK_ORIGIN_START, &position);
    if (error != CORE_OK) {
        return error;
    }
    return position == offset ? CORE_OK : CORE_IO_ERROR;
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

static ErrorCode write_at(FileHandle file, DeathboobsPageId page_id,
                          const u8 *data)
{
    ErrorCode error = seek_page(file, page_id);
    if (error != CORE_OK) {
        return error;
    }
    return file_write_all(file, (Bytes){(u8 *)data, DEATHBOOBS_PAGE_SIZE});
}

static void encode_file_header(u8 *out)
{
    usize i;
    for (i = 0; i < DEATHBOOBS_PAGE_SIZE; ++i) {
        out[i] = 0;
    }
    for (i = 0; i < sizeof(FILE_MAGIC); ++i) {
        out[i] = FILE_MAGIC[i];
    }
    put_u16_le(out + 8, DEATHBOOBS_FORMAT_MAJOR);
    put_u16_le(out + 10, DEATHBOOBS_FORMAT_MINOR);
    put_u32_le(out + 12, (u32)DEATHBOOBS_PAGE_SIZE);
    put_u32_le(out + 16, FILE_HEADER_SIZE);
    put_u64_le(out + 20, 0);
    put_u32_le(out + FILE_HEADER_CRC_OFFSET, 0);
    put_u32_le(out + FILE_HEADER_CRC_OFFSET,
               crc32(out, DEATHBOOBS_PAGE_SIZE));
}

static ErrorCode validate_file_header(u8 *header)
{
    u32 stored_crc;
    u32 actual_crc;
    usize i;
    for (i = 0; i < sizeof(FILE_MAGIC); ++i) {
        if (header[i] != FILE_MAGIC[i]) {
            return CORE_IO_ERROR;
        }
    }
    if (get_u16_le(header + 8) != DEATHBOOBS_FORMAT_MAJOR) {
        return CORE_UNSUPPORTED;
    }
    if (get_u16_le(header + 10) != DEATHBOOBS_FORMAT_MINOR ||
        get_u32_le(header + 12) != DEATHBOOBS_PAGE_SIZE ||
        get_u32_le(header + 16) != FILE_HEADER_SIZE ||
        get_u64_le(header + 20) != 0) {
        return CORE_UNSUPPORTED;
    }
    if (!bytes_are_zero(header + FILE_HEADER_SIZE,
                        DEATHBOOBS_PAGE_SIZE - FILE_HEADER_SIZE)) {
        return CORE_IO_ERROR;
    }
    stored_crc = get_u32_le(header + FILE_HEADER_CRC_OFFSET);
    put_u32_le(header + FILE_HEADER_CRC_OFFSET, 0);
    actual_crc = crc32(header, DEATHBOOBS_PAGE_SIZE);
    put_u32_le(header + FILE_HEADER_CRC_OFFSET, stored_crc);
    return stored_crc == actual_crc ? CORE_OK : CORE_IO_ERROR;
}

static bool page_type_valid(DeathboobsPageType type)
{
    return type >= DEATHBOOBS_PAGE_TYPE_METADATA &&
           type <= DEATHBOOBS_PAGE_TYPE_INDEX_RESERVED;
}

static ErrorCode encode_page(const DeathboobsPage *page, u8 *out)
{
    usize i;
    if (!page || !page_type_valid(page->type) ||
        page->payload_size > DEATHBOOBS_PAGE_PAYLOAD_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    for (i = 0; i < DEATHBOOBS_PAGE_SIZE; ++i) {
        out[i] = 0;
    }
    put_u64_le(out, page->id);
    put_u16_le(out + 8, (u16)page->type);
    put_u16_le(out + 10, page->flags);
    put_u32_le(out + 12, page->payload_size);
    put_u64_le(out + 16, page->generation);
    for (i = 0; i < page->payload_size; ++i) {
        out[DEATHBOOBS_PAGE_HEADER_SIZE + i] = page->payload[i];
    }
    put_u32_le(out + PAGE_CRC_OFFSET, 0);
    put_u32_le(out + PAGE_CRC_OFFSET, crc32(out, DEATHBOOBS_PAGE_SIZE));
    return CORE_OK;
}

static ErrorCode decode_page(u8 *input, DeathboobsPageId expected_id,
                             DeathboobsPage *out)
{
    DeathboobsPage page;
    u16 raw_type;
    u32 stored_crc;
    u32 actual_crc;
    usize i;
    if (!out) {
        return CORE_INVALID_ARGUMENT;
    }
    if (get_u64_le(input) != expected_id) {
        return CORE_IO_ERROR;
    }
    raw_type = get_u16_le(input + 8);
    if (raw_type < DEATHBOOBS_PAGE_TYPE_METADATA ||
        raw_type > DEATHBOOBS_PAGE_TYPE_INDEX_RESERVED) {
        return CORE_IO_ERROR;
    }
    page.id = expected_id;
    page.type = (DeathboobsPageType)raw_type;
    page.flags = get_u16_le(input + 10);
    page.payload_size = get_u32_le(input + 12);
    page.generation = get_u64_le(input + 16);
    if (page.payload_size > DEATHBOOBS_PAGE_PAYLOAD_SIZE ||
        !bytes_are_zero(input + PAGE_RESERVED_OFFSET, 4) ||
        !bytes_are_zero(input + DEATHBOOBS_PAGE_HEADER_SIZE + page.payload_size,
                        DEATHBOOBS_PAGE_PAYLOAD_SIZE - page.payload_size)) {
        return CORE_IO_ERROR;
    }
    stored_crc = get_u32_le(input + PAGE_CRC_OFFSET);
    put_u32_le(input + PAGE_CRC_OFFSET, 0);
    actual_crc = crc32(input, DEATHBOOBS_PAGE_SIZE);
    put_u32_le(input + PAGE_CRC_OFFSET, stored_crc);
    if (stored_crc != actual_crc) {
        return CORE_IO_ERROR;
    }
    for (i = 0; i < DEATHBOOBS_PAGE_PAYLOAD_SIZE; ++i) {
        page.payload[i] = i < page.payload_size
                              ? input[DEATHBOOBS_PAGE_HEADER_SIZE + i]
                              : 0;
    }
    *out = page;
    return CORE_OK;
}

static ErrorCode storage_cleanup(DeathboobsStorage *storage,
                                 ErrorCode primary)
{
    ErrorCode error;
    MemoryBlock block;
    if (!storage) {
        return primary;
    }
    if (storage->file) {
        error = file_close(&storage->file);
        if (primary == CORE_OK && error != CORE_OK) {
            primary = error;
        }
    }
    block = storage->self;
    error = memory_free(&block);
    if (primary == CORE_OK && error != CORE_OK) {
        primary = error;
    }
    return primary;
}

ErrorCode deathboobs_storage_open(const String *path,
                                  DeathboobsStorage **out_storage)
{
    MemoryBlock block = {0};
    DeathboobsStorage *storage;
    FileInfo info;
    ErrorCode error;
    if (!path || !out_storage) {
        return CORE_INVALID_ARGUMENT;
    }
    error = memory_allocate(sizeof(DeathboobsStorage), &block);
    if (error != CORE_OK) {
        return error;
    }
    storage = (DeathboobsStorage *)block.data;
    *storage = (DeathboobsStorage){0};
    storage->self = block;
    error = file_open(path, FILE_OPEN_MODE_READ_WRITE_CREATE,
                      FILE_DEFAULT_PERMISSIONS, &storage->file);
    if (error != CORE_OK) {
        return storage_cleanup(storage, error);
    }
    error = file_info(storage->file, &info);
    if (error != CORE_OK) {
        return storage_cleanup(storage, error);
    }
    if (info.kind != FILE_KIND_REGULAR ||
        info.size % DEATHBOOBS_PAGE_SIZE != 0) {
        return storage_cleanup(storage, CORE_IO_ERROR);
    }
    if (info.size == 0) {
        encode_file_header(storage->scratch);
        error = write_at(storage->file, 0, storage->scratch);
        if (error == CORE_OK) {
            error = file_flush(storage->file);
        }
        if (error != CORE_OK) {
            return storage_cleanup(storage, error);
        }
    } else {
        error = seek_page(storage->file, 0);
        if (error == CORE_OK) {
            error = read_exact(storage->file, storage->scratch,
                               DEATHBOOBS_PAGE_SIZE);
        }
        if (error == CORE_OK) {
            error = validate_file_header(storage->scratch);
        }
        if (error != CORE_OK) {
            return storage_cleanup(storage, error);
        }
    }
    storage->page_count = info.size == 0
                              ? 0
                              : info.size / DEATHBOOBS_PAGE_SIZE - 1;
    *out_storage = storage;
    return CORE_OK;
}

ErrorCode deathboobs_storage_close(DeathboobsStorage **storage)
{
    DeathboobsStorage *value;
    ErrorCode error;
    if (!storage || !*storage) {
        return CORE_INVALID_ARGUMENT;
    }
    value = *storage;
    if (!value->file || value->self.data != (u8 *)value) {
        return CORE_INVALID_STATE;
    }
    error = file_flush(value->file);
    error = storage_cleanup(value, error);
    *storage = NULL;
    return error;
}

ErrorCode deathboobs_storage_page_count(const DeathboobsStorage *storage,
                                        u64 *out_count)
{
    if (!storage || !out_count) {
        return CORE_INVALID_ARGUMENT;
    }
    if (!storage->file || storage->self.data != (u8 *)storage) {
        return CORE_INVALID_STATE;
    }
    *out_count = storage->page_count;
    return CORE_OK;
}

ErrorCode deathboobs_storage_page_read(DeathboobsStorage *storage,
                                       DeathboobsPageId page_id,
                                       DeathboobsPage *out_page)
{
    ErrorCode error;
    if (!storage || !out_page) {
        return CORE_INVALID_ARGUMENT;
    }
    if (!storage->file || storage->self.data != (u8 *)storage) {
        return CORE_INVALID_STATE;
    }
    if (page_id == 0 || page_id > storage->page_count) {
        return CORE_NOT_FOUND;
    }
    error = seek_page(storage->file, page_id);
    if (error == CORE_OK) {
        error = read_exact(storage->file, storage->scratch,
                           DEATHBOOBS_PAGE_SIZE);
    }
    return error == CORE_OK
               ? decode_page(storage->scratch, page_id, out_page)
               : error;
}

ErrorCode deathboobs_storage_page_write(DeathboobsStorage *storage,
                                        const DeathboobsPage *page)
{
    ErrorCode error;
    if (!storage || !page) {
        return CORE_INVALID_ARGUMENT;
    }
    if (!storage->file || storage->self.data != (u8 *)storage) {
        return CORE_INVALID_STATE;
    }
    if (page->id == 0 || page->id > storage->page_count) {
        return CORE_NOT_FOUND;
    }
    error = encode_page(page, storage->scratch);
    return error == CORE_OK
               ? write_at(storage->file, page->id, storage->scratch)
               : error;
}

ErrorCode deathboobs_storage_page_append(DeathboobsStorage *storage,
                                         DeathboobsPage *page,
                                         DeathboobsPageId *out_page_id)
{
    DeathboobsPageId id;
    DeathboobsPageId previous_id;
    ErrorCode error;
    if (!storage || !page || !out_page_id) {
        return CORE_INVALID_ARGUMENT;
    }
    if (!storage->file || storage->self.data != (u8 *)storage) {
        return CORE_INVALID_STATE;
    }
    if (storage->page_count == (u64)0xffffffffffffffffULL) {
        return CORE_OVERFLOW;
    }
    id = storage->page_count + 1;
    previous_id = page->id;
    page->id = id;
    error = encode_page(page, storage->scratch);
    if (error == CORE_OK) {
        error = write_at(storage->file, id, storage->scratch);
    }
    if (error != CORE_OK) {
        page->id = previous_id;
        return error;
    }
    storage->page_count = id;
    *out_page_id = id;
    return CORE_OK;
}

ErrorCode deathboobs_storage_flush(DeathboobsStorage *storage)
{
    if (!storage) {
        return CORE_INVALID_ARGUMENT;
    }
    if (!storage->file || storage->self.data != (u8 *)storage) {
        return CORE_INVALID_STATE;
    }
    return file_flush(storage->file);
}
