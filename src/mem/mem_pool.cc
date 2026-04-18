#include "kbx_mem.h"
#include "kbx_types.h"

#include <numa.h>

kbx_status_t kbx_mem_pool_init(kbx_mem_manager *mem_manager, size_t size) {
  size_t pool_count = numa_max_node() + 1;
  size_t bytes = size * 1024 * 1024;
  
  int node = 0;

  mem_manager->cpu_pool = (kbx_mem_pool *)numa_alloc_onnode(
      sizeof(kbx_mem_pool) * pool_count, node);

  if (mem_manager->cpu_pool == NULL) {
    return KBX_STATUS_ERR_NO_MEM;
  }

  mem_manager->cpu_pool->size = pool_count;

  for (size_t i = 0; i < pool_count; i++) {
    mem_manager->cpu_pool[i].buf = numa_alloc_onnode(bytes, node);
    if (mem_manager->cpu_pool[i].buf == NULL) {
      return KBX_STATUS_ERR_NO_MEM;
    }
    mem_manager->cpu_pool[i].size = pool_count;
    mem_manager->cpu_pool[i].used = 0;
    mem_manager->cpu_pool[i].peak_used = 0;
    mem_manager->cpu_pool[i].block_size = 4096;
    mem_manager->cpu_pool[i].blocks = (kbx_mem_block *)numa_alloc_onnode(
        sizeof(kbx_mem_block) * (bytes / 4096), node);
    if (mem_manager->cpu_pool[i].blocks == NULL) {
      return KBX_STATUS_ERR_NO_MEM;
    }
    for (size_t j = 0; j < (bytes / 4096); j++) {
      mem_manager->cpu_pool[i].blocks[j].size = 4096;
      mem_manager->cpu_pool[i].blocks[j].ptr =
          (void *)((char *)mem_manager->cpu_pool[i].buf + (j * 4096));
      mem_manager->cpu_pool[i].blocks[j].offset = j * 4096;
      mem_manager->cpu_pool[i].blocks[j].used_size = 0;
      atomic_flag_clear(&mem_manager->cpu_pool[i].blocks[j].is_used);
    }
  }
  return KBX_STATUS_SUCCESS;
};

