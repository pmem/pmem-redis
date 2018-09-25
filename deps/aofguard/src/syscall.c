#include "common.h"
#include "syscall.h"

#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/syscall.h>

int syscall_faccessat(int dirfd, const char* file, int mode, int flags)
{
    return syscall(SYS_faccessat, dirfd, file, mode, flags);
}

int syscall_rename(const char* oldpath, const char* newpath)
{
    return syscall(SYS_rename, oldpath, newpath);
}

int syscall_renameat(int olddirfd, const char* oldpath, int newdirfd, const char* newpath)
{
    return syscall(SYS_renameat, olddirfd, oldpath, newdirfd, newpath);
}

int syscall_unlink(const char* file)
{
    return syscall(SYS_unlink, file);
}

int syscall_unlinkat(int dirfd, const char* file, int flags)
{
    return syscall(SYS_unlinkat, dirfd, file, flags);
}

int syscall_open(const char* file, int flags, ...)
{
    return syscall(SYS_open, file, flags, GET_OPEN_MODE(flags));
}

int syscall_openat(int dirfd, const char* file, int flags, ...)
{
    return syscall(SYS_openat, dirfd, file, flags, GET_OPEN_MODE(flags));
}

ssize_t syscall_write(int fd, const void* data, size_t len)
{
    return syscall(SYS_write, fd, data, len);
}

int syscall_fsync(int fd)
{
    return syscall(SYS_fsync, fd);
}

int syscall_fdatasync(int fd)
{
    return syscall(SYS_fdatasync, fd);
}

int syscall_fstat(int fd, struct stat* stat)
{
    return syscall(SYS_fstat, fd, stat);
}

int syscall_ftruncate(int fd, size_t size)
{
    return syscall(SYS_ftruncate, fd, size);
}

void* syscall_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    return (void*)syscall(SYS_mmap, addr, length, prot, flags, fd, offset);
}

int syscall_close(int fd)
{
    return syscall(SYS_close, fd);
}
