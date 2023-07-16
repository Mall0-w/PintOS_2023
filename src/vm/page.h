#ifndef VM_PAGE_H
#define VM_PAGE_H
#include <stdio.h>
#include <list.h>
#include <filesys/file.h>
#include <list.h>
#include <stdbool.h>

enum page_type {
    FILE_ORIGIN, // File
    SWAP_ORIGIN, // Swap
    ZERO_ORIGIN // All-zero page
};

struct sup_pt_list {
    struct list_elem elem; // Element in the list
    enum page_type type; // Type of the page
    uint8_t *upage; // User virtual address
    struct file *file; // File pointer
    off_t offset; // Offset of the file
    bool writable; // Writable or not
    size_t read_bytes; // Number of bytes to read
    size_t zero_bytes; // Number of bytes to zeros
    size_t swap_slot;   //index of swap slot
    bool loaded;        //boolean used to indicate if its loaded in memeory
};
/* Supplemental table functions */
void sup_pt_init(struct list *sup_pt_list); // Initialize the supplemental page table
void sup_pt_insert(struct list *sup_pt_list, enum page_type type, void *upage, struct file *file, off_t offset, bool writable, size_t read_bytes, size_t zero_bytes); // Add a new entry to the supplemental page table
void sup_pt_remove(struct list *sup_pt_list, void *upage); // Delete an entry from the supplemental page table
struct sup_pt_list *sup_pt_find(struct list *sup_pt_list, void *upage); // Find an entry in the supplemental page table

/* Getting the information from previous pages (file, swap, etc)*/
bool sup_load_file(struct sup_pt_list *spt);
bool sup_load_swap(struct sup_pt_list *spt);
bool sup_load_zero(struct sup_pt_list *spt);

#endif /* vm/page.h */
