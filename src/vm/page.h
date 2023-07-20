#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "threads/thread.h"
#include "filesys/file.h"
#include "kernel/hash.h"

/*the limit above the stack to start considering*/
#define ABOVE_STACK_LIMIT 32
/*max stack size (8MB)*/
#define MAX_STACK_SIZE 8388608

enum page_type {
    FILE_ORIGIN, // File
    SWAP_ORIGIN, // Swap
    ZERO_ORIGIN // All-zero page
};

struct sup_page {
    struct hash_elem hash_elem; // Element in the list
    enum page_type type; // Type of the page
    void* user_page_addr; // User virtual address
    struct file *file; // File pointer
    off_t offset; // Offset of the file
    bool writable; // Writable or not
    size_t read_bytes; // Number of bytes to read
    size_t zero_bytes; // Number of bytes to zeros
    size_t swap_slot;   //index of swap slot
    bool loaded;        //boolean used to indicate if its loaded in memeory
};

unsigned
page_hash (const struct hash_elem *h, void *aux UNUSED);

bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED);

void init_sup_page_table(struct thread* t); /*Function used to initalize supplementary page table*/

bool allocate_sup_pt(struct thread* t, enum page_type type, void *upage, struct file *file, off_t offset, bool writable, size_t read_bytes, size_t zero_bytes); // Add a new entry to the supplemental page table
bool free_sup_page(struct thread* t, void *upage); // Delete an entry from the supplemental page table
void free_entire_sup(struct thread* t);
struct sup_page* find_page(struct thread* t, void *upage); // Find an entry in the supplemental page table
struct sup_page* find_page_without_locks(struct thread* t, void* upage);
void free_pte(struct hash_elem *e, void* aux UNUSED);
/* Getting the information from previous pages (file, swap, etc)*/
bool sup_load_file(struct sup_page *spt);
bool sup_load_swap(struct sup_page *spt);
bool sup_load_zero(struct sup_page *spt);

bool increase_stack_size(void* user_address, struct thread* t);


#endif /* vm/page.h */