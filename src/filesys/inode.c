#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t direct[MAX_DIRECT_SECTORS];               /*direct sectors of inode */
    block_sector_t indirect; /*sector container all indirect sectors*/
    block_sector_t dub_indirect; /*sector containing all double indirect sectors*/
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/*function used to get sector belonging to inode disk at position pos*/
static block_sector_t get_sector(struct inode_disk* d, int pos){
  //if pos is an index to a direct sector
  if(pos < MAX_DIRECT_SECTORS){
    return d->direct[pos];
  }else if (pos < MAX_DIRECT_SECTORS + MAX_INDIRECT_SECTORS){
    //otherwise handle indirect pointer
    pos -= MAX_DIRECT_SECTORS;
    //load sector
    block_sector_t indirect[MAX_INDIRECT_SECTORS];
    block_read(fs_device, d->indirect, indirect);
    return indirect[pos];
  }

  //otherwise double indirect
  pos -= (MAX_DIRECT_SECTORS + MAX_INDIRECT_SECTORS);

  block_sector_t dub_indirect[MAX_INDIRECT_SECTORS];
  block_sector_t indirect[MAX_INDIRECT_SECTORS];

  //raed from double indirect
  block_read(fs_device, d->dub_indirect, dub_indirect);
  //now get proper offsets
  //find what double indirect sector it belongs to
  int dub_sector = pos / MAX_INDIRECT_SECTORS;
  //find what single indirect sector it belongs to
  int ind_sector = pos % MAX_INDIRECT_SECTORS;

  block_read(fs_device, dub_indirect[dub_sector], indirect);

  return indirect[ind_sector];

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

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return get_sector(&inode->data, pos / BLOCK_SECTOR_SIZE);
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

/*function used to extend an inode*/
static bool
extend_inode(struct inode* in, size_t start, size_t num_sectors){
  //declaring arrays for managing indirect / double indirect sectors
  block_sector_t indirect[MAX_INDIRECT_SECTORS];
  block_sector_t dub_indirect[MAX_INDIRECT_SECTORS] = {0};

  block_sector_t* dub_contents[MAX_INDIRECT_SECTORS];

  bool written_indirect = false;
  bool written_double = false;

  //index used to keep relative indexes in respect to indirect pointers
  size_t relative_index = 0;

  //zero bytes used to zero out memory
  uint8_t zeros[BLOCK_SECTOR_SIZE] = {0};

  struct inode_disk* d = &in->data;
  
  for(size_t i = start; i < (start + num_sectors); i++){
    //if in indirect sectors
    if(i < MAX_DIRECT_SECTORS){
      if(!free_map_allocate(1, &d->direct[i]))
        return false;
      block_write(fs_device, d->direct[i], zeros);
    }else if(i < MAX_DIRECT_SECTORS + MAX_INDIRECT_SECTORS){
      //if haven't already given a sector to the indirect pointer
      if(!written_indirect){
        
        //check if indirect already exists
        if(d->indirect == 0){
          //if not, allocate indirect sector for inode and change flag
          if(!free_map_allocate(1, &d->indirect))
            return false;
        }else{
          block_read(fs_device, d->indirect, indirect);
        }
        
        written_indirect = true;
      }
      relative_index = i - MAX_DIRECT_SECTORS;
      //allocate sector within indirect
      if(!free_map_allocate(1, &indirect[relative_index]))
        return false;
      block_write(fs_device, indirect[relative_index], zeros);
    }else{
      //otherwise dealing with double indirect (uh oh)
      if(!written_double){
        //check if double exists
        if(d->dub_indirect == 0){
          if(!free_map_allocate(1, &d->dub_indirect))
            return false;
        }else{
          block_read(fs_device, d->dub_indirect, dub_indirect);
        }
      }
      relative_index = i - (MAX_DIRECT_SECTORS + MAX_INDIRECT_SECTORS);
      size_t dub_sector = relative_index / MAX_INDIRECT_SECTORS;
      size_t ind_sector = relative_index % MAX_INDIRECT_SECTORS;
      
      //essentially go through single indirect for that sector
      if(dub_contents[dub_sector] == NULL){
        //dynamically allocate and check if pointer is null that way don't have to carry around
        //an array of flags for each indirect pointer under double indirect
        dub_contents[dub_sector] = calloc(sizeof(block_sector_t), MAX_INDIRECT_SECTORS);
        if(dub_indirect[dub_sector] == 0){
          if(!free_map_allocate(1, &dub_indirect[dub_sector]))
            return false;
        }else{
          block_read(fs_device, dub_indirect[dub_sector], dub_contents[dub_sector]);
        }
      }

      //allocate as per normal
      if(!free_map_allocate(1, &dub_contents[dub_sector][ind_sector]))
        return false;
      
      block_write(fs_device, dub_contents[dub_sector][ind_sector], zeros);
    }
  }

  //now that done iterating through, check if we need to read back in all indirect memory
  if(written_indirect){
    block_write(fs_device, d->indirect, indirect);
  }

  if(written_double){
    //iterate through all single directs and link them to double indirect
    size_t start_sector = (start - (MAX_DIRECT_SECTORS + MAX_INDIRECT_SECTORS)) / MAX_INDIRECT_SECTORS;
    size_t end_sector = ((start + num_sectors) - (MAX_DIRECT_SECTORS + MAX_INDIRECT_SECTORS)) / MAX_INDIRECT_SECTORS;
    for(size_t i = start_sector; i <= end_sector; i++){
      //free dynamically allocated and write
      block_write(fs_device, dub_indirect[i], dub_contents[i]);
      free(dub_contents[i]);
    }
    //write double indirect to device
    block_write(fs_device, d->dub_indirect, dub_indirect);
  }

  //write entire thing back to disk
  block_write(fs_device, in->sector, d);

  return true;   
}

/*function used to allocate all starting sectors for a disk*/
static
bool allocate_sectors(struct inode_disk* d, size_t num_sectors, block_sector_t write_sector){
  struct inode* dummy = malloc(sizeof(struct inode));
  dummy->data = *d;
  dummy->sector = write_sector;
  bool success = extend_inode(dummy, 0, num_sectors);
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
      success = allocate_sectors(disk_inode, sectors, sector);
      // if (free_map_allocate (sectors, &disk_inode->start)) 
      //   {
      //     block_write (fs_device, sector, disk_inode);
      //     if (sectors > 0) 
      //       {
      //         static char zeros[BLOCK_SECTOR_SIZE];
      //         size_t i;
              
      //         for (i = 0; i < sectors; i++) 
      //           block_write (fs_device, disk_inode->start + i, zeros);
      //       }
      //     success = true; 
      //   } 
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
          // free_map_release (inode->data.start,
          //                   bytes_to_sectors (inode->data.length));
          //iterate through all sectors and release
          for(size_t i = 0; i < bytes_to_sectors(inode->data.length); i++){
            free_map_release(get_sector(&inode->data, i), 1);
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

  //check if inode is getting bigger
  if(size + offset > inode->data.length){
    size_t length = bytes_to_sectors(inode->data.length);
    size_t new_length = bytes_to_sectors(size + offset);
    //check if need to add new sectors
    if(length < new_length){
      //if now there's more sectors, try to extend inode
      if(!extend_inode(inode, length, new_length - length))
      return false;
    }
    //update length
    inode->data.length = size+offset;
    //write again so it remembers the length I HATE THIS
    block_write(fs_device, inode->sector, &inode->data);
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
