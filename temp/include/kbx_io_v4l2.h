#pragma once

#include "ZCVR_types.h"
#include <linux/videodev2.h>

typedef struct ZCVR_buffer {
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
} ZCVR_buffer;

typedef struct ZCVR_v4l2_device {
  int fd;
  ZCVR_image image;
  ZCVR_buffer *buffers;
  uint32_t n_buffers;
} ZCVR_v4l2_device;

typedef struct ZCVR_v4l2_init_params {
  char *device_name;
  uint32_t width;
  uint32_t height;
  uint32_t format;
  uint32_t interval;
  uint32_t io_type;
  uint32_t frame_count;
} ZCVR_v4l2_init_params_t;

typedef struct ZCVR_camera_device {

  int fd;
  struct v4l2_capability capability;
  struct v4l2_cropcap cropcap;
  struct v4l2_input input;
  struct v4l2_format format;
  struct v4l2_streamparm streamparm;
  struct v4l2_buffer buffer;
  struct v4l2_requestbuffers requestbuffers;
  struct v4l2_crop crop;
} ZCVR_camera_device;

extern "C" {

ZCVR_status_t ZCVR_v4l2_init(ZCVR_v4l2_device *device,
                           const ZCVR_v4l2_init_params_t *params);
void ZCVR_v4l2_destroy(ZCVR_v4l2_device *device);

ZCVR_status_t ZCVR_v4l2_start_capture(ZCVR_v4l2_device *device,
                                    ZCVR_v4l2_init_params_t *params);

ZCVR_status_t ZCVR_v4l2_stop_capture(ZCVR_v4l2_device *device,
                                   ZCVR_v4l2_init_params_t *params);

ZCVR_status_t ZCVR_v4l2_read(ZCVR_v4l2_device *device, ZCVR_image *image);
ZCVR_status_t ZCVR_v4l2_write(ZCVR_v4l2_device *device, const ZCVR_image *image);

ZCVR_status_t ZCVR_v4l2_export_dmabuf(ZCVR_v4l2_device *device, uint32_t index,
                                    int *fd);
}