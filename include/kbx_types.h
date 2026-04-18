#pragma once
#include <stddef.h>
#include <stdint.h>

// Explicit 64-byte L1 Cache Line alignment
#define KBX_CACHE_ALIGNED alignas(64)

typedef enum {
  KBX_TASK_IDLE,
  KBX_TASK_INFERENCE,
  KBX_TASK_IMAGE_ACQUISITION,
  KBX_TASK_PERFORMANCE_TEST
} kbx_task_type;

typedef enum {
  KBX_DETECTION_MODEL,
  KBX_CLASSIFICATION_MODEL,
} kbx_model_type;

typedef enum {
  KBX_IMAGE_DATA_TYPE_UNKNOWN,
  KBX_IMAGE_DATA_TYPE_GRAYSCALE,
  KBX_IMAGE_DATA_TYPE_RGB,
  KBX_IMAGE_DATA_TYPE_RGBA
} kbx_image_data_type;

typedef enum {
  KBX_TASK_PRIORITY_LOW,
  KBX_TASK_PRIORITY_MEDIUM,
  KBX_TASK_PRIORITY_HIGH,
  KBX_TASK_PRIORITY_REALTIME
} kbx_task_priority;

typedef enum {
  KBX_MODEL_CONFIG_TYPE_DEFAULT,
  KBX_MODEL_CONFIG_TYPE_PERFORMANCE,
  KBX_MODEL_CONFIG_TYPE_ACCURACY
} kbx_model_config_type;

typedef enum {
  KBX_MEMORY_TYPE_RAM,
  KBX_MEMORY_TYPE_VRAM,
  KBX_MEMORY_TYPE_SYSTEM_MEMORY,
  KBX_MEMORY_TYPE_SHARED_MEMORY
} kbx_memory_type;

typedef enum {
  KBX_DEVICE_TYPE_CPU,
  KBX_DEVICE_TYPE_GPU,
  KBX_DEVICE_TYPE_NPU
} kbx_device_type;

typedef enum {
  KBX_BACKEND_TYPE_OPENVINO,
  KBX_BACKEND_TYPE_TENSORFLOW_LITE,
} kbx_backend_type;

typedef enum {
  KBX_STATUS_SUCCESS = 0,
  KBX_STATUS_ERR_NO_MEM = -1,
  KBX_STATUS_ERR_IO = -2,
  KBX_STATUS_ERR_GPU = -3,
  KBX_STATUS_ERR_DRM = -4,
  KBX_STATUS_ERR_BPF = -5,
  KBX_STATUS_ERR_VK = -6
} kbx_status_t;

typedef struct {
  kbx_task_type task_type;
  kbx_task_priority task_priority;
} kbx_task_params;

typedef struct {
  kbx_model_type model_type;
  kbx_model_config_type model_config_type;
  kbx_memory_type memory_type;
  kbx_device_type device_type;
  kbx_backend_type backend_type;
} kbx_model_params;

typedef struct {
  kbx_image_data_type image_data_type;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  size_t data_size;
  void *data;
  int fd;
  int dma_fd;
} kbx_image;