#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define TRUE 1
#define FALSE 0

static void* free_list = NULL;
static void* heap_start = NULL;
static void* heap_end = NULL;

#define PAGE_SIZE 0x4000
#define MMAP_THRESHOLD 0x20000
#define ALIGNMENT 16
#define ROUND_UP(x,a) (((x) + ((a) - 1)) & ~((a) - 1))

typedef struct block_header {
    uint8_t is_free;
    size_t size;          // payload size
    void* prev_block;
    void* next_block;
} block_header;
_Static_assert(sizeof(block_header) % ALIGNMENT == 0, "header not aligned!");
#define BLOCK_HEADER_SIZE sizeof(block_header)

typedef struct block_footer {
    size_t size;          // payload size
} block_footer;
//_Static_assert(sizeof(block_footer) % ALIGNMENT == 0, "footer not aligned!"); Probably wont compile since size is only 8 bytes
#define BLOCK_FOOTER_SIZE sizeof(block_footer)

#define MIN_SPLIT (BLOCK_HEADER_SIZE + BLOCK_FOOTER_SIZE + ALIGNMENT)

typedef struct mmap_block_header {
    size_t size;
    uint8_t is_mmap;
} mmap_block_header;
_Static_assert(sizeof(mmap_block_header) % ALIGNMENT == 0, "mmap header not aligned!");
#define MMAP_HEADER_SIZE sizeof(mmap_block_header)


void* memalloc(size_t requested_size)
{
    if (requested_size == 0)
        return NULL;

    void* user_ptr = NULL;

    size_t prelim_size = BLOCK_HEADER_SIZE + requested_size + BLOCK_FOOTER_SIZE;
    size_t total_size  = ROUND_UP(prelim_size, ALIGNMENT);

    size_t mmap_prelim = MMAP_HEADER_SIZE + requested_size;
    size_t mmap_total  = ROUND_UP(mmap_prelim, ALIGNMENT);


    if (mmap_total >= MMAP_THRESHOLD) //request mmap memory
    {
        void* mmap_mem = mmap(NULL, mmap_total,
                              PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS,
                              -1, 0);

        if (mmap_mem == MAP_FAILED) {
            perror("mmap");
            return NULL;
        }

        mmap_block_header* h = (mmap_block_header*)mmap_mem;
        h->size = mmap_total - MMAP_HEADER_SIZE;
        h->is_mmap = TRUE;
        return (char*)h + MMAP_HEADER_SIZE;
    }


allocation:

    // Initialize free list if empty
    if (free_list == NULL)
    {
        heap_start = brk(0);
        void* region = sbrk(PAGE_SIZE);
        if (region == (void*)-1) {
            perror("sbrk");
            heap_start = NULL;
            return NULL;
        }
        heap_end = heap_start + PAGE_SIZE;

        block_header* h = (block_header*)region;
        h->size = PAGE_SIZE - (BLOCK_HEADER_SIZE + BLOCK_FOOTER_SIZE);
        h->is_free = TRUE;
        h->prev_block = NULL;
        h->next_block = NULL;

        block_footer* f = (block_footer*)((char*)h + BLOCK_HEADER_SIZE + h->size);
        f->size = h->size;

        free_list = h;
    }

    // payload the user needs
    size_t user_payload = total_size - (BLOCK_HEADER_SIZE + BLOCK_FOOTER_SIZE);

    block_header* curr = (block_header*)free_list;

    while (curr)
    {
        if (curr->is_free && curr->size >= user_payload)
        {
            size_t remaining = curr->size - user_payload;


            if (remaining >= ROUND_UP(MIN_SPLIT, ALIGNMENT))
            {
                size_t old_size = curr->size;

                curr->is_free = FALSE;
                curr->size = user_payload;

                block_footer* alloc_footer =
                    (block_footer*)((char*)curr + BLOCK_HEADER_SIZE + curr->size);
                alloc_footer->size = curr->size;

                // new free block start
                block_header* new_h =
                    (block_header*)((char*)alloc_footer + BLOCK_FOOTER_SIZE);

                size_t new_payload = old_size - user_payload -
                                     (BLOCK_HEADER_SIZE + BLOCK_FOOTER_SIZE);

                new_h->size = new_payload;
                new_h->is_free = TRUE;

                block_footer* new_f =
                    (block_footer*)((char*)new_h + BLOCK_HEADER_SIZE + new_payload);
                new_f->size = new_payload;

                // relink free list
                new_h->prev_block = curr->prev_block;
                new_h->next_block = curr->next_block;

                if (curr->prev_block)
                    ((block_header*)curr->prev_block)->next_block = new_h;
                else
                    free_list = new_h;

                if (curr->next_block)
                    ((block_header*)curr->next_block)->prev_block = new_h;

                curr->prev_block = NULL;
                curr->next_block = NULL;

                return (char*)curr + BLOCK_HEADER_SIZE;
            }

            curr->is_free = FALSE;

            curr->size = user_payload;

            block_footer* f =
                (block_footer*)((char*)curr + BLOCK_HEADER_SIZE + curr->size);
            f->size = curr->size;

            // unlink from free list
            if (curr->prev_block)
                ((block_header*)curr->prev_block)->next_block = curr->next_block;
            else
                free_list = curr->next_block;

            if (curr->next_block)
                ((block_header*)curr->next_block)->prev_block = curr->prev_block;

            curr->prev_block = NULL;
            curr->next_block = NULL;

            return (char*)curr + BLOCK_HEADER_SIZE;
        }

        curr = curr->next_block;
    }

    // No block found (retry and extend heap if possible)
    goto allocation;
}


