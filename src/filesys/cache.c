//cache.c
#include "cache.h"
#include "filesys.h"
#include <stdio.h>
#include <string.h>
#include "threads/palloc.h"

/* Is this OK? We do not have SECTOR_ERROR is we use codes from prj2 */
#define SECTOR_ERROR -1;

struct cache_block cache_block[64];

bool cache_initialized = false;

void
cache_init ()
{
  uint8_t *kpage;
  int i, j;

  for (i = 0; i < 8; i++)
  {
    kpage = (uint8_t*)palloc_get_page (PAL_ZERO);
    for (j = 0; j < 8; j++)
    {
      cache_block[i*8+j].data = kpage + j * BLOCK_SECTOR_SIZE;
      memset (cache_block[i*8+j].data, 0, BLOCK_SECTOR_SIZE);
    }
  }
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
  cache_initialized = true;
  return;
}

struct cache_block *
cache_get (block_sector_t sector_no)
{
  int idx = -1;
  uint32_t min_time_stamp = 1 << 31;
  int victim = -1;

  int i = 0;
  for (i = 0; i < 64; i++)
  {
    if (cache_block[i].sector_no == sector_no)
    {
      idx = i;
      cache_block[i].time_stamp = (1 << 30);
    } else 
    {
      if (cache_block[i].time_stamp) 
        cache_block[i].time_stamp --;
    }
    if ( (cache_block[i].shared_lock.i == 0)
      && (cache_block[i].time_stamp < min_time_stamp) )
    {
      min_time_stamp = cache_block[i].time_stamp;
      victim = i;
    }
  }

  if ((idx == -1) && (victim != -1))
  {
 //   printf ("Evict no: %ld\n", victim);
    /* Eviction process begins */
    if (cache_block[victim].dirty)
    {
    /* Write behind to be implemented here *******************************************/
      block_write (fs_device, cache_block[victim].sector_no, cache_block[victim].data);
      cache_block[victim].dirty = false;
    }
    cache_block[victim].sector_no = sector_no;
    cache_block[victim].present = false;
    cache_block[victim].time_stamp = (1 << 30);
    return &cache_block[victim];
  }

  if ((idx == -1) && (victim == -1))
  {
//    printf ("Cache busy!\n");   /*********/
    return NULL;
  }

  if (idx != -1)
  { 
//    printf ("Found record %ld block\n", idx);
    return &cache_block[idx];
  }
}

void 
cache_read ( struct cache_block *cb, void *data, off_t ofs, int length)
{
//  if (cb->sector_no == 105)
//    printf ("************** Cache READ!!!!!!!!!!!!!!!!!!!!!! %ld \n", cb->sector_no);
//  acquire_shared (&cb->shared_lock);
  if (!cb->present)
  {
//    printf ("read %ld block from disk\n", cb->sector_no);
    block_read (fs_device, cb->sector_no, cb->data);
    cb->present = true;
  }
  memcpy ( data, cb->data + ofs, length);
  /* Read ahead to be implemented here ****************/
//  release_shared (&cb->shared_lock);
}

void 
cache_write ( struct cache_block *cb, void *data, off_t ofs, int length)
{
//  if (cb->sector_no == 105)
//    printf ("************** Cache Write!!!!!!!!!!!!!!!!!!!!!! %ld \n", cb->sector_no);
  acquire_exclusive (&cb->shared_lock);
  cb->dirty = blkcmp (cb->data+ofs, data, length);
  cb->dirty =1;
  cb->present = true;
  memcpy (cb->data+ofs, data, length);

//  block_write (fs_device, cb->sector_no, cb->data);
  release_exclusive (&cb->shared_lock);
//  if (!cb->sector_no)
//  cache_flush (); /* To be removed */
}

void 
cache_flush (void)
{ 
//  printf ("FFFFFFFFLLLLLLLLLLLLLUUUUUUUUUUUUSSSSSSSSSHHHHHHHHHH\n");
  int i;
  for (i = 0; i < 64; i++)
  {
    if (cache_block[i].dirty)
    {
      block_write (fs_device, cache_block[i].sector_no, cache_block[i].data);
      cache_block[i].dirty = false;
    }
  }
}

/* Compare a and b for a given length, return true if different, 
   false if the same */
bool
blkcmp (uint8_t *a, uint8_t *b, int length)
{
  while ((length > 0) && (*a == *b))
  {
    a ++;
    b ++;
    length --;
  }
  return (length > 0);
}


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

void 
release_shared (struct shared_lock *s)
{
  lock_acquire (&s->lock);
  s->i --;
  if (!s->i) 
    cond_signal (&s->cond, &s->lock);
  lock_release (&s->lock);
}

void
release_exclusive (struct shared_lock *s)
{
  lock_acquire (&s->lock);
  s->i = 0;
  cond_broadcast (&s->cond, &s->lock);
  lock_release (&s->lock);
}


