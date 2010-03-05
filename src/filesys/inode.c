#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Number of direct blocks */
/* yinfeng ***********************************************/
/* temporarily set to 5, so that we can easily see the effect of extensible
   files and debug, later expand to the largest possible in an inode_disk */
#define NUM_DBLOCK 5

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    //block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    off_t end;                          /* End of the portion of file
                                           actually written in bytes. */
    unsigned magic;                     /* Magic number. */

    block_sector_t blocks[NUM_DBLOCK + 2];
                                        /* Direct blocks and 2 more for 
                                           single, double indirect blocks */

    uint32_t unused[123 - NUM_DBLOCK]; /* Not used. */
  };

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

/* yinfeng ***************************************************/   
    //bool is_dir;
    struct inode_disk* data;            /* Inode content */
/* yinfeng ***************************************************/   
  };


/* yinfeng ***************************************************/
/* Allocate a sector with given BLOCK_CONTENT written */
block_sector_t
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
block_sector_t
allocate_indirect_sector (block_sector_t* block_content, int range)
{
  /* TODO define 128 */
  if (range > BLOCK_SECTOR_SIZE / sizeof (block_sector_t))
    {
      printf ("Allocate range no greater than 128\n");
      return -1;
    }

  /* Allocate and record leaf nodes */
  block_sector_t* indirect_block = calloc (1, BLOCK_SECTOR_SIZE);
  int i = 0;
  for (i = 0; i < range; i++)
    {
      indirect_block[i] = allocate_sector (block_content);
    }

  /* Allocate and write indirect_sector */
  block_sector_t allocated_sector = allocate_sector (indirect_block);

  /* Clean up */
  free (indirect_block);

  return allocated_sector;
}
/* yinfeng ***************************************************/


/* yinfeng ***************************************************/
/* Expand inode, allocate new blocks from previous END all the way to POS
   update END and LENGTH if necessary */
