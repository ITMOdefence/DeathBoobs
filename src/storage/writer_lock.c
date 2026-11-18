#include "../internal/writer_lock.h"

#include <punos/syscall/syscall.h>
#include <sys/file.h>

/* Linux x86_64 ABI constants absent from this revision of the PUNos headers. */
#define LINUX_O_NOFOLLOW 0400000
#define LINUX_F_OFD_SETLK 37
#define LINUX_F_WRLCK 1

struct LinuxFileLock {
    i16 type;
    i16 whence;
    i64 start;
    i64 length;
    i32 pid;
};

_Static_assert(sizeof(struct LinuxFileLock) == 32, "Linux flock ABI mismatch");

ErrorCode deathboobs_writer_lock(const String *path, DeathboobsWriterLock *out_lock)
{
    const char *name;
    struct stat info;
    struct LinuxFileLock lock = { .type = LINUX_F_WRLCK, .whence = SEEK_SET };
    ErrorCode error = string_c_str(path, &name);
    i32 fd;

    if (error != CORE_OK) {
        return error;
    }
    fd = open(name, O_RDWR | O_CREAT | O_CLOEXEC | LINUX_O_NOFOLLOW,
              FILE_DEFAULT_PERMISSIONS);
    if (fd < 0) {
        return core_error_from_errno(errno);
    }
    if (fstat(fd, &info) < 0) {
        error = core_error_from_errno(errno);
    } else if ((info.st_mode & S_IFMT) != S_IFREG || info.st_nlink != 1) {
        error = CORE_UNSUPPORTED;
    } else if (fcntl(fd, LINUX_F_OFD_SETLK, (unsigned long)&lock) < 0) {
        i32 cause = errno;
        error = (cause == EAGAIN || cause == EACCES)
                    ? CORE_WOULD_BLOCK : core_error_from_errno(cause);
    }
    if (error != CORE_OK) {
        (void)close(fd);
        return error;
    }
    *out_lock = (DeathboobsWriterLock){fd, info.st_dev, info.st_ino};
    return CORE_OK;
}

ErrorCode deathboobs_writer_unlock(DeathboobsWriterLock *lock)
{
    i32 fd = lock->fd;
    lock->fd = -1;
    return close(fd) == 0 ? CORE_OK : core_error_from_errno(errno);
}
