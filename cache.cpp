// Ewan Parker, created 21st September 2023.
// USB Mass Storage, backed by sparse file on remote SSH host.
// Sector cache routines.
//
// Copyright (C) 2023 Ewan Parker.
// https://www.ewan.cc

#include "cache.h"
#include "Arduino.h"

#if ARDUINO_USB_CDC_ON_BOOT
#define HWSerial Serial0
#else
#define HWSerial Serial
#endif

uint16_t blocks = 0;
struct cache_list list;
void *block_list = 0;
uint16_t _block_size = 0;

void _dump_cache_chain()
{
  struct cache_chain * ent = list.next;
  HWSerial.printf("&HEAD=%u\r\n", ent);
  while (ent)
  {
    HWSerial.printf("  &ENT=%u\r\n", ent);
    HWSerial.printf("    CHAIN prev=%u next=%u\r\n", ent->chain.prev,
      ent->chain.next);
    HWSerial.printf("    DATA: used=%u blk#=%u ix=%u\r\n", ent->data.in_use,
      ent->data.block, ent->data.block_ix);
    ent = ent->chain.next;
  }
  HWSerial.printf("&TAIL=%u\r\n", list.prev);
}

void allocate_cache(uint16_t block_size, uint16_t blocks)
{
  // Allocate memory.
  int entsz = sizeof (struct cache_chain);
  int list_bytes = entsz * blocks;
  list.next = (struct cache_chain*)malloc(list_bytes);
  list.prev = list.next + blocks - 1;
  bzero(list.next, list_bytes);
  block_list = malloc(block_size * blocks);
  bzero(block_list, block_size * blocks);

  // Link memory.
  void *tmp = list.next;
  struct cache_chain *ent = NULL;
  for (uint16_t b = 0; b < blocks; b++)
  {
    ent = (struct cache_chain *)tmp;
    if (b) ent->chain.prev = (struct cache_chain *)(tmp - entsz);
    if (b != blocks - 1) ent->chain.next = (struct cache_chain *)(tmp + entsz);
    ent->data.block_ix = b;
    tmp += entsz;
  }
  //_dump_cache_chain();
}

uint16_t init_cache(uint16_t block_size, uint32_t max_blocks)
{
  int free = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
  if (free >= 1024 * 1024)
  {
    // Allocate as much cache as we can, leaving 0.5 MB free RAM.
    int entry_size = sizeof (struct cache_chain) + block_size;
    blocks = (free - 512 * 1024)/entry_size;
    if (blocks < 2) blocks = 0;
    else if (blocks > max_blocks) blocks = max_blocks;
    if (blocks) allocate_cache(block_size, blocks);
  }
  _block_size = block_size;
  return blocks;
}

void* get_cache_block(uint16_t block)
{
  if (!blocks) return 0;

  // Search from MRU head.
  bool hit = 0;
  struct cache_chain * ent = list.next;
  while (ent && !hit)
  {
    if (ent->data.in_use && ent->data.block == block) hit = 1;
    else ent = ent->chain.next;
  }

  // Re-link to MRU head of list.
  if (hit && ent != list.next)
  {
    //HWSerial.printf("%%MEM-CACHE-PROMOTE block=%u\r\n", block);
    ent->chain.prev->chain.next = ent->chain.next;
    if (ent->chain.next) ent->chain.next->chain.prev = ent->chain.prev;
    else list.prev = ent->chain.prev;
    ent->chain.prev = 0;
    ent->chain.next = list.next;
    list.next->chain.prev = ent;
    list.next = ent;
  }
  //_dump_cache_chain();

  //HWSerial.printf("%%MEM-CACHE-GET block=%u hit=%d\r\n", block, hit);
  if (hit) return block_list + _block_size * ent->data.block_ix;
  else return NULL;
}

void put_cache_block(uint16_t block, void* block_data)
{
  if (!blocks) return;

  // Search from MRU head.
  bool hit = 0;
  struct cache_chain * ent = list.next;
  while (ent && !hit)
  {
    if (ent->data.in_use && ent->data.block == block) hit = 1;
    else ent = ent->chain.next;
  }

  if (!hit)
  {
    // Block not in cache so unlink LRU at tail and link at head with our block.
    ent = list.prev;
    ent->chain.next = list.next;
    list.next->chain.prev = ent;
    list.next = ent;
    list.prev = ent->chain.prev;
    ent->chain.prev = NULL;
    list.prev->chain.next = NULL;
    //if (ent->data.in_use)
    //  HWSerial.printf("%%MEM-CACHE-EVICT block=%u\r\n", ent->data.block);

    // Update cache.
    ent->data.in_use = true;
    ent->data.block = block;
  }
  else
  {
    // Block in cache, update just the data.
  }
  memcpy(block_list + _block_size * ent->data.block_ix, block_data, _block_size);
  //_dump_cache_chain();

  //HWSerial.printf("%%MEM-CACHE-PUT block=%u overwrite=%d\r\n", block, hit);
}
