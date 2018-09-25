#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>

#ifdef NO_ASSERT
#define assert(x) ((void*)0)
#else
#include <assert.h>
#endif

#define DEBUG(msgs...)                                                      \
({                                                                          \
    fprintf(stderr, "[<%s> @ %s: %d]: ", __FUNCTION__, __FILE__, __LINE__); \
    fprintf(stderr, ##msgs);                                                \
    printf("\n");                                                           \
})

#define ERROR(ret, show_errstr, msgs...)                                    \
({                                                                          \
    fprintf(stderr, "[<%s> @ %s: %d]: ", __FUNCTION__, __FILE__, __LINE__); \
    fprintf(stderr, ##msgs);                                                \
    if(show_errstr)                                                         \
        perror(0);                                                          \
    else                                                                    \
        printf("\n");                                                       \
    return (ret);                                                           \
})

#define GET_OPEN_MODE(flags)        \
({                                  \
    int _mode = 0;                  \
    if(flags & O_CREAT)             \
    {                               \
        va_list _args;              \
        va_start(_args, flags);     \
        _mode = va_arg(_args, int);  \
        va_end(_args);              \
    }                               \
    _mode;                          \
})

#endif
