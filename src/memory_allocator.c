#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>

#define TRUE 1
#define FALSE 0

static void* free_list = NULL;
static void* heap_start = NULL;
static void* heap_end = NULL;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

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

void* memalloc(size_t requested_size);
void* defalloc(size_t num_elements, size_t element_size);
void memfree(void* ptr);
void* memresize(void* ptr, size_t new_size);
void* memoryset(void* ptr, int c, size_t n);
void* memdup(const void* ptr, size_t size);
static void unlink_from_free_list(block_header* b);
static void insert_at_tail_free_list(block_header* h);



//helper function to detach from free list
static void unlink_from_free_list(block_header* b) 
{

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
static void insert_at_tail_free_list(block_header* h) 
{

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

// custom memset function
void* memoryset(void* ptr, int c, size_t n) 
{

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
void* memdup(const void* ptr, size_t size)
{

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

void* memalloc(size_t requested_size)
{
    if (requested_size == 0)
        return NULL;

    pthread_mutex_lock(&lock);
    void* user_ptr = NULL;

    size_t prelim_size = BLOCK_HEADER_SIZE + requested_size + BLOCK_FOOTER_SIZE;
    size_t total_size  = ROUND_UP(prelim_size, ALIGNMENT);

    size_t mmap_prelim = MMAP_HEADER_SIZE + requested_size;
    size_t mmap_total  = ROUND_UP(mmap_prelim, ALIGNMENT);

    if (mmap_total >= MMAP_THRESHOLD) // request mmap memory
    {
        void* mmap_mem = mmap(NULL, mmap_total,
                              PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS,
                              -1, 0);

        if (mmap_mem == MAP_FAILED) {
            perror("mmap");
            pthread_mutex_unlock(&lock);
            return NULL;
        }

        mmap_block_header* h = (mmap_block_header*)mmap_mem;
        h->size = mmap_total - MMAP_HEADER_SIZE;
        h->is_mmap = TRUE;
        pthread_mutex_unlock(&lock);
        return (char*)h + MMAP_HEADER_SIZE;
    }

    // Initialize heap if first allocation
    if (heap_start == NULL)
        heap_start = (void*)brk(0);

    if (free_list == NULL)
    {
        void* region = sbrk(PAGE_SIZE);
        if (region == (void*)-1) {
            perror("sbrk");
            pthread_mutex_unlock(&lock);
            return NULL;
        }
        heap_end = (char*)heap_start + PAGE_SIZE;

        block_header* h = (block_header*)region;
        h->size = PAGE_SIZE - (BLOCK_HEADER_SIZE + BLOCK_FOOTER_SIZE);
        h->is_free = TRUE;
        h->prev_block = NULL;
        h->next_block = NULL;

        block_footer* f = (block_footer*)((char*)h + BLOCK_HEADER_SIZE + h->size);
        f->size = h->size;

        free_list = h;
    }

retry_allocation:

    size_t user_payload = total_size - (BLOCK_HEADER_SIZE + BLOCK_FOOTER_SIZE);
    block_header* curr = (block_header*)free_list;

    while (curr)
    {
        if (curr->is_free && curr->size >= user_payload)
        {
            // Split if large enough
            size_t remaining = curr->size - user_payload;

            if (remaining >= ROUND_UP(MIN_SPLIT, ALIGNMENT))
            {
                unlink_from_free_list(curr);

                size_t old_size = curr->size;
                curr->is_free = FALSE;
                curr->size = user_payload;

                block_footer* alloc_footer =
                    (block_footer*)((char*)curr + BLOCK_HEADER_SIZE + curr->size);
                alloc_footer->size = curr->size;

                // new free block
                block_header* new_h =
                    (block_header*)((char*)alloc_footer + BLOCK_FOOTER_SIZE);

                size_t new_payload = old_size - user_payload -
                                     (BLOCK_HEADER_SIZE + BLOCK_FOOTER_SIZE);

                new_h->size = new_payload;
                new_h->is_free = TRUE;
                new_h->prev_block = new_h->next_block = NULL;

                block_footer* new_f =
                    (block_footer*)((char*)new_h + BLOCK_HEADER_SIZE + new_payload);
                new_f->size = new_payload;

                insert_at_tail_free_list(new_h);
                pthread_mutex_unlock(&lock);
                return (char*)curr + BLOCK_HEADER_SIZE;
            }

            // No split
            curr->is_free = FALSE;
            curr->size = user_payload;

            block_footer* f =
                (block_footer*)((char*)curr + BLOCK_HEADER_SIZE + curr->size);
            f->size = curr->size;

            unlink_from_free_list(curr);
            pthread_mutex_unlock(&lock);
            return (char*)curr + BLOCK_HEADER_SIZE;
        }

        curr = curr->next_block;
    }

    // No suitable block found - extend heap safely
    void* region = sbrk(PAGE_SIZE);
    if (region == (void*)-1) {
        perror("sbrk failed");
        pthread_mutex_unlock(&lock);
        return NULL;
    }
    heap_end = (char*)heap_end + PAGE_SIZE;

    block_header* h = (block_header*)region;
    h->size = PAGE_SIZE - (BLOCK_HEADER_SIZE + BLOCK_FOOTER_SIZE);
    h->is_free = TRUE;
    h->prev_block = h->next_block = NULL;

    block_footer* f = (block_footer*)((char*)h + BLOCK_HEADER_SIZE + h->size);
    f->size = h->size;

    insert_at_tail_free_list(h);

    goto retry_allocation; // only retry after adding new free block
}

void* defalloc(size_t num_elements, size_t element_size)
{

    if (num_elements == 0 || element_size == 0){
        return NULL;
    }
    if (num_elements > SIZE_MAX / element_size){ // overflow (too many elements)
        return NULL;
    }
    pthread_mutex_lock(&lock);
    size_t total_size = num_elements * element_size;
    void* ptr = memalloc(total_size);
    if (!ptr) {
        pthread_mutex_unlock(&lock);
        return NULL;
    }
    memoryset(ptr, 0, total_size); // set each byte to 0
    pthread_mutex_unlock(&lock);
    return ptr; 

}

void memfree(void* ptr)
{

    if (!ptr) return;
    pthread_mutex_lock(&lock);
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
                pthread_mutex_unlock(&lock);
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
            pthread_mutex_unlock(&lock);
            return;
        }
    }

    // handle heap region
    block_header* hdr = (block_header*)((char*)ptr - BLOCK_HEADER_SIZE);

    // mark it free 
    hdr->is_free = TRUE;

    block_footer* left_ftr = NULL;
    block_header* right_hdr = NULL;

    if (heap_start && (char*)hdr > (char*)heap_start + BLOCK_HEADER_SIZE + BLOCK_FOOTER_SIZE)
        left_ftr = (block_footer*)((char*)hdr - BLOCK_FOOTER_SIZE);

    if (heap_end && (char*)hdr + BLOCK_HEADER_SIZE + hdr->size + BLOCK_FOOTER_SIZE < (char*)heap_end)
        right_hdr = (block_header*)((char*)hdr + BLOCK_HEADER_SIZE + hdr->size + BLOCK_FOOTER_SIZE);

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
    if (!new_hdr->prev_block && !new_hdr->next_block) {
        insert_at_tail_free_list(new_hdr);
    }
    pthread_mutex_unlock(&lock);
}

void* memresize(void* ptr, size_t new_size)
{
    // Case 1: null pointer allocate new block
    if (!ptr) return memalloc(new_size);

    // Case 2: requested size is 0 free block
    if (new_size == 0) {
        memfree(ptr);
        return NULL;
    }
    pthread_mutex_lock(&lock);
    block_header* hdr = (block_header*)((char*)ptr - BLOCK_HEADER_SIZE);
    size_t old_size = hdr->size;
    size_t required_size = ROUND_UP(new_size, ALIGNMENT);

    // Case 3: requested size equals current size  return ptr
    if (required_size == old_size){
        pthread_mutex_unlock(&lock);
        return ptr;
    }

    if (required_size < old_size) {
        size_t leftover = old_size - required_size;

        if (leftover >= ROUND_UP(MIN_SPLIT, ALIGNMENT)) {
            // shrink current block
            hdr->size = required_size;

            // update footer of allocated block
            block_footer* alloc_ftr = (block_footer*)((char*)hdr + BLOCK_HEADER_SIZE + required_size);
            alloc_ftr->size = required_size;

            // create new free block from leftover
            block_header* new_free = (block_header*)((char*)alloc_ftr + BLOCK_FOOTER_SIZE);
            new_free->size = leftover - (BLOCK_HEADER_SIZE + BLOCK_FOOTER_SIZE);
            new_free->is_free = TRUE;
            new_free->prev_block = new_free->next_block = NULL;

            block_footer* free_ftr = (block_footer*)((char*)new_free + BLOCK_HEADER_SIZE + new_free->size);
            free_ftr->size = new_free->size;

            // insert leftover into free list
            insert_at_tail_free_list(new_free);
        }
        pthread_mutex_unlock(&lock);
        return ptr;
    }

    block_header* right_hdr = (block_header*)((char*)hdr + BLOCK_HEADER_SIZE + old_size + BLOCK_FOOTER_SIZE);
    bool has_right = (heap_start && heap_end) &&
                    ((char*)right_hdr >= (char*)heap_start) &&
                    ((char*)right_hdr < (char*)heap_end);

    // attempt in-place expansion by merging with right free block
    if (has_right && right_hdr->is_free) {
        size_t merged_payload = old_size + right_hdr->size + BLOCK_HEADER_SIZE + BLOCK_FOOTER_SIZE;

        if (merged_payload >= required_size) {
            // unlink right block from free list
            unlink_from_free_list(right_hdr);

            // expand current block
            hdr->size = merged_payload;

            block_footer* ftr = (block_footer*)((char*)hdr + BLOCK_HEADER_SIZE + hdr->size);
            ftr->size = hdr->size;

            // split leftover if any
            size_t leftover = merged_payload - required_size;
            if (leftover >= ROUND_UP(MIN_SPLIT, ALIGNMENT)) {
                hdr->size = required_size;

                // new footer for allocated block
                block_footer* alloc_ftr = (block_footer*)((char*)hdr + BLOCK_HEADER_SIZE + required_size);
                alloc_ftr->size = required_size;

                // leftover free block
                block_header* new_free = (block_header*)((char*)alloc_ftr + BLOCK_FOOTER_SIZE);
                new_free->size = leftover - (BLOCK_HEADER_SIZE + BLOCK_FOOTER_SIZE);
                new_free->is_free = TRUE;
                new_free->prev_block = new_free->next_block = NULL;

                block_footer* free_ftr = (block_footer*)((char*)new_free + BLOCK_HEADER_SIZE + new_free->size);
                free_ftr->size = new_free->size;

                insert_at_tail_free_list(new_free);
            }
            pthread_mutex_unlock(&lock);
            return ptr; 
        }
    }

    pthread_mutex_unlock(&lock);

    void* new_ptr = memalloc(required_size);
    if (!new_ptr){
        return NULL;
    }

    // copy old content
    memcpy(new_ptr, ptr, old_size);

    // free old block
    memfree(ptr);
    return new_ptr;

}




