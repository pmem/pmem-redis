#include <stdlib.h>
#include "common.h"
#include "jemallocat.h"

#define DIV_CEIL(a, b) (((a) - 1) / (b) + 1)

int jemallocat_init(struct jemallocat* jemallocat,
    size_t total_size,
    size_t page_size,
    size_t max_small_size,
    void* udata,
    void* (*base_addr)(void* udata),
    void* (*malloc)(void* udata, size_t size),
    void (*free)(void* udata, void* ptr),
    size_t (*standardize_size)(void* udata, size_t size),
    int (*is_page_allocatable)(void* udata, size_t index))
{
    assert(jemallocat);
    assert(total_size);
    assert(page_size);
    assert(max_small_size);
    assert(base_addr);
    assert(malloc);
    assert(free);
    assert(standardize_size);
    assert(is_page_allocatable);
    size_t page_count = DIV_CEIL(total_size, page_size);
    struct page* pages = calloc(page_count, sizeof(struct page));
    if(!pages)
        ERROR(0, 1, "calloc(%lu, sizeof(struct page)) failed", page_count);
    jemallocat->pages = pages;
    jemallocat->page_count = page_count;
    jemallocat->page_size = page_size;
    jemallocat->max_small_size = max_small_size;
    jemallocat->udata = udata;
    jemallocat->base_addr = base_addr;
    jemallocat->malloc = malloc;
    jemallocat->free = free;
    jemallocat->standardize_size = standardize_size;
    jemallocat->is_page_allocatable = is_page_allocatable;
    jemallocat->highest_used_page = 0;
    return 1;
}

/*
计算一个页包含多少个单元（即bitmap的长度）
该个数是指，有多少个单元的首地址在该页内（最后一个单元可能跨页）
*/
static size_t get_item_count(struct page* page, size_t page_size)
{
    assert(page);
    assert(page->std_size);
    if(page->bias < page_size)
        return DIV_CEIL(page_size - page->bias, page->std_size);
    else
        return 0;
}

/*
在一个页中,将指定的bit置为1
*/
static int set_page_bitmap(struct page* page, size_t page_size, size_t item_index)
{
    size_t item_count = get_item_count(page, page_size);
    if(item_index >= item_count)
        ERROR(0, 0, "item_index = %lu is out of range", item_index);
    if(item_count <= 64)
    {
        if((page->bitmap64 >> item_index) & 1)
            ERROR(0, 0, "this bit is not 0");
        page->bitmap64 |= (size_t)1 << item_index;
    }
    else
    {
        if(!page->bitmap)
        {
            size_t byte_count = DIV_CEIL(item_count, 8);
            if(!(page->bitmap = calloc(byte_count, 1)))
                ERROR(0, 0, "calloc(%lu, 1) failed", byte_count);
        }
        size_t byte_index = item_index / 8;
        size_t bit_index = item_index % 8;
        if((page->bitmap[byte_index] >> bit_index) & 1)
            ERROR(0, 0, "this bit is not 0");
        page->bitmap[byte_index] |= 1 << bit_index;
    }
    return 1;
}

