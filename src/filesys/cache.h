#include "off_t.h"
#include "threads/synch.h"
#include "devices/block.h"

#define FLUSH_PERIOD 10000000	/* Period of flushing cache, in ticks */

extern bool cache_initialized;

/* Shared lock that is used to protect cache read and write,
   Multiple readers or at most one writer are allowed */
struct shared_lock
{
  int i;
  struct lock lock;
  struct condition cond;
};

/* Cache metadata */
struct cache_block
{
  block_sector_t sector_no;	 	/* Sector number that is cached */
  bool dirty;				/* Dirty flag. If dirty when evicting,
                                   	   write the data back to disk */
  bool present;				/* Present bit */
  unsigned int time_stamp;		/* Time stamp for evicting algorithm */
  struct shared_lock shared_lock;       /* Shared lock for synchronization */
  void *data;				/* Cache data */
};

void cache_init (void);
struct cache_block *cache_get (block_sector_t sector_no);
void cache_read ( struct cache_block *cb, void *data, off_t ofs, int length);
void cache_write ( struct cache_block *cb, void *data, off_t ofs, int length);
void acquire_shared (struct shared_lock *s);
void acquire_exclusive (struct shared_lock *s);
void release_shared (struct shared_lock *s);
void release_exclusive (struct shared_lock *s);
void cache_flush (void);
