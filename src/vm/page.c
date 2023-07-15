#include <vm/page.h>
#include "threads/synch.h"
#include "threads/malloc.h"

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

void
sup_pt_insert(struct list *sup_pt_list, enum page_type type, void *upage, struct file *file, off_t offset, bool writable, size_t read_bytes, size_t zero_bytes) {
    struct sup_pt_list *spt = malloc(sizeof(struct sup_pt_list));
    spt->type = type;
    spt->upage = upage;
    spt->file = file;
    spt->offset = offset;
    spt->writable = writable;
    spt->read_bytes = read_bytes;
    spt->zero_bytes = zero_bytes;
    spt->loaded = false;
    list_push_front(sup_pt_list, &spt->elem);
}

void 
sup_pt_remove(struct list *sup_pt_list, void *upage) {
    struct list_elem *e;
    for (e = list_begin(sup_pt_list); e != list_end(sup_pt_list); e = list_next(e)) {
        struct sup_pt_list *spt = list_entry(e, struct sup_pt_list, elem);
        if (spt->upage == upage) {
            list_remove(e);
            free(spt);
            break;
        }
    }
}

struct sup_pt_list*
sup_pt_find(struct list *sup_pt_list, void *upage) {
    struct list_elem *e;
    for (e = list_begin(sup_pt_list); e != list_end(sup_pt_list); e = list_next(e)) {
        struct sup_pt_list *spt = list_entry(e, struct sup_pt_list, elem);
        if (spt->upage == upage) {
            return spt;
        }
    }
    return NULL;
}