int jemallocat_add(struct jemallocat* jemallocat, size_t offset, size_t size)
{
    assert(jemallocat);
    /* 得到标准化的size */
    size_t std_size = jemallocat->standardize_size(jemallocat->udata, size);
    if(std_size < size)
        ERROR(0, 0, "jemallocat->standardize_size(udata, %lu) = %lu is wrong", size, std_size);
    if(std_size > jemallocat->max_small_size)
        ERROR(0, 0, "std_size = %lu is greater than max_small_size = %lu",
            std_size, jemallocat->max_small_size);
    /* 该单元所在页号 */
    size_t page_id = offset / jemallocat->page_size;
    if(page_id >= jemallocat->page_count)
        ERROR(0, 0, "offset = %lu is out of range", offset);
    /* 页内偏移 */
    size_t offset_in_page = offset - page_id * jemallocat->page_size;
    /* 页内单元序号 */
    size_t item_index = offset_in_page / std_size;
    /* 该页中第一个单元相对于页首的偏移 */
    size_t bias = offset_in_page - item_index * std_size;
    struct page* page = jemallocat->pages + page_id;
    /* 若该页已被用于分配，检查std_size与bias是否相符，然后设置对应的bit */
    if(page->std_size)
    {
        if(page->std_size != std_size)
            ERROR(0, 0, "this page(page_id = %lu) is for size = %u", page_id, page->std_size);
        if(page->bias != bias)
            ERROR(0, 0, "the bias of this page(page_id = %lu) is %u", page_id, page->bias);
        if(!set_page_bitmap(page, jemallocat->page_size, item_index))
            ERROR(0, 0, "this allocation(offset = %lu, size = %lu) is impossible", offset, size);
    }
    /* 若是空闲页，则初始化该页，设置对应的bit，并且前推、后推以探明该bin */
    else
    {
        page->std_size = std_size;
        page->bias = bias;
        if(!set_page_bitmap(page, jemallocat->page_size, item_index))
            ERROR(0, 0, "this allocation(offset = %lu, size = %lu) is impossible", offset, size);
        /* 以下，前推所有用于分配该std_size的页 */
        struct page* current_page = page;
        /* 当前页的偏置 */
        size_t current_bias = bias;
        /* 只要偏置不为0,就说明当前页不是该bin的第一个页 */
        while(current_bias)
        {
            /* 当前页不可能是全局第一个页 */
            if(current_page == jemallocat->pages + 0)
                ERROR(0, 0, "this allocation(offset = %lu, size = %lu) is impossible", offset, size);
            /* 计算前一个页的偏置 */
            size_t prev_bias = (current_bias + jemallocat->page_size) % std_size;
            struct page* prev_page = current_page - 1;
            /* 既然要前推、后推，那么该bin必然从未被碰触过，因此前一个页一定是空闲页 */
            assert(!prev_page->std_size);
            prev_page->std_size = std_size;
            prev_page->bias = prev_bias;
            current_page = prev_page;
            current_bias = prev_bias;
        }
        /* 以下，后推所有用于分配该std_size的页 */
        current_page = page;
        /* 当前页的余量 */
        size_t current_rest = (jemallocat->page_size - bias) % std_size;
        /* 只要偏置不为0,就说明当前页不是该bin的最后一个页 */
        while(current_rest)
        {
            /* 当前页不可能是全局最后一个页 */
            if(current_page == jemallocat->pages + jemallocat->page_count - 1)
                ERROR(0, 0, "this allocation(offset = %lu, size = %lu) is impossible", offset, size);
            /* 计算下一个页的余量 */
            size_t next_rest = (current_rest + jemallocat->page_size) % std_size;
            /* 计算下一个页的偏置 */
            size_t next_bias = next_rest <= jemallocat->page_size ? 
                (jemallocat->page_size - next_rest) % std_size :
                std_size + jemallocat->page_size - next_rest;
            struct page* next_page = current_page + 1;
            /* 既然要前推、后推，那么该bin必然从未被碰触过，因此后一个页一定是空闲页 */
            assert(!next_page->std_size);
            next_page->std_size = std_size;
            next_page->bias = next_bias;
            current_page = next_page;
            current_rest = next_rest;
        }
    }
    if(page_id > jemallocat->highest_used_page)
        jemallocat->highest_used_page = page_id;
    return 1;
}

/* 获取指定页中指定的bit */
static int get_page_bitmap(struct page* page, size_t item_count, size_t item_index)
{
    assert(item_index < item_count);
    if(item_count <= 64)
        return (page->bitmap64 >> item_index) & 1;
    else
    {
        if(!page->bitmap)
            return 0;
        assert(page->bitmap);
        size_t byte_index = item_index / 8;
        size_t bit_index = item_index % 8;
        return (page->bitmap[byte_index] >> bit_index) & 1;
    }
}

