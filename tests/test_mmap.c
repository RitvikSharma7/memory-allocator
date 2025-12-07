#include <stdio.h>
#include "memory_allocator.h"

#define BIG_SIZE (300000) // > mmap threshold

int main() {
    printf("Testing mmap allocation...\n");


    void* p = memalloc(BIG_SIZE);
    if (!p) return printf("FAIL: mmap allocation failed\n"), 1;

    memoryset(p, 0xAA, BIG_SIZE);

    memfree(p);

    printf("PASS: mmap test OK\n");
    return 0;

}
