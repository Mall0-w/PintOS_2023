#include "vm/page.h"
#include "threads/thread.h"
#include "kernel/hash.h"

/* Returns a hash value for frame f. */
unsigned
page_hash (const struct hash_elem *h, void *aux UNUSED)
{
  const struct sup_page *p = hash_entry (h, struct sup_page, hash_elem);
  return hash_bytes (&p->user_page_addr, sizeof p->user_page_addr);
}

/* Returns true if frame a precedes frame b. */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct sup_page *a = hash_entry (a_, struct sup_page, hash_elem);
  const struct sup_page *b = hash_entry (b_, struct sup_page, hash_elem);

  return a->user_page_addr < b->user_page_addr;
}

void init_sup_page_table(struct thread* t){
    hash_init(&t->spt,page_hash,page_less,NULL);
}