int jemallocat_finish(struct jemallocat* jemallocat)
{
    assert(jemallocat);
    void* base_addr = jemallocat->base_addr(jemallocat->udata);
    if(!base_addr)
        ERROR(0, 0, "jemallocat->base_addr(udata) = 0 is wrong");
    /* 遍历所有页，填满所有可分配对象 */
    for(size_t page_id = 0; page_id <= jemallocat->highest_used_page; page_id++)
    {
        struct page* page = jemallocat->pages + page_id;
        /* 跳过不可分配的页 */
        if(!jemallocat->is_page_allocatable(jemallocat->udata, page_id))
        {
            if(page->std_size)
                ERROR(0, 0, "jemallocat->is_page_allocatable(udata, %lu) "
                    "says page %lu is not allocatable, but this page has been allocated"
                    "for std_size = %u", page_id, page_id, page->std_size);
            /* 使不可分配的页不参与malloc()，见下方代码逻辑 */
            page->bias = jemallocat->page_size;
            continue;
        }
        /* 这里被过滤掉的，有：1、整页都被某个大对象占用；2、上面的不可分配页 */
        if(page->bias >= jemallocat->page_size)
            continue;
        /* 如果是空闲页，那么则用于分配一个大小刚好为一页的对象 */
        if(!page->std_size)
        {
            page->std_size = jemallocat->page_size;
            assert(!page->bias);
        }
        /* 所有单元的全局基础偏移量 */
        size_t item_base_offset = page_id * jemallocat->page_size + page->bias;
        /* 该页可分配的对象个数 */
        size_t item_count = get_item_count(page, jemallocat->page_size);
        for(size_t item_index = 0; item_index < item_count; item_index++)
        {
            /* 依次分配 */
            void* ptr = jemallocat->malloc(jemallocat->udata, page->std_size);
            if(!ptr)
                ERROR(0, 0, "jemallocat->malloc(udata, %u) failed", page->std_size);
            assert(ptr >= base_addr);
            /* 计算偏移量 */
            size_t offset = (char*)ptr - (char*)base_addr;
//printf("malloc %lu, page_id: %lu, std_size: %lu, bias: %lu, item_count: %lu, item_index: %lu\n", offset, page_id, page->std_size, page->bias, item_count, item_index);
            /* 计算“理应的”偏移量 */
            size_t expect_offset = item_base_offset + item_index * page->std_size;
            if(offset != expect_offset)
                ERROR(0, 0, "jemallocat->malloc(udata, %u) is at offset = %lu, instead of %lu", 
                    page->std_size, offset, expect_offset);
        }
    }
    /* 遍历所有页，释放所有不需要的对象 */
    for(size_t page_id = 0; page_id <= jemallocat->highest_used_page; page_id++)
    {
        struct page* page = jemallocat->pages + page_id;
        /* 这里被过滤掉的，有：1、整页都被某个大对象占用；2、上面的不可分配页 */
        if(page->bias >= jemallocat->page_size)
            continue;
        assert(page->std_size);
        /* 所有单元的全局基础偏移量 */
        size_t item_base_offset = page_id * jemallocat->page_size + page->bias;
        /* 该页可分配的对象个数 */
        size_t item_count = get_item_count(page, jemallocat->page_size);
        for(size_t item_index = 0; item_index < item_count; item_index++)
        {
            /* 如果该对象不需要，则释放之 */
            if(!get_page_bitmap(page, item_count, item_index))
            {
                size_t offset = item_base_offset + item_index * page->std_size;
//printf("free %lu, page_id: %lu, std_size: %lu, bias: %lu, item_count: %lu, item_index: %lu\n", offset, page_id, page->std_size, page->bias, item_count, item_index);
                void* ptr = (char*)base_addr + offset;
                jemallocat->free(jemallocat->udata, ptr);
            }
        }
        /* 如果bitmap是动态分配的，释放之 */
        if(item_count > 64)
            free(page->bitmap);
    }
    free(jemallocat->pages);
    return 1;
}
