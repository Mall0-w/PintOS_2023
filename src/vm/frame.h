#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "kernel/hash.h"
#include "threads/palloc.h"
#include "threads/thread.h"

/*struct representing an entry in the frame table*/
struct frame {
  void* kernel_page_addr; /*kernel address from frame in frame table*/
  void* user_page_addr; /*user address for frame in frame table*/
  struct hash_elem hash_elem; /*elem for keeping track in hash table*/
  struct thread* frame_thread; /*thread that frame was created under*/
  bool pinned; /*whether or not frame is "pinned" in the frame table*/
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

bool save_frame(struct frame* f); /*Function used to save a frame that is being evicted*/

bool evict_frame(void); /*Function used to evict a frame*/

void deallocate_frame(struct frame* f, bool use_locks); /*function used to deallocate frame*/

struct frame* find_frame_to_evict(void); /*function used to find a frame to evict*/

#endif /*vm/frame.h*/