void* defalloc(size_t num_elements, size_t element_size){
    if (num_elements == 0 || element_size == 0){
        return NULL;
    }
    if (num_elements > SIZE_MAX / element_size){ // overflow (too many elements)
        return NULL;
    }
    size_t total_size = num_elements * element_size;
    void* ptr = memalloc(total_size);
    if (!ptr) {
        return NULL;
    }
    return memoryset(ptr, 0, total_size); // set each byte to 0
}

void memfree(void* ptr) {
    if (!ptr) return;

    // detect mmap block safely
    /* If ptr outside heap range, treat as mmap'ed block.
       If heap_start/heap_end are not initialized, fall back to checking header flag (best-effort). */
    if (heap_start && heap_end) {
        if ((char*)ptr < (char*)heap_start || (char*)ptr >= (char*)heap_end) {
            mmap_block_header* mh = (mmap_block_header*)((char*)ptr - MMAP_HEADER_SIZE);
            if (mh->is_mmap == TRUE) {
                size_t total = mh->size + MMAP_HEADER_SIZE;
                if (munmap((void*)mh, total) == -1) {
                    perror("munmap failed");
                }
                return;
            } else {
                /* Not mmap - treat as heap block (defensive). */
            }
        }
    } else {
        /* heap bounds unknown: try to read header flag anyway */
        mmap_block_header* mh = (mmap_block_header*)((char*)ptr - MMAP_HEADER_SIZE);
        if (mh->is_mmap == TRUE) {
            size_t total = mh->size + MMAP_HEADER_SIZE;
            if (munmap((void*)mh, total) == -1) {
                perror("munmap failed");
            }
            return;
        }
    }

    // handle heap region
    block_header* hdr = (block_header*)((char*)ptr - BLOCK_HEADER_SIZE);

    // mark it free 
    hdr->is_free = TRUE;

    block_footer* left_ftr = (block_footer*)((char*)hdr - BLOCK_FOOTER_SIZE);
    block_header* right_hdr = (block_header*)((char*)hdr + BLOCK_HEADER_SIZE + hdr->size + BLOCK_FOOTER_SIZE);

    // check whether left_ftr and right_hdr are inside heap region 
    bool has_left = (heap_start && heap_end) &&
                    ((char*)left_ftr >= (char*)heap_start) && ((char*)left_ftr < (char*)heap_end);

    bool has_right = (heap_start && heap_end) &&
                     ((char*)right_hdr >= (char*)heap_start) && ((char*)right_hdr < (char*)heap_end);

    block_header* new_hdr = hdr;
    size_t new_payload = hdr->size;

    if (has_left) {
        /* recover left header using left footer's stored payload size */
        size_t left_payload = left_ftr->size;
        block_header* left_hdr = (block_header*)((char*)left_ftr - BLOCK_HEADER_SIZE - left_payload);

        if ((char*)left_hdr >= (char*)heap_start && (char*)left_hdr < (char*)heap_end && left_hdr->is_free) {
            unlink_from_free_list(left_hdr);
            new_payload = left_hdr->size + new_payload + (BLOCK_HEADER_SIZE + BLOCK_FOOTER_SIZE);
            new_hdr = left_hdr;
        }
    }

    if (has_right) {
        if (right_hdr->is_free) {
            unlink_from_free_list(right_hdr);
            new_payload = new_payload + right_hdr->size + (BLOCK_HEADER_SIZE + BLOCK_FOOTER_SIZE);
        }
    }

    new_hdr->size = new_payload;
    new_hdr->is_free = TRUE;

    block_footer* new_ftr = (block_footer*)((char*)new_hdr + BLOCK_HEADER_SIZE + new_payload);
    new_ftr->size = new_payload;
    insert_at_tail_free_list(new_hdr);
}

