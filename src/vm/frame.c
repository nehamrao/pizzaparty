#include <hash.h>
#include <list.h>
#include "frame.h"
#include "swap.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "threads/pte.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/init.h"

/* Supplemental Page Table is global. */
struct hash sup_pt;
struct lock sup_pt_lock;

/* Frame Table */
struct list frame_list;
struct lock frame_list_lock;

/* "Hand" in clock algorithm for frame eviction */
struct frame_struct* evict_pointer;

/* Hash function used to organize supplemental page table as a hash table */
static unsigned
sup_pt_hash_func (const struct hash_elem *element, void *aux UNUSED);
static bool
sup_pt_less_func (const struct hash_elem *a, const struct hash_elem *b,
                  void *aux UNUSED);


/* Initialize supplemental page table and frame table */
void 
sup_pt_init (void)
{
  hash_init (&sup_pt, sup_pt_hash_func, sup_pt_less_func, NULL);
  list_init (&frame_list);
  lock_init (&sup_pt_lock);
  lock_init (&frame_list_lock);
  evict_pointer = NULL;
}

/* Given pd and virtual address, find the page table entry */
uint32_t *
sup_pt_pte_lookup (uint32_t *pd, const void *vaddr, bool create)
{
  uint32_t *pt, *pde;

  if (pd == NULL)
    return NULL;

  /* Check for a page table for VADDR. */
  pde = pd + pd_no (vaddr);
  if (*pde == 0) 
  {
    if (create) /* Create as instructed */
    {
      pt = palloc_get_page (PAL_ZERO);
      if (pt == NULL)
        return NULL;
      *pde = pde_create (pt);    
    }
    else 
    {
      return NULL;
    }
  }

  /* Return the page table entry. */
  pt = pde_get_pt (*pde);
  return &pt[pt_no (vaddr)];
}

/* Given pte, find the corresonding page_struct entry */
struct page_struct *
sup_pt_ps_lookup (uint32_t *pte)
{
  struct page_struct ps;
  ps.key = (uint32_t)pte;

  lock_acquire (&sup_pt_lock);
  struct hash_elem *e = hash_find (&sup_pt, &ps.elem);
  lock_release (&sup_pt_lock);

  return (e != NULL) ? hash_entry (e, struct page_struct, elem) : NULL;
}

/* Create an entry to sup_pt, according to the given info */
struct page_struct * 
sup_pt_add (uint32_t *pd, void *upage, uint8_t *vaddr, size_t length,
            uint32_t flag, block_sector_t sector_no)
{
  /* Find pte */
  uint32_t *pte = sup_pt_pte_lookup (pd, upage, true);

  /* Allocate page_struct, i.e., a new entry in sup_pt */
  struct page_struct *ps =
    (struct page_struct*) malloc (sizeof (struct page_struct));
  if (ps == NULL)
    return NULL;

  /* Fill in sup_pt entry info */
  ps->key = (uint32_t) pte;
  ps->fs = malloc (sizeof (struct frame_struct));
  if (ps->fs == NULL)
  {
    free (ps);
    return NULL;
  }
  ps->fs->vaddr = vaddr;
  ps->fs->length = length;
  ps->fs->flag = flag;
  ps->fs->sector_no = sector_no; 
  list_init (&ps->fs->pte_list);

  /* Register the page itself to pte_list of frame_struct */
  struct pte_shared *pshr =
    (struct pte_shared *)malloc (sizeof (struct pte_shared));
  if (pshr == NULL)
  {
    free (ps->fs);
    free (ps);
    return NULL;
  }
  pshr->pte = pte;
  list_push_back (&ps->fs->pte_list, &pshr->elem);

  /* Register at supplemental page table */
  lock_acquire (&sup_pt_lock);
  hash_insert (&sup_pt, &ps->elem);
  lock_release (&sup_pt_lock);

  /* Register at frame table */
  lock_acquire (&frame_list_lock);
  list_push_back (&frame_list, &ps->fs->elem);
  lock_release (&frame_list_lock);

  return ps;
}

/* Create a sup_pt entry, but share with others for an exsiting frame */
struct page_struct *
sup_pt_shared_add (uint32_t *pd, void *upage, struct frame_struct *fs)
{
  /* Find pte */
  uint32_t *pte = sup_pt_pte_lookup (pd, upage, true);

  /* Create page_struct and register in sup_pt */
  struct page_struct *ps;
  ps = malloc (sizeof (struct page_struct));
  if (ps == NULL)
    return NULL;
  ps->key = (uint32_t) pte;
  ps->fs = fs;
  
  lock_acquire (&sup_pt_lock);
  hash_insert (&sup_pt, &ps->elem);
  lock_release (&sup_pt_lock);

  /* Register share memory in frame table */
  struct pte_shared* pshr =
    (struct pte_shared*)malloc (sizeof (struct pte_shared));
  if (pshr == NULL)
    {
      free (ps);
      return NULL;
    }
  pshr->pte = pte;
  list_push_back (&ps->fs->pte_list, &pshr->elem);

  return ps;
}

/* Delete an entry from sup_pt, given upage */
bool
sup_pt_find_and_delete (uint32_t *pd, void *upage)
{
  uint32_t *pte = sup_pt_pte_lookup (pd, upage, false);

  if (pte != NULL)
    return sup_pt_delete (pte);
  else 
    return false;
}

/* Delete an entry from sup_pt, given pte */
bool
sup_pt_delete (uint32_t *pte)
{
  struct page_struct *ps = sup_pt_ps_lookup (pte);
  if (ps == NULL)
    return false;

  bool last_entry = false;  /* Last entry pointing to a frame_struct */
  struct list *list = &ps->fs->pte_list;
  struct list_elem *e;
  for (e = list_begin (list); e != list_end (list); e = list_next (e))
  {
    struct pte_shared *pte_shared = list_entry (e, struct pte_shared, elem);
    if (pte_shared->pte == pte)
    {
      /* Synch dirty and access bit */
      if (*pte & PTE_D)
        {
          ps->fs->flag |= FS_DIRTY;
        }
      if (*pte & PTE_A)
        {
          ps->fs->flag |= FS_ACCESS;
        }

      /* Remove and release resource */
      list_remove (&pte_shared->elem);
      free (pte_shared);
      if (list_empty (list))  /* Special case: removed the last element */
      {
        last_entry = true;
        lock_acquire (&frame_list_lock);
        list_remove (&ps->fs->elem);
        lock_release (&frame_list_lock);
        free (ps->fs);
      }
      
      lock_acquire (&sup_pt_lock);
      hash_delete (&sup_pt, &ps->elem);
      lock_release (&sup_pt_lock);
      free (ps);
      break;
    }
  }
  return last_entry;
}

/* Used when swapping in, map the pages to frame in memeory */
void
sup_pt_set_swap_in (struct frame_struct *fs, void *kpage)
{
  fs->vaddr = kpage;
  fs->flag = (fs->flag & POSMASK) | POS_MEM;

  sup_pt_fs_set_pte_list (fs, kpage, true);
}

/* Used when swapping out
   register flags reflecting that the frame is no longer in mem */
void
sup_pt_set_swap_out (struct frame_struct *fs,
                     block_sector_t sector_no,
                     bool is_on_disk)
{
  fs->vaddr = NULL;
  fs->sector_no = sector_no;
  fs->flag = (fs->flag & POSMASK) | (is_on_disk ? POS_DISK : POS_SWAP);

  sup_pt_fs_set_pte_list (fs, NULL, false);
}

/* Set up mapping from kpage to the frame associated with pte */
bool 
sup_pt_set_memory_map (uint32_t *pte, void *kpage)
{
  struct page_struct *ps = sup_pt_ps_lookup (pte);
  if (ps == NULL)
    return false;
  sup_pt_set_swap_in (ps->fs, kpage);
  return true;
}

/* Determine if a frame is dirty return true when
        fs->flag indicates dirty or
        any one of the pte's indicates dirty */
bool
sup_pt_fs_is_dirty (struct frame_struct *fs)
{
  /* Frame struct is dirty */
  if (fs->flag & FS_DIRTY)
    {
      return true;
    }

  struct list *list = &fs->pte_list;
  struct list_elem *e;
  for (e = list_begin (list); e != list_end (list); e = list_next (e))
  {
    struct pte_shared *pte_shared = list_entry (e, struct pte_shared, elem);

    /* Found a dirty pte */
    if ((*pte_shared->pte & PTE_D) != 0)
      {
        /* Set frame_struct flag is enough for future query */
        /* Refer to sup_pt_delete() for synching dirty bit */
        fs->flag |= FS_DIRTY;
        return true;
      }
  }

  return false;
}

/* Set frame_struct and all linked ptes' dirty bits */
void 
sup_pt_fs_set_dirty (struct frame_struct *fs, bool dirty)
{
  /* Set frame_struct */
  if (dirty)
    fs->flag |= FS_DIRTY;
  else 
    fs->flag &= ~FS_DIRTY;

  /* Set pte's */
  struct list *list = &fs->pte_list;
  struct list_elem *e;
  for (e = list_begin (list); e != list_end (list); e = list_next (e))
  {
    struct pte_shared *pte_shared = list_entry (e, struct pte_shared, elem);
    if (dirty)
      *pte_shared->pte |= PTE_D;
    else 
      *pte_shared->pte &= ~PTE_D;
  }

  /* Flush TLB */
  struct thread* t = thread_current ();
  pagedir_activate (t->pagedir);
}

