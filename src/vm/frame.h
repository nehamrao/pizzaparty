// Frame.h
#include <stdbool.h>
#include <stdint.h>

#define POS_MEM			0x0;
#define POS_SWAP 		0x1;
#define POS_DISK		0x2;
#define POS_ZERO		0x3;
#define POSBITS			0x3;
#define POSMASK			~POSBITS;

#define RW_R			0x4;

#define TYPE_Executable 	0x8;
#define TYPE_MMFile 		0x10;
#define TYPE_Stack		0x18;  

#define FS_ACCESS		0x20;
#define FS_DIRTY		0x40;

struct frame_struct
{
  uint32_t *vaddr;
  size_t length;
  uint32_t flag; // Disk Swap 
  block_sector_t sector;
  struct list pte_list;
};

struct page_struct
{
  uint32_t key;
  struct frame_struct *fs;
  hash_elem elem;
};

struct pte_shared
{
  uint32_t *pte;
  list_elem elem;
}

// Supplemental page table is global.
struct page_struct *sup_pt_lookup (uint32_t *pte);
void sup_pt_init (void);
bool sup_pt_add (uint32_t *pd, void *upage, uint32_t *vaddr, int length, uint32_t flag, int sector);
bool sup_pt_shared_add (uint32_t *pd, void *upage, struct frame_struct *fs);
void sup_pt_find_and_delete (uint32_t *pd, void *upage);
void sup_pt_delete (uint32_t *pte);

bool sup_pt_set_memory_map (uint32_t *pte, void *kpage);
void sup_pt_set_swap_in (struct frame_struct *fs, void *kpage);
void sup_pt_set_swap_out (struct frame_struct *fs, int sector, bool is_on_disk);
void sup_pt_fs_set_access (struct frame_struct *fs, bool access);
void sup_pt_fs_set_dirty (struct frame_struct *fs, bool dirty);
void sup_pt_fs_set_pte_list (struct frame_struct *fs, bool present);

