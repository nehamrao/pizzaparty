#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "threads/thread.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Number of direct blocks */
#define NUM_DBLOCK 122

/* Max block_sector number */
#define MAX_NUM_RECORD (BLOCK_SECTOR_SIZE / sizeof (block_sector_t))

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */   
    off_t end;                          /* End of the portion of file
                                           actually written in bytes. */
    bool isdir;				/* Directory indicator */
    unsigned magic;                     /* Magic number. */

    block_sector_t blocks[NUM_DBLOCK + 2];
                                        /* Direct blocks and 2 more for 
                                           single, double indirect blocks */
    uint32_t unused[122 - NUM_DBLOCK];  /* Not used. */
  };

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    struct lock dir_lock;               /* Lock to prevent race conditions in directory operations */
    struct lock expand_lock;            /* Lock to prevent race conditions in inode expansion */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */ 
  };

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

static bool expand_inode (const struct inode* inode, off_t pos);
static block_sector_t allocate_indirect_sector (block_sector_t* block_content, int range);
static block_sector_t allocate_sector (block_sector_t* block_content);
static block_sector_t byte_to_sector (const struct inode *inode, off_t pos, bool enable_expand);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Allocate a sector with given BLOCK_CONTENT written */
static block_sector_t
allocate_sector (block_sector_t* block_content)
{
  block_sector_t allocated_sector = -1;

  /* Allocate sector and write content */
  if (free_map_allocate (1, &allocated_sector))
    {
      cache_write (cache_get (allocated_sector), block_content,
                   0, BLOCK_SECTOR_SIZE);
    }

  return allocated_sector;
}

/* Allocate an indirect sector, containing RANGE number of leaf sectors
   each written with given BLOCK_CONTENT */
static block_sector_t
allocate_indirect_sector (block_sector_t* block_content, int range)
{
  /* If range exceeds the max number of records, return false */
  if (range > MAX_NUM_RECORD)
    return -1;

  /* Allocate and record leaf nodes */
  block_sector_t* indirect_block = calloc (1, BLOCK_SECTOR_SIZE);
  int i;
  for (i = 0; i < range; i++)
    indirect_block[i] = allocate_sector (block_content);

  /* Allocate and write indirect_sector */
  block_sector_t sector_no = allocate_sector (indirect_block);

  /* Clean up */
  free (indirect_block);
  return sector_no;
}

/* Expand inode, allocate new blocks from previous END all the way to POS
   update END and LENGTH if necessary */
