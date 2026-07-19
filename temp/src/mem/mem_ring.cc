#include <numa.h>

#include "ZCVR_mem.h"

ZCVR_status_t ZCVR_ring_init(ZCVR_task_queue *ring, size_t size) {
  ring->tasks =
      (ZCVR_task_params *)numa_alloc_onnode(sizeof(ZCVR_task_params) * size, 0);

  if (ring->tasks == NULL) {
    return ZCVR_STATUS_ERR_NOMEM;
  }

  ring->data = (void **)numa_alloc_onnode(sizeof(void *) * size, 0);

  if (ring->data == NULL) {
    numa_free(ring->tasks, sizeof(ZCVR_task_params) * size);
    return ZCVR_STATUS_ERR_NOMEM;
  }

  ring->size = size;
  ring->head = 0; // write index
  ring->tail = 0; // read index
  return ZCVR_STATUS_SUCCESS;
}

bool ZCVR_ring_is_full(const ZCVR_task_queue *ring) {
  return ((ring->head + 1) % ring->size) == ring->tail;
}

bool ZCVR_ring_is_empty(const ZCVR_task_queue *ring) {
  return ring->head == ring->tail;
}

bool ZCVR_ring_push(ZCVR_task_queue *ring, const ZCVR_task_params *task,
                   void *data) {
  if (ZCVR_ring_is_full(ring)) {
    return false;
  }
  ring->tasks[ring->head] = *task;
  ring->data[ring->head] = data;
  ring->head = (ring->head + 1) % ring->size;
  return true;
}

bool ZCVR_ring_pop(ZCVR_task_queue *ring, ZCVR_task_params *task, void **data) {
  if (ZCVR_ring_is_empty(ring)) {
    return false;
  }
  *task = ring->tasks[ring->tail];
  if (data) {
    *data = ring->data[ring->tail];
  }
  ring->tail = (ring->tail + 1) % ring->size;
  return true;
}

void ZCVR_ring_destroy(ZCVR_task_queue *ring) {
  if (ring->tasks) {
    numa_free(ring->tasks, sizeof(ZCVR_task_params) * ring->size);
    ring->tasks = NULL;
  }
  if (ring->data) {
    numa_free(ring->data, sizeof(void *) * ring->size);
    ring->data = NULL;
  }
}