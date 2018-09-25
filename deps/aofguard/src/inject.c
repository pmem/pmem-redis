#include "sha1.h"
#include "common.h"
#include "syscall.h"
#include "aofguard.h"

#include <fcntl.h>
#include <regex.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define FD_TABLE_INIT_CAPACITY  16
#define NVM_SIZE_DEFAULT        (64 << 20)

struct filedes
{
    char* name;
    struct aofguard* aofguard;
};

static int debug;
static int disable_sync;
static int nvm_dir_fd;
static size_t nvm_size;
static regex_t fname_regex;
static struct filedes* fd_table;
static size_t fd_table_capacity;

static int init()
{
    const char* dbg = getenv("AOFGUARD_DEBUG");
    debug = dbg && strcmp(dbg, "yes") == 0;
    const char* dis_sync = getenv("AOFGUARD_DISABLE_SYNC");
    disable_sync = dis_sync && strcmp(dis_sync, "yes") == 0;
    const char* nvm_dir = getenv("AOFGUARD_NVM_DIR");
    if(!nvm_dir)
        nvm_dir = ".";
    if((nvm_dir_fd = syscall_open(nvm_dir, O_DIRECTORY)) < 0)
        ERROR(0, 0, "wrong AOFGUARD_NVM_DIR = '%s'!", nvm_dir);
    const char* nvm_size_mb = getenv("AOFGUARD_NVM_SIZE_MB");
    if(!nvm_size_mb)
        nvm_size = NVM_SIZE_DEFAULT;
    else
    {
        if(sscanf(nvm_size_mb, "%lu", &nvm_size) != 1)
            ERROR(0, 0, "wrong AOFGUARD_NVM_SIZE_MB = '%s'!", nvm_size_mb);
        nvm_size <<= 20;
    }
    const char *pattern = getenv("AOFGUARD_FILENAME_PATTERN");
    if(!pattern)
        pattern = ".*";
    if(regcomp(&fname_regex, pattern, REG_EXTENDED | REG_NOSUB) != 0)
        ERROR(0, 0, "wrong AOFGUARD_FILENAME_PATTERN = '%s'!", pattern);
    fd_table_capacity = FD_TABLE_INIT_CAPACITY;
    if(!(fd_table = calloc(fd_table_capacity, sizeof(struct filedes))))
        ERROR(0, 1, "calloc(%lu, sizeof(struct filedes)) failed: ", fd_table_capacity);
    return 1;
}

static void __attribute__((constructor)) constructor()
{
    if(!init())
        exit(1);
}

static void get_nvm_file_name(char* buffer, const char* file)
{
    uint8_t digest[20];
    struct sha1_context sha1;
    sha1_init(&sha1);
    sha1_update(&sha1, (const uint8_t*)file, strlen(file));
    sha1_final(&sha1, digest);
    for(int i = 0; i < 20; i++)
        sprintf(buffer + i * 2, "%02x", digest[i]);
    strcpy(buffer + 40, ".ag");
}

static int handle_open(const char* file, int fd)
{
    if(fd < 0)
        return 1;
    struct stat stat;
    if(syscall_fstat(fd, &stat) != 0)
        ERROR(0, 1, "fstat(%d, &stat) failed: ", fd);
    if(!S_ISREG(stat.st_mode))
        return 1;
    struct aofguard* aofguard = 0;
    if(regexec(&fname_regex, file, 0, 0, 0) == 0)
    {
        if(!(aofguard = malloc(sizeof(struct aofguard))))
            ERROR(0, 1, "malloc(sizeof(struct aofguard)) failed: ");
        char nvm_file[PATH_MAX];
        get_nvm_file_name(nvm_file, file);
        if(!aofguard_init(aofguard, fd, nvm_dir_fd, nvm_file, nvm_size, 0))
            ERROR(0, 0, "aofguard_init(aofguard, %d, %d, '%s', %lu, 0) failed!", fd, nvm_dir_fd, nvm_file, nvm_size);
    }
    if(fd >= fd_table_capacity)
    {
        size_t new_capacity = fd_table_capacity;
        while(new_capacity <= fd)
            new_capacity *= 2;
        if(!(fd_table = realloc(fd_table, new_capacity * sizeof(struct filedes))))
            ERROR(0, 1, "realloc(%p, %lu * sizeof(struct filedes)) failed: ", fd_table, new_capacity);
        memset(fd_table + fd_table_capacity, 0, (new_capacity - fd_table_capacity) * sizeof(struct filedes));
        fd_table_capacity = new_capacity;
    }
    assert(fd < fd_table_capacity);
    struct filedes* filedes = fd_table + fd;
    if(filedes->name)
        free(filedes->name);
    if(!(filedes->name = strdup(file)))
        ERROR(0, 1, "strdup('%s') failed: ", file);
    if(filedes->aofguard)
    {
        if(!aofguard_deinit(filedes->aofguard))
            ERROR(0, 0, "aofguard_deinit(filedes->aofguard) failed!");
        free(filedes->aofguard);
    }
    filedes->aofguard = aofguard;
    return 1;
}

