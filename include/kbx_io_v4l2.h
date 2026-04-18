#pragma once

#include "kbx_types.h"

typedef struct kbx_v4l2_device {
  int fd;
  kbx_image image;
} kbx_v4l2_device;

typedef struct kbx_v4l2_init_params {
  char *device_name;
  uint32_t width;
  uint32_t height;
  uint32_t format;
  uint32_t interval;
  uint32_t io_type;
} kbx_v4l2_init_params_t;

extern "C" {

kbx_status_t kbx_v4l2_init(kbx_v4l2_device *device,
                           const kbx_v4l2_init_params_t *params);
void kbx_v4l2_destroy(kbx_v4l2_device *device);

kbx_status_t kbx_v4l2_start(kbx_v4l2_device *device);
kbx_status_t kbx_v4l2_stop(kbx_v4l2_device *device);

kbx_status_t kbx_v4l2_read(kbx_v4l2_device *device, kbx_image *image);
kbx_status_t kbx_v4l2_write(kbx_v4l2_device *device, const kbx_image *image);

kbx_status_t kbx_v4l2_get_dmabuf(kbx_v4l2_device *device, int *fd);
}