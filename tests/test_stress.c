#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "memoryallocator.h"

#define N 5000

int main() {
    printf("Stress testing allocator...\n");

    void* ptrs[N];
    srand(1234);

    // random alloc/free
    for (int i = 0; i < N; i++) {
        size_t s = rand() % 2048 + 1;
        ptrs[i] = memalloc(s);

        if (!ptrs[i]) {
            return printf("FAIL: memalloc returned NULL\n"), 1;
        }

        if (rand() % 3 == 0) {
            memfree(ptrs[i]);
            ptrs[i] = NULL;
        }
    }

    // free remaining
    for (int i = 0; i < N; i++) {
        memfree(ptrs[i]);
    }

    printf("PASS: stress test passed\n");
    return 0;

}