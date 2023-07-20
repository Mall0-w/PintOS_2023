#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "kernel/bitmap.h"
#include "devices/block.h"

void init_swap(void);  /*init function for swap*/

/*function used to swap page into swap slot
returns index in swap table or BITMAP_ERROR if there
are no free swap slots*/
size_t page_swap_in(void* page_address);

void page_swap_out(size_t swap_index, void* page_address); /*function used to write from swap slot swap_index into page_address*/

uint32_t num_pages_in_swap(struct block* swap_block); /*function used to determine the number of pages in a swap*/

uint32_t num_sectors_in_page(void);  /*function used to determine the number of sectors in a page*/

void swap_free_slot(size_t swap_slot);

#endif /* vm/swap.h */