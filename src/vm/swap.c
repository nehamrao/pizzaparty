#include <string.h>
#include <bitmap.h>
#include <stdio.h>
#include <debug.h>
#include "swap.h"
#include "frame.h"
#include "devices/block.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "filesys/free-map.h"
#include "userprog/pagedir.h"


struct block *sp_device;
static struct bitmap *swap_free_map;
struct lock swap_set_lock;

/* Initialize swap device and swap table */
void swap_init ()
{
  /*swap device pointer */
  sp_device = block_get_role (BLOCK_SWAP);
  lock_init (&swap_set_lock);
  /* Bitmap for swap */
  swap_free_map = bitmap_create (block_size (sp_device));
  if (swap_free_map == NULL)
  {
     PANIC ("Out of kernel memory pool\n");
  }
}

void swap_free (uint32_t * pte)
{
   
   struct page_struct * ps = sup_pt_ps_lookup (pte);

   
   if (ps != NULL && (ps->fs->flag&POSBITS) == POS_SWAP )
   {  
      lock_acquire (&swap_set_lock);
      bitmap_set_multiple (swap_free_map, ps->fs->sector_no, PGSIZE / BLOCK_SECTOR_SIZE, false);
       lock_release (&swap_set_lock);
   }
   return;

}

/* TODO need better comment swap in */
bool swap_in (struct frame_struct *pframe)
{
  struct block *device;
  size_t length = pframe->length;
  block_sector_t sector_no = pframe->sector_no;
  if (sector_no == SECTOR_ERROR)
  {
     /* Sector number invalid */
     return false;
  }

  /* Get a frame, from memory or by evict another frame */
  uint8_t *kpage = palloc_get_page (PAL_USER | PAL_ZERO);

  if (kpage == NULL)
  {
    /* Evict to get a frame */
   // lock_acquire (&swap_set_lock);
    kpage = sup_pt_evict_frame ();
   //  lock_release (&swap_set_lock);
    if (kpage == NULL)
    {
      PANIC ("Out of swap space!\n");
      return false;
    }
  }
  
  uint32_t pos = pframe->flag & POSBITS;
  uint32_t is_all_zero = pframe->flag & FS_ZERO;
   
  /* If zero page, just write a page of 0's */
  if (is_all_zero)
  {
    memset (kpage, 0, PGSIZE);
    sup_pt_set_swap_in (pframe, kpage);
    return true;
  } 

  /* On disk */
  if (pos == POS_DISK)
  {
    device = fs_device;
  }
  /* On swap */
  else if (pos == POS_SWAP)
  {
    device = sp_device;
  }
  else
  {
    PANIC ("Eviction error, flag not set.\n");
    palloc_free_page (kpage);
    return false;
  }
  

  /* Read from disk or swap */
  block_sector_t i;

  for (i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++)
  {
    block_read (device, sector_no + i, kpage + BLOCK_SECTOR_SIZE * i); 
  }

  /* Set remaining of the page to 0, only necessary for disk */
  if ((length < PGSIZE) && (device == fs_device))
    memset (kpage + length, 0, PGSIZE - length);
  
  /* Free swap table entries */
  if (device == sp_device)
   {
    lock_acquire (&swap_set_lock);
    bitmap_set_multiple (swap_free_map, sector_no, PGSIZE / BLOCK_SECTOR_SIZE, false);
    lock_release (&swap_set_lock);
   }
  /* Update sup_pt entry information */
  sup_pt_set_swap_in (pframe, kpage);              

  return true;
}

/* TODO need better comment swap out */
bool swap_out (struct frame_struct *pframe)
{  
  struct block *device;
  block_sector_t sector_no;
  uint8_t *kpage = pframe->vaddr;
  if (kpage == NULL)
  {
    /* Virtual address invalid */
    return false;
  }  

  uint32_t type = pframe->flag & TYPEBITS;
  uint32_t dirty = sup_pt_fs_is_dirty (pframe);
  uint32_t is_all_zero = pframe->flag & FS_ZERO;

  ASSERT (pframe->flag & POSBITS == POS_MEM);

  /* Zero and not dirty page need not swap out */
  if (is_all_zero && !dirty)
  {
    pframe->flag = (pframe->flag & POSMASK) | POS_DISK;
    goto done;
  }
  else 
  {
    pframe->flag &= ~FS_ZERO;
  }

  if (type == TYPE_Stack)
  {
    device = sp_device;
    lock_acquire (&swap_set_lock);
    sector_no = bitmap_scan_and_flip (swap_free_map, 0,
                                      PGSIZE / BLOCK_SECTOR_SIZE, false);
    lock_release (&swap_set_lock);
    sup_pt_set_swap_out (pframe, sector_no, false); 
    goto write;
  }
    
  /* Write memory mapped file to disk */
  if (type == TYPE_MMFile)
  {
    device = fs_device;
    sector_no = pframe->sector_no;    
    sup_pt_set_swap_out (pframe, pframe->sector_no, true); 
    if (dirty)
      goto write;
  }
  /* */
  if (type == TYPE_Executable) 
  {
    if (dirty)
    {
      device = sp_device;
      lock_acquire (&swap_set_lock);
      sector_no = bitmap_scan_and_flip (swap_free_map, 0,
                                        PGSIZE / BLOCK_SECTOR_SIZE, false);
      lock_release (&swap_set_lock);

      sup_pt_set_swap_out (pframe, sector_no, false); 
      goto write;
    } else
    {
      sup_pt_set_swap_out (pframe, pframe->sector_no, true); 
      goto done;
    }
  }
  ASSERT ("Reach places which should never be reached"); 

write:
  /* Out of swap space */
  if (sector_no == SECTOR_ERROR)
    return false;

  /* Write to disk or swap device */
  int i = 0;
  for (i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++)
  {
    block_write (device, sector_no + i, kpage + BLOCK_SECTOR_SIZE * i); 
  }
  return true;

done:
  return true;
}

