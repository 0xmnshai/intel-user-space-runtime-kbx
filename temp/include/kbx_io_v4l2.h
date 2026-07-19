#pragma once

#include "kbx_types.h"
#include <linux/videodev2.h>

typedef struct kbx_buffer {
  void *start;
  size_t length;
  size_t bytesused;
  __u32 type;
  __u32 index;
  __u32 field;
  __u32 sequence;
  __u32 timestamp_type;
  __u64 timestamp;
  void *priv;
  __u32 memory;
} kbx_buffer;

typedef struct kbx_v4l2_device {
  int fd;
  kbx_image image;
  kbx_buffer *buffers;
  uint32_t n_buffers;
} kbx_v4l2_device;

typedef struct kbx_v4l2_init_params {
  char *device_name;
  uint32_t width;
  uint32_t height;
  uint32_t format;
  uint32_t interval;
  uint32_t io_type;
  uint32_t frame_count;
} kbx_v4l2_init_params_t;

typedef struct kbx_camera_device {

  int fd;
  struct v4l2_capability capability;
  struct v4l2_cropcap cropcap;
  struct v4l2_input input;
  struct v4l2_format format;
  struct v4l2_streamparm streamparm;
  struct v4l2_buffer buffer;
  struct v4l2_requestbuffers requestbuffers;
  struct v4l2_crop crop;
} kbx_camera_device;

extern "C" {

kbx_status_t kbx_v4l2_init(kbx_v4l2_device *device,
                           const kbx_v4l2_init_params_t *params);
void kbx_v4l2_destroy(kbx_v4l2_device *device);

kbx_status_t kbx_v4l2_start_capture(kbx_v4l2_device *device,
                                    kbx_v4l2_init_params_t *params);

kbx_status_t kbx_v4l2_stop_capture(kbx_v4l2_device *device,
                                   kbx_v4l2_init_params_t *params);

kbx_status_t kbx_v4l2_read(kbx_v4l2_device *device, kbx_image *image);
kbx_status_t kbx_v4l2_write(kbx_v4l2_device *device, const kbx_image *image);

kbx_status_t kbx_v4l2_export_dmabuf(kbx_v4l2_device *device, uint32_t index,
                                    int *fd);
}