static bool
expand_inode (const struct inode* inode, off_t pos)
{
  block_sector_t *dind_block = NULL, *ind_block = NULL, *empty_block = NULL;
  struct inode_disk *meta_block = NULL;
  bool success = false;
  int i;

  /* Read in metadata block inodes */
  meta_block = calloc (1, BLOCK_SECTOR_SIZE);
  if (meta_block == NULL)
    goto done;
  cache_read (cache_get (inode->sector), meta_block,
              0, BLOCK_SECTOR_SIZE);
  
  /* In memory indirect- and double-indirect- block inodes */
  ind_block  = calloc (1, BLOCK_SECTOR_SIZE);
  if (ind_block == NULL)
    goto done;

  dind_block = calloc (1, BLOCK_SECTOR_SIZE);
  if (dind_block == NULL)
    goto done;

  /* Prepare empty block, used to fill newly allocated sectors */
  empty_block = calloc (1, BLOCK_SECTOR_SIZE);
  if (empty_block == NULL)
    goto done;

  /* Calculate the sector offset of POS and END */
  int sec_pos = pos / BLOCK_SECTOR_SIZE;
  int sec_end;
  if (meta_block->end < 0)
    sec_end = -1;
  else 
    sec_end = meta_block->end / BLOCK_SECTOR_SIZE;

  /* Judge if the length of the position exceeds the maximum capacity */
  if (sec_pos >= NUM_DBLOCK + MAX_NUM_RECORD + MAX_NUM_RECORD * MAX_NUM_RECORD)
    goto done;

  /* Begin expansion */
  while (sec_end < sec_pos)
    {
      /* Allocate blocks in direct nodes */
      if (sec_end < NUM_DBLOCK - 1)             
        {
          for (i = sec_end + 1; i < NUM_DBLOCK && i <= sec_pos; i++)
            {
              meta_block->blocks[i] = allocate_sector (empty_block);
            }
          sec_end = i - 1;
        }
      /* Allocate blocks in first level indirect nodes */
      else if (sec_end < NUM_DBLOCK + MAX_NUM_RECORD - 1)      
        {
          /* Not yet allocated first level indirect nodes */
          if (meta_block->blocks[NUM_DBLOCK] == 0)
            {
              int range = (sec_pos - NUM_DBLOCK + 1 < MAX_NUM_RECORD) ?
                          (sec_pos - NUM_DBLOCK + 1) : MAX_NUM_RECORD;

              /* Allocate first level indirect nodes */
              meta_block->blocks[NUM_DBLOCK] =
                allocate_indirect_sector (empty_block, range);

              /* Update inode end position */
              sec_end = NUM_DBLOCK + range - 1;
            }
          else  /* Already allocated first level indirect nodes */
            {
              /* Read in first level indirect nodes */
              cache_read (cache_get (meta_block->blocks[NUM_DBLOCK]),
                          ind_block, 0, BLOCK_SECTOR_SIZE);

              /* Fill data block sectors */
              for (i = sec_end + 1; 
                   i < NUM_DBLOCK + MAX_NUM_RECORD && i <= sec_pos; i++)
                {
                  ind_block[i - NUM_DBLOCK] = allocate_sector (empty_block);
                }

              /* Update first level indirect nodes */
              cache_write (cache_get (meta_block->blocks[NUM_DBLOCK]),
                           ind_block, 0, BLOCK_SECTOR_SIZE);

              /* Update inode end position */
              sec_end = i - 1;
            }
        }
      /* Allocate blocks in second level indirect nodes */
      else                                     
        {
          /* Two level of index into blocks */
          int idx1 = (sec_pos - NUM_DBLOCK) % MAX_NUM_RECORD;
          int idx2 = (sec_pos - NUM_DBLOCK) / MAX_NUM_RECORD - 1;

          /* If not yet allocated second level indirect nodes */
          if (meta_block->blocks[NUM_DBLOCK + 1] == 0)
            {
              /* Allocate first level indirect nodes necessary, up to idx2-1,
                 all with MAX_NUM_RECORD data blocks */
              for (i = 0; i < idx2; i++)
                {
                  dind_block[i] = allocate_indirect_sector (empty_block, MAX_NUM_RECORD);
                }

              /* Allocate second level indirect node at idx2,
                 with idx1 + 1 data blocks */
              dind_block[idx2] =
                allocate_indirect_sector (empty_block, idx1 + 1);

              /* Record the first level indirect node at on-disk inode */
              meta_block->blocks[NUM_DBLOCK + 1] = allocate_sector (dind_block);

              /* Update inode end position */
              sec_end = sec_pos;
            }
          else  /* Already allocated second level indirect nodes */
            {
              /* Read first level indirect nodes */
              cache_read (cache_get (meta_block->blocks[NUM_DBLOCK + 1]),
                          dind_block, 0, BLOCK_SECTOR_SIZE);

              /* Allocate first level indirect nodes, up to idx2 - 1,
                 if not yet allocated
                 all with MAX_NUM_RECORD data blocks */
              int idx = 0;
              for (i = 0; i < idx2; i++)
                {
                  if (dind_block[i] == 0)
                    {
                      dind_block[i] =
                        allocate_indirect_sector (empty_block, MAX_NUM_RECORD);
                    }
                  else
                    {
                      idx = i;
                    }
                }
              if (idx != idx2)
                {
                  cache_read (cache_get (dind_block[idx]), ind_block,
                              0, BLOCK_SECTOR_SIZE);
                  int j = 0;
                  for (j = 0; j < MAX_NUM_RECORD; j++)
                    {
                      if (ind_block[j] == 0)
                        {
                          ind_block[j] =
                            allocate_sector (empty_block);
                        }
                    }
                  cache_write (cache_get (dind_block[idx]), ind_block,
                               0, BLOCK_SECTOR_SIZE);
                }

              /* Allocate first level indirect nodes at idx2,
                 if not yet allocated
                 with idx1 + 1 data blocks */
              if (dind_block[idx2] == 0)
                {
                  dind_block[idx2] =
                        allocate_indirect_sector (empty_block, idx1 + 1);
                }
              else  /* Already allocated first level indirect node */
                {
                  /* Read first level indirect node */
                  cache_read (cache_get (dind_block[idx2]),
                              ind_block, 0, BLOCK_SECTOR_SIZE);

                  /* Record newly allocated sector */
                  for (i = 0; i < idx1 + 1; i++)
                    {
                      if (ind_block[i] == 0)
                        {
                          ind_block[i] = allocate_sector (empty_block);
                        }
                    }

                  /* Update first level indirect nodes */
                  cache_write (cache_get (dind_block[idx2]),
                               ind_block, 0, BLOCK_SECTOR_SIZE);
                }

                cache_write (cache_get (meta_block->blocks[NUM_DBLOCK + 1]),
                           dind_block, 0, BLOCK_SECTOR_SIZE);
              /* Update inode end position */
              sec_end = sec_pos;
            }
        }
    }

  /* Actually record updated inode end position */
  meta_block->end = pos;

  /* Update length if end exceed previous file length */
  if (meta_block->length < meta_block->end)
    meta_block->length = meta_block->end;

  /* Write the updated inode info back to disk */
  cache_write (cache_get (inode->sector), meta_block, 0 , BLOCK_SECTOR_SIZE);
  success = true;

done:
  /* Clean up */
  if (meta_block != NULL)
    free (meta_block);
  if (ind_block != NULL)
    free (ind_block);
  if (dind_block != NULL)
    free (dind_block);
  if (empty_block != NULL)
    free (empty_block);

  return success;
}