void* memresize(void* ptr, size_t new_size) {
    // Case 1: requested size is 0 → free block
    if (new_size == 0) {
        if (ptr) {
            memfree(ptr);
        }
        return NULL;
    }

    // Case 2: null pointer → allocate new block
    if (!ptr) {
        void* new_ptr = memalloc(new_size);
        if (!new_ptr) {
            perror("memalloc");
            return NULL;
        }
        return new_ptr;
    }

    // Get block header and compute sizes
    block_header* ptr_hdr = (block_header*)((char*)ptr - BLOCK_HEADER_SIZE);
    size_t old_size = ptr_hdr->size;
    size_t required_size = ROUND_UP(new_size, ALIGNMENT);

    // Case 3: requested size equals current size → return ptr
    if (required_size == old_size) {
        return ptr;
    }

    // Case 4: grow block
    if (required_size > old_size) {
        // Try to merge with right neighbor
        block_header* right_hdr = (block_header*)((char*)ptr_hdr + BLOCK_HEADER_SIZE + old_size + BLOCK_FOOTER_SIZE);
        bool has_right = (heap_start && heap_end) &&
                         ((char*)right_hdr >= (char*)heap_start) &&
                         ((char*)right_hdr < (char*)heap_end);

        if (has_right && right_hdr->is_free) {
            size_t merged_size = old_size + BLOCK_HEADER_SIZE + right_hdr->size + BLOCK_FOOTER_SIZE;

            if (merged_size >= required_size) {
                // Remove right from free list
                if (right_hdr->prev_block) right_hdr->prev_block->next_block = right_hdr->next_block;
                if (right_hdr->next_block) right_hdr->next_block->prev_block = right_hdr->prev_block;
                if (free_list == right_hdr) free_list = right_hdr->next_block;

                // Expand current block
                ptr_hdr->size = merged_size;

                // Update footer
                block_footer* ftr = (block_footer*)((char*)ptr_hdr + BLOCK_HEADER_SIZE + merged_size - BLOCK_FOOTER_SIZE);
                ftr->size = merged_size;

                // Split leftover if any
                size_t leftover = merged_size - required_size;
                if (leftover >= ROUND_UP(MIN_SPLIT, ALIGNMENT)) {
                    ptr_hdr->size = required_size;

                    // New footer for allocated block
                    block_footer* alloc_ftr = (block_footer*)((char*)ptr + required_size - BLOCK_FOOTER_SIZE);
                    alloc_ftr->size = required_size;

                    // New free block
                    block_header* new_free = (block_header*)((char*)ptr_hdr + BLOCK_HEADER_SIZE + required_size);
                    new_free->size = leftover;
                    new_free->is_free = TRUE;
                    new_free->prev_block = new_free->next_block = NULL;

                    block_footer* free_ftr = (block_footer*)((char*)new_free + leftover - BLOCK_FOOTER_SIZE);
                    free_ftr->size = leftover;

                    insert_at_tail_free_list(new_free);
                }

                return ptr; // expanded in-place
            }
        }

        // Scan free list for a suitable block
        block_header* curr = free_list;
        while (curr) {
            if (curr->is_free && curr->size >= required_size) {
                // unlink from free list
                if (curr->prev_block) curr->prev_block->next_block = curr->next_block;
                if (curr->next_block) curr->next_block->prev_block = curr->prev_block;
                if (free_list == curr) free_list = curr->next_block;

                void* new_ptr = (void*)((char*)curr + BLOCK_HEADER_SIZE);
                memcpy(new_ptr, ptr, old_size);
                memfree(ptr);
                return new_ptr;
            }
            curr = curr->next_block;
        }

        // allocate new block
        void* new_ptr = memalloc(required_size);
        if (!new_ptr) return NULL;
        memcpy(new_ptr, ptr, old_size);
        memfree(ptr);
        return new_ptr;
    }

    // Case 5: shrink block
    if (required_size < old_size) {
        size_t leftover = old_size - required_size;

        if (leftover >= ROUND_UP(MIN_SPLIT, ALIGNMENT)) {
            // Shrink current block
            ptr_hdr->size = required_size;

            // New footer for allocated block
            block_footer* alloc_ftr = (block_footer*)((char*)ptr + required_size - BLOCK_FOOTER_SIZE);
            alloc_ftr->size = required_size;

            // Create new free block
            block_header* new_free = (block_header*)((char*)ptr_hdr + BLOCK_HEADER_SIZE + required_size);
            new_free->size = leftover;
            new_free->is_free = TRUE;
            new_free->prev_block = new_free->next_block = NULL;

            block_footer* free_ftr = (block_footer*)((char*)new_free + leftover - BLOCK_FOOTER_SIZE);
            free_ftr->size = leftover;

            insert_at_tail_free_list(new_free);
        }

        return ptr;
    }

    return NULL; // fallback, should not reach here
}

