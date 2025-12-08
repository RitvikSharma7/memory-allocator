# Custom Memory Allocator (C)

A lightweight custom memory allocator written in C, implementing:

* Free-list allocator
* Block splitting
* Coalescing (merge adjacent free blocks)
* `mmap()` for large allocations
* `calloc`-like zero-initialized allocation
* `realloc`-like resizing (in-place growth/shrink + relocation)
* 16-byte alignment
* Custom `memset` and `memdup`

This project was built to deeply understand how `malloc`, `free`, and `realloc` behave internally.

For a more complete explanation of how memory is laid out inside a process and how allocators operate under the hood, you can read my detailed article in the [docs](docs/Introduction_Memory_Internals.md) folder

---

## Features

### Free List Allocation

Uses a doubly-linked free list stored inside block headers.

### Splitting / Coalescing

* Blocks split when extra space remains.
* Adjacent free blocks are merged to reduce fragmentation.

### mmap Allocation

Large allocations (≥ `MMAP_THRESHOLD`) use:

```c
mmap(PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS)
```

### Custom Helpers

* `memoryset()` – optimized memset using 64-bit chunks
* `memdup()` – fast duplicate memory
* `defalloc()` – zero-initialized allocate (like calloc)

---

## API

```c
void* memalloc(size_t size);
void* defalloc(size_t n, size_t elem_size);
void  memfree(void* ptr);
void* memresize(void* ptr, size_t new_size);
These are not public, but may be if required:
void* memdup(const void* src, size_t size);
void* memoryset(void* ptr, int c, size_t size);
```

---

## Build

```bash
make test_[type]
```

Produces:

```
./p[n].out
```

---

## Running Tests

```bash
make tests
```

---

## Project Goals

This project was created to:

* Understand how heap memory works internally
* Practice systems-level C programming
* Explore fragmentation, alignment, and OS memory APIs
* Prepare for systems programming / operating systems courses

---

## Example Usage

```c
#include "memory_allocator.h"

int main() {
    int* arr = memalloc(10 * sizeof(int));
    arr[0] = 123;

    arr = memresize(arr, 50 * sizeof(int));
    memfree(arr);
}
```
---

## Room for Improvement / Future Enhancements 
The allocator is thread-safe. Adding per-thread arenas would allow for more safer usage in multithreaded programs.  
Segregated Free Lists / Binning  
Introducing bins (size-segregated lists) would significantly improve allocation speed and reduce fragmentation.   
There are no guard bytes or canaries to detect writes beyond the payload.  
Making it adaptive or tunable based on workload could improve performance.  
More Sophisticated Coalescing   
Alignment Improvements  
Memory is aligned, but adding support for user-requested or higher alignment (e.g., 32/64-byte for SIMD) would extend capabilities.  
Better Error Handling & Debug Tools  


---

## License

MIT License

