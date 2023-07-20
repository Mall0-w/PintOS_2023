#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"
#include "kernel/hash.h"

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

bool allocate_frame(void* kernel_page); /*function used to allocate a frame and add to frame table*/

bool free_frame(void* kernel_page); /*function used to free a frame*/

struct frame* find_frame(void* kernel_page);

bool deallocate_frame(struct frame* f); /*function used to deallocate a frame*/

bool evict_frame(void);

struct frame* find_victim_frame(void);

#endif /* vm/frame.h */