// custom memset function
void* memoryset(void* ptr, int c, size_t n) {
    if (!ptr) return ptr;

    unsigned char byte_value = (unsigned char)c;

    size_t chunks = n / 8;
    uint64_t pattern = 0;
    for (int i = 0; i < 8; i++) {
        pattern <<= 8;
        pattern |= byte_value;
    }

    uint64_t* ptr64 = (uint64_t*)ptr;
    for (size_t i = 0; i < chunks; i++) {
        ptr64[i] = pattern;
    }

    unsigned char* leftover = (unsigned char*)(ptr64 + chunks);
    for (size_t i = 0; i < n % 8; i++) {
        leftover[i] = byte_value;
    }

    return ptr;
}

// custom memdup function (helper)
void* memdup(const void* ptr, size_t size){
    if (ptr == NULL || size == 0)
        return NULL;

    void* dup = memalloc(size);
    if (!dup)
        return NULL;

    const unsigned char* src = ptr;
    unsigned char* dst = dup;

    size_t i = 0;
    for (; i + 8 <= size; i += 8) {
        *(uint64_t*)(dst + i) = *(const uint64_t*)(src + i);
    }

    for (; i < size; i++) {
        dst[i] = src[i];
    }

    return dup;
}

//helper function to detach from free list
static void unlink_from_free_list(block_header* b) {
    if (!b) return;
    if (b->prev_block) {
        ((block_header*)b->prev_block)->next_block = b->next_block;
    } else {
        free_list = b->next_block;
    }
    if (b->next_block) {
        ((block_header*)b->next_block)->prev_block = b->prev_block;
    }
    b->prev_block = NULL;
    b->next_block = NULL;
}

//helper function to put free block at end of free list
static void insert_at_tail_free_list(block_header* h) {
    if (!free_list) {
        h->prev_block = NULL;
        h->next_block = NULL;
        free_list = (void*)h;
        return;
    }
    block_header* curr = (block_header*)free_list;
    while (curr->next_block) {
        curr = (block_header*)curr->next_block;
    }
    curr->next_block = h;
    h->prev_block = curr;
    h->next_block = NULL;
}