int open(const char* file, int flags, ...)
{
    if(debug)
        DEBUG("open('%s')", file);
    int fd = syscall_open(file, flags, GET_OPEN_MODE(flags));
    if(handle_open(file, fd))
        return fd;
    if(fd >= 0)
        syscall_close(fd);
    ERROR(-1, 0, "handle_open('%s', %d) failed!", file, fd);
}

int open64(const char* file, int flags, ...)
{
    if(debug)
        DEBUG("open64('%s')", file);
    int fd = syscall_open(file, flags, GET_OPEN_MODE(flags));
    if(handle_open(file, fd))
        return fd;
    if(fd >= 0)
        syscall_close(fd);
    ERROR(-1, 0, "handle_open('%s', %d) failed!", file, fd);
}

int openat(int dirfd, const char* file, int flags, ...)
{
    if(debug)
        DEBUG("openat(%d, '%s')", dirfd, file);
    int fd = syscall_openat(dirfd, file, flags, GET_OPEN_MODE(flags));
    if(handle_open(file, fd))
        return fd;
    if(fd >= 0)
        syscall_close(fd);
    ERROR(-1, 0, "handle_open('%s', %d) failed!", file, fd);
}

int openat64(int dirfd, const char* file, int flags, ...)
{
    if(debug)
        DEBUG("openat64(%d, '%s')", dirfd, file);
    int fd = syscall_openat(dirfd, file, flags, GET_OPEN_MODE(flags));
    if(handle_open(file, fd))
        return fd;
    if(fd >= 0)
        syscall_close(fd);
    ERROR(-1, 0, "handle_open('%s', %d) failed!", file, fd);
}

ssize_t write(int fd, const void* data, size_t len)
{
    if(debug)
        DEBUG("write(%d, data, %lu)", fd, len);
    if(!data || len == 0)
        return 0;
    if(0 <= fd && fd < fd_table_capacity)
    {
        struct aofguard* aofguard = fd_table[fd].aofguard;
        if(aofguard)
        {
            if(aofguard_write(aofguard, data, len))
                return len;
            else
                ERROR(-1, 0, "aofguard_write(aofguard, data, %lu) failed!", len);
        }
    }
    return syscall_write(fd, data, len);
}

int fsync(int fd)
{
    if(debug)
        DEBUG("fsync(%d)", fd);
    if(disable_sync)
    {
        if(0 <= fd && fd < fd_table_capacity)
        {
            struct aofguard* aofguard = fd_table[fd].aofguard;
            if(aofguard)
                return 0;
        }
    }
    return syscall_fsync(fd);
}

int fdatasync(int fd)
{
    if(debug)
        DEBUG("fdatasync(%d)", fd);
    if(disable_sync)
    {
        if(0 <= fd && fd < fd_table_capacity)
        {
            struct aofguard* aofguard = fd_table[fd].aofguard;
            if(aofguard)
                return 0;
        }
    }
    return syscall_fdatasync(fd);
}

int close(int fd)
{
    if(debug)
        DEBUG("close(%d)", fd);
    if(0 <= fd && fd < fd_table_capacity)
    {
        struct filedes* filedes = fd_table + fd;
        if(filedes->name)
        {
            free(filedes->name);
            filedes->name = 0;
        }
        if(filedes->aofguard)
        {
            if(!aofguard_deinit(filedes->aofguard))
                ERROR(-1, 0, "aofguard_deinit(filedes->aofguard) failed!");
            free(filedes->aofguard);
            filedes->aofguard = 0;
        }
    }
    return syscall_close(fd);
}

