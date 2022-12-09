#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "cache.h"
#include "jbod.h"

/**
  static global variables to keep the cache's information:
     cache: pointer to where the data of the cache is
     cache_size: size of the cache
     num_queries: number of requests made to the cache
     num_hits: number of hits
 */
static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int num_queries = 0;
static int num_hits = 0;

bool cache_enabled(void);

/**
  cache_create:
     parameters: 
       num_entries: size of the cache
     description:  allocate memory to the cache and store it's size. This function 
     returns 1 in success and -1 otherwise.
 */
int cache_create(int num_entries) {
  if(cache_enabled() || num_entries < 2 || num_entries > 4096){
    return -1;
  }

  cache = calloc(num_entries, sizeof(cache_entry_t));
  cache_size = num_entries;
  return 1;
}

/**
  cache_destory:
     description: deallocate memory and reset the size of the cache to 0. This function
     returns 1 on success and -1 otherwise.
 */
int cache_destroy(void) {
  if(!cache_enabled())
    return -1;

  free(cache);
  cache = NULL;
  cache_size = 0;
  return 1;
}

/**
  cache_lookup:
     parameters: 
       disk_num: the disk ID being looked up by the function
       block_num: the block ID being looked up by the function
       buf: a pointer of where the infromation of the block being search for will be stored if found
     description: searches the cache for a block with disk ID: disk_num and block ID: block_num. If
     found, the data in the block will be stored in buf and it returns 1, otherwise return -1.
 */
int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
  if(!cache_enabled() || disk_num > JBOD_NUM_DISKS || disk_num < 0|| block_num > JBOD_NUM_BLOCKS_PER_DISK || block_num < 0|| buf == NULL)
    return -1;

  num_queries += 1;
  
  for(int i=0; i < cache_size; i++){
    if(!cache[i].valid)
      return -1;
    if(cache[i].disk_num == disk_num && cache[i].block_num == block_num){
      memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
      num_hits += 1;
      cache[i].num_accesses += 1;
      return 1;
    }
  }
  return -1;
}

/**
  cache_update:
     parameters:
       disk_num: the disk ID being serached for in the cache
       block_num: the block ID being serached for in the cache
       buf: the address of information to be stored in the specificed block in the cache
     description: set the contents of the block in the cache with disk ID: disk_num and 
     block ID: block_num to the data pointed by buf.
 */
void cache_update(int disk_num, int block_num, const uint8_t *buf) {
  for(int i = 0; i < cache_size; i++){
    if(cache[i].disk_num == disk_num && cache[i].block_num == block_num){
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      cache[i].num_accesses += 1;
      return;
    }
  }
}

/**
  cache_insert:
     parameters:
       disk_num: the disk ID of the block to be stored in the cache
       block_num: the block ID of the block to be stored in the cache
       buf: the data to be stored in the inserted block in cache
     description: it insertes the block with disk ID: disk_num and block ID: block_num with the data
     pointed by buf into the cache using a Least-Frequently-Used (LFU) implementation.
 */
int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
  if(!cache_enabled() || disk_num > JBOD_NUM_DISKS || disk_num < 0 || block_num > JBOD_NUM_BLOCKS_PER_DISK || block_num < 0|| buf == NULL)
    return -1;

  /*
    index: index of the block with the least number of accesses
    least_accesses: the smalles number of accesses of the block with index "index" found in the cache
   */
  int index = 0;
  int least_accesses = cache[0].num_accesses;

  // Search for an empty space, otherwise replace the block to be inserted  with the LFU block
  for(int i = 0; i < cache_size; i++){
    if(cache[i].valid == 1 && cache[i].disk_num == disk_num && cache[i].block_num == block_num) 
      return -1;
    
    if(cache[i].valid == 0){
      index = i;
      break;
    }
    if(cache[i].num_accesses < least_accesses){
      least_accesses = cache[i].num_accesses;
      index = i;
    }
  }
  
  cache[index].valid = 1;
  cache[index].disk_num = disk_num;
  cache[index].block_num = block_num;
  memcpy(cache[index].block, buf, JBOD_BLOCK_SIZE);
  cache[index].num_accesses = 1;
  
  return 1;
}

/**
   cache_enabled:
      description: checks if the cache is enabled
 */
bool cache_enabled(void) {
  return (cache != NULL) && (cache_size > 0);
}

/**
   cache_print_hit_rate:
      description: prints the number of hits, number of queries, and the hit rate of the cache.
 */
void cache_print_hit_rate(void) {
  fprintf(stderr, "num_hits: %d, num_queries: %d\n", num_hits, num_queries);
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}
