#include <vm/page.h>
#include "threads/synch.h"
#include "threads/malloc.h"
#include "vm/frame.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"
#include "userprog/process.h"
#include "lib/string.h"
#include "threads/vaddr.h"

/* lock to ensure concurrency of the supplementary page table*/
struct lock sup_pt_lock;
bool sup_pt_lock_init = false;


/*function used to init the supplementary page table*/
void 
sup_pt_init(struct list *sup_pt_list) {
    /* Ensure lock is only initialized once */
    if(sup_pt_lock_init == false) {
        lock_init(&sup_pt_lock);
        sup_pt_lock_init = true;
    }
    list_init(sup_pt_list);
}

bool
sup_pt_insert(struct list *sup_pt_list, enum page_type type, void *upage, struct file *file, off_t offset, bool writable, size_t read_bytes, size_t zero_bytes) {
    lock_acquire(&sup_pt_lock);
    struct sup_pt_list *spt = malloc(sizeof(struct sup_pt_list));
    if(spt == NULL)
        return false;
    //printf("\n\nINSERTING UPAGE: %p\n\n", upage);
    spt->type = type;
    spt->upage = upage;
    spt->file = file;
    spt->offset = offset;
    spt->writable = writable;
    spt->read_bytes = read_bytes;
    spt->zero_bytes = zero_bytes;
    spt->loaded = false;
    list_push_front(sup_pt_list, &spt->elem);
    lock_release(&sup_pt_lock);
    return true;
}

void 
sup_pt_remove(struct list *sup_pt_list, void *upage) {
    lock_acquire(&sup_pt_lock);
    struct list_elem *e;
    for (e = list_begin(sup_pt_list); e != list_end(sup_pt_list); e = list_next(e)) {
        struct sup_pt_list *spt = list_entry(e, struct sup_pt_list, elem);
        if (spt->upage == upage) {
            list_remove(e);
            free(spt);
            break;
        }
    }
    lock_release(&sup_pt_lock);
}

struct sup_pt_list*
sup_pt_find(struct list *sup_pt_list, void *upage) {
    struct list_elem *e;
    for (e = list_begin(sup_pt_list); e != list_end(sup_pt_list); e = list_next(e)) {
        struct sup_pt_list *spt = list_entry(e, struct sup_pt_list, elem);
        //printf("sup_pt_find: %p\n", spt->upage);
        //printf("curr spt pointer: %p\n", spt);
        if (spt->upage == upage) {
            return spt;
        }
    }
    return NULL;
}

bool sup_page_cleanup(struct list* sup_pt_list){
    struct list_elem *e;
    if(list_size(sup_pt_list) == 0)
        return true;

    lock_acquire(&sup_pt_lock);
    while(!list_empty(sup_pt_list)){
        e = list_pop_front(sup_pt_list);
        struct sup_pt_list *spt = list_entry(e, struct sup_pt_list, elem);
        //if loaded, release the frame
        if(spt->loaded){
            void* frame = pagedir_get_page(thread_current()->pagedir, spt->upage);
            if(frame == NULL){
                lock_release(&sup_pt_lock);
                return false;
            }
           struct frame* f = frame_get(f);
           //only need to free f since pagedir_destory is called after, which frees all allocated pages
           free(f);

        }else{
            //if not loaded check to see if its reserved in a swap slot
            if(spt->type == SWAP_ORIGIN){
                unlock_swap_slot(spt->swap_slot);
            }
        }
        free(spt);    
    }
    lock_release(&sup_pt_lock);
    return true;
}

bool
sup_load_swap(struct sup_pt_list* spt){
    ASSERT(spt->type == SWAP_ORIGIN);
    ASSERT(spt->loaded == false);
    struct thread* t = thread_current();
    //allocate frame and check that it was allocated
    void* kernel_addr = frame_add(PAL_USER, t);
    if(kernel_addr == NULL)
        return false;
    //remap pages
    if(!pagedir_set_page(t->pagedir, spt->upage, kernel_addr, spt->writable)){
        //if remap failed, free frame and return false;
        frame_free(kernel_addr);
        return false;
    }

    //swap in from swap slot
    page_swap_out(spt->swap_slot, spt->upage);
    spt->loaded = true;
    //get a page for the swap slot
    return true;
}

bool
sup_load_file(struct sup_pt_list* spt){
    ASSERT(spt->loaded == false);
    
    file_seek(spt->file, spt->offset);
    //printf("Seeking FILE with upage and offset: %p, %d\n", spt->upage, spt->offset);
    /* Get a page of memory. */
    uint8_t* kpage = frame_add (PAL_USER, thread_current());
    //uint8_t* kpage = palloc_get_page (PAL_USER);
    if (kpage == NULL) {
        return false;
    }

    /* Load this page. */
    if (file_read (spt->file, kpage, spt->read_bytes) != (int) spt->read_bytes)
    {
        frame_free (kpage);
        //palloc_free_page (kpage);
        return false; 
    }
    memset (kpage + spt->read_bytes, 0, spt->zero_bytes);
    struct thread* t = thread_current();
    /* Add the page to the process's address space. */
    if (!(pagedir_get_page (t->pagedir, spt->upage) == NULL
          && pagedir_set_page (t->pagedir, spt->upage, kpage, spt->writable))) 
    {
        frame_free (kpage);
        return false; 
    }

    spt->loaded = true;

    return true;
}

bool
sup_load_zero(struct sup_pt_list* spt){
    ASSERT(spt->loaded == false);
    //printf("Seeking ZERO with upage and offset: %p, %d\n", spt->upage, spt->offset);
    /* Get a page of memory. */
    uint8_t* kpage = frame_add (PAL_USER | PAL_ZERO, thread_current());
    if (kpage == NULL) {
        return false;
    }

    memset (kpage, 0, PGSIZE);
    struct thread* t = thread_current();
    /* Add the page to the process's address space. */
    if (!(pagedir_get_page (t->pagedir, spt->upage) == NULL
          && pagedir_set_page (t->pagedir, spt->upage, kpage, spt->writable))) 
    {
        frame_free (kpage);
        return false; 
    }

    spt->loaded = true;

    return true;
}

bool increase_stack_size(void* user_address, struct thread* t){
    /*allocating a frame for the stack, using same flags for original stack frame*/
    void* frame = frame_add(PAL_USER | PAL_ZERO, t);
    if(frame == NULL)
        return false;
    /*create supplemenal page table entry*/
    struct sup_pt_list *spt = malloc(sizeof(struct sup_pt_list));
    spt->type = SWAP_ORIGIN;
    spt->upage = user_address;
    spt->file = NULL;
    spt->offset = 0;
    spt->writable = true;
    spt->read_bytes = 0;
    spt->zero_bytes = 0;
    spt->loaded = true;
    list_push_front(&t->spt, &spt->elem);
    
    /*mapping the frames*/
    if(!pagedir_set_page(t->pagedir, pg_round_down(user_address), frame, true)){
        //if mapping failed, free the frame and return false
	    frame_free(frame);
        return false; 
	}

    return true;
}