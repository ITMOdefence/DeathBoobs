#include <deathboobs/format.h>
#include <sys/file.h>
#include <sys/memory.h>
#include <sys/string.h>

extern void *malloc(usize size);
extern void free(void *pointer);

#define FAKE_FILE_COUNT ((usize)4)
#define FAKE_FILE_CAPACITY ((usize)(DEATHBOOBS_PAGE_SIZE * 4))
#define FAKE_PATH_CAPACITY ((usize)128)
#define FAKE_READ_CHUNK ((usize)137)

typedef struct FakeFile FakeFile;

struct FileHandle {
    FakeFile *owner;
    u64 position;
    bool open;
};

struct FakeFile {
    bool used;
    char path[FAKE_PATH_CAPACITY];
    usize path_length;
    u8 data[FAKE_FILE_CAPACITY];
    u64 size;
    struct FileHandle handle;
};

static FakeFile files[FAKE_FILE_COUNT];

static bool same_path(const FakeFile *file, const String *path)
{
    usize i;
    if (!file->used || !path || !path->data || file->path_length != path->length) {
        return false;
    }
    for (i = 0; i < path->length; ++i) {
        if (file->path[i] != path->data[i]) {
            return false;
        }
    }
    return true;
}

static FakeFile *find_file(const String *path)
{
    usize i;
    for (i = 0; i < FAKE_FILE_COUNT; ++i) {
        if (same_path(&files[i], path)) {
            return &files[i];
        }
    }
    return NULL;
}

static FakeFile *create_file(const String *path)
{
    usize i;
    usize j;
    if (!path || !path->data || path->length == 0 || path->length >= FAKE_PATH_CAPACITY) {
        return NULL;
    }
    for (i = 0; i < FAKE_FILE_COUNT; ++i) {
        if (!files[i].used) {
            files[i].used = true;
            files[i].path_length = path->length;
            files[i].size = 0;
            files[i].handle = (struct FileHandle){0};
            for (j = 0; j < path->length; ++j) {
                files[i].path[j] = path->data[j];
            }
            files[i].path[path->length] = 0;
            return &files[i];
        }
    }
    return NULL;
}

ErrorCode memory_allocate(usize size, MemoryBlock *out_block)
{
    u8 *data;
    usize i;
    if (!out_block) return CORE_INVALID_ARGUMENT;
    if (size == 0) {
        *out_block = (MemoryBlock){0};
        return CORE_OK;
    }
    data = (u8 *)malloc(size);
    if (!data) return CORE_OUT_OF_MEMORY;
    for (i = 0; i < size; ++i) data[i] = 0;
    *out_block = (MemoryBlock){data, size};
    return CORE_OK;
}

ErrorCode memory_free(MemoryBlock *block)
{
    if (!block || (!block->data && block->size != 0)) return CORE_INVALID_ARGUMENT;
    if (block->data) free(block->data);
    *block = (MemoryBlock){0};
    return CORE_OK;
}

ErrorCode string_from_c_str(const char *source, String *out_string)
{
    usize length = 0;
    usize i;
    char *copy;
    if (!source || !out_string) return CORE_INVALID_ARGUMENT;
    while (source[length] != 0) ++length;
    copy = (char *)malloc(length + 1);
    if (!copy) return CORE_OUT_OF_MEMORY;
    for (i = 0; i <= length; ++i) copy[i] = source[i];
    *out_string = (String){copy, length};
    return CORE_OK;
}

ErrorCode string_destroy(String *string)
{
    if (!string) return CORE_INVALID_ARGUMENT;
    if (string->data) free(string->data);
    *string = (String){0};
    return CORE_OK;
}

ErrorCode file_open(const String *path, FileOpenMode mode, FilePermissions permissions,
                    FileHandle *out_file)
{
    FakeFile *file;
    if (!path || !out_file || permissions > 0777 ||
        mode < FILE_OPEN_MODE_READ || mode > FILE_OPEN_MODE_APPEND_CREATE) {
        return CORE_INVALID_ARGUMENT;
    }
    file = find_file(path);
    if (!file) {
        if (mode == FILE_OPEN_MODE_READ) return CORE_NOT_FOUND;
        file = create_file(path);
        if (!file) return CORE_OUT_OF_MEMORY;
    }
    if (file->handle.open) return CORE_INVALID_STATE;
    if (mode == FILE_OPEN_MODE_WRITE_TRUNCATE) file->size = 0;
    file->handle.owner = file;
    file->handle.position = mode == FILE_OPEN_MODE_APPEND_CREATE ? file->size : 0;
    file->handle.open = true;
    *out_file = &file->handle;
    return CORE_OK;
}

ErrorCode file_close(FileHandle *file)
{
    if (!file || !*file || !(*file)->open) return CORE_INVALID_ARGUMENT;
    (*file)->open = false;
    *file = NULL;
    return CORE_OK;
}

ErrorCode file_read(FileHandle file, Bytes buffer, usize *out_read)
{
    usize available;
    usize count;
    usize i;
    if (!file || !file->open || !out_read || (!buffer.data && buffer.size != 0)) {
        return CORE_INVALID_ARGUMENT;
    }
    if (file->position >= file->owner->size) {
        *out_read = 0;
        return CORE_OK;
    }
    available = (usize)(file->owner->size - file->position);
    count = buffer.size < available ? buffer.size : available;
    if (count > FAKE_READ_CHUNK) count = FAKE_READ_CHUNK;
    for (i = 0; i < count; ++i) buffer.data[i] = file->owner->data[file->position + i];
    file->position += count;
    *out_read = count;
    return CORE_OK;
}

ErrorCode file_write_all(FileHandle file, Bytes data)
{
    usize i;
    if (!file || !file->open || (!data.data && data.size != 0)) {
        return CORE_INVALID_ARGUMENT;
    }
    if (file->position > FAKE_FILE_CAPACITY ||
        data.size > FAKE_FILE_CAPACITY - file->position) return CORE_IO_ERROR;
    for (i = 0; i < data.size; ++i) file->owner->data[file->position + i] = data.data[i];
    file->position += data.size;
    if (file->position > file->owner->size) file->owner->size = file->position;
    return CORE_OK;
}

ErrorCode file_seek(FileHandle file, i64 offset, FileSeekOrigin origin, u64 *out_position)
{
    i64 base;
    i64 position;
    if (!file || !file->open || !out_position ||
        origin < FILE_SEEK_ORIGIN_START || origin > FILE_SEEK_ORIGIN_END) {
        return CORE_INVALID_ARGUMENT;
    }
    base = origin == FILE_SEEK_ORIGIN_START
               ? 0
               : origin == FILE_SEEK_ORIGIN_CURRENT ? (i64)file->position
                                                    : (i64)file->owner->size;
    position = base + offset;
    if (position < 0 || (u64)position > FAKE_FILE_CAPACITY) return CORE_INVALID_ARGUMENT;
    file->position = (u64)position;
    *out_position = file->position;
    return CORE_OK;
}

ErrorCode file_info(FileHandle file, FileInfo *out_info)
{
    if (!file || !file->open || !out_info) return CORE_INVALID_ARGUMENT;
    *out_info = (FileInfo){file->owner->size, FILE_KIND_REGULAR};
    return CORE_OK;
}

ErrorCode file_flush(FileHandle file)
{
    return file && file->open ? CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode file_remove(const String *path)
{
    FakeFile *file;
    if (!path) return CORE_INVALID_ARGUMENT;
    file = find_file(path);
    if (!file) return CORE_NOT_FOUND;
    if (file->handle.open) return CORE_INVALID_STATE;
    *file = (FakeFile){0};
    return CORE_OK;
}
