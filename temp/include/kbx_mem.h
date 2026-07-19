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

#include "ZCVR_types.h"

#define ZCVR_PAGE_SIZE 4096

typedef struct {
  size_t size;
  void *ptr;
  size_t offset;
  size_t used_size;
  atomic_flag is_used;
} ZCVR_mem_block;

typedef struct {
  void *buf;
  size_t size;
  size_t used;
  size_t peak_used;
  size_t block_size;
  ZCVR_mem_block *blocks;
} ZCVR_mem_pool;

typedef struct ZCVR_CACHE_ALIGNED {
  uint32_t head;
  uint32_t tail;
  uint32_t size;
  ZCVR_task_params *tasks;
  void **data;
} ZCVR_task_queue;

typedef struct ZCVR_mem_manager {
  ZCVR_mem_pool *cpu_pool;
  ZCVR_mem_pool *gpu_pool;
  ZCVR_mem_pool *shared_pool;
  ZCVR_mem_pool *system_pool;
  uint8_t *framebuffer;
  size_t framebuffer_size;
  uint8_t *display_buffer;
  size_t display_buffer_size;
  int drm_fd;
  int drm_fb_id;
} ZCVR_mem_manager;

typedef struct ZCVR_CACHE_ALIGNED {
  size_t head;
  size_t tail;
  ZCVR_task_params *tasks;
  size_t capacity;
  ZCVR_mem_manager *mem_manager;
} ZCVR_ring_t;

extern "C" {
ZCVR_status_t ZCVR_mem_pool_init(ZCVR_mem_manager *mem_manager, size_t size);
void ZCVR_mem_pool_destroy(ZCVR_mem_manager *mem_manager);

ZCVR_status_t ZCVR_ring_init(ZCVR_task_queue *ring, size_t size);
void ZCVR_ring_destroy(ZCVR_task_queue *ring);
bool ZCVR_ring_push(ZCVR_task_queue *ring, const ZCVR_task_params *task, void *data);
bool ZCVR_ring_pop(ZCVR_task_queue *ring, ZCVR_task_params *task, void **data);
bool ZCVR_ring_is_full(const ZCVR_task_queue *ring);
bool ZCVR_ring_is_empty(const ZCVR_task_queue *ring);

void *ZCVR_mem_alloc(ZCVR_mem_manager *mem_manager, size_t size);
void ZCVR_mem_free(ZCVR_mem_manager *mem_manager, void *ptr);

void *ZCVR_mem_cpu_alloc(ZCVR_mem_manager *mem_manager, size_t size);
void ZCVR_mem_cpu_free(ZCVR_mem_manager *mem_manager, void *ptr);

void *ZCVR_mem_gpu_alloc(ZCVR_mem_manager *mem_manager, size_t size);
void ZCVR_mem_gpu_free(ZCVR_mem_manager *mem_manager, void *ptr);

void *ZCVR_mem_shared_alloc(ZCVR_mem_manager *mem_manager, size_t size);
void ZCVR_mem_shared_free(ZCVR_mem_manager *mem_manager, void *ptr);

void *ZCVR_mem_system_alloc(ZCVR_mem_manager *mem_manager, size_t size);
void ZCVR_mem_system_free(ZCVR_mem_manager *mem_manager, void *ptr);
}