bool
expand_inode (const struct inode* inode, off_t pos)
{
  /* In memory indirect- and double-indirect- block inodes */
  block_sector_t* ind_block  = calloc (1, BLOCK_SECTOR_SIZE);
  if (ind_block == NULL)
    return false;
  block_sector_t* dind_block = calloc (1, BLOCK_SECTOR_SIZE);
  if (dind_block == NULL)
    {
      free (ind_block);
      return false;
    }


  /* Prepare empty block, used to fill newly allocated sectors */
  block_sector_t* empty_block = calloc (1, BLOCK_SECTOR_SIZE);
  if (empty_block == NULL)
    return false;

  int i = 0;
  block_sector_t sec_pos = pos / BLOCK_SECTOR_SIZE;
  block_sector_t end_pos = inode->data->end / BLOCK_SECTOR_SIZE;
  /* TODO determine if pos exceed limit */
  while (end_pos < sec_pos)
    {
      if (end_pos < NUM_DBLOCK - 1)             /* Direct nodes */
        {
          for (i = end_pos + 1; i < NUM_DBLOCK && i <= sec_pos; i++)
            {
              inode->data->blocks[i] = allocate_sector (empty_block);
            }

          /* Update inode end position */
          end_pos = i - 1;
        }
      else if (end_pos < NUM_DBLOCK + 127)      /* Single indirect nodes */
        {
          /* Not yet allocated first level indirect nodes */
          if (inode->data->blocks[NUM_DBLOCK] == 0)
            {
              int range = (sec_pos - NUM_DBLOCK + 1 < 128) ?
                          (sec_pos - NUM_DBLOCK + 1) : 128;

              /* Allocate first level indirect nodes */
              inode->data->blocks[NUM_DBLOCK] =
                allocate_indirect_sector (empty_block, range);

              /* Update inode end position */
              end_pos = NUM_DBLOCK + range - 1;
            }
          else  /* Already allocated first level indirect nodes */
            {
              /* Read in first level indirect nodes */
              cache_read (cache_get (inode->data->blocks[NUM_DBLOCK]),
                          ind_block, 0, BLOCK_SECTOR_SIZE);

              /* Fill data block sectors */
              for (i = end_pos + 1; i < NUM_DBLOCK + 128 && i <= sec_pos; i++)
                {
                  ind_block[i-NUM_DBLOCK] = allocate_sector (empty_block);
                }

              /* Update first level indirect nodes */
              cache_write (cache_get (inode->data->blocks[NUM_DBLOCK]),
                           ind_block, 0, BLOCK_SECTOR_SIZE);

              /* Update inode end position */
              end_pos = i - 1;
            }
        }
      else                                      /* Double indirect nodes */
        {
          /* Two level of index into blocks */
          block_sector_t idx1 = (sec_pos - NUM_DBLOCK) % 128;
          block_sector_t idx2 = (sec_pos - NUM_DBLOCK) / 128 - 1;

          /* If not yet allocated second level indirect nodes */
          if (inode->data->blocks[NUM_DBLOCK + 1] == 0)
            {
              /* Allocate first level indirect nodes necessary, up to idx2-1,
                 all with 128 data blocks */
              for (i = 0; i < idx2; i++)
                {
                  dind_block[i] = allocate_indirect_sector (empty_block, 128);
                }

              /* Allocate second level indirect node at idx2,
                 with idx1 + 1 data blocks */
              dind_block[idx2] =
                allocate_indirect_sector (empty_block, idx1 + 1);

              /* Record the first level indirect node at on-disk inode */
              inode->data->blocks[NUM_DBLOCK + 1] = allocate_sector (dind_block);

              /* Update inode end position */
              end_pos = sec_pos;
            }
          else  /* Already allocated second level indirect nodes */
            {
              /* Read first level indirect nodes */
              cache_read (cache_get (inode->data->blocks[NUM_DBLOCK + 1]),
                          dind_block, 0, BLOCK_SECTOR_SIZE);

              /* Allocate first level indirect nodes, up to idx2 - 1,
                 if not yet allocated
                 all with 128 data blocks */
              int idx = 0;
              for (i = 0; i < idx2; i++)
                {
                  if (dind_block[i] == 0)
                    {
                      dind_block[i] =
                        allocate_indirect_sector (empty_block, 128);
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
                  for (j = 0; j < 128; j++)
                    {
                      if (ind_block[j] == 0)
                        {
                          ind_block[j] =
                            allocate_sector (empty_block);
                        }
                    }
                  cache_write (cache_get (dind_block[idx], ind_block,
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

                /* Update inode end position */
                end_pos = sec_pos;
            }
        }
    }

  /* Actually record updated inode end position */
  inode->data->end = pos;

  /* Update length if end exceed previous file length */
  if (inode->data->length < inode->data->end)
    inode->data->length = inode->data->end;

  /* Write the updated inode info back to disk */
  cache_write (cache_get (inode->sector), inode->data, 0 , BLOCK_SECTOR_SIZE);

  /* Clean up */
  free (ind_block);
  free (dind_block);
  free (empty_block);

  return true;
}
/* yinfeng ***************************************************/

/* yinfeng ***************************************************/
/* Returns the block device sector that contains byte offset POS within INODE.
   First expand the inode as indicated by ENABLE_EXPAND
   Then lookup in the inode */
block_sector_t
byte_to_sector (const struct inode *inode, off_t pos, bool enable_expand)
{
  ASSERT (inode != NULL);

  // read data

  /* Expand the file if necessary
     also updated LENGTH and END in inode for following lookups */
  if (pos > inode->data->end)
    {
      if (enable_expand)
        {
          // free data
          expand_inode (inode, pos);
          // read again data
        }
      else
        return -1;
    }


 /* By now we have expanded file if necessary,
    and it is safe to query all positions before inode->end */
    block_sector_t result = -1;
    block_sector_t sec_pos = pos / BLOCK_SECTOR_SIZE;

    if (sec_pos < NUM_DBLOCK)            /* From direct blocks */
      {
        /* Get result sector number */
        result = inode->data->blocks[sec_pos];
      }
    else if (sec_pos < NUM_DBLOCK + 128) /* From single indirect blocks */
      {
        /* Read indirect blocks */
        block_sector_t* ind_inode = calloc (1, BLOCK_SECTOR_SIZE);
        if (ind_inode == NULL)
          return -1;
        cache_read (cache_get (inode->data->blocks[NUM_DBLOCK]),
                    ind_inode, 0, BLOCK_SECTOR_SIZE);

        /* Get result sector number */
        result = ind_inode[sec_pos - NUM_DBLOCK];

        /* Clean up */
        free (ind_inode);
      }
    else                                 /* From double indirect blocks */
      {
         /* Read indirect and double indirect blocks */
        block_sector_t* ind_inode = calloc (1, BLOCK_SECTOR_SIZE);
        if (ind_inode == NULL)
          return -1;
        block_sector_t* dind_inode = calloc (1, BLOCK_SECTOR_SIZE);
        if (dind_inode == NULL)
          {
            free (ind_inode);
            return -1;
          }

        cache_read (cache_get (inode->data->blocks[NUM_DBLOCK + 1]),
                     dind_inode, 0, BLOCK_SECTOR_SIZE);

        /* Compute the double indirect block needed */
        block_sector_t idx1 = (sec_pos - NUM_DBLOCK) %128;
        block_sector_t idx2 = (sec_pos - NUM_DBLOCK) / 128 - 1;

        cache_read (cache_get (dind_inode[idx2]),
                    ind_inode, 0, BLOCK_SECTOR_SIZE);

        /* Get result sector number */
        result = ind_inode[idx1];

        /* Clean up */
        free (ind_inode);
        free (dind_inode);
      }

    return result;
}
/* yinfeng ***************************************************/

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

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
/* yinfeng **********************************************************/
      /* Write only the metadata block
         and actual end of the file is initalized to 0
         other blocks are only write when later writes are past end */
      disk_inode->length = length;
      disk_inode->end = -1;
      disk_inode->magic = INODE_MAGIC;
      cache_write (cache_get (sector), disk_inode, 0, BLOCK_SECTOR_SIZE);
      success = true;
      free (disk_inode);
/* yinfeng **********************************************************/
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

//  inode->is_dir = false;
  //inode->start = -1; // change
  //inode->length = -1; // change
/*********************************************************************/
  /* Now we have to allocate memory for inode->data,
     and record this in inode */
  struct inode_disk* data = calloc (1, BLOCK_SECTOR_SIZE);
  cache_read (cache_get (inode->sector), data, 0, BLOCK_SECTOR_SIZE);
  inode->data = data;
//  block_read (fs_device, inode->sector, &inode->data);
/*********************************************************************/
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

  /* Write content back to disk */
  cache_flush ();

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
/* yinfeng *************************************************************/
          /* Find sectors associated with this inode,
             and release free-map accordingly */
          block_sector_t* ind_block = NULL;
          block_sector_t* dind_block = NULL;

          //ind_block = calloc (1, BLOCK_SECTOR_SIZE);
          //cache_read (cache_get (inode->sector),
                      //ind_block, 0, BLOCK_SECTOR_SIZE);
                      /*TODO */

          int i = 0;
          block_sector_t end_pos = inode->data->end / BLOCK_SECTOR_SIZE;
          for (i = 0; i <= end_pos; i++)
            {
              if (i < NUM_DBLOCK)
                {
                  free_map_release (inode->data->blocks[i], 1);
                }
              else if (i < NUM_DBLOCK + 128)
                {
                  if (ind_block == NULL)
                    {
                      ind_block = calloc (1, BLOCK_SECTOR_SIZE);
                      /* TODO check out of memory
                         use goto Done to free all resource */
                      cache_read (cache_get (inode->data->blocks[NUM_DBLOCK]),
                                  ind_block, 0, BLOCK_SECTOR_SIZE);
                    }

                  free_map_release (ind_block[i - NUM_DBLOCK], 1);

                  /* TODO free whatever */
                  if (i == NUM_DBLOCK + 127)
                    {
                      free_map_release (inode->data->blocks[NUM_DBLOCK], 1);
                    }
                }
              else
                {
                  if (dind_block == NULL)
                    {
                      dind_block = calloc (1, BLOCK_SECTOR_SIZE);
                      cache_read (cache_get (inode->data->blocks[NUM_DBLOCK + 1]),
                                  dind_block, 0, BLOCK_SECTOR_SIZE);
                    }

                  block_sector_t idx1 = (i - NUM_DBLOCK) % 128;
                  block_sector_t idx2 = (i - NUM_DBLOCK) / 128 - 1;
                  cache_read (cache_get (dind_block[idx2]),
                              ind_block, 0, BLOCK_SECTOR_SIZE);

                  free_map_release (ind_block[idx1], 1);

                  if (idx1 == 127)
                    {
                      free_map_release (dind_block[idx1], 1);
                    }
                }
            }

          free_map_release (inode->sector, 1);

          if (ind_block != NULL)
            free (ind_block);
          if (dind_block != NULL)
            free (dind_block);

          //free_map_release (inode->data->start,
          //                  bytes_to_sectors (inode->data->length));
          //free_map_release (inode->start, // change
          //                  bytes_to_sectors (inode->length));  // change
/* yinfeng *************************************************************/
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
  //uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset, false);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

/*********************************************************************/
      cache_read (cache_get (sector_idx), buffer + bytes_read,
                  sector_ofs, chunk_size);
/*********************************************************************/
//     if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
//        {
//          /* Read full sector directly into caller's buffer. */
//        block_read (fs_device, sector_idx, buffer + bytes_read);
//        }
//      else 
//        {
//          /* Read sector into bounce buffer, then partially copy
//             into caller's buffer. */
//         if (bounce == NULL) 
//            {
//              bounce = malloc (BLOCK_SECTOR_SIZE);
//              if (bounce == NULL)
//                break;
//            }
//          block_read (fs_device, sector_idx, bounce);
//          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
//        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  //free (bounce);

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
  //uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
  {
//    printf ("DENY write **************\n");
    return 0;
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset, true);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
/*********************************************************************/
      cache_write (cache_get (sector_idx), (uint8_t *)buffer + bytes_written,
                   sector_ofs, chunk_size);
/*********************************************************************/
//      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
//        {
//          /* Write full sector directly to disk. */
//          block_write (fs_device, sector_idx, buffer + bytes_written);
//        }
//      else 
//        {
//          /* We need a bounce buffer. */
//          if (bounce == NULL) 
//            {
//              bounce = malloc (BLOCK_SECTOR_SIZE);
//              if (bounce == NULL)
//                break;
//            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
//          if (sector_ofs > 0 || chunk_size < sector_left) 
//            block_read (fs_device, sector_idx, bounce);
//          else
//            memset (bounce, 0, BLOCK_SECTOR_SIZE);
//          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
//          block_write (fs_device, sector_idx, bounce);
//        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  //free (bounce);
//  printf ("bytes_written = %ld\n", bytes_written);
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
  return inode->data->length;
}
