#include <stdio.h>
#include "common.h"
#include "memkind.h"
#include "jemallocat.h"

#define MEMKIND_PAGE_SIZE           4096
#define MEMKIND_MAX_SMALL_SIZE      14336

size_t memkind_standardize_size(void* udata, size_t size)
{
    assert(size <= (4<<20));
    if(size <= 8)
        return 8;
    for(size_t upper = 128; upper <= (4<<20); upper *= 2)
    {
        if(size <= upper)
        {
            size_t step = upper / 8;
            size_t std_size = (((size - 1) / step) + 1) * step;
            return std_size;
        }
    }
    assert(0);
}

int memkind_is_page_allocatable(void* udata, size_t index)
{
    if(index % 512 < 13)
        return 0;
    return 1;
}

int main(int argc, char** argv)
{
    size_t pmem_size;
    if(argc != 2 || sscanf(argv[1], "%lu", &pmem_size) != 1)
    {
        printf("USAGE: %s <pmem_size M>\n", argv[0]);
        return 1;
    }
    pmem_size <<= 20;

    memkind_t memkind;
    int err = memkind_create_pmem(".", "do_test_case.pmem", pmem_size, &memkind);
    if(err)
        ERROR(1, 0, "memkind_create_pmem('./do_test_case.pmem') failed, code = %d", err);

    struct jemallocat jemallocat;
    if(!jemallocat_init(&jemallocat,
        pmem_size,
        MEMKIND_PAGE_SIZE,
        MEMKIND_MAX_SMALL_SIZE,
        (void*)memkind,
        (void* (*)(void* udata))memkind_base_addr,
        (void* (*)(void* udata, size_t size))memkind_malloc,
        (void (*)(void* udata, void* ptr))memkind_free,
        memkind_standardize_size,
        memkind_is_page_allocatable))
        ERROR(1, 0, "jemallocat_init() failed!");

    size_t obj_count;
    if(scanf("%lu", &obj_count) != 1)
        ERROR(1, 0, "scanf(\"%%lu\", &obj_count) failed");
    for(size_t i = 0; i < obj_count; i++)
    {
        size_t offset, item_size;
        if(scanf("%lu, %lu", &offset, &item_size) != 2)
            ERROR(1, 0, "scanf(\"%%lu, %%lu\", &offset, &item_size) failed");
        if(!jemallocat_add(&jemallocat, offset, item_size))
            ERROR(1, 0, "jemallocat_add(&jemallocat, %lu, %lu) failed", offset, item_size);
    }

    if(!jemallocat_finish(&jemallocat))
        ERROR(1, 0, "jemalloc_finish(&jemallocat) failed");

    return 0;
}