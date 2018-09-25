MEMKIND_DIR=../memkind

MEMKIND_GCC_FLAGS=-I$(MEMKIND_DIR)/include -L$(MEMKIND_DIR)/.libs -lmemkind

all: lib/libjemallocat.a lib/libjemallocat.so tests/gen_test_case tests/do_test_case

lib/libjemallocat.a: src/common.h src/jemallocat.h src/jemallocat.c
	mkdir -p lib
	gcc -std=gnu99 -c src/*.c -O3
	ar cr lib/libjemallocat.a *.o
	rm -f *.o

lib/libjemallocat.so: src/common.h src/jemallocat.h src/jemallocat.c
	mkdir -p lib
	gcc -std=gnu99 src/*.c -o lib/libjemallocat.so -fPIC -shared -O3

tests/gen_test_case: src/common.h tests/gen_test_case.c
	gcc -std=gnu99 tests/gen_test_case.c -o tests/gen_test_case -O3 -Isrc $(MEMKIND_GCC_FLAGS)

tests/do_test_case: src/common.h include/jemallocat.h tests/do_test_case.c
	gcc -std=gnu99 tests/do_test_case.c -o tests/do_test_case -O3 -Isrc -Llib -ljemallocat $(MEMKIND_GCC_FLAGS)

clean:
	rm -f lib/libjemallocat.a
	rm -f lib/libjemallocat.so
	rm -f tests/gen_test_case
	rm -f tests/do_test_case
	rm -f tests/gen_test_case.pmem
	rm -f tests/do_test_case.pmem
