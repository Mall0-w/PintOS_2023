#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include <stdio.h>

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    // block_sector_t start;               /* First data sector. */
    block_sector_t direct[MAX_DIRECT_POINTERS]; /*array of max_direct_pointers sectors*/
    block_sector_t indirect;  /*sector containing single indirect pointers*/
    block_sector_t dub_indirect;  /*sector containing double indirect pointers*/
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
  };


static block_sector_t
get_sector_from_index(const struct inode_disk* d, off_t index){
  //assert using a valid index
  ASSERT(index > 0);
  ASSERT(index < NUM_SECTORS_IN_BLOCK + (MAX_DIRECT_POINTERS * (MAX_DIRECT_POINTERS + 1)));
  //check if in direct points
  //if so return as usual
  if(index < MAX_DIRECT_POINTERS)
    return d->direct[index];
  //check if index belongs in single indirect
  else if(index < MAX_DIRECT_POINTERS + NUM_SECTORS_IN_BLOCK){
    //if single indirect
    //offset index so that can read properly
    index -= MAX_DIRECT_POINTERS;

    //load single indirect from memeory
    block_sector_t inds[NUM_SECTORS_IN_BLOCK];
    block_read(fs_device, d->indirect, inds);

    //return proper index
    return inds[index];
  }

  //otherwise double indirect
  //offset index so can read properly
  index -= MAX_DIRECT_POINTERS + NUM_SECTORS_IN_BLOCK;

  //load double indirect
  block_sector_t double_inds[NUM_SECTORS_IN_BLOCK];
  block_read(fs_device, d->dub_indirect, double_inds);

  //get the index need to read from to get single indirect
  //do this by floor dividing index by the number of sectors in a block
  //to get block (single ind) it belongs to
  block_sector_t inds[NUM_SECTORS_IN_BLOCK];
  block_read(fs_device, double_inds[index / NUM_SECTORS_IN_BLOCK], inds);

  //use remainder (modulo) to load actual sector inside found block
  return inds[index % NUM_SECTORS_IN_BLOCK];
}

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

static
bool allocate_starting_sectors(struct inode_disk* d, size_t num_sectors, block_sector_t inode_sector);

static
bool grow_inode(struct inode* inode, size_t starting_index, size_t num_sectors);

bool allocate_or_read(block_sector_t* sector, void* buffer);

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return get_sector_from_index(&inode->data, pos/BLOCK_SECTOR_SIZE);
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/*function used to allocate a sector if it does not exist
if it does exist, it reads its contents to buffer*/
bool allocate_or_read(block_sector_t* sector, void* buffer){
  if(*sector == 0){
     //allocate sector appropriatley
    if(!free_map_allocate(1, sector))
      return false;
  }else{
    //otherwise load it
    block_read(fs_device, *sector, buffer);
  }
  return true;
}

/*function used to allocate starting sectors of an inode*/
static
bool grow_inode(struct inode* inode, size_t starting_index, size_t num_sectors){

  //setting up arrays for indirect and double_indirect
  block_sector_t indirect[NUM_SECTORS_IN_BLOCK] = {0};
  block_sector_t double_indirect[NUM_SECTORS_IN_BLOCK] = {0};
  block_sector_t double_children[NUM_SECTORS_IN_BLOCK][NUM_SECTORS_IN_BLOCK];

  //setting up flags on whether to save indirects
  bool wrote_indirect = false;
  bool wrote_double = false;

  /*declaring an array of 0s that takes up the size of a sector
  used to clear out sectors*/
  int zero_arr[BLOCK_SECTOR_SIZE] = {0};

  size_t corrected_index = 0;

  //iterate through sectors
  for(size_t i = starting_index; i < starting_index + num_sectors; i++){
    //if in a direct pointer
    if(i < MAX_DIRECT_POINTERS){
      //check that theres space for the sector and write
      if(free_map_allocate(1, &(inode->data.direct[i])))
        block_write(fs_device, inode->data.direct[i], zero_arr);
      else
        return false;
    }
    //if in indirect
    else if(i < MAX_DIRECT_POINTERS + NUM_SECTORS_IN_BLOCK){
      //check if we have written to indirect
      if(!wrote_indirect){
        //allocate or read from indirect depending on if it exists
        if(!allocate_or_read(&inode->data.indirect, indirect))
          return false;
        //mark flag
        wrote_indirect = true;
      }
      //correct index
      corrected_index = i - MAX_DIRECT_POINTERS;
      //allocate and write as normal
      if(free_map_allocate(1, &indirect[corrected_index]))
        block_write(fs_device, indirect[corrected_index], zero_arr);
      else
        return false;
    }
    //otherwise in double indirect
    else{
      //check if we have written to double indirect yet
      if(!wrote_double){
        //if not allocate sector for double indirect
        if(!allocate_or_read(&inode->data.dub_indirect, double_indirect))
          return false;
        
        wrote_double = true;
      }

      //correct the index
      corrected_index = i - MAX_DIRECT_POINTERS - NUM_SECTORS_IN_BLOCK;
      block_sector_t sector = i / NUM_SECTORS_IN_BLOCK;
      block_sector_t offset = i % NUM_SECTORS_IN_BLOCK;

      //check if need to allocate sector for the indirect array
      if(double_children[sector] == NULL){
        if(!allocate_or_read(&double_indirect[sector], double_children[sector]))
          return false;
      }

      //allocate individual sector as usual
      if(free_map_allocate(1, &double_children[sector][offset]))
        block_write(fs_device, double_children[sector][offset], zero_arr);
      else
        return false;

    }
  }

  //now that done writing, save comleted indirect sectors

  if(wrote_indirect){
    block_write(fs_device, inode->data.indirect, indirect);
  }

  if(wrote_double){
    
    block_sector_t double_sectors = (num_sectors - MAX_DIRECT_POINTERS - NUM_SECTORS_IN_BLOCK) / NUM_SECTORS_IN_BLOCK;
    //get rows for the matrix of indirect
    for(block_sector_t i = 0; i < double_sectors; i++){
      //write all single indirect to memeory
      block_write(fs_device, double_indirect[i], double_children[i]);
    }
    //write double indirect to memory
    block_write(fs_device, inode->data.dub_indirect, double_indirect);
  }

  //save all of it back to disk

  block_write(fs_device, inode->sector, &inode->data);
  return true;
}

/*function used to allocate starting sectors of an inode*/
static
bool allocate_starting_sectors(struct inode_disk* d, size_t num_sectors, block_sector_t inode_sector){
  //to allocate everything, we essentially just grow from index 0

  //setting up dummy inode that we can actually pass along to the function
  struct inode* dummy = malloc(sizeof(struct inode));
  dummy->data = *d;
  dummy->sector = inode_sector;
  bool success = grow_inode(dummy, 0, num_sectors);
  free(dummy);
  return success;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */

  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->indirect = 0;
      disk_inode->dub_indirect = 0;
      success = allocate_starting_sectors(disk_inode, sectors, sector);
      
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          //find sectors and free them
          for(size_t i = 0; i < bytes_to_sectors(inode->data.length); i++){
            free_map_release(get_sector_from_index(&inode->data, i), 1);
          } 
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  //check if need to grow inode

  if(size + offset > inode_length(inode)){
    //grow the inode
    if(!grow_inode(inode, bytes_to_sectors(inode_length(inode)), bytes_to_sectors(offset + size)))
      PANIC("FAILED TO GROW INODE ON WRITE");

    //mark its new length|
    inode->data.length = offset + size;
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