/* Returns the block device sector that contains byte offset POS within INODE.
   First expand the inode as indicated by ENABLE_EXPAND
   Then lookup in the inode */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos, bool enable_expand)
{
  block_sector_t *dind_block = NULL, *ind_block = NULL, *empty_block = NULL;
  struct inode_disk* meta_block = NULL;

  ASSERT (inode != NULL);

  /* In memory meta-block inode */
  meta_block = calloc (1, BLOCK_SECTOR_SIZE);
  if (meta_block == NULL)
    return -1;
  cache_read (cache_get (inode->sector), meta_block,
              0, BLOCK_SECTOR_SIZE);

  /* Calculate the sector offset of POS and END */
  int sec_pos = pos / BLOCK_SECTOR_SIZE;
  int sec_end;
  if (meta_block->end < 0)
    sec_end = -1;
  else 
    sec_end = meta_block->end / BLOCK_SECTOR_SIZE;

  /* Expand the file if necessary
     also updated LENGTH and END in inode for following lookups */
  if (sec_pos > sec_end)
    {
      if (enable_expand)
        {
          /* Expand inode */
          lock_acquire (&inode->expand_lock);
          expand_inode (inode, pos);
          lock_release (&inode->expand_lock);

          /* read updated metadata from disk */
          cache_read (cache_get (inode->sector), meta_block,
                      0, BLOCK_SECTOR_SIZE);
        }
      else
        {
         /* If read past current allocated blocks, return -2 */
          free (meta_block);
          return -2;
        }
    }

    /* By now we have expanded file if necessary,
       and it is safe to query all positions before inode->end */
    int result = -1;

    if (sec_pos < NUM_DBLOCK)            /* From direct blocks */
      {
        result = meta_block->blocks[sec_pos];
      }
    else if (sec_pos < NUM_DBLOCK + MAX_NUM_RECORD) /* From single indirect blocks */
      {
        /* Read indirect blocks */
        ind_block = calloc (1, BLOCK_SECTOR_SIZE);
        if (ind_block == NULL)
          goto done;

        cache_read (cache_get (meta_block->blocks[NUM_DBLOCK]),
                    ind_block, 0, BLOCK_SECTOR_SIZE);

        /* Get result sector number */
        result = ind_block[sec_pos - NUM_DBLOCK];
      }
    else                                 /* From double indirect blocks */
      {
         /* Read indirect and double indirect blocks */
        ind_block = calloc (1, BLOCK_SECTOR_SIZE);
        if (ind_block == NULL)
          goto done;

        dind_block = calloc (1, BLOCK_SECTOR_SIZE);
        if (dind_block == NULL)
          goto done;

        cache_read (cache_get (meta_block->blocks[NUM_DBLOCK + 1]),
                    dind_block, 0, BLOCK_SECTOR_SIZE);

        /* Compute the double indirect block needed */
        block_sector_t idx1 = (sec_pos - NUM_DBLOCK) % MAX_NUM_RECORD;
        block_sector_t idx2 = (sec_pos - NUM_DBLOCK) / MAX_NUM_RECORD - 1;

        cache_read (cache_get (dind_block[idx2]),
                    ind_block, 0, BLOCK_SECTOR_SIZE);

        /* Get result sector number */
        result = ind_block[idx1];
      }

done:
  /* Clean up */
  if (meta_block != NULL)
    free (meta_block);
  if (ind_block != NULL)
    free (ind_block);
  if (dind_block != NULL)
    free (dind_block);
  if (empty_block != NULL)
    free (empty_block);

  return result;
}

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool isdir)
{
  struct inode_disk *disk_inode = NULL;
  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode == NULL)
    return false;

  /* Write only the metadata block
     and actual end of the file is initalized to 0
     other blocks are only write when later writes are past end */
  disk_inode->length = length;
  disk_inode->end = -1;
  disk_inode->magic = INODE_MAGIC;
  disk_inode->isdir = isdir;
  cache_write (cache_get (sector), disk_inode, 0, BLOCK_SECTOR_SIZE);
  free (disk_inode);
  return true;
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
  lock_init (&inode->dir_lock);
  lock_init (&inode->expand_lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
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
          /* In memory meta-block */
          struct inode_disk* meta_block = calloc (1, BLOCK_SECTOR_SIZE);
          if (meta_block == NULL)
            goto DONE;

          /* Find sectors associated with this inode,
             and release free-map accordingly */
          block_sector_t* ind_block = NULL;
          block_sector_t* dind_block = NULL;

          int i = 0;
          int j = 0;
          cache_read (cache_get (inode->sector),
                      meta_block, 0, BLOCK_SECTOR_SIZE);
          
          int sec_end;
          if (meta_block->end < 0)
            sec_end = -1;
          else 
            sec_end = meta_block->end / BLOCK_SECTOR_SIZE;

          /* Release direct nodes */
          for (i = 0; i < NUM_DBLOCK && i <= sec_end; i++)
            {
              free_map_release (meta_block->blocks[i], 1);
            }
          if (i == sec_end + 1)
            goto DONE;

          /* Release single indirect nodes */
          ind_block = calloc (1, BLOCK_SECTOR_SIZE);
          if (ind_block == NULL)
            goto DONE;

          cache_read (cache_get (meta_block->blocks[NUM_DBLOCK]),
                      ind_block, 0, BLOCK_SECTOR_SIZE);
          for (i = NUM_DBLOCK; i < NUM_DBLOCK + MAX_NUM_RECORD && i <= sec_end; i++)
            {
              free_map_release (ind_block[i - NUM_DBLOCK], 1);
            }
          free_map_release (meta_block->blocks[NUM_DBLOCK], 1);
          if (i == sec_end + 1)
            goto DONE;

          /* Release double indirect nodes */
          dind_block = calloc (1, BLOCK_SECTOR_SIZE);
          if (dind_block == NULL)
            goto DONE;
          cache_read (cache_get (meta_block->blocks[NUM_DBLOCK + 1]),
                      dind_block, 0, BLOCK_SECTOR_SIZE);
          for (j = 0; j < MAX_NUM_RECORD; j++)
            {
              if (dind_block[j] != 0)
                {
                  cache_read (cache_get (dind_block[j]),
                              ind_block, 0, BLOCK_SECTOR_SIZE);
                  for (i = 0; i < MAX_NUM_RECORD; i++)
                    {
                      if (ind_block[i] != 0)
                        {
                          free_map_release (ind_block[i], 1);
                        }
                    }
                  free_map_release (dind_block[j], 1);
                }
            }

          /* Release metadata node */
          free_map_release (inode->sector, 1);

DONE:
          if (meta_block != NULL)
            free (meta_block);
          if (ind_block != NULL)
            free (ind_block);
          if (dind_block != NULL)
            free (dind_block);
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

  if (offset >= inode_length (inode))
    return 0;

  int sector_idx = byte_to_sector (inode, offset, false);
  int sector_idx_next;
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      if (sector_idx == -1)
      {
        break;
      } 

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      sector_idx_next = byte_to_sector (inode, offset + chunk_size, false);

      /* If read past current allocated blocks, fill the buffer with 0;
         otherwise read data from cache and write to buffer */
      if (sector_idx == -2)
      {
        memset (buffer + bytes_read, 0, chunk_size);
      } 
      else 
      {
        bool enable_read_ahead = cache_read (cache_get (sector_idx), 
                                 buffer + bytes_read, sector_ofs, chunk_size);
        if ((sector_idx_next > 0) && (inode->sector > 1) && enable_read_ahead)
        {
           struct read_struct *rs = malloc (sizeof (struct read_struct));
           rs->sector = sector_idx_next;
           list_push_back (&read_ahead_list, &rs->elem);
        }
      }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
      sector_idx = sector_idx_next;
    }

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

  if (inode->deny_write_cnt)
  {
    return 0;
  }

  if (inode == NULL)
  {
    return 0;
  }
   
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset, true);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = BLOCK_SECTOR_SIZE;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
      {
        break;
      }
      cache_write (cache_get (sector_idx), (uint8_t *)buffer + bytes_written,
                   sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  /* Update inode length */
  struct inode_disk* meta_block = calloc (1, BLOCK_SECTOR_SIZE);
  if (meta_block == NULL)
    return -1;

  /* Extend the length of inode to the last byte written */
  cache_read (cache_get (inode->sector), meta_block,
              0, BLOCK_SECTOR_SIZE);
  meta_block->length = meta_block->length > offset + size ?
                       meta_block->length : offset + size;
  meta_block->end = meta_block->end > offset + size - 1 ?
                    meta_block->end : offset + size - 1;
  cache_write (cache_get (inode->sector), meta_block,
               0, BLOCK_SECTOR_SIZE);

  free (meta_block);
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
  /* In memory meta-block */
  struct inode_disk* meta_block = calloc (1, BLOCK_SECTOR_SIZE);
  if (meta_block == NULL)
    return -1;

  cache_read (cache_get (inode->sector), meta_block,
              0, BLOCK_SECTOR_SIZE);

  off_t result_length = meta_block->length;

  free (meta_block);

  return result_length;
}

/* TRUE if inode is a directory inode, FALSE otherwise */
bool
inode_isdir (const struct inode *inode)
{
  struct inode_disk* meta_block = calloc (1, BLOCK_SECTOR_SIZE);
  if (meta_block == NULL)
    return false;

  cache_read (cache_get (inode->sector), meta_block,
              0, BLOCK_SECTOR_SIZE);

  bool result_isdir = meta_block->isdir;

  free (meta_block);

  return result_isdir;

}

/* Return the open count of inode */
int
inode_isopen (const struct inode * inode)
{
  return inode->open_cnt; 
}

struct lock * 
inode_getlock (struct inode *inode)
{
  return &inode->dir_lock;
}


