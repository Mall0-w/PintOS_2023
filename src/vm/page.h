#include <stdio.h>
#include <list.h>
#include <filesys/file.h>
#include <list.h>
#include <stdbool.h>

enum page_type {
    FILE, // File
    SWAP, // Swap
    ZERO // All-zero page
};

struct sup_pt_list {
    struct list_elem elem; // Element in the list
    enum page_type type; // Type of the page
    uint8_t *upage; // User virtual address
    struct file *file; // File pointer
    off_t offset; // Offset of the file
    bool writable; // Writable or not
    size_t read_bytes; // Number of bytes to read
    size_t zero_bytes; // Number of bytes to zero
};

void sup_pt_init(struct list *sup_page_table); // Initialize the supplemental page table
void sup_pt_insert(struct list *sup_page_table, void *upage, struct file *file, off_t offset); // Add a new entry to the supplemental page table
void sup_pt_remove(struct list *sup_page_table, void *upage); // Delete an entry from the supplemental page table
struct list_elem *sup_pt_find(void *upage); // Find an entry in the supplemental page table
bool sup_pt_less(const struct list_elem *a, const struct list_elem *b, void *aux);

