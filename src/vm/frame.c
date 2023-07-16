#include "vm/frame.h"
#include "threads/palloc.h"
#include "lib/debug.h"
#include "kernel/hash.h"
#include "threads/synch.h"
#include "threads/malloc.h"

/* hash used to map frames*/
struct hash frame_table;
/*lock used to ensure concurrency of frame_table*/
struct lock frame_lock;

/* Returns a hash value for frame f. */
unsigned
frame_hash (const struct hash_elem *h, void *aux UNUSED)
{
  const struct frame *f = hash_entry (h, struct frame, hash_elem);
  return hash_bytes (&f->page_addr, sizeof f->page_addr);
}

/* Returns true if frame a precedes frame b. */
bool
frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct frame *a = hash_entry (a_, struct frame, hash_elem);
  const struct frame *b = hash_entry (b_, struct frame, hash_elem);

  return a->page_addr < b->page_addr;
}

/*function used to init the frame table*/
void init_frame_table(void){
    lock_init(&frame_lock);
    hash_init(&frame_table, frame_hash, frame_less, NULL);
}

/*function used to add a frame to frame_table
returns true if added sucessfully false otherwise*/
bool add_frame_to_table(void* frame){
    //malloc the frame
    struct frame* f = malloc(sizeof frame);
    if(f == NULL)
        return false;

    //insert into the frame table
    f->page_addr = frame;
    lock_acquire(&frame_lock);
    hash_insert(&frame_table, &f->hash_elem);
    lock_release(&frame_lock);    

    return true;
}

/*Function used to allocate a frame using page from userpool*/
void* 
frame_add (enum palloc_flags flags) {

    //check to make sure at least one of the flags is for PAL_USER
    //if not return NULL since we are only allowing PAL_USER
    if(!(flags & PAL_USER))
        return NULL;
    //check for PAL_ZERO and allocate accordingly (basically code)
    //that used to be in process.c
    void* frame = NULL;
    if(flags & PAL_ZERO)
        frame = palloc_get_page(PAL_USER | PAL_ZERO);
    else
        frame = palloc_get_page(PAL_USER);

    //check if room for page
    if(frame == NULL){
        PANIC("Not enough room to allocate frame");
        //would hypothetically evict frame here since there's not enough space for it
    }

    //add frame to frame table
    if(!add_frame_to_table(frame))
        PANIC("could not add frame to table");
    return frame;
}

/* Returns the frame containing the given virtual address,
   or a null pointer if no such frame exists. */
struct frame*
frame_get (void* address) {
    
    struct frame f;
    struct hash_elem* e;
    f.page_addr = address;
    lock_acquire(&frame_lock);
    e = hash_find(&frame_table, &f.hash_elem);
    struct frame* result = NULL;
    if(e != NULL)
        result = hash_entry (e, struct frame, hash_elem);
    lock_release(&frame_lock);
    return result;
}

void
frame_free (void* address) {
    //get frame
    struct frame* f = frame_get(address);
    lock_acquire(&frame_lock);
    //free its respective page and remove from hash table
    palloc_free_page(f->page_addr);
    hash_delete(&frame_table, &f->hash_elem);
    free(f);
    
    lock_release(&frame_lock);

    return;
}