#include "vm/swap.h"
#include "kernel/bitmap.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/*bitmap used to keep track of used swap slots*/
struct bitmap* swap_map;
/*block used for a swap*/
struct block* swap_block;
/*lock used for swap concurrency*/
struct lock swap_lock;

/*function used to init swap */
void init_swap(void){
    //get swap block and check that it was found
    swap_block = block_get_role(BLOCK_SWAP);
    if (swap_block == NULL){
        PANIC("swap block failed to init");
    }
    //init lock for managing swap table
    lock_init(&swap_lock);
    //init bitmap
    //size should be

    //create a bitmap with a bit for each page
    swap_map = bitmap_create(num_pages_in_swap(swap_block));
    if(swap_map == NULL)
        PANIC("swap bitmap failed to init");

    //zero out swap map to inidcate no spots have been reserved
    bitmap_set_all(swap_map, false);
}

/*function used to determine the number of pages in a swap*/
uint32_t num_pages_in_swap(struct block* swap_block){
    //divide number of sectors in swap by num sectors in a page
    //can do int division since would have to floor anyways
    return block_size(swap_block) / num_sectors_in_page();
}

/*function used to determine the number of sectors in a page*/
uint32_t num_sectors_in_page(void){
    //can use int since have to round down anyways
    return PGSIZE / BLOCK_SECTOR_SIZE;
}