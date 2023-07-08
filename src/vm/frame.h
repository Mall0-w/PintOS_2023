#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "kernel/hash.h"
#include "threads/palloc.h"

struct frame {
  void* frame_addr;
  struct hash_elem hash_elem;
};

unsigned
frame_hash (const struct hash_elem *h, void *aux);  /*Hash function used for frame table*/

bool
frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux); /*comparioson function used for frame table*/

void init_frame_table(void); /*function to initalize frame table*/

bool add_frame_to_table(void* address); /*function used to add a frame to frame table*/

void* frame_add (enum palloc_flags flags); /*function used to get a frame from user space (will also put in frame table)*/

struct frame* frame_get (void* address); /*function used to get a frame from its address*/

void frame_free (void* address); /*function used to free a frame*/

#endif /*vm/frame.h*/