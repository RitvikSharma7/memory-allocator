#ifndef MEMORY_ALLOCATOR_H
#define MEMORY_ALLOCATOR_H

#include <stddef.h>

/**
 * @brief Allocates memory of a given size.
 *
 * This function allocates `requested_size` bytes of memory from the heap and
 * returns a pointer to the beginning of the block. The allocator tracks free
 * and allocated blocks internally using a free list. Allocated memory is aligned
 * to 16 bytes to satisfy alignment requirements for most data types. For requests over
 * a certain limit, `mmap()` is invoked to satisfy user requirements.
 *
 * If allocation fails due to insufficient memory, the function returns NULL.
 * The caller is responsible for freeing the memory using `memfree()` to avoid
 * memory leaks.
 *
 * @param requested_size Number of bytes to allocate.
 * @return void* Pointer to allocated memory, or NULL if allocation fails.
 * @note This allocator is now thread-safe.
 * 
 */
void* memalloc(size_t requested_size);

/**
 * @brief Allocates memory for an array of elements and initializes all bytes to zero.
 *
 * This function behaves like `memalloc(num_elements * element_size)` followed by
 * zeroing all bytes in the allocated memory. It is useful for initializing arrays
 * of structs or primitive types without manually setting each element to zero.
 *
 * If allocation fails, NULL is returned. The caller must free the memory using
 * `memfree()` when done.
 *
 * @param num_elements Number of elements to allocate memory for.
 * @param element_size Size of each element in bytes.
 * @return void* Pointer to zero-initialized memory, or NULL if allocation fails.
 * @note Memory is aligned to 16 bytes. It is now thread-safe.
 */
void* defalloc(size_t num_elements, size_t element_size);

/**
 * @brief Frees memory previously allocated by memalloc, defalloc, or memresize.
 *
 * This function returns the memory pointed to by `ptr` to the allocator's free
 * list and if the memory was mapped using `mmap()`, it is returned to the OS.
 * The memory can then be reused for future allocations. Passing NULL has
 * no effect.
 *
 * @param ptr Pointer to memory to free. Can be NULL (no operation).
 * @return void
 * @note Do not pass a pointer that was not allocated by this allocator.
 */
void memfree(void* ptr);

/**
 * @brief Resizes a previously allocated memory block.
 *
 * Attempts to resize the memory block pointed to by `ptr` to `new_size` bytes.
 * If the new size is smaller, excess memory may be returned to the free list.
 * If the new size is larger, the allocator may move the block and copy existing
 * data. Memory blocks are always aligned to 16 bytes.
 *
 * If `ptr` is NULL, behaves like memalloc(new_size).  
 * If `new_size` is 0, behaves like memfree(ptr) and returns NULL.  
 * If resizing fails, NULL is returned and the original block remains unchanged.
 *
 * @param ptr Pointer to memory to resize. Can be NULL.
 * @param new_size New size in bytes.
 * @return void* Pointer to resized memory block, or NULL if allocation fails.
 * @note It is Thread-safe. May lead to fragmentation under heavy use.
 */
void* memresize(void* ptr, size_t new_size);

#endif // MEMORY_ALLOCATOR_H