int unlink(const char* file)
{
    if(debug)
        DEBUG("unlink('%s')", file);
    if(syscall_unlink(file) == 0)
    {
        if(regexec(&fname_regex, file, 0, 0, 0) == 0)
        {
            char nvm_file[PATH_MAX];
            get_nvm_file_name(nvm_file, file);
            if(syscall_faccessat(nvm_dir_fd, nvm_file, F_OK, 0) == 0)
            {
                if(syscall_unlinkat(nvm_dir_fd, nvm_file, 0) != 0)
                    ERROR(-1, 1, "unlinkat(%d, '%s') failed: ", nvm_dir_fd, nvm_file);
            }
        }
        return 0;
    }
    return -1;
}

int rename(const char* oldpath, const char* newpath)
{
    if(debug)
        DEBUG("rename('%s', '%s')", oldpath, newpath);
    if(syscall_rename(oldpath, newpath) == 0)
    {
        int oldpath_need_nvm = regexec(&fname_regex, oldpath, 0, 0, 0) == 0;
        int newpath_need_nvm = regexec(&fname_regex, newpath, 0, 0, 0) == 0;
        char nvm_file_old[PATH_MAX];
        char nvm_file_new[PATH_MAX];
        if(oldpath_need_nvm)
            get_nvm_file_name(nvm_file_old, oldpath);
        if(newpath_need_nvm)
            get_nvm_file_name(nvm_file_new, newpath);
        if(newpath_need_nvm)
        {
            if(oldpath_need_nvm)
            {
                if(syscall_faccessat(nvm_dir_fd, nvm_file_old, F_OK, 0) == 0)
                {
                    if(syscall_renameat(nvm_dir_fd, nvm_file_old, nvm_dir_fd, nvm_file_new) != 0)
                        ERROR(-1, 1, "remameat(%d, '%s', %d, '%s') failed: ", nvm_dir_fd, nvm_file_old, nvm_dir_fd, nvm_file_new);
                }
                else if(syscall_faccessat(nvm_dir_fd, nvm_file_new, F_OK, 0) == 0)
                {
                    if(syscall_unlinkat(nvm_dir_fd, nvm_file_new, 0) != 0)
                        ERROR(-1, 1, "unlinkat(%d, '%s', 0) failed: ", nvm_dir_fd, nvm_file_new);
                }
            }
            else
            {
                for(size_t i = 0; i < fd_table_capacity; i++)
                {
                    struct filedes* filedes = fd_table + i;
                    if(filedes->name && strcmp(filedes->name, oldpath) == 0)
                    {
                        assert(!filedes->aofguard);
                        if(!(filedes->aofguard = malloc(sizeof(struct aofguard))))
                            ERROR(0, 1, "malloc(sizeof(struct aofguard)) failed: ");
                        if(!aofguard_init(filedes->aofguard, i, nvm_dir_fd, nvm_file_new, nvm_size, 1))
                            ERROR(0, 0, "aofguard_init(fildes->aofguard, %lu, %d, '%s', %lu, 1) failed!", i, nvm_dir_fd, nvm_file_new, nvm_size);
                    }
                } 
            }
        }
        else if(oldpath_need_nvm)
        {
            if(syscall_faccessat(nvm_dir_fd, nvm_file_old, F_OK, 0) == 0)
            {
                if(syscall_unlinkat(nvm_dir_fd, nvm_file_old, 0) != 0)
                    ERROR(-1, 1, "unlink(%d, '%s', 0) failed: ", nvm_dir_fd, nvm_file_old);
                for(size_t i = 0; i < fd_table_capacity; i++)
                {
                    struct filedes* filedes = fd_table + i;
                    if(filedes->name && strcmp(filedes->name, oldpath) == 0)
                    {
                        assert(filedes->aofguard);
                        if(!aofguard_deinit(filedes->aofguard))
                            ERROR(-1, 0, "aofguard_deinit(filedes->aofguard) failed!");
                        free(filedes->aofguard);
                        filedes->aofguard = 0;
                    }
                }
            }
        }
        for(size_t i = 0; i < fd_table_capacity; i++)
        {
            struct filedes* filedes = fd_table + i;
            if(filedes->name && strcmp(filedes->name, oldpath) == 0)
            {
                free(filedes->name);
                if(!(filedes->name = strdup(newpath)))
                    ERROR(-1, 1, "strdup('%s') failed: ", newpath);
            }
        }
        return 0;
    }
    return -1;
}
