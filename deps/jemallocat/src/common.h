#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>

#ifdef NO_ASSERT
#define assert(x) ((void*)0)
#endif

#define ERROR(ret, show_errstr, msgs...)                                        \
    do                                                                          \
    {                                                                           \
        fprintf(stderr, "[<%s> @ %s: %d]: ", __FUNCTION__, __FILE__, __LINE__); \
        fprintf(stderr, ##msgs);                                                \
        if(show_errstr)                                                         \
            perror(0);                                                          \
        else                                                                    \
            printf("\n");                                                       \
        return (ret);                                                           \
    }                                                                           \
    while(0)

#ifndef assert
#include <assert.h>
#endif

#endif