// Ewan Parker, created 21st September 2023.
// USB Mass Storage, backed by sparse file on remote SSH host.
// Sector cache routines.
//
// Copyright (C) 2023 Ewan Parker.
// https://www.ewan.cc

#include <stdint.h>

uint16_t init_cache(uint16_t block_size, uint32_t max_blocks);
void* get_cache_block(uint16_t block);
void put_cache_block(uint16_t block, void* block_data);

struct cache_list
{
  struct cache_chain *next;
  struct cache_chain *prev;
};

struct cache_data
{
  bool in_use;
  uint32_t block;
  uint16_t block_ix;
  //int reads, writes;
};

struct cache_chain
{
  struct cache_list chain;
  struct cache_data data;
};
