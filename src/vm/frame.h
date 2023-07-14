#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "kernel/hash.h"
#include "threads/palloc.h"
#include "threads/thread.h"

struct frame {
  void* page_addr;
  struct hash_elem hash_elem;
  struct thread* frame_thread;
};

unsigned
frame_hash (const struct hash_elem *h, void *aux);  /*Hash function used for frame table*/

bool
frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux); /*comparioson function used for frame table*/

void init_frame_table(void); /*function to initalize frame table*/

bool add_frame_to_table(void* address, struct thread* frame_thread); /*function used to add a frame to frame table*/

void* frame_add (enum palloc_flags flags, struct thread* frame_thread); /*function used to get a frame from user space (will also put in frame table)*/

struct frame* frame_get (void* address); /*function used to get a frame from its address*/

void frame_free (void* address); /*function used to free a frame*/

bool evict_frame(void); /*Function used to evict a frame*/

void deallocate_frame(struct frame* f, bool use_locks); /*function used to deallocate frame*/

struct frame* find_frame_to_evict(void); /*function used to find a frame to evict*/

#endif /*vm/frame.h*/