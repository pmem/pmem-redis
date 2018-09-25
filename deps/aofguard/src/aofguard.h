#ifndef AOFGUARD_H
#define AOFGUARD_H

#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>

struct aofguard
{
    struct
    {
        int fd;
        size_t fsync_len;
    }
    file;
    struct
    {
        char* data;
        size_t capacity;
        size_t start;
        size_t len;
    }
    buffer;
    struct
    {
        size_t* fsync_len_and_start_block;
        size_t* buf_end;
    }
    meta;
    struct
    {
        size_t block_count;
        pthread_t thread;
        sem_t sem_start, sem_done;
    }
    afsync;
};

int aofguard_init(struct aofguard* aofguard, int fd, int nvm_dir_fd, const char* nvm_file, size_t nvm_size, int reset);

int aofguard_write(struct aofguard* aofguard, const void* data, size_t len);

int aofguard_deinit(struct aofguard* aofguard);

#endif
