#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "memkind.h"

struct object
{
    void* ptr;
    size_t size;
};

int main(int argc, char** argv)
{
    size_t pmem_size;
    size_t max_item_size;
    size_t loop_count;
    unsigned int obj_percent;

    if(argc != 5 ||
        sscanf(argv[1], "%lu", &pmem_size) != 1 ||
        sscanf(argv[2], "%lu", &max_item_size) != 1||
        sscanf(argv[3], "%lu", &loop_count) != 1 ||
        sscanf(argv[4], "%u", &obj_percent) != 1 ||
        obj_percent >= 100)
    {
        printf("USAGE: %s <pmem_size M> <max_item_size> <loop_count> <obj_percent %%>\n", argv[0]);
        return 1;
    }
    pmem_size <<= 20;

    memkind_t memkind;
    int err = memkind_create_pmem(".", "gen_test_case.pmem", pmem_size, &memkind);
    if(err)
        ERROR(1, 0, "memkind_create_pmem('./gen_test_case.pmem') failed, code = %d", err);
    void* base_addr = memkind_base_addr(memkind);

    struct object* objs = malloc(sizeof(struct object) * loop_count);
    if(!objs)
        ERROR(1, 0, "malloc(sizeof(struct object) * %lu) failed", loop_count);
    size_t obj_count = 0;

    srand(time(0));
    for(size_t i = 0; i < loop_count; i++)
    {
        if(obj_count && rand() % 200 < 100 - obj_percent)
        {
            size_t index = rand() % obj_count;
            memkind_free(memkind, objs[index].ptr);
            objs[index] = objs[--obj_count];
        }
        else
        {
            size_t item_size = 1 + (rand() % max_item_size);
            void* ptr = memkind_malloc(memkind, item_size);
            if(!ptr)
            {
                printf("memkind_malloc(%lu) failed!\n", item_size);
                return 1;
            }
            objs[obj_count].ptr = ptr;
            objs[obj_count++].size = item_size;
        }
    }

    printf("%lu\n", obj_count);
    for(size_t i = 0; i < obj_count; i++)
    {
        struct object* obj = objs + i;
        printf("%lu,%lu\n", obj->ptr - base_addr, obj->size);
    }

    free(objs);

    return 0;
}
