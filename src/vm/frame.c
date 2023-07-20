#include "vm/frame.h"
#include "threads/palloc.h"
#include "lib/debug.h"
#include "kernel/hash.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "threads/vaddr.h"
#include "kernel/bitmap.h"
#include "lib/string.h"
#include "threads/pte.h"

/* hash used to map frames*/
struct hash frame_table;
/*lock used to ensure concurrency of frame_table*/
struct lock frame_lock;
/*last elem iterated through when evicting*/
struct hash_iterator i;
/*boolean used to indicate whether or not iterator should start from begining*/
bool restart_iterator = true;

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

/*function used to init the frame table*/
void init_frame_table(void){
    lock_init(&frame_lock);
    hash_init(&frame_table, frame_hash, frame_less, NULL);
}

/*function used to add a frame to frame_table
returns true if added sucessfully false otherwise*/
bool add_frame_to_table(void* frame, struct thread* frame_thread){
    //malloc the frame
    struct frame* f = malloc(sizeof(struct frame));
    if(f == NULL)
        return false;

    //insert into the frame table
    f->kernel_page_addr = frame;
    f->frame_thread = frame_thread;
    lock_acquire(&frame_lock);
    hash_insert(&frame_table, &f->hash_elem);
    lock_release(&frame_lock);    

    return true;
}

/*Function used to find the frame to evict from the frame table,
returns the address of that frame (key in hash table)*/
struct frame* find_frame_to_evict(void){
    if(hash_size(&frame_table) == 0)
        return NULL;

    //declare hash iterator
    
    //go through max 2 clock rounds
    for(int clock_round = 0; clock_round < 2; clock_round++){
        //check to see if need to restart iterator
        //if not, just restart from previous element
        if( restart_iterator){
            hash_first(&i, &frame_table);
            restart_iterator = false;
        }
        
        while (hash_next (&i)){
            struct frame *f = hash_entry(hash_cur (&i), struct frame, hash_elem);
            //check if page is accessed
            if(pagedir_is_accessed(f->frame_thread->pagedir, f->user_page_addr))
                //if is, set it to false and continue
                pagedir_set_accessed(f->frame_thread->pagedir, f->user_page_addr, false);
            else{
                //otherwise, its the frame to evict
                return f;
            }
        }
        //mark to restart iterator
        restart_iterator = true;
    }

    return NULL;
}

/*Function used to save a frame that is being evicted*/
bool save_frame(struct frame* f){
    //check to see if the frame has a dirty bit

    struct sup_pt_list* spte = sup_pt_find(&f->frame_thread->spt, f->user_page_addr);
    if(spte == NULL)
        PANIC("no supplementary page table entry found for frame");

    //if dirty or designated to go into swap slot, swap into swap slot
    if(pagedir_is_dirty(f->frame_thread->pagedir, f->user_page_addr) || spte->type == SWAP_ORIGIN){
        //if dirty, need to load to a swap slot
        //printf("loading to swap slot");
        //find an empty swap_index and dump page into it
        size_t swap_index = page_swap_in(f->user_page_addr);
        if(swap_index == BITMAP_ERROR){
            PANIC("NO FREE SWAP SLOTS");
            return false;
        }   
        //update index and sup page table details
        spte->swap_slot = swap_index;
        spte->type = SWAP_ORIGIN;
        spte->writable = *(f->pte) & PTE_W;
    }
    //NOTE if it wasn't dirty then we can just reread it from the exe
    //which is compltley possible if type remains FILE_ORIGIN

    //zero out frame
    memset(f->kernel_page_addr, 0, PGSIZE);
    //if it isn't dirty don't really have to do anything since can just reload from the file
    spte->loaded = false;

    //make pte as no longer existing within the page table
    pagedir_clear_page(f->frame_thread->pagedir, f->user_page_addr);

    return true;
}

/*Function used to find and evict a frame from the frame table*/
bool evict_frame(void){
    ASSERT(hash_size(&frame_table) > 0);

    //acquire lock for frame table
    lock_acquire(&frame_lock);
    
    //get frame that we are supposed to evict
    struct frame* frame_to_evict = find_frame_to_evict();
    //check that frame was found
    if (frame_to_evict == NULL){
        lock_release(&frame_lock);
        PANIC("NO FRAME TO EVICT");
        return false;
    }
    
    //save the frame
    if(!save_frame(frame_to_evict))
        PANIC("failed to save frame");
    
    deallocate_frame(frame_to_evict, false);
    lock_release(&frame_lock);

    return true;
}

/*Function used to allocate a frame using page from userpool*/
void* 
frame_add (enum palloc_flags flags, struct thread* frame_thread) {

    //check to make sure at least one of the flags is for PAL_USER
    //if not return NULL since we are only allowing PAL_USER
    if(!(flags & PAL_USER))
        return NULL;
    //check for PAL_ZERO and allocate accordingly (basically code)
    //that used to be in process.c
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
        if(!evict_frame()){
            PANIC("failed to evict frame");
            return NULL;
        }
        if(flags & PAL_ZERO) {
            frame = palloc_get_page(PAL_USER | PAL_ZERO);
        }
        else {
            frame = palloc_get_page(PAL_USER);
        }
        //PANIC("Not enough room to allocate frame");
        //would hypothetically evict frame here since there's not enough space for it
    }

    //add frame to frame table
    if(!add_frame_to_table(frame, frame_thread))
        PANIC("could not add frame to table");
    return frame;
}

/* Returns the frame containing the given virtual address,
   or a null pointer if no such frame exists. */
struct frame*
frame_get (void* address) {
    
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

/*function used to free from frame table using address*/
void
frame_free (void* address) {
    //get frame
    struct frame* f = frame_get(address);
    deallocate_frame(f, true);
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