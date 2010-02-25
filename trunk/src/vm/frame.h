#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include <stdint.h>
#include <list.h>
#include <hash.h>
#include "devices/block.h"

/* Position of a frame */
#define POS_SWAP 		0x1
#define POS_DISK		0x2
#define POS_MEM			0x3
#define POSBITS			0x3
#define POSMASK			~POSBITS

/* Content type of a frame */
#define TYPE_Executable 	0x4
#define TYPE_MMFile 		0x8
#define TYPE_Stack		0xc  
#define TYPEBITS		0xc
#define TYPEMASK		~TYPEBITS

/* Property bits of a frame */
#define FS_READONLY		0x10
#define FS_DIRTY		0x20
#define FS_ACCESS		0x40
#define FS_ZERO			0x80

#define FS_PINED		0x10000

#define SECTOR_ERROR		SIZE_MAX

/* A frame structure corresponds to exactly one frame,
   tracking the frame whether it on memeory, disk, or swap.
   Unit structure making up frame table */
struct frame_struct
{
  uint32_t flag;                /* Flag bits */
  uint8_t *vaddr;               /* Virtual address if on memeory */
  size_t length;                /* Length of meaningful contents */
  block_sector_t sector_no;     /* Sector # if on disk or swap */
  struct list pte_list;         /* A list of pte's representing
                                   user pages sharing this frame */
  struct list_elem elem;
};

/* A page structure corresponds to on user virtual page,
   it is specific to each process, and maybe more than one page
   strucutre point to a single frame.
   Unit structure making up supplemental page table */
struct page_struct
{
  uint32_t key;
  struct frame_struct *fs;
  struct hash_elem elem;
};

/* A unit structure representing page pte's
   sharing a common frame.
   Unit structure making up pte_list in frame structure */
struct pte_shared
{
  uint32_t *pte;
  struct list_elem elem;
};


void
sup_pt_init (void);

struct page_struct *
sup_pt_add (uint32_t *, void *, uint8_t *,
            size_t, uint32_t, block_sector_t);

struct page_struct *
sup_pt_shared_add (uint32_t *, void *, struct frame_struct *);

bool
sup_pt_find_and_delete (uint32_t *, void *);

bool
sup_pt_delete (uint32_t *);

uint32_t *
sup_pt_pte_lookup (uint32_t *, const void *, bool);

struct page_struct *
sup_pt_ps_lookup (uint32_t *);

void
sup_pt_set_swap_in  (struct frame_struct *, void *);

void
sup_pt_set_swap_out (struct frame_struct *, block_sector_t, bool);

bool
sup_pt_set_memory_map (uint32_t *, void *);

void
sup_pt_fs_set_dirty (struct frame_struct *, bool);

bool
sup_pt_fs_is_dirty  (struct frame_struct *);

void
sup_pt_fs_set_pte_list (struct frame_struct *, uint8_t *, bool);

bool
sup_pt_fs_scan_and_reset_access (struct frame_struct *);

uint8_t *
sup_pt_evict_frame (void);

bool
mark_page (void *, uint8_t *, size_t, uint32_t, block_sector_t);

#endif /* vm/frame.h */
