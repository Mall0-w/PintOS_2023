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

/*function used to swap page into swap slot
returns index in swap table or BITMAP_ERROR if there
are no free swap slots*/
size_t page_swap_in(void* page_address){
    //acquire lock for the swap
    lock_acquire(&swap_lock);
    //find an open index and flip it to claim it
    size_t bitmap_index = bitmap_scan_and_flip(swap_map, 0, 1, true);

    //if not found, return the error
    if(bitmap_index == BITMAP_ERROR){
        lock_release(&swap_lock);
        return BITMAP_ERROR;
    }
    //write to the different sectors of the bitmap
    uint32_t num_sectors = num_sectors_in_page();
    for(uint32_t i = 0; i < num_sectors; i++){
        //writing to each sector
        block_write(swap_block, (bitmap_index * num_sectors) + i, page_address + (i * BLOCK_SECTOR_SIZE));
    }

    //release lock
    lock_release(&swap_lock);
    //return index to update supplementary page table
    return bitmap_index;
}

/*function used to write from swap slot swap_index into page_address*/
void page_swap_out(size_t swap_index, void* page_address){
    //acquire lock for swapp
    lock_acquire(&swap_lock);

    //check that the bitmap swap is actually filled
    if(!bitmap_test(swap_map, swap_index)){
        lock_release(&swap_lock);
        PANIC("Tried to read from empty swap slot");
        return;
    }

    //read sectors from swap block
    uint32_t num_sectors = num_sectors_in_page();
    for(uint32_t i = 0; i < num_sectors; i++){
        block_read(swap_block, (swap_index * num_sectors) + i, page_address + (i * BLOCK_SECTOR_SIZE));
    }

    //now that we have read from swap slot, mark it as empty
    bitmap_flip(swap_map, swap_index);
    
}

/*function used to determine the number of pages in a swap*/
uint32_t num_pages_in_swap(struct block* swap_block){
    //divide number of sectors in swap by num sectors in a page
    return block_size(swap_block) / num_sectors_in_page();
}

/*function used to determine the number of sectors in a page*/
uint32_t num_sectors_in_page(void){
    //can use int since have to round down anyways
    return PGSIZE / BLOCK_SECTOR_SIZE;
}