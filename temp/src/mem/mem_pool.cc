#include "ZCVR_mem.h"
#include "ZCVR_types.h"

#include <cstdio>
#include <cstdlib>
#include <numa.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <map>
#include <mutex>

// similar to hugepages
ZCVR_status_t ZCVR_mem_pool_init(ZCVR_mem_manager *mem_manager, size_t size) {
  size_t pool_count = numa_max_node() + 1;
  size_t bytes = size * 1024 * 1024;
  int node = 0;

  mem_manager->cpu_pool = (ZCVR_mem_pool *)numa_alloc_onnode(
      sizeof(ZCVR_mem_pool) * pool_count, node);
  if (mem_manager->cpu_pool == NULL) {
    return ZCVR_STATUS_ERR_NOMEM;
  }

  for (size_t i = 0; i < pool_count; i++) {
    mem_manager->cpu_pool[i].buf = numa_alloc_onnode(bytes, node);
    if (mem_manager->cpu_pool[i].buf == NULL) {
      return ZCVR_STATUS_ERR_NOMEM;
    }
    
    size_t num_blocks = bytes / ZCVR_PAGE_SIZE;
    mem_manager->cpu_pool[i].size = num_blocks;
    mem_manager->cpu_pool[i].used = 0;
    mem_manager->cpu_pool[i].peak_used = 0;
    mem_manager->cpu_pool[i].block_size = ZCVR_PAGE_SIZE;

    mem_manager->cpu_pool[i].blocks = (ZCVR_mem_block *)numa_alloc_onnode(
        sizeof(ZCVR_mem_block) * num_blocks, node);
    if (mem_manager->cpu_pool[i].blocks == NULL) {
      return ZCVR_STATUS_ERR_NOMEM;
    }

    for (size_t j = 0; j < num_blocks; j++) {
      mem_manager->cpu_pool[i].blocks[j].size = ZCVR_PAGE_SIZE;
      mem_manager->cpu_pool[i].blocks[j].ptr =
          (void *)((char *)mem_manager->cpu_pool[i].buf + (j * ZCVR_PAGE_SIZE));
      mem_manager->cpu_pool[i].blocks[j].offset = j * ZCVR_PAGE_SIZE;
      mem_manager->cpu_pool[i].blocks[j].used_size = 0;
      atomic_flag_clear(&mem_manager->cpu_pool[i].blocks[j].is_used);
    }
  }

  mem_manager->gpu_pool = NULL;
  mem_manager->shared_pool = NULL;
  mem_manager->system_pool = NULL;

  printf("Pool initialized\n");
  return ZCVR_STATUS_SUCCESS;
}

void ZCVR_mem_pool_destroy(ZCVR_mem_manager *mem_manager) {
  if (!mem_manager->cpu_pool) return;
  size_t pool_count = numa_max_node() + 1;
  for (size_t i = 0; i < pool_count; i++) {
    if (mem_manager->cpu_pool[i].buf) {
      numa_free(mem_manager->cpu_pool[i].buf, mem_manager->cpu_pool[i].size * ZCVR_PAGE_SIZE);
      numa_free(mem_manager->cpu_pool[i].blocks, mem_manager->cpu_pool[i].size * sizeof(ZCVR_mem_block));
    }
  }
  numa_free(mem_manager->cpu_pool, pool_count * sizeof(ZCVR_mem_pool));
}

void *ZCVR_mem_cpu_alloc(ZCVR_mem_manager *mem_manager, size_t size) {
  if (!mem_manager->cpu_pool) return NULL;
  size_t pool_count = numa_max_node() + 1;
  int node = numa_node_of_cpu(sched_getcpu());
  if (node < 0 || node >= (int)pool_count) node = 0;

  ZCVR_mem_pool *pool = &mem_manager->cpu_pool[node];
  size_t blocks_needed = (size + ZCVR_PAGE_SIZE - 1) / ZCVR_PAGE_SIZE;
  
  size_t start_idx = 0;
  size_t found = 0;
  for (size_t j = 0; j < pool->size; j++) {
    if (!atomic_flag_test_and_set(&pool->blocks[j].is_used)) {
       if (found == 0) start_idx = j;
       found++;
       pool->blocks[j].used_size = size; // rough usage tracking
       if (found == blocks_needed) {
         pool->used += blocks_needed;
         if (pool->used > pool->peak_used) pool->peak_used = pool->used;
         return pool->blocks[start_idx].ptr;
       }
    } else {
       for (size_t k = start_idx; k < start_idx + found; k++) {
           atomic_flag_clear(&pool->blocks[k].is_used);
       }
       found = 0;
    }
  }
  return NULL;
}

