#include "common.h"
#include "syscall.h"
#include "aofguard.h"

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define BLOCK_SIZE      (1 << 20)

#define FSYNC(fd)       syscall_fdatasync(fd)

#define MAKE_MIXED_META(fsync_len, buf_start)                           \
    (((fsync_len) & (size_t)0xffffffffffff) |                           \
    ((((buf_start) / BLOCK_SIZE) << 48) & (size_t)0xffff000000000000))  \

#define GET_FSYNC_LEN(mixed_meta)   ((mixed_meta) & (size_t)0xffffffffffff)

#define GET_BUF_START(mixed_meta)   ((((mixed_meta) >> 48) & 0xffff) * BLOCK_SIZE)

static void nvm_set_64(void* ptr, long long val)
{
    assert(((size_t)ptr & 7) == 0);
    __builtin_ia32_movnti64(ptr, val);
}

static void nvm_memcpy(void* dst, const void* src, size_t len)
{
    if(!len)
        return;
    void* align = (void*)((size_t)dst & ~(size_t)7);
    assert(align <= dst);
    if(align < dst)
    {
        long long head = *((long long*)align);
        size_t front_sz = dst - align;
        assert(front_sz < 8);
        size_t back_sz = 8 - front_sz;
        if(len <= back_sz)
        {
            for(size_t i = 0; i < len; i++)
                ((char*)&head)[front_sz + i] = ((char*)src)[i];
            __builtin_ia32_movnti64(align, head);
            return;
        }
        else
        {
            for(size_t i = 0; i < back_sz; i++)
                ((char*)&head)[front_sz + i] = ((char*)src)[i];
            __builtin_ia32_movnti64(align, head);
            dst += back_sz;
            src += back_sz;
            len -= back_sz;
        }
    }
    assert(len);
    assert(((size_t)dst & 7) == 0);
    while(len >= 8)
    {
        __builtin_ia32_movnti64(dst, *((long long*)src));
        dst += 8;
        src += 8;
        len -= 8;
    }
    if(len)
    {
        long long tail = *((long long*)dst);
        for(size_t i = 0; i < len; i++)
            ((char*)&tail)[i] = ((char*)src)[i];
        __builtin_ia32_movnti64(dst, tail);
    }
}

static void* afsync_thread(void* arg)
{
    struct aofguard* aofguard = arg;
    while(1)
    {
        if(sem_wait(&(aofguard->afsync.sem_start)) != 0)
            ERROR((void*)0, 1, "sem_wait(&(aofguard->afsync.sem_start)) failed: ");
        if(FSYNC(aofguard->file.fd) != 0)
            ERROR((void*)0, 1, "FSYNC(%d) failed: ", aofguard->file.fd);
        if(sem_post(&(aofguard->afsync.sem_done)) != 0)
            ERROR((void*)0, 1, "sem_post(&(aofguard->afsync.sem_done)) failed: ");
    }
}

