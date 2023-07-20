#include "vm/frame.h"
#include "kernel/hash.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"

struct lock frame_lock;
struct hash frame_table;
struct hash_iterator i;

bool restart_iterator = false;

/* Returns a hash value for frame f. */
unsigned
frame_hash (const struct hash_elem *h, void *aux UNUSED)
{
  const struct frame *f = hash_entry (h, struct frame, hash_elem);
  return hash_bytes (&f->kernel_page_addr, sizeof f->kernel_page_addr);
}

/* Returns true if frame a precedes frame b. */
bool
frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct frame *a = hash_entry (a_, struct frame, hash_elem);
  const struct frame *b = hash_entry (b_, struct frame, hash_elem);

  return a->kernel_page_addr < b->kernel_page_addr;
}

void init_frame_table(void){
  lock_init(&frame_lock);
  hash_init(&frame_table, frame_hash, frame_less, NULL);
}

/* Returns the frame containing the given virtual address,
   or a null pointer if no such frame exists. */
struct frame*
find_frame (void* address) {
  struct frame f;
  struct hash_elem* e;
  f.kernel_page_addr = address;
  lock_acquire(&frame_lock);
  e = hash_find(&frame_table, &f.hash_elem);
  struct frame* result = NULL;
  if(e != NULL)
      result = hash_entry (e, struct frame, hash_elem);
  lock_release(&frame_lock);
  return result;
}

/*function used to add a frame to the frame table*/
bool add_frame_to_table(void* kernel_addr){
  struct frame* f = malloc(sizeof(struct frame));
  if(f == NULL)
    return false;
  f->kernel_page_addr = kernel_addr;
  f->frame_thread = thread_current();
  f->pinned = false;

  hash_insert(&frame_table, &f->hash_elem);

  return true;
}

/*function used to save a frame*/
bool save_frame(struct frame* f){
  return false;
}

/*function used to find which frame to evict*/
struct frame* find_victim_frame(void){
  return NULL;
}

/*function used to evict a frame without using locks*/
static bool evict_frame_without_locks(void){
  return false;
}

/*function used to evict a frame*/
bool evict_frame(void){
  lock_acquire(&frame_lock);
  bool result = evict_frame_without_locks();
  lock_release(&frame_lock);
  return result;
}

void* allocate_frame(enum palloc_flags flags){
  //check to make sure at least one of the flags is for PAL_USER
  //if not return NULL since we are only allowing PAL_USER
  if(!(flags & PAL_USER))
      return NULL;
  //check for PAL_ZERO and allocate accordingly (basically code)
  //that used to be in process.c
  lock_acquire(&frame_lock);
  void* frame = NULL;
  if(flags & PAL_ZERO) {
      frame = palloc_get_page(PAL_USER | PAL_ZERO);
  }
  else {
      frame = palloc_get_page(PAL_USER);
  }

  //check if room for page
  if(frame == NULL){
      //no more room for frame so evict
      // if(!evict_frame()){
      //     PANIC("failed to evict frame");
      //     return NULL;
      // }
      // if(flags & PAL_ZERO) {
      //     frame = palloc_get_page(PAL_USER | PAL_ZERO);
      // }
      // else {
      //     frame = palloc_get_page(PAL_USER);
      // }
      lock_release(&frame_lock);
      PANIC("Not enough room to allocate frame");
      //would hypothetically evict frame here since there's not enough space for it
  }

  //add frame to frame table
  if(!add_frame_to_table(frame)){
    lock_release(&frame_lock);
    PANIC("could not add frame to table");
  }

  lock_release(&frame_lock);
  return frame;
}

/*function used to free from frame table using address*/
bool
free_frame (void* address) {
    //get frame
    struct frame* f = find_frame(address);
    if(f == NULL)
      return false;
    deallocate_frame(f, true);
    return true;
}

/*function used to deallocate frame*/
void deallocate_frame(struct frame* f, bool use_locks){
    if(use_locks)
        lock_acquire(&frame_lock);
    //free its respective page and remove from hash table
    palloc_free_page(f->kernel_page_addr);
    hash_delete(&frame_table, &f->hash_elem);
    free(f);
    
    if(use_locks)
        lock_release(&frame_lock);

    return;
}