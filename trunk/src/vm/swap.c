#include <string.h>
#include <bitmap.h>
#include <stdio.h>
#include <debug.h>
#include "swap.h"
#include "frame.h"
#include "devices/block.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "filesys/free-map.h"
#include "userprog/pagedir.h"

struct block *sp_device;
static struct bitmap *swap_free_map;

/* Initialize swap device and swap table */
void swap_init ()
{
  /*swap device pointer */
  sp_device = block_get_role (BLOCK_SWAP);

  /* Bitmap for swap */
  swap_free_map = bitmap_create (block_size (sp_device));
  if (swap_free_map == NULL)
  {
     PANIC ("Out of kernel memory pool\n");
  }
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
  uint32_t *kpage = palloc_get_page (PAL_USER);
  if (kpage == NULL)
  {
    /* Evict to get a frame */
    kpage = sup_pt_evict_frame ();
    if (kpage == NULL)
    {
      PANIC ("Out of swap space!\n");
      return false;
    }
  }
 
  /*if ((pframe->flag & POS_MEM)||(pframe->flag&POS_EXEC)) Need change
  {
    device = fs_device;
  }*/
   
  /* If zero page, just write a page of 0's */
  if ((pframe->flag & POSBITS) == POS_ZERO)
  {
    memset (kpage, 0, PGSIZE);
    return true;
  } 

  /* On disk */
  if ((pframe->flag & POSBITS) == POS_DISK)
  {
    device = fs_device;
  }
  /* On swap */
  else if ((pframe->flag & POSBITS) == POS_SWAP)
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
  int i;
  for (i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++)
  {
    block_read (device, sector_no + i, kpage + BLOCK_SECTOR_SIZE * i); 
  }

  /* Set remaining of the page to 0, only necessary for disk */
  if ((length < PGSIZE) && (device == fs_device))
    memset (kpage + length, 0, PGSIZE - length);
  
  /* Free swap table entries */
  if (device == sp_device)
    bitmap_set_multiple (swap_free_map, sector_no, PGSIZE / BLOCK_SECTOR_SIZE, false);

  /* Update sup_pt entry information */
  sup_pt_set_swap_in (pframe, kpage);              

  return true;
}

/* TODO need better comment swap out */
bool swap_out (struct frame_struct *pframe)
{  
  struct block *device;
  block_sector_t sector_no;
  uint32_t *kpage = pframe->vaddr;
  if (kpage == NULL)
  {
    /* Virtual address invalid */
    return false;
  }  

  /* Zero and not dirty page need not swap out */
  if ((pframe->flag & POSBITS) == POS_ZERO )
    if ((pframe->flag & FS_DIRTY) == 0)
      goto done;

  /* Write memory mapped file to disk */
  if ((pframe->flag & TYPE_MMFile) &&
      (pframe->flag & POSBITS) == POS_MEM)
  {
    device = fs_device;
    sector_no = pframe->sector_no;
    
    sup_pt_set_swap_out (pframe, pframe->sector_no, true); 

    goto write;
  }
  /* Write dirty or stack pages to swap */
  else if ((pframe->flag & FS_DIRTY) ||
           (pframe->flag & TYPEBITS) == TYPE_Stack)
  {
    device = sp_device;
    sector_no = bitmap_scan_and_flip (swap_free_map, 0,
                                      PGSIZE / BLOCK_SECTOR_SIZE, false);

    sup_pt_set_swap_out (pframe, sector_no, false); 

    goto write;
  }
  /* The same content as on disk, no need to write */
  else
  {
    sup_pt_set_swap_out (pframe, pframe->sector_no, true); 
    goto done;
  } 


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