int aofguard_init(struct aofguard* aofguard, int fd, int nvm_dir_fd, const char* nvm_file, size_t nvm_size, int reset)
{
    assert(aofguard);
    assert(nvm_file);
    if(fd < 0)
        ERROR(0, 0, "param <fd = %d> is invaild!", fd);
    size_t block_count = nvm_size / BLOCK_SIZE;
    if(block_count < 2)
        ERROR(0, 0, "param <nvm_size = %lu> is too small!", nvm_size);
    if(block_count > 65536)
        ERROR(0, 0, "param <nvm_size = %lu> is too big!", nvm_size);
    size_t nvm_file_size = 2 * sizeof(size_t) + block_count * BLOCK_SIZE;
    int nvm_file_exist = syscall_faccessat(nvm_dir_fd, nvm_file, F_OK, 0) == 0;
    int nvm_fd;
    if(nvm_file_exist)
    {
        if((nvm_fd = syscall_openat(nvm_dir_fd, nvm_file, O_RDWR)) < 0)
            ERROR(0, 1, "cannot open file '%s': ", nvm_file);
        struct stat stat;
        if(syscall_fstat(nvm_fd, &stat) != 0)
            ERROR(0, 1, "fstat(%d, &stat) failed: ", nvm_fd);
        if(stat.st_size != nvm_file_size)
            ERROR(0, 0, "file '%s' is broken!", nvm_file);
    }
    else
    {
        if((nvm_fd = syscall_openat(nvm_dir_fd, nvm_file, O_CREAT | O_RDWR, 0666)) < 0)
            ERROR(0, 1, "cannot create file '%s': ", nvm_file);
        if(syscall_ftruncate(nvm_fd, nvm_file_size) != 0)
            ERROR(0, 1, "ftruncate(%d, %lu) failed: ", nvm_fd, nvm_file_size);
    }
    void* nvm_buf = syscall_mmap(0, nvm_file_size, PROT_READ | PROT_WRITE, MAP_SHARED, nvm_fd, 0);
    if(nvm_buf == MAP_FAILED)
        ERROR(0, 1, "mmap(0, %lu, PROT_READ | PROT_WRITE, MAP_SHARED, %d, 0) failed: ", nvm_file_size, nvm_fd);
    syscall_close(nvm_fd);
    struct stat stat;
    if(syscall_fstat(fd, &stat) != 0)
        ERROR(0, 1, "fstat(%d, &stat) failed: ", fd);
    aofguard->file.fd = fd;
    aofguard->buffer.data = (char*)nvm_buf + 2 * sizeof(size_t);
    aofguard->buffer.capacity = block_count * BLOCK_SIZE;
    aofguard->meta.fsync_len_and_start_block = (size_t*)nvm_buf;
    aofguard->meta.buf_end = (size_t*)nvm_buf + 1;
    if(nvm_file_exist && !reset)
    {
        size_t mixed_meta = *(aofguard->meta.fsync_len_and_start_block);
        aofguard->file.fsync_len = GET_FSYNC_LEN(mixed_meta);
        aofguard->buffer.start = GET_BUF_START(mixed_meta);
        if(aofguard->buffer.start >= aofguard->buffer.capacity)
            ERROR(0, 0, "file '%s' is broken!", nvm_file);
        size_t buf_end = (*aofguard->meta.buf_end);
        if(buf_end >= aofguard->buffer.capacity)
            ERROR(0, 0, "file '%s' is broken!", nvm_file);
        if(buf_end >= aofguard->buffer.start)
            aofguard->buffer.len = buf_end - aofguard->buffer.start;
        else
            aofguard->buffer.len = aofguard->buffer.capacity + buf_end - aofguard->buffer.start;
        if(stat.st_size < aofguard->file.fsync_len)
            ERROR(0, 0, "file fd = %d is broken!", fd);
        if(stat.st_size > aofguard->file.fsync_len && syscall_ftruncate(fd, aofguard->file.fsync_len) != 0)
            ERROR(0, 1, "ftruncate(%d, %lu) failed: ", fd, aofguard->file.fsync_len);
        size_t buf_start = aofguard->buffer.start, buf_len = aofguard->buffer.len;
        if(buf_start + buf_len <= aofguard->buffer.capacity)
        {
            if(syscall_write(fd, aofguard->buffer.data + buf_start, buf_len) != buf_len)
                ERROR(0, 1, "write(%d, aofguard->buffer.data + %lu, %lu) failed: ", fd, buf_start, buf_len);
        }
        else
        {
            size_t front_sz = aofguard->buffer.capacity - buf_start;
            if(syscall_write(fd, aofguard->buffer.data + buf_start, front_sz) != front_sz)
                ERROR(0, 1, "write(%d, aofguard->buffer.data + %lu, %lu) failed: ", fd, buf_start, front_sz);
            size_t back_sz = buf_len - front_sz;
            if(syscall_write(fd, aofguard->buffer.data, back_sz) != back_sz)
                ERROR(0, 1, "write(%d, aofguard->buffer.data, %lu) failed: ", fd, back_sz);
        }
    }
    else
    {
        aofguard->file.fsync_len = stat.st_size;
        aofguard->buffer.start = 0;
        aofguard->buffer.len = 0;
        nvm_set_64(aofguard->meta.fsync_len_and_start_block, aofguard->file.fsync_len);
        nvm_set_64(aofguard->meta.buf_end, 0);
    }
    aofguard->afsync.block_count = 0;
    if(sem_init(&(aofguard->afsync.sem_start), 0, 0) != 0)
        ERROR(0, 1, "sem_init(&(aofguard->afsync.sem_start), 0, 0) failed: ");
    if(sem_init(&(aofguard->afsync.sem_done), 0, 0) != 0)
        ERROR(0, 1, "sem_init(&(aofguard->afsync.sem_done), 0, 0) failed: ");
    if(pthread_create(&(aofguard->afsync.thread), 0, afsync_thread, aofguard) != 0)
        ERROR(0, 1, "pthread_create(&(aofguard->afsync.thread), 0, afsync_thread, aofguard) failed!");
    return 1;
}

#define RING_BUF_FORWARD(val, addition, capacity)       \
({                                                      \
    (val) += (addition);                                \
    if((val) >= (capacity))                             \
        (val) -= (capacity);                            \
    assert((val) < (capacity));                         \
})

#define RING_BUF_END(start, len, capacity)              \
({                                                      \
    typeof(start) _end = (start);                       \
    RING_BUF_FORWARD(_end, len, capacity);              \
    _end;                                               \
})

static void update_after_fsync(struct aofguard* aofguard, size_t block_count)
{
    if(block_count == 0)
        return;
    size_t fsync_size = block_count * BLOCK_SIZE;
    RING_BUF_FORWARD(aofguard->buffer.start, fsync_size, aofguard->buffer.capacity);
    assert(aofguard->buffer.start % BLOCK_SIZE == 0);
    assert(fsync_size <= aofguard->buffer.len);
    aofguard->buffer.len -= fsync_size;
    aofguard->file.fsync_len += fsync_size;
    size_t mixed_meta = MAKE_MIXED_META(aofguard->file.fsync_len, aofguard->buffer.start);
    nvm_set_64(aofguard->meta.fsync_len_and_start_block, mixed_meta);
}

