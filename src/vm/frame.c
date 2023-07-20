#include "vm/frame.h"
#include "kernel/hash.h"
#include "threads/synch.h"

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