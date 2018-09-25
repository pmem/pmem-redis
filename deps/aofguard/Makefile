all: lib/libaofguard.so lib/libaofguard.a lib/libaofguard_inject.so

lib/libaofguard.so: src/common.h src/syscall.h src/aofguard.h src/syscall.c src/aofguard.c
	mkdir -p lib
	gcc -std=gnu99 src/syscall.c src/aofguard.c -o lib/libaofguard.so -fPIC -shared -lpthread -Wall

lib/libaofguard.a: src/common.h src/syscall.h src/aofguard.h src/syscall.c src/aofguard.c
	mkdir -p lib
	gcc -std=gnu99 -c src/syscall.c -o src/syscall.o -Wall
	gcc -std=gnu99 -c src/aofguard.c -o src/aofguard.o -Wall
	ar cr lib/libaofguard.a src/syscall.o src/aofguard.o
	rm -f src/syscall.o
	rm -f src/aofguard.o

lib/libaofguard_inject.so: src/common.h src/syscall.h src/sha1.h src/aofguard.h src/syscall.c src/sha1.c src/aofguard.c src/inject.c
	mkdir -p lib
	gcc -std=gnu99 src/syscall.c src/sha1.c src/aofguard.c src/inject.c -o lib/libaofguard_inject.so -fPIC -shared -lpthread -Wall

clean:
	rm -rf lib
