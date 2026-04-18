#pragma once

#ifdef __cplusplus
#include <atomic>
using std::atomic_flag;
#else
#include <stdatomic.h>
#endif
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include "kbx_types.h"

#define KBX_PAGE_SIZE 4096

typedef struct {
  size_t size;
  void *ptr;
  size_t offset;
  size_t used_size;
  atomic_flag is_used;
} kbx_mem_block;

typedef struct {
  void *buf;
  size_t size;
  size_t used;
  size_t peak_used;
  size_t block_size;
  kbx_mem_block *blocks;
} kbx_mem_pool;

typedef struct KBX_CACHE_ALIGNED {
  uint32_t head;
  uint32_t tail;
  uint32_t size;
  kbx_task_params *tasks;
  void **data;
} kbx_task_queue;

typedef struct {
  kbx_mem_pool *cpu_pool;
  kbx_mem_pool *gpu_pool;
  kbx_mem_pool *shared_pool;
  kbx_mem_pool *system_pool;
  uint8_t *framebuffer;
  size_t framebuffer_size;
  uint8_t *display_buffer;
  size_t display_buffer_size;
  int drm_fd;
  int drm_fb_id;
} kbx_mem_manager;

typedef struct KBX_CACHE_ALIGNED {
  size_t head;
  size_t tail;
  kbx_task_params *tasks;
  size_t capacity;
  kbx_mem_manager *mem_manager;
} kbx_ring_t;

extern "C" {
kbx_status_t kbx_mem_pool_init(kbx_mem_manager *mem_manager, size_t size);
void kbx_mem_destroy(kbx_mem_manager *mem_manager);

kbx_status_t kbx_ring_init(kbx_task_queue *ring, size_t size);
void kbx_ring_destroy(kbx_task_queue *ring);
bool kbx_ring_push(kbx_task_queue *ring, const kbx_task_params *task);
bool kbx_ring_pop(kbx_task_queue *ring, kbx_task_params *task);
bool kbx_ring_is_full(const kbx_task_queue *ring);
bool kbx_ring_is_empty(const kbx_task_queue *ring);

void *kbx_mem_alloc(kbx_mem_manager *mem_manager, size_t size);
void kbx_mem_free(kbx_mem_manager *mem_manager, void *ptr);

void *kbx_mem_cpu_alloc(kbx_mem_manager *mem_manager, size_t size);
void kbx_mem_cpu_free(kbx_mem_manager *mem_manager, void *ptr);

void *kbx_mem_gpu_alloc(kbx_mem_manager *mem_manager, size_t size);
void kbx_mem_gpu_free(kbx_mem_manager *mem_manager, void *ptr);

void *kbx_mem_shared_alloc(kbx_mem_manager *mem_manager, size_t size);
void kbx_mem_shared_free(kbx_mem_manager *mem_manager, void *ptr);

void *kbx_mem_system_alloc(kbx_mem_manager *mem_manager, size_t size);
void kbx_mem_system_free(kbx_mem_manager *mem_manager, void *ptr);
}