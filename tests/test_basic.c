#include <stdio.h>
#include "memory_allocator.h"

int main() {
    printf("Running basic allocation tests...\n");

    // allocate
    int* a = memalloc(sizeof(int));
    if (!a) return printf("FAIL: memalloc returned NULL\n"), 1;
    *a = 42;

    // realloc grow
    a = memresize(a, sizeof(int) * 10);
    if (!a) return printf("FAIL: memresize grow\n"), 1;

    // realloc shrink
    a = memresize(a, sizeof(int));
    if (!a) return printf("FAIL: memresize shrink\n"), 1;

    // calloc-like
    int* b = defalloc(5, sizeof(int));
    for (int i = 0; i < 5; i++) {
        if (b[i] != 0)
            return printf("FAIL: defalloc not zeroed\n"), 1;
    }

    // free
    memfree(a);
    memfree(b);

    printf("PASS: basic tests passed\n");
    return 0;
    
}
