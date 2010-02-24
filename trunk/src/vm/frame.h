#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include <stdint.h>
#include <list.h>
#include <hash.h>
#include "devices/block.h"

#define POS_MEM			0x0
#define POS_SWAP 		0x1
#define POS_DISK		0x2
#define POS_ZERO		0x3
#define POSBITS			0x3
#define POSMASK			~POSBITS

#define TYPE_Executable 	0x4
#define TYPE_MMFile 		0x8
#define TYPE_Stack		0xc  
#define TYPEBITS		0xc
#define TYPEMASK		~TYPEBITS

#define FS_READONLY		0x10
#define FS_DIRTY		0x20
#define FS_ACCESS		0x40
#define FS_PINED		0x10000

#define SECTOR_ERROR		SIZE_MAX

struct frame_struct
{
  uint8_t *vaddr;
  size_t length;
  uint32_t flag;
  block_sector_t sector_no;
  struct list pte_list;
  struct list_elem elem;
};

struct page_struct
{
  uint32_t key;
  struct frame_struct *fs;
  struct hash_elem elem;
};

struct pte_shared
{
  uint32_t *pte;
  struct list_elem elem;
};

void sup_pt_init (void);

bool sup_pt_add (uint32_t *pd, void *upage, uint8_t *vaddr, size_t length,
                 uint32_t flag, block_sector_t sector_no);
bool sup_pt_shared_add (uint32_t *pd, void *upage, struct frame_struct *fs);

bool sup_pt_find_and_delete (uint32_t *pd, void *upage);
bool sup_pt_delete (uint32_t *pte);

uint32_t *sup_pt_pte_lookup (uint32_t *pd, const void *vaddr, bool create);
struct page_struct *sup_pt_ps_lookup (uint32_t *pte);

void sup_pt_set_swap_in  (struct frame_struct *fs, void *kpage);
void sup_pt_set_swap_out (struct frame_struct *fs, block_sector_t sector_no,
                          bool is_on_disk);

bool sup_pt_set_memory_map (uint32_t *pte, void *kpage);

void sup_pt_fs_set_dirty (struct frame_struct *fs, bool dirty);
bool sup_pt_fs_is_dirty  (struct frame_struct *fs);

void sup_pt_fs_set_pte_list (struct frame_struct *fs, uint8_t *kpage,
                             bool is_swapping_in);

bool sup_pt_fs_scan_and_reset_access (struct frame_struct *fs);

uint8_t *sup_pt_evict_frame (void);

bool mark_page (void *upage, uint8_t *addr, size_t length, uint32_t flag,
                block_sector_t sector_no);

#endif /* vm/frame.h */
