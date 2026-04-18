#include "kbx_io_v4l2.h"
#include "kbx_types.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

kbx_status_t kbx_v4l2_init(kbx_v4l2_device *device,
                           const kbx_v4l2_init_params *params) {
  int ret;
  struct v4l2_capability cap;
  struct v4l2_format fmt;

  device->fd = open(params->device_name, O_RDWR | O_NONBLOCK);
  if (device->fd < 0) {
    return KBX_STATUS_ERR_IO;
  }

  ret = ioctl(device->fd, VIDIOC_QUERYCAP, &cap);
  if (ret < 0) {
    return KBX_STATUS_ERR_IO;
  }

  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    return KBX_STATUS_ERR_IO;
  }

  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = params->width;
  fmt.fmt.pix.height = params->height;
  fmt.fmt.pix.pixelformat = params->format;
  fmt.fmt.pix.field = V4L2_FIELD_ANY;
  ret = ioctl(device->fd, VIDIOC_S_FMT, &fmt);

  if (ret < 0) {
    return KBX_STATUS_ERR_IO;
  }

  struct v4l2_requestbuffers req;
  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_DMABUF;
  ret = ioctl(device->fd, VIDIOC_REQBUFS, &req);
  if (ret < 0) {
    return KBX_STATUS_ERR_IO;
  }

  struct v4l2_buffer buf;
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_DMABUF;
  buf.index = 0;
  ret = ioctl(device->fd, VIDIOC_QUERYBUF, &buf);
  if (ret < 0) {
    return KBX_STATUS_ERR_IO;
  }

  ret = ioctl(device->fd, VIDIOC_G_FMT, &fmt);
  if (ret < 0) {
    return KBX_STATUS_ERR_IO;
  }

  return KBX_STATUS_SUCCESS;
}

void kbx_v4l2_destroy(kbx_v4l2_device *device) { close(device->fd); }

kbx_status_t kbx_v4l2_get_dmabuf(kbx_v4l2_device *device, int *fd) {
  struct v4l2_exportbuffer exp_buf;
  exp_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  exp_buf.index = 0;
  exp_buf.fd = -1;
  exp_buf.flags = O_CLOEXEC | O_RDWR;

  int ret = ioctl(device->fd, VIDIOC_EXPBUF, &exp_buf);
  if (ret < 0) {
    return KBX_STATUS_ERR_IO;
  }
  *fd = exp_buf.fd;
  return KBX_STATUS_SUCCESS;
}