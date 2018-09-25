#ifndef JEMALLOCAT_H
#define JEMALLOCAT_H

#include <stdint.h>

/* 一个页的信息 */
struct page
{
    uint32_t std_size;          /* 该页用于分配的单元大小 */
    uint32_t bias;              /* 该页第一个可分配单元距页首的偏移量 */
    union                       /* bitmap，用于标记哪些单元已分配 */
    {
        uint8_t* bitmap;        /* 如果该页可分配单元超过64个，使用动态分配的字节数组作为bitmap */
        uint64_t bitmap64;      /* 如果该页可分配单元不超过64个，则使用该64位整数作为bitmap */
    };
};

/* jemalloc对象 */
struct jemallocat
{
    struct page* pages;         /* 页数组，描述了jemalloc管理的空间的分配信息 */
    size_t page_count;          /* 页数量 */
    size_t page_size;           /* 页大小 */
    size_t max_small_size;      /* 最大的small size（参加jemalloc的尺度划分），只有small size用bin管理 */
    void* udata;                                            /* 私有数据（通常为用户的分配器） */
    void* (*base_addr)(void* udata);                        /* 分配器中，获取基地址的函数 */
    void* (*malloc)(void* udata, size_t size);              /* 分配器中，分配内存的函数 */
    void (*free)(void* udata, void* ptr);                   /* 分配器中，释放内存的函数 */
    size_t (*standardize_size)(void* udata, size_t size);   /* 分配器中，标准化传给malloc()的size的函数 */
    int (*is_page_allocatable)(void* udata, size_t index);  /* 分配器中，判断页是否可分配的函数 */
    size_t highest_used_page;   /* 使用到的最后一个页 */
};

/*
jemallocat初始化函数
    jemallocat:     jemallocat对象
    total_size:     分配器管理的空间的大小
    page_size:      页的大小
    max_small_size: 最大的small size（参加jemalloc的尺度划分）
    udata:          私有数据
    base_addr:      分配器中，获取基地址的函数
    malloc:         分配器中，分配内存的函数（务必确保je_mallocx()带有MALLOCX_TCACHE_NONE标记）
    free:           分配器中，释放内存的函数
    standardize_size:       分配器中，标准化传给malloc()的size的函数
    is_page_allocatable:    分配器中，判断页是否可分配的函数
成功返回1，失败返回0
*/
int jemallocat_init(struct jemallocat* jemallocat,
    size_t total_size,
    size_t page_size,
    size_t max_small_size,
    void* udata,
    void* (*base_addr)(void* udata),
    void* (*malloc)(void* udata, size_t size),
    void (*free)(void* udata, void* ptr),
    size_t (*standardize_size)(void* udata, size_t size),
    int (*is_page_allocatable)(void* udata, size_t index));

/*
向jemallocat添加一个已被占用的区域
    jemallocat:     jemallocat对象
    offset:         偏移量
    size:           区域大小
成功返回1,失败返回0
*/
int jemallocat_add(struct jemallocat* jemallocat, size_t offset, size_t size);

/*
告知jemallocat，已经添加完所有区域，开始执行malloc_at()操作
    jemallocat:     jemallocat对象
成功返回1,失败返回0
*/
int jemallocat_finish(struct jemallocat* jemallocat);

#endif