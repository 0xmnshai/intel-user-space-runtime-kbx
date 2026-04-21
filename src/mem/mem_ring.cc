#include <numa.h>

#include "kbx_mem.h"

kbx_status_t kbx_ring_init(kbx_task_queue *ring, size_t size) {
  ring->tasks =
      (kbx_task_params *)numa_alloc_onnode(sizeof(kbx_task_params) * size, 0);

  if (ring->tasks == NULL) {
    return KBX_STATUS_ERR_NOMEM;
  }

  ring->data = (void **)numa_alloc_onnode(sizeof(void *) * size, 0);

  if (ring->data == NULL) {
    numa_free(ring->tasks, sizeof(kbx_task_params) * size);
    return KBX_STATUS_ERR_NOMEM;
  }

  ring->size = size;
  ring->head = 0; // write index
  ring->tail = 0; // read index
  return KBX_STATUS_SUCCESS;
}

bool kbx_ring_is_full(const kbx_task_queue *ring) {
  return ((ring->head + 1) % ring->size) == ring->tail;
}

bool kbx_ring_is_empty(const kbx_task_queue *ring) {
  return ring->head == ring->tail;
}

bool kbx_ring_push(kbx_task_queue *ring, const kbx_task_params *task,
                   void *data) {
  if (kbx_ring_is_full(ring)) {
    return false;
  }
  ring->tasks[ring->head] = *task;
  ring->data[ring->head] = data;
  ring->head = (ring->head + 1) % ring->size;
  return true;
}

bool kbx_ring_pop(kbx_task_queue *ring, kbx_task_params *task, void **data) {
  if (kbx_ring_is_empty(ring)) {
    return false;
  }
  *task = ring->tasks[ring->tail];
  if (data) {
    *data = ring->data[ring->tail];
  }
  ring->tail = (ring->tail + 1) % ring->size;
  return true;
}

void kbx_ring_destroy(kbx_task_queue *ring) {
  if (ring->tasks) {
    numa_free(ring->tasks, sizeof(kbx_task_params) * ring->size);
    ring->tasks = NULL;
  }
  if (ring->data) {
    numa_free(ring->data, sizeof(void *) * ring->size);
    ring->data = NULL;
  }
}