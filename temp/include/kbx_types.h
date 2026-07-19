#pragma once
#include <stddef.h>
#include <stdint.h>

// Explicit 64-byte L1 Cache Line alignment
#define ZCVR_CACHE_ALIGNED alignas(64)

typedef enum {
  ZCVR_TASK_IDLE,
  ZCVR_TASK_INFERENCE,
  ZCVR_TASK_IMAGE_ACQUISITION,
  ZCVR_TASK_PERFORMANCE_TEST
} ZCVR_task_type;

typedef enum {
  ZCVR_DETECTION_MODEL,
  ZCVR_CLASSIFICATION_MODEL,
} ZCVR_model_type;

typedef enum {
  ZCVR_IMAGE_DATA_TYPE_UNKNOWN,
  ZCVR_IMAGE_DATA_TYPE_GRAYSCALE,
  ZCVR_IMAGE_DATA_TYPE_RGB,
  ZCVR_IMAGE_DATA_TYPE_RGBA
} ZCVR_image_data_type;

typedef enum {
  ZCVR_TASK_PRIORITY_LOW,
  ZCVR_TASK_PRIORITY_MEDIUM,
  ZCVR_TASK_PRIORITY_HIGH,
  ZCVR_TASK_PRIORITY_REALTIME
} ZCVR_task_priority;

typedef enum {
  ZCVR_MODEL_CONFIG_TYPE_DEFAULT,
  ZCVR_MODEL_CONFIG_TYPE_PERFORMANCE,
  ZCVR_MODEL_CONFIG_TYPE_ACCURACY
} ZCVR_model_config_type;

typedef enum {
  ZCVR_MEMORY_TYPE_RAM,
  ZCVR_MEMORY_TYPE_VRAM,
  ZCVR_MEMORY_TYPE_SYSTEM_MEMORY,
  ZCVR_MEMORY_TYPE_SHARED_MEMORY
} ZCVR_memory_type;

typedef enum {
  ZCVR_DEVICE_TYPE_CPU,
  ZCVR_DEVICE_TYPE_GPU,
  ZCVR_DEVICE_TYPE_NPU
} ZCVR_device_type;

typedef enum {
  ZCVR_BACKEND_TYPE_OPENVINO,
  ZCVR_BACKEND_TYPE_TENSORFLOW_LITE,
} ZCVR_backend_type;

typedef enum {
  ZCVR_STATUS_SUCCESS = 0,
  ZCVR_STATUS_ERR_NOMEM = -1,
  ZCVR_STATUS_ERR_IO = -2,
  ZCVR_STATUS_ERR_GPU = -3,
  ZCVR_STATUS_ERR_DRM = -4,
  ZCVR_STATUS_ERR_BPF = -5,
  ZCVR_STATUS_ERR_VK = -6,

  ZCVR_STATUS_ERR_GFX = -7,

  ZCVR_STATUS_CL_ERROR = -8
} ZCVR_status_t;

typedef struct {
  ZCVR_task_type task_type;
  ZCVR_task_priority task_priority;
} ZCVR_task_params;

typedef struct {
  ZCVR_model_type model_type;
  ZCVR_model_config_type model_config_type;
  ZCVR_memory_type memory_type;
  ZCVR_device_type device_type;
  ZCVR_backend_type backend_type;
} ZCVR_model_params;

typedef struct {
  ZCVR_image_data_type image_data_type;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  size_t data_size;
  void *data;
  int fd;
  int dma_fd;
} ZCVR_image;