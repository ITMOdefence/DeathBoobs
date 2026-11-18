#include <deathboobs/format.h>
#include <sys/file.h>
#include <sys/memory.h>
#include <sys/string.h>

#include "../fakes/punos_faults.h"
#include "../../src/internal/writer_lock.h"

extern void *malloc(usize size);
extern void free(void *pointer);

#define FAKE_FILE_COUNT ((usize)12)
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
    bool locked;
    u64 inode;
    bool durable_exists;
    char durable_path[FAKE_PATH_CAPACITY];
    usize durable_path_length;
    u8 durable_data[FAKE_FILE_CAPACITY];
    u64 durable_size;
};

static FakeFile files[FAKE_FILE_COUNT];
static FakeFile saved_files[FAKE_FILE_COUNT];
static FakeFile directory;
static u64 next_inode;
static usize allocations;

typedef struct Fault {
    FakeOperation operation;
    const char *path;
    usize remaining;
    ErrorCode error;
    usize partial;
} Fault;

static Fault faults[4];

static bool text_matches(const char *left, const char *right)
{
    if (!left || !right) {
        return left == right;
    }
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

void fake_fail(FakeOperation operation, const char *path, usize nth,
               ErrorCode error, usize partial)
{
    for (usize i = 0; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        if (faults[i].remaining == 0) {
            faults[i] = (Fault){operation, path, nth, error, partial};
            return;
        }
    }
    __builtin_trap();
}

static Fault fault_at(FakeOperation operation, const char *path)
{
    for (usize i = 0; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        Fault *fault = &faults[i];
        if (fault->remaining && fault->operation == operation &&
            text_matches(fault->path, path) && --fault->remaining == 0) {
            return *fault;
        }
    }
    return (Fault){0};
}

bool fake_no_resources(void)
{
    if (allocations || directory.handle.open) {
        return false;
    }
    for (usize i = 0; i < FAKE_FILE_COUNT; ++i) {
        if (files[i].handle.open || files[i].locked) {
            return false;
        }
    }
    return true;
}

void fake_capture_disk(bool durable)
{
    for (usize i = 0; i < FAKE_FILE_COUNT; ++i) {
        FakeFile *source = &files[i];
        FakeFile *saved = &saved_files[i];
        *saved = *source;
        saved->handle = (struct FileHandle){0};
        saved->locked = false;
        if (durable) {
            saved->used = source->durable_exists;
            saved->size = source->durable_size;
            saved->path_length = source->durable_path_length;
            for (usize n = 0; n < FAKE_PATH_CAPACITY; ++n) {
                saved->path[n] = source->durable_path[n];
            }
            for (usize n = 0; n < saved->size; ++n) {
                saved->data[n] = source->durable_data[n];
            }
        }
    }
}

void fake_restore_disk(void)
{
    if (!fake_no_resources()) {
        __builtin_trap();
    }
    for (usize i = 0; i < FAKE_FILE_COUNT; ++i) {
        files[i] = saved_files[i];
    }
}


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
        if (!files[i].used && !files[i].durable_exists) {
            files[i] = (FakeFile){0};
            files[i].used = true;
            files[i].inode = ++next_inode;
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
    ++allocations;
    *out_block = (MemoryBlock){data, size};
    return CORE_OK;
}

ErrorCode memory_free(MemoryBlock *block)
{
    if (!block || (!block->data && block->size != 0)) return CORE_INVALID_ARGUMENT;
    if (block->data) {
        Fault fault = fault_at(FAKE_FREE, NULL);
        if (fault.error) return fault.error;
        --allocations;
        free(block->data);
    }
    *block = (MemoryBlock){0};
    return CORE_OK;
}

ErrorCode memory_reallocate(MemoryBlock *block, usize new_size)
{
    MemoryBlock replacement = {0};
    usize copy_size;
    usize i;
    ErrorCode error;
    if (!block || (!block->data && block->size != 0)) return CORE_INVALID_ARGUMENT;
    if (new_size == block->size) return CORE_OK;
    if (new_size == 0) return memory_free(block);
    error = memory_allocate(new_size, &replacement);
    if (error != CORE_OK) return error;
    copy_size = block->size < new_size ? block->size : new_size;
    for (i = 0; i < copy_size; ++i) replacement.data[i] = block->data[i];
    error = memory_free(block);
    if (error != CORE_OK) {
        (void)memory_free(&replacement);
        return error;
    }
    *block = replacement;
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
    ++allocations;
    *out_string = (String){copy, length};
    return CORE_OK;
}

ErrorCode string_destroy(String *string)
{
    if (!string) return CORE_INVALID_ARGUMENT;
    if (string->data) {
        --allocations;
        free(string->data);
    }
    *string = (String){0};
    return CORE_OK;
}

ErrorCode string_copy(const String *source, String *out)
{
    if (!source || !source->data) return CORE_INVALID_ARGUMENT;
    return string_from_c_str(source->data, out);
}

void fake_truncate(const char *path, usize size)
{
    usize length = 0;
    while (path[length]) ++length;
    FakeFile *file = find_file(&(String){(char *)path, length});
    if (!file || size > file->size) __builtin_trap();
    file->size = size;
}

ErrorCode deathboobs_writer_lock(const String *path, DeathboobsWriterLock *out)
{
    FakeFile *file = find_file(path);
    if (!file) file = create_file(path);
    if (!file) return CORE_IO_ERROR;
    if (file->locked) return CORE_WOULD_BLOCK;
    file->locked = true;
    *out = (DeathboobsWriterLock){(i32)(file - files), 1, file->inode};
    return CORE_OK;
}

ErrorCode deathboobs_writer_unlock(DeathboobsWriterLock *lock)
{
    files[lock->fd].locked = false;
    lock->fd = -1;
    return CORE_OK;
}

ErrorCode deathboobs_writer_resize(DeathboobsWriterLock *lock, u64 byte_size)
{
    FakeFile *file;
    if (!lock || lock->fd < 0 || (usize)lock->fd >= FAKE_FILE_COUNT ||
        byte_size > FAKE_FILE_CAPACITY) {
        return CORE_INVALID_ARGUMENT;
    }
    file = &files[lock->fd];
    if (!file->locked) {
        return CORE_INVALID_STATE;
    }
    if (byte_size > file->size) {
        for (u64 i = file->size; i < byte_size; ++i) {
            file->data[i] = 0;
        }
    }
    file->size = byte_size;
    if (file->handle.position > byte_size) {
        file->handle.position = byte_size;
    }
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
    Fault fault = fault_at(FAKE_OPEN, path->data);
    if (fault.error) {
        if (fault.partial && mode == FILE_OPEN_MODE_READ_WRITE_CREATE &&
            !find_file(path)) {
            (void)create_file(path);
        }
        return fault.error;
    }
    file = find_file(path);
    if (!file && mode == FILE_OPEN_MODE_READ &&
        (text_matches(path->data, ".") || text_matches(path->data, "/tmp/"))) {
        file = &directory;
        for (usize i = 0; i <= path->length; ++i) {
            directory.path[i] = path->data[i];
        }
    }
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
    Fault fault = fault_at(FAKE_CLOSE, (*file)->owner->path);
    (*file)->open = false;
    *file = NULL;
    return fault.error;
}

ErrorCode file_read(FileHandle file, Bytes buffer, usize *out_read)
{
    usize available;
    usize count;
    usize i;
    if (!file || !file->open || !out_read || (!buffer.data && buffer.size != 0)) {
        return CORE_INVALID_ARGUMENT;
    }
    Fault fault = fault_at(FAKE_READ, file->owner->path);
    if (fault.error) return fault.error;
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
    Fault fault = fault_at(FAKE_WRITE, file->owner->path);
    if (fault.error && fault.partial < data.size) data.size = fault.partial;
    if (file->position > FAKE_FILE_CAPACITY ||
        data.size > FAKE_FILE_CAPACITY - file->position) return CORE_IO_ERROR;
    for (i = 0; i < data.size; ++i) file->owner->data[file->position + i] = data.data[i];
    file->position += data.size;
    if (file->position > file->owner->size) file->owner->size = file->position;
    return fault.error;
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
    *out_info = (FileInfo){file->owner->size,
                           file->owner == &directory ? FILE_KIND_DIRECTORY : FILE_KIND_REGULAR};
    return CORE_OK;
}

ErrorCode file_flush(FileHandle file)
{
    if (!file || !file->open) return CORE_INVALID_ARGUMENT;
    FakeFile *owner = file->owner;
    Fault fault = fault_at(FAKE_FLUSH, owner->path);
    if (fault.error) return fault.error;
    if (owner == &directory) {
        for (usize i = 0; i < FAKE_FILE_COUNT; ++i) {
            files[i].durable_exists = files[i].used;
            files[i].durable_path_length = files[i].path_length;
            for (usize n = 0; n < FAKE_PATH_CAPACITY; ++n) {
                files[i].durable_path[n] = files[i].path[n];
            }
        }
    } else {
        owner->durable_size = owner->size;
        for (usize n = 0; n < owner->size; ++n) {
            owner->durable_data[n] = owner->data[n];
        }
    }
    return CORE_OK;
}

ErrorCode file_remove(const String *path)
{
    FakeFile *file;
    if (!path) return CORE_INVALID_ARGUMENT;
    Fault fault = fault_at(FAKE_REMOVE, path->data);
    if (fault.error) return fault.error;
    file = find_file(path);
    if (!file) return CORE_NOT_FOUND;
    if (file->handle.open) return CORE_INVALID_STATE;
    file->used = false;
    return CORE_OK;
}

ErrorCode file_rename(const String *old_path, const String *new_path)
{
    Fault fault = fault_at(FAKE_RENAME, old_path->data);
    if (fault.error) return fault.error;
    FakeFile *file = find_file(old_path);
    if (!file) return CORE_NOT_FOUND;
    if (find_file(new_path)) return CORE_ALREADY_EXISTS;
    if (new_path->length >= FAKE_PATH_CAPACITY) return CORE_OVERFLOW;
    file->path_length = new_path->length;
    for (usize i = 0; i <= new_path->length; ++i) {
        file->path[i] = new_path->data[i];
    }
    return CORE_OK;
}
