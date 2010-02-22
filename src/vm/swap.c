#include <vm/swap.h>
#include <vm/frame.h>
#include <devices/block.h>
#include <filesys/free-map.h>
#include <filesys/inode.h>
#include <userprog/pagedir.h>
#include <lib/string.h>
#include "lib/kernel/bitmap.h"

struct block *sp_device;
static struct bitmap *swap_free_map;


void swap_init ()
{
  sp_device = block_get_role (BLOCK_SWAP);                  //swap device pointer
  swap_free_map = bitmap_create (block_size (sp_device));   // Bitmap for swap
  if (swap_free_map == NULL)
  {
     PANIC ("Out of kernel memory pool\n");
  }
}


bool swap_in (struct frame_struct *pframe)
{
  struct block *device;
  block_sector_t sector_no = pframe->sector;
  size_t length = pframe->length;
  if (sector_no == BITMAP_ERROR)
  {
     printf ("Sector number invalid!\n");
     return FALSE;
  }
  uint32_t *kpage = palloc_get_page (PAL_USER);
  if (kpage == NULL)
  {
    // To implement eviction
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
   
  if (pframe->flag & POSBITS == POS_ZERO)
  {
    memset (kpage, 0, PGSIZE);                                // If zero page write  
    goto done;
  } 
  else if (pframe->flag & POSBITS == POS_DISK)
  {
    device = fs_device;                                               // On disk
  }
  else if (pframe->flag & POSBITS == POS_SWAP)
  {
    device = sp_device;                                               // On swap
  }
  else
  {
    PANIC ("Eviction error, flag not set.\n");
    palloc_free_page (kpage);
    return FALSE;
  }
  
  int i;
  for (i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++)                                // Read 8 blocks
  {
    block_read (device, sector_no + i, kpage + BLOCK_SECTOR_SIZE * i); 
  }
  if ((length < PGSIZE) && (device == fs_device))         // Not enought length and from disk
    memset (kpage + length, 0, PGSIZE - length);
  
  if (device == sp_device)                               // Only swap disk needs free
    bitmap_set_multiple (swap_free_map, sector_no, PGSIZE / BLOCK_SECTOR_SIZE, false);
   
done:
  sup_pt_set_swap_in (pframe, kpage);              
  return TRUE;
}


bool swap_out (struct frame_struct *pframe)
{  
  struct block *device;
  block_sector_t sector_no;
  uint32_t *kpage = pframe->vaddr;
  if (kpage == NULL)
  {
    printf ("Virtual address invalid!\n");             
    return FALSE;
  }  
  
  if ((pframe->flag & POSBITS == POS_ZERO) && (pframe->flag & FS_DIRTY == 0))                         //Zero page needs not write
    goto done;

 

  if ((pframe->flag & FS_DIRTY) || (pframe->flag & TYPEBITS == TYPE_Stack))                 // Only dirty write to swap
  {
    device = sp_device;
  }
  else            
  {
    sup_pt_set_swap_out (pframe, pframe->sector, TRUE); 
    return true;
  } 

  sector_no = bitmap_scan_and_flip (swap_free_map, 0, PGSIZE / BLOCK_SECTOR_SIZE, false);
  if (sector_no == BITMAP_ERROR)
  {
     printf ("Out of swap space!\n");
     return FALSE;
  }

  for (i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++)
  {
    block_write (device, sector_no + i, kpage + BLOCK_SECTOR_SIZE * i); 
  }

   sup_pt_set_swap_out (pframe, sector_no, FALSE); 
   return true;
}
