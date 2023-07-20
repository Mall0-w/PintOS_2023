#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"
#include "kernel/hash.h"
#include "threads/palloc.h"

struct frame{
    void* kernel_page_addr;
    void* user_page_addr;
    bool pinned;
    struct thread* frame_thread;
    struct hash_elem hash_elem;
};

void init_frame_table(void);

unsigned
frame_hash (const struct hash_elem *h, void *aux);  /*Hash function used for frame table*/

bool
frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux); /*comparioson function used for frame table*/

void* allocate_frame(enum palloc_flags flags); /*function used to allocate a frame and add to frame table*/

bool free_frame(void* kernel_page); /*function used to free a frame*/

struct frame* find_frame(void* kernel_page);

bool add_frame_to_table(void* kernel_addr);

bool free_frame(void* address);

void deallocate_frame(struct frame* f, bool use_locks); /*function used to deallocate a frame*/

bool evict_frame(void);

struct frame* find_victim_frame(void);

#endif /* vm/frame.h */