#include <numa.h>

#include "kbx_mem.h"

kbx_status_t kbx_ring_init(kbx_task_queue *ring, size_t size) {
  ring->tasks =
      (kbx_task_params *)numa_alloc_onnode(sizeof(kbx_task_params) * size, 0);

  if (ring->tasks == NULL) {
    return KBX_STATUS_ERR_NO_MEM;
  }

  ring->data = (void **)numa_alloc_onnode(sizeof(void *) * size, 0);

  if (ring->data == NULL) {
    return KBX_STATUS_ERR_NO_MEM;
  }

  ring->size = size;
  ring->head = 0;
  ring->tail = 0;
  return KBX_STATUS_SUCCESS;
}

bool kbx_ring_push(kbx_task_queue *ring, const kbx_task_params *task,
                   void *data) {
  if (kbx_ring_is_full(ring)) {
    return false;
  }
  ring->tasks[ring->tail] = *task;
  ring->data[ring->tail] = data;
  ring->tail = (ring->tail + 1) % ring->size;
  ring->head++;

  if (ring->tail == ring->head) {
    ring->head--;
    return false;
  }

  return true;
}

bool kbx_ring_pop(kbx_task_queue *ring, kbx_task_params *task, void **data) {
  if (kbx_ring_is_empty(ring)) {
    return false;
  }
  *task = ring->tasks[ring->tail];
  *data = ring->data[ring->tail];
  ring->tail = (ring->tail + 1) % ring->size;
  ring->head--;
  return true;
}

bool kbx_ring_is_full(const kbx_task_queue *ring) {
  return ring->tail == ring->head;
}

bool kbx_ring_is_empty(const kbx_task_queue *ring) {
  return ring->head == ring->tail;
}

void kbx_ring_destroy(kbx_task_queue *ring) {
  numa_free(ring->tasks, sizeof(kbx_task_params) * ring->size);
  numa_free(ring->data, sizeof(void *) * ring->size);
}