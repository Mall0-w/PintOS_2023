#include "vm/page.h"
#include "threads/thread.h"
#include "kernel/hash.h"
#include "threads/malloc.h"
#include "vm/swap.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "lib/string.h"

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
    lock_init(&t->spt_lock);
}

/* Returns the frame containing the given virtual address,
   or a null pointer if no such frame exists. */
struct sup_page*
find_page (struct thread* t, void* user_address) {
  lock_acquire(&t->spt_lock);
  struct sup_page* p = find_page_without_locks(t, user_address);
  lock_release(&t->spt_lock);
  return p;
}

/*function used to find a page in sup page table without using locks*/
struct sup_page*
find_page_without_locks(struct thread* t, void* user_address){
  struct sup_page p;
  struct hash_elem* e;
  p.user_page_addr = pg_round_down(user_address);
  e = hash_find(&t->spt, &p.hash_elem);
  struct sup_page* result = NULL;
  if(e != NULL)
      result = hash_entry (e, struct sup_page, hash_elem);
  return result;
}

/*function used to allocate supplemetary page table without using locks*/
static bool
allocate_sup_pt_lockless(struct thread* t, enum page_type type, 
void *upage, struct file *file, off_t offset, bool writable, size_t read_bytes, size_t zero_bytes) {
  struct sup_page *spt = malloc(sizeof(struct sup_page));
  if(spt == NULL)
      return false;
  spt->type = type;
  spt->user_page_addr = pg_round_down(upage);
  spt->file = file;
  spt->offset = offset;
  spt->writable = writable;
  spt->read_bytes = read_bytes;
  spt->zero_bytes = zero_bytes;
  spt->loaded = false;

  hash_insert(&t->spt, &spt->hash_elem);
  return true;
}

/*function used to allcoate a page table entry and insert it*/
bool
allocate_sup_pt(struct thread* t, enum page_type type, void *upage, struct file *file, off_t offset, bool writable, size_t read_bytes, size_t zero_bytes) {
  lock_acquire(&t->spt_lock);
  bool result = allocate_sup_pt_lockless(t,type,upage,file,offset,writable,read_bytes,zero_bytes);
  lock_release(&t->spt_lock);
  return result;
}

/*function used to remvoe and free a sup page table entry*/
bool free_sup_page(struct thread* t, void* upage){
  lock_acquire(&t->spt_lock);
  struct sup_page* p = find_page_without_locks(t, upage);
  if(p == NULL){
    return false;
  }
  hash_delete(&t->spt, &p->hash_elem);
  if(p->type == SWAP_ORIGIN && !p->loaded){
    swap_free_slot(p->swap_slot);
  }
  free(p);
  lock_release(&t->spt_lock);
  return true;
}

void free_pte(struct hash_elem* e, void* aux UNUSED){
  struct sup_page* spte = hash_entry (e, struct sup_page, hash_elem);
  if (spte->type == SWAP_ORIGIN && !spte->loaded)
    swap_free_slot(spte->swap_slot);
  free (spte);
}

/*function used to free a sup page table*/
void free_entire_sup(struct thread* t){
  lock_acquire(&t->spt_lock);
  hash_destroy(&t->spt, free_pte);
  lock_release(&t->spt_lock);
}

/*function to load a file from supplemental page table*/
bool sup_load_file(struct sup_page *spt){
  file_seek (spt->file, spt->offset);
  struct thread* t = thread_current();
  /* Calculate how to fill this page.
      We will read PAGE_READ_BYTES bytes from FILE
      and zero the final PAGE_ZERO_BYTES bytes. */

  /* Get a page of memory. */
  //uint8_t *kpage = palloc_get_page (PAL_USER);
  uint8_t* kpage = allocate_frame(PAL_USER);
  if (kpage == NULL)
    return false;
  
  //find frame belonging to it and pin/link it
  struct frame* f = find_frame(kpage);
  f->pinned = true;
  f->user_page_addr = spt->user_page_addr;
  /* Load this page. */
  if (file_read (spt->file, kpage, spt->read_bytes) != (int) spt->read_bytes)
    {
      free_frame(kpage);
      return false; 
    }
  memset (kpage + spt->read_bytes, 0, spt->zero_bytes);

  /* Add the page to the process's address space. */
  if (!(pagedir_get_page (t->pagedir, spt->user_page_addr) == NULL
          && pagedir_set_page (t->pagedir, spt->user_page_addr, kpage, spt->writable)))
  {
    free_frame(kpage);
    return false; 
  }
  //unpin frame
  f->pinned = false;
  //if all has gone well, mark sup page as loaded
  spt->loaded = true;

  return true;
}

bool sup_load_swap(struct sup_page* spt){
  //allocate a frame
  void* frame = allocate_frame(PAL_USER);
  if (frame == NULL)
    return false;
  //linking frame and virtual addr
  struct frame* f = find_frame(frame);
  f->user_page_addr = spt->user_page_addr;
 
  //mapping the pages
  if (!pagedir_set_page(thread_current()->pagedir, spt->user_page_addr, frame, 
			 spt->writable)){
    free_frame(frame);
    return false;
  }
  //pin frame
  f->pinned = true;
  //taking data from swap
  page_swap_out(spt->swap_slot, spt->user_page_addr);

  f->pinned = false;
  
  //marking as loaded
  spt->loaded = true;

  return true;
}

/*function used to increase stack size*/
bool increase_stack_size(void* user_address, struct thread* t){
  void *frame;
  //allocate frame for stack
  frame = allocate_frame(PAL_USER | PAL_ZERO);
  if (frame == NULL)
    return false;
  //link it to the page table
  if (!pagedir_set_page (t->pagedir, pg_round_down(user_address), frame, true)){
	  free_frame(frame);
    return false; 
	}
  //set up a supplementary page table entry
  if(!allocate_sup_pt(t, SWAP_ORIGIN, user_address, NULL, 0, true, 0, 0)){
    free_frame(frame);
    return false;
  }
  //linking the two
  struct frame* f = find_frame(frame);
  f->user_page_addr = user_address;

  return true;
}