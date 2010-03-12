#include "cache.h"
#include "filesys.h"
#include <stdio.h>
#include <string.h>
#include "threads/palloc.h"
#include "threads/interrupt.h"

#define SECTOR_ERROR -1;

struct cache_block cache_block[64];

struct lock evict_lock;

/* Flag indicating whether cache is set up */
bool cache_initialized = false;

/* Initialize buffer cache */
void
cache_init ()
{
  uint8_t *kpage;
  int i, j;

  /* Allocate buffer cache space in memory */
  for (i = 0; i < 8; i++)
  {
    kpage = (uint8_t*)palloc_get_page (PAL_ZERO);
    for (j = 0; j < 8; j++)
    {
      cache_block[i*8+j].data = kpage + j * BLOCK_SECTOR_SIZE;
      memset (cache_block[i*8+j].data, 0, BLOCK_SECTOR_SIZE);
    }
  }
  /* Set parameters */
  for (i = 0; i < 64; i++)
  {
    cache_block[i].sector_no = SECTOR_ERROR;
    cache_block[i].dirty = false;
    cache_block[i].present = false;
    cache_block[i].time_stamp = 0;
    cache_block[i].shared_lock.i = 0;
    lock_init (&cache_block[i].shared_lock.lock);
    cond_init (&cache_block[i].shared_lock.cond);
  }
  lock_init (&evict_lock);///
  cache_initialized = true;
  return;
}

/* Get the cache block storing SECTOR_NO sector on disk  */
struct cache_block *
cache_get (block_sector_t sector_no)
{
  int idx = -1;
  int victim = -1, retval = -1;
  uint32_t min_time_stamp = 1 << 31;

  /* Look for SECTOR_NO sector in buffer cache, if not found, 
     pick the victim with the smallest time_stamp for eviction */

  lock_acquire (&evict_lock);
  int i = -1;
  for (i = 0; i < 64; i++)
    if (cache_block[i].time_stamp)
      cache_block[i].time_stamp --;

  for (i = 0; i < 64; i++)
  {
    if (cache_block[i].sector_no == sector_no)	
    {
      idx = i;
      cache_block[i].time_stamp = (1 << 30);
      break;
    }

    if ( (cache_block[i].shared_lock.i == 0)
      && (cache_block[i].time_stamp < min_time_stamp) )
    {
      min_time_stamp = cache_block[i].time_stamp;
      victim = i;
    }
  }
  /* Record found in cache buffer */
  if (idx != -1)
  { 
    retval = idx;
  } 
  else
    /* Record not found, evict a victim cache block */
    if (victim != -1)
    {
      /* If dirty, write to disk before eviction */
      if (cache_block[victim].dirty)
      {
        block_write (fs_device, cache_block[victim].sector_no, 
                     cache_block[victim].data);
        cache_block[victim].dirty = false;
      }
      cache_block[victim].sector_no = sector_no;
      cache_block[victim].present = false;
      cache_block[victim].time_stamp = (1 << 30);
      retval = victim;
    }
  
  enum intr_level old_level = intr_disable ();
  lock_release (&evict_lock);

  acquire_exclusive (&cache_block[retval].shared_lock);
  intr_set_level (old_level);

  return &cache_block[retval];
}

/* Read LENGTH data from cache_block at OFS offset, to buffer DATA */
void
cache_read ( struct cache_block *cb, void *data, off_t ofs, int length)
{
  /* Allow multiple reader, so acquire lock in shared mode*/
  if (!cb->present)
  {
    block_read (fs_device, cb->sector_no, cb->data);
    cb->present = true;
  }

  enum intr_level old_level = intr_disable ();
  release_exclusive (&cb->shared_lock);
  acquire_shared (&cb->shared_lock);
  intr_set_level (old_level);

  if (data != NULL)
    memcpy (data, cb->data + ofs, length);
  release_shared (&cb->shared_lock);
}

/* Write LENGTH data to cache_block at OFS offset, from buffer DATA */
void 
cache_write ( struct cache_block *cb, void *data, off_t ofs, int length)
{
  /* Do NOT allow other reader nor writer, so acquire lock in exclusive mode*/
  cb->dirty = true;
  cb->present = true;
  memcpy (cb->data+ofs, data, length);
  release_exclusive (&cb->shared_lock);
}

/* Flush cache, check the dirty bits of all cache buffer blocks,
   if dirty, write to disk */
void 
cache_flush (void)
{ 
  int i;
  for (i = 0; i < 64; i++)
  {
    acquire_shared (&cache_block[i].shared_lock);
    if (cache_block[i].dirty)
    {
      block_write (fs_device, cache_block[i].sector_no, cache_block[i].data);
      cache_block[i].dirty = false;
    }
    release_shared (&cache_block[i].shared_lock);
  }
}

/* Acquire lock in shared mode */
void 
acquire_shared (struct shared_lock *s)
{
  lock_acquire (&s->lock);
  while (s->i < 0)
  {
    cond_wait (&s->cond, &s->lock);
  }
  s->i ++;
  lock_release (&s->lock);
}

/* Acquire lock in exlusive mode */
void 
acquire_exclusive (struct shared_lock *s)
{
  lock_acquire (&s->lock);
  while (s->i)
  {
    cond_wait (&s->cond, &s->lock);
  }
  s->i = -1;
  lock_release (&s->lock);
}

/* Release lock in shared mode */
void 
release_shared (struct shared_lock *s)
{
  lock_acquire (&s->lock);
  s->i --;
  if (!s->i) 
    cond_signal (&s->cond, &s->lock);
  lock_release (&s->lock);
}

/* Release lock in exclusive mode */
void
release_exclusive (struct shared_lock *s)
{
  lock_acquire (&s->lock);
  s->i = 0;
  cond_broadcast (&s->cond, &s->lock);
  lock_release (&s->lock);
}


