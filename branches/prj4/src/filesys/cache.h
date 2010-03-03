//cache.h
#include "off_t.h"
#include "threads/synch.h"
#include "devices/block.h"

struct shared_lock
{
  int i;
  struct lock lock;
  struct condition cond;
};

struct cache_block
{
  block_sector_t sector_no;
  bool dirty;
  bool present;
  unsigned int time_stamp;
  struct shared_lock shared_lock;
  void *data;
};

void cache_init (void);
struct cache_block *cache_get (block_sector_t sector_no);
void cache_read ( struct cache_block *cb, void *data, off_t ofs, int length);
void cache_write ( struct cache_block *cb, void *data, off_t ofs, int length);
bool blkcmp (uint8_t *a, uint8_t *b, int length);
void acquire_shared (struct shared_lock *s);
void acquire_exclusive (struct shared_lock *s);
void release_shared (struct shared_lock *s);
void release_exclusive (struct shared_lock *s);
void cache_flush (void);