static int handle_afsync_if_finish(struct aofguard* aofguard)
{
    if(aofguard->afsync.block_count == 0)
        return 1;
    int ret = sem_trywait(&(aofguard->afsync.sem_done));
    if(ret == 0)
    {
        update_after_fsync(aofguard, aofguard->afsync.block_count);
        aofguard->afsync.block_count = 0;
        return 1;
    }
    else
    {
        if(errno == EAGAIN)
            return 1;
        ERROR(0, 1, "sem_trywait(&(aofguard->afsync.sem_done)) failed: ");
    }
}

static int do_fsync(struct aofguard* aofguard)
{
    if(FSYNC(aofguard->file.fd) != 0)
        ERROR(0, 1, "FSYNC(%d) failed: ", aofguard->file.fd);
    update_after_fsync(aofguard, aofguard->buffer.len / BLOCK_SIZE);
    return 1;
}

static int wait_afsync_done(struct aofguard* aofguard)
{
    if(aofguard->afsync.block_count == 0)
        return 1;
    if(sem_wait(&((aofguard)->afsync.sem_done)) != 0)
        ERROR(0, 1, "sem_wait(&((aofguard)->afsync.sem_done)) failed: ");
    update_after_fsync(aofguard, aofguard->afsync.block_count);
    aofguard->afsync.block_count = 0;
    return 1;
}

int aofguard_write(struct aofguard* aofguard, const void* data, size_t len)
{
    assert(aofguard);
    assert(data);
    if(!handle_afsync_if_finish(aofguard))
        ERROR(0, 0, "handle_afsync_if_finish(aofguard) failed!");
    if(aofguard->buffer.len + len > aofguard->buffer.capacity)
    {
        if((aofguard->buffer.len % BLOCK_SIZE) + len > aofguard->buffer.capacity)
            ERROR(0, 1, "param <len = %lu> is too big to write atomicly!", len);
        if(!wait_afsync_done(aofguard))
            ERROR(0, 0, "wait_afsyc_done(aofguard) failed!");
        if(aofguard->buffer.len + len > aofguard->buffer.capacity && !do_fsync(aofguard))
            ERROR(0, 0, "do_fsync(aofguard) failed!");
    }
    assert(aofguard->buffer.len + len <= aofguard->buffer.capacity);
    if(syscall_write(aofguard->file.fd, data, len) != len)
        ERROR(0, 1, "write(%d, data, %lu) failed: ", aofguard->file.fd, len);
    size_t write_pos = RING_BUF_END(aofguard->buffer.start, aofguard->buffer.len, aofguard->buffer.capacity);
    if(write_pos + len <= aofguard->buffer.capacity)
        nvm_memcpy(aofguard->buffer.data + write_pos, data, len);
    else
    {
        size_t front_sz = aofguard->buffer.capacity - write_pos;
        nvm_memcpy(aofguard->buffer.data + write_pos, data, front_sz);
        size_t back_sz = len - front_sz;
        nvm_memcpy(aofguard->buffer.data, data + front_sz, back_sz);
    }
    aofguard->buffer.len += len;
    assert(aofguard->buffer.len <= aofguard->buffer.capacity);
    size_t buf_end = RING_BUF_END(aofguard->buffer.start, aofguard->buffer.len, aofguard->buffer.capacity);
    nvm_set_64(aofguard->meta.buf_end, buf_end);
    if(aofguard->buffer.len >= BLOCK_SIZE && aofguard->afsync.block_count == 0)
    {
        aofguard->afsync.block_count = aofguard->buffer.len / BLOCK_SIZE;
        if(sem_post(&(aofguard->afsync.sem_start)) != 0)
            ERROR(0, 1, "sem_post(&(aofguard->afsync.sem_start)) failed: ");
    }
    return 1;
}

int aofguard_deinit(struct aofguard* aofguard)
{
    assert(aofguard);
    if(pthread_cancel(aofguard->afsync.thread) != 0)
        ERROR(0, 1, "pthread_cancel(%lu) failed: ", aofguard->afsync.thread);
    void* ret_val;
    if(pthread_join(aofguard->afsync.thread, &ret_val) != 0)
        ERROR(0, 1, "pthread_join(%lu, &ret_val) failed: ", aofguard->afsync.thread);
    assert(ret_val == PTHREAD_CANCELED);
    void* map_addr = aofguard->meta.fsync_len_and_start_block;
    size_t map_size = aofguard->buffer.capacity + 2 * sizeof(size_t);
    if(munmap(map_addr, map_size) != 0)
        ERROR(0, 1, "munmap(%p, %lu) failed: ", map_addr, map_size);
    return 1;
}
