#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdlib.h>
#include <sys/stat.h>

int syscall_faccessat(int dirfd, const char* file, int mode, int flags);

int syscall_rename(const char* oldpath, const char* newpath);

int syscall_renameat(int olddirfd, const char* oldpath, int newdirfd, const char* newpath);

int syscall_unlink(const char* file);

int syscall_unlinkat(int dirfd, const char* file, int flags);

int syscall_open(const char* file, int flags, ...);

int syscall_openat(int dirfd, const char* file, int flags, ...);

ssize_t syscall_write(int fd, const void* data, size_t len);

int syscall_fsync(int fd);

int syscall_fdatasync(int fd);

int syscall_fstat(int fd, struct stat* stat);

int syscall_ftruncate(int fd, size_t size);

void* syscall_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset);

int syscall_close(int fd);

#endif