/* Find any accessed pte's associated with frame_struct
   also reset the accessed bits for future use */
bool 
sup_pt_fs_scan_and_reset_access (struct frame_struct *fs)
{
  bool flag = false;
  struct list_elem *e;
  struct list *list = &fs->pte_list;
  for (e = list_begin (list); e != list_end (list); e = list_next (e))
  {
    struct pte_shared *pte_shared = list_entry (e, struct pte_shared, elem);
    if ((*pte_shared->pte & PTE_A) != 0)
    {
      flag = true;
      *pte_shared->pte &= ~PTE_A;       /* Reset pte's */
    }
  }

  /* Flush TLB */
  struct thread* t = thread_current ();
  pagedir_activate (t->pagedir);

  /* Refer to sup_pt_delete() for synching access bit */
  if (fs->flag & FS_ACCESS)
  {
    flag = true;
    fs->flag &= ~FS_ACCESS;             /* Reset frame_struct */
  }

  return flag;
}

/* Evict a frame
   return the freed virtual address, which can be used by others */
uint8_t *
sup_pt_evict_frame ()
{
  struct list *list = &frame_list;
  struct list_elem *e;

  /* Get evict_pointer, initialize if necessary */
  if (evict_pointer == NULL)
    {
      lock_acquire (&frame_list_lock);
      e = list_begin (list); 
      evict_pointer = list_entry (e, struct frame_struct, elem);
      lock_release (&frame_list_lock);
    }
  e = &evict_pointer->elem;

  while (true)
    {
      /* Circularly update evict_pointer around frame table */
      lock_acquire (&frame_list_lock);
      e = list_next (&evict_pointer->elem);
      if (e == list_end (list))
        {
          e = list_begin (list); 
        }
      evict_pointer = list_entry (e, struct frame_struct, elem);
      lock_release (&frame_list_lock);

      /* Query PINED bit */
      /* TODO ??? */
      if ((evict_pointer->flag & FS_PINED) != 0)
      {
        printf ("PINNED\n");
        continue;
      }

      /* Frames in memory are candidates for eviction */
      if ((evict_pointer->flag & POSBITS) == POS_MEM)
        if (sup_pt_fs_scan_and_reset_access (evict_pointer))
          break;
    } 

  uint8_t *vaddr = evict_pointer->vaddr;
  swap_out (evict_pointer);

  return vaddr;
}

/* Used when swapping in or out, determined by is_swapping_in,
   set relevant bits for all pte's sharing this particular frame_struct */
void 
sup_pt_fs_set_pte_list (struct frame_struct *fs, uint8_t *kpage,
                        bool is_swapping_in)
{
  struct list *list = &fs->pte_list;
  struct list_elem *e;
  for (e = list_begin (list); e != list_end (list); e = list_next (e))
  {
    struct pte_shared *pte_shared = list_entry (e, struct pte_shared, elem);
    if (is_swapping_in)
    {
      bool writable = !(fs->flag & FS_READONLY);
      bool dirty    = *pte_shared->pte & PTE_D;
      *pte_shared->pte = pte_create_user (kpage, writable);
      *pte_shared->pte |= PTE_A | (dirty ? PTE_D : 0);
    }
    else 
    {
      *pte_shared->pte &= ~PTE_P;
    }
  }

  /* Flush TLB */
  struct thread* t = thread_current ();
  pagedir_activate (t->pagedir);
}

/* Hash function used to organize supplemental page table as a hash table */
static unsigned 
sup_pt_hash_func (const struct hash_elem *elem, void *aux UNUSED)
{
  struct page_struct *ps = hash_entry (elem, struct page_struct, elem);
  return hash_int ((int)ps->key);
}

/* Hash function used to organize supplemental page table as a hash table */
static bool
sup_pt_less_func (const struct hash_elem *a, const struct hash_elem *b,
                  void *aux UNUSED)
{
  struct page_struct *psa = hash_entry (a, struct page_struct, elem);
  struct page_struct *psb = hash_entry (b, struct page_struct, elem);
  return psa->key < psb->key;
}

/* install_page without actually reading data from disk */
bool
mark_page (void *upage, uint8_t *addr,
           size_t length, uint32_t flag,
           block_sector_t sector_no)
{
  struct thread *t = thread_current ();

  if (!(pagedir_get_page (t->pagedir, upage) == NULL))
    return false;

  return sup_pt_add (t->pagedir, upage, addr, length, flag, sector_no)
         != NULL;
}