void ZCVR_mem_cpu_free(ZCVR_mem_manager *mem_manager, void *ptr) {
  if (!mem_manager->cpu_pool || !ptr) return;
  size_t pool_count = numa_max_node() + 1;
  for (size_t i = 0; i < pool_count; i++) {
    ZCVR_mem_pool *pool = &mem_manager->cpu_pool[i];
    if (ptr >= pool->buf && ptr < (void*)((char*)pool->buf + pool->size * ZCVR_PAGE_SIZE)) {
        size_t idx = ((char*)ptr - (char*)pool->buf) / ZCVR_PAGE_SIZE;
        atomic_flag_clear(&pool->blocks[idx].is_used);
        pool->used--;
        break;
    }
  }
}

void *ZCVR_mem_alloc(ZCVR_mem_manager *mem_manager, size_t size) {
  return ZCVR_mem_cpu_alloc(mem_manager, size);
}

void ZCVR_mem_free(ZCVR_mem_manager *mem_manager, void *ptr) {
  ZCVR_mem_cpu_free(mem_manager, ptr);
}

// --- GPU Allocator (DRM Dumb Buffers) ---
static std::map<void *, std::pair<size_t, uint32_t>> g_gpu_allocs;
static std::mutex g_gpu_allocs_mtx;

void *ZCVR_mem_gpu_alloc(ZCVR_mem_manager *mem_manager, size_t size) {
  if (!mem_manager || mem_manager->drm_fd <= 0) return NULL;

  struct drm_mode_create_dumb create_dumb = {};
  create_dumb.width = size;
  create_dumb.height = 1;
  create_dumb.bpp = 8;
  
  if (ioctl(mem_manager->drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb) < 0) {
      return NULL;
  }

  struct drm_mode_map_dumb map_dumb = {};
  map_dumb.handle = create_dumb.handle;
  if (ioctl(mem_manager->drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb) < 0) {
      struct drm_mode_destroy_dumb destroy_dumb = {};
      destroy_dumb.handle = create_dumb.handle;
      ioctl(mem_manager->drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);
      return NULL;
  }

  void *ptr = mmap(0, create_dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, 
                   mem_manager->drm_fd, map_dumb.offset);
  
  if (ptr == MAP_FAILED) {
      struct drm_mode_destroy_dumb destroy_dumb = {};
      destroy_dumb.handle = create_dumb.handle;
      ioctl(mem_manager->drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);
      return NULL;
  }

  std::lock_guard<std::mutex> lock(g_gpu_allocs_mtx);
  g_gpu_allocs[ptr] = {create_dumb.size, create_dumb.handle};

  return ptr;
}

void ZCVR_mem_gpu_free(ZCVR_mem_manager *mem_manager, void *ptr) {
  if (!ptr || mem_manager->drm_fd <= 0) return;

  uint32_t handle = 0;
  size_t size = 0;
  {
      std::lock_guard<std::mutex> lock(g_gpu_allocs_mtx);
      auto it = g_gpu_allocs.find(ptr);
      if (it != g_gpu_allocs.end()) {
          size = it->second.first;
          handle = it->second.second;
          g_gpu_allocs.erase(it);
      }
  }

  if (handle) {
      munmap(ptr, size);
      struct drm_mode_destroy_dumb destroy_dumb = {};
      destroy_dumb.handle = handle;
      ioctl(mem_manager->drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);
  }
}

// --- Shared Allocator (MAP_SHARED/Swap) ---
void *ZCVR_mem_shared_alloc(ZCVR_mem_manager *mem_manager, size_t size) {
  (void)mem_manager;
  size_t total_size = size + ZCVR_PAGE_SIZE;
  void *ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (ptr == MAP_FAILED) return NULL;
  
  *(size_t *)ptr = total_size;
  return (void *)((char *)ptr + ZCVR_PAGE_SIZE);
}

void ZCVR_mem_shared_free(ZCVR_mem_manager *mem_manager, void *ptr) {
  (void)mem_manager;
  if (!ptr) return;
  void *real_ptr = (void *)((char *)ptr - ZCVR_PAGE_SIZE);
  size_t total_size = *(size_t *)real_ptr;
  munmap(real_ptr, total_size);
}

// --- System Allocator (posix_memalign API) ---
void *ZCVR_mem_system_alloc(ZCVR_mem_manager *mem_manager, size_t size) {
  (void)mem_manager;
  void *ptr = NULL;
  if (posix_memalign(&ptr, ZCVR_PAGE_SIZE, size) != 0) {
      return NULL;
  }
  return ptr;
}

void ZCVR_mem_system_free(ZCVR_mem_manager *mem_manager, void *ptr) {
  (void)mem_manager;
  free(ptr);
}