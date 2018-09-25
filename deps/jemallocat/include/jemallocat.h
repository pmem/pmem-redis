#ifndef JEMALLOCAT_H
#define JEMALLOCAT_H

#include <stdlib.h>

/* jemalloc对象 */
struct jemallocat
{
    char dim[88];   // 对外为黑盒
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
