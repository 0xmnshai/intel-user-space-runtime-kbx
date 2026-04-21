#include "kbx_io_v4l2.h"
#include "kbx_types.h"

#include <bits/types/struct_timeval.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

static int xioctl(int fh, int request, void *arg) {
  int r;

  do {
    r = ioctl(fh, request, arg);
  } while (-1 == r && EINTR == errno);

  return r;
}

const char *extension = "raw";

static void save_frame(const char *path, const void *p, int size) {
  int fd, rz;

  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  if (fd < 0)
    perror("open");
  else {
    rz = write(fd, p, size);
    printf("Wrote %d of %d bytes to %s\n", rz, size, path);
    close(fd);
  }
}

static void save_frame_as_jpeg(const char *path, const void *p, int size,
                               uint32_t width, uint32_t height,
                               uint32_t format) {
  if (format == V4L2_PIX_FMT_MJPEG) {
    save_frame(path, p, size);
    return;
  }

  const char *ff_fmt = "yuv420p";
  if (format == V4L2_PIX_FMT_UYVY)
    ff_fmt = "uyvy422";
  else if (format == V4L2_PIX_FMT_YUYV)
    ff_fmt = "yuyv422";

  char cmd[512];
  snprintf(cmd, sizeof(cmd),
           "ffmpeg -y -f rawvideo -pixel_format %s -video_size %dx%d -i - "
           "-frames:v 1 %s > /dev/null 2>&1",
           ff_fmt, width, height, path);

  FILE *fp = popen(cmd, "w");
  if (fp) {
    fwrite(p, 1, size, fp);
    pclose(fp);
    printf("Encoded and saved JPEG to %s (software fallback using ffmpeg)\n",
           path);
  } else {
    perror("popen");
    save_frame(path, p, size);
  }
}

kbx_status_t kbx_v4l2_init(kbx_v4l2_device *device,
                           const kbx_v4l2_init_params_t *params) {
  unsigned int n_buffers;
  kbx_buffer *buffers;

  kbx_camera_device camera;
  v4l2_std_id std_id = V4L2_STD_UNKNOWN;

  camera.fd = open(params->device_name, O_RDWR | O_NONBLOCK);
  if (-1 == camera.fd) {
    fprintf(stderr, "Cannot open '%s': %d, %s\n", params->device_name, errno,
            strerror(errno));
    return KBX_STATUS_ERR_IO;
  }

  if (-1 == xioctl(camera.fd, VIDIOC_QUERYCAP, &camera.capability)) {
    if (errno == EINVAL) {
      fprintf(stderr, "%s is no V4L2 device\n", params->device_name);
      return KBX_STATUS_ERR_IO;
    } else {
      fprintf(stderr, "VIDIOC_QUERYCAP error %d, %s\n", errno, strerror(errno));
      return KBX_STATUS_ERR_IO;
    }
  }

  if (!(camera.capability.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    fprintf(stderr, "%s is no video capture device\n", params->device_name);
    return KBX_STATUS_ERR_IO;
  }
  if (!(camera.capability.capabilities & V4L2_CAP_STREAMING)) {
    fprintf(stderr, "%s does not support streaming i/o\n", params->device_name);
    return KBX_STATUS_ERR_IO;
  }

  if (!(camera.capability.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    return KBX_STATUS_ERR_IO;
  }

  /* get standard (only if it has a tuner/analog support) */
  if (camera.capability.capabilities &
      (V4L2_CAP_TUNER | V4L2_CAP_VBI_CAPTURE)) {
    if (-1 == xioctl(camera.fd, VIDIOC_G_STD, &std_id))
      perror("VIDIOC_G_STD");
    for (int i = 0; std_id == V4L2_STD_ALL && i < 10; i++) {
      usleep(100000);
      xioctl(camera.fd, VIDIOC_G_STD, &std_id);
    }

    /* set the standard to the detected standard */
    if (std_id != V4L2_STD_UNKNOWN) {
      if (-1 == xioctl(camera.fd, VIDIOC_S_STD, &std_id))
        perror("VIDIOC_S_STD");
      if (std_id & V4L2_STD_NTSC)
        printf("found NTSC TV decoder\n");
      if (std_id & V4L2_STD_SECAM)
        printf("found SECAM TV decoder\n");
      if (std_id & V4L2_STD_PAL)
        printf("found PAL TV decoder\n");
    }
  }

  /* set video input */
  CLEAR(camera.input);
  camera.input.index = 0;

  /* set framerate */
  CLEAR(camera.streamparm);
  camera.streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(camera.fd, VIDIOC_S_PARM, &camera.streamparm))
    perror("VIDIOC_S_PARM");

  /* get framerate */
  CLEAR(camera.streamparm);
  camera.streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(camera.fd, VIDIOC_G_PARM, &camera.streamparm))
    perror("VIDIOC_G_PARM");

  /* set format */
  CLEAR(camera.format);
  camera.format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  camera.format.fmt.pix.width = params->width;
  camera.format.fmt.pix.height = params->height;
  camera.format.fmt.pix.pixelformat = params->format;
  camera.format.fmt.pix.field = V4L2_FIELD_ANY;
  if (-1 == xioctl(camera.fd, VIDIOC_S_FMT, &camera.format)) {
    fprintf(stderr, "VIDIOC_S_FMT error %d, %s\n", errno, strerror(errno));
    return KBX_STATUS_ERR_IO;
  }

  /* get and display format */
  CLEAR(camera.format);
  camera.format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(camera.fd, VIDIOC_G_FMT, &camera.format)) {
    fprintf(stderr, "VIDIOC_G_FMT error %d, %s\n", errno, strerror(errno));
    return KBX_STATUS_ERR_IO;
  }

  if (camera.format.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG ||
      (params->format == V4L2_PIX_FMT_MJPEG)) {
    extension = "jpg";
  } else {
    extension = "raw";
  }

  printf("%s: %dx%d %c%c%c%c %2.2ffps\n", params->device_name,
         camera.format.fmt.pix.width, camera.format.fmt.pix.height,
         (camera.format.fmt.pix.pixelformat >> 0) & 0xff,
         (camera.format.fmt.pix.pixelformat >> 8) & 0xff,
         (camera.format.fmt.pix.pixelformat >> 16) & 0xff,
         (camera.format.fmt.pix.pixelformat >> 24) & 0xff,
         camera.streamparm.parm.capture.timeperframe.numerator
             ? (float)camera.streamparm.parm.capture.timeperframe.denominator /
                   (float)camera.streamparm.parm.capture.timeperframe.numerator
             : 0);

  /* request buffers */
  CLEAR(camera.requestbuffers);
  camera.requestbuffers.count = 4;
  camera.requestbuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  camera.requestbuffers.memory = V4L2_MEMORY_MMAP;

  if (-1 == xioctl(camera.fd, VIDIOC_REQBUFS, &camera.requestbuffers)) {
    if (EINVAL == errno) {
      fprintf(stderr, "%s does not support memory mapping\n",
              params->device_name);
      return KBX_STATUS_ERR_IO;
    } else {
      fprintf(stderr, "VIDIOC_REQBUFS error %d, %s\n", errno, strerror(errno));
      return KBX_STATUS_ERR_IO;
    }
  }
  if (camera.requestbuffers.count < 2) {
    fprintf(stderr, "Insufficient buffer memory on %s\n", params->device_name);
    exit(EXIT_FAILURE);
  }

  /* allocate buffers */
  buffers = (kbx_buffer *)calloc(camera.requestbuffers.count, sizeof(*buffers));
  if (!buffers) {
    fprintf(stderr, "Out of memory\n");
    return KBX_STATUS_ERR_NOMEM;
  }

  /* mmap buffers */
  for (n_buffers = 0; n_buffers < camera.requestbuffers.count; ++n_buffers) {
    struct v4l2_buffer buffer_info;

    CLEAR(buffer_info);

    buffer_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer_info.memory = V4L2_MEMORY_MMAP;
    buffer_info.index = n_buffers;

    if (-1 == xioctl(camera.fd, VIDIOC_QUERYBUF, &buffer_info))
      printf("VIDIOC_QUERYBUF error %d, %s\n", errno, strerror(errno));

    buffers[n_buffers].length = buffer_info.length;
    buffers[n_buffers].start =
        mmap(NULL /* start anywhere */, buffer_info.length,
             PROT_READ | PROT_WRITE /* required */,
             MAP_SHARED /* recommended */, camera.fd, buffer_info.m.offset);

    if (MAP_FAILED == buffers[n_buffers].start) {
      printf("mmap error %d, %s\n", errno, strerror(errno));
      return KBX_STATUS_ERR_IO;
    } else
      printf("mmap success\n");
  }

  /* queue buffers */
  for (unsigned int i = 0; i < n_buffers; ++i) {
    struct v4l2_buffer buffer_info;

    CLEAR(buffer_info);
    buffer_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer_info.memory = V4L2_MEMORY_MMAP;
    buffer_info.index = i;

    if (-1 == xioctl(camera.fd, VIDIOC_QBUF, &buffer_info)) {
      printf("VIDIOC_QBUF error %d, %s\n", errno, strerror(errno));
      return KBX_STATUS_ERR_IO;
    } else
      printf("VIDIOC_QBUF success\n");
  }

  device->fd = camera.fd;
  device->buffers = buffers;
  device->n_buffers = n_buffers;
  return KBX_STATUS_SUCCESS;
}

kbx_status_t kbx_v4l2_start_capture(kbx_v4l2_device *device,
                                    kbx_v4l2_init_params_t *params) {
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  kbx_buffer *buffers = device->buffers;
  char filename[32];
  uint32_t count = params->frame_count;

  if (-1 == xioctl(device->fd, VIDIOC_STREAMON, &type)) {
    printf("VIDIOC_STREAMON error %d, %s\n", errno, strerror(errno));
    return KBX_STATUS_ERR_IO;
  }

  // If count is 0, we loop indefinitely. If count > 0, we capture 'count'
  // frames.
  for (unsigned int i = 0; (count == 0) || (i < count); i++) {
    for (;;) {
      fd_set fds;
      struct timeval time_val;
      int r;

      FD_ZERO(&fds);
      FD_SET(device->fd, &fds);

      time_val.tv_sec = 2;
      time_val.tv_usec = 0;

      r = select(device->fd + 1, &fds, NULL, NULL, &time_val);
      if (-1 == r) {
        if (EINTR == errno)
          continue;
        return KBX_STATUS_ERR_IO;
      }
      if (0 == r) {
        fprintf(stderr, "Timeout waiting for frame\n");
        return KBX_STATUS_ERR_IO;
      }

      struct v4l2_buffer buffer_info;
      CLEAR(buffer_info);
      buffer_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buffer_info.memory = V4L2_MEMORY_MMAP;

      if (-1 == xioctl(device->fd, VIDIOC_DQBUF, &buffer_info)) {
        if (errno == EAGAIN)
          continue;
        printf("VIDIOC_DQBUF error %d, %s\n", errno, strerror(errno));
        return KBX_STATUS_ERR_IO;
      }

      printf("Captured frame %d, size: %d bytes\n", i + 1,
             buffer_info.bytesused);

      sprintf(filename, "frame%d.%s", i + 1, extension);
      if (strcmp(extension, "jpg") == 0) {
        struct v4l2_format fmt;
        CLEAR(fmt);
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(device->fd, VIDIOC_G_FMT, &fmt);
        save_frame_as_jpeg(filename, buffers[buffer_info.index].start,
                           buffer_info.bytesused, params->width, params->height,
                           fmt.fmt.pix.pixelformat);
      } else {
        save_frame(filename, buffers[buffer_info.index].start,
                   buffer_info.bytesused);
      }

      /* re-queue buffer */
      if (-1 == xioctl(device->fd, VIDIOC_QBUF, &buffer_info)) {
        printf("VIDIOC_QBUF error %d, %s\n", errno, strerror(errno));
        return KBX_STATUS_ERR_IO;
      }
      break;
    }
  }

  return KBX_STATUS_SUCCESS;
}

kbx_status_t kbx_v4l2_stop_capture(kbx_v4l2_device *device,
                                   kbx_v4l2_init_params_t *params) {
  (void)params;
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (-1 == xioctl(device->fd, VIDIOC_STREAMOFF, &type)) {
    printf("VIDIOC_STREAMOFF error %d, %s\n", errno, strerror(errno));
    return KBX_STATUS_ERR_IO;
  }

  return KBX_STATUS_SUCCESS;
}

void kbx_v4l2_destroy(kbx_v4l2_device *device) {
  if (device->buffers) {
    for (unsigned int i = 0; i < device->n_buffers; ++i) {
      munmap(device->buffers[i].start, device->buffers[i].length);
    }
    free(device->buffers);
    device->buffers = NULL;
  }
  if (device->fd >= 0) {
    close(device->fd);
    device->fd = -1;
  }
}

kbx_status_t kbx_v4l2_export_dmabuf(kbx_v4l2_device *device, uint32_t index,
                                    int *fd) {
  struct v4l2_exportbuffer exp_buf;
  CLEAR(exp_buf);
  exp_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  exp_buf.index = index;
  exp_buf.fd = -1;
  exp_buf.flags = O_CLOEXEC | O_RDWR;

  int ret = xioctl(device->fd, VIDIOC_EXPBUF, &exp_buf);
  if (ret < 0) {
    printf("VIDIOC_EXPBUF error %d, %s\n", errno, strerror(errno));
    return KBX_STATUS_ERR_IO;
  }
  *fd = exp_buf.fd;
  return KBX_STATUS_SUCCESS;
}

kbx_status_t kbx_v4l2_read(kbx_v4l2_device *device, kbx_image *image) {
  if (!device || !image) return KBX_STATUS_ERR_IO;

  struct v4l2_buffer buffer_info;
  CLEAR(buffer_info);
  buffer_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buffer_info.memory = V4L2_MEMORY_MMAP;

  // Dequeue best available buffer
  if (-1 == xioctl(device->fd, VIDIOC_DQBUF, &buffer_info)) {
    if (errno == EAGAIN) return KBX_STATUS_ERR_IO; // Non-blocking retry
    return KBX_STATUS_ERR_IO;
  }

  // If user provided a buffer, copy the frame data natively
  if (image->data) {
    size_t copy_size = (image->data_size < buffer_info.bytesused) ? image->data_size : buffer_info.bytesused;
    memcpy(image->data, device->buffers[buffer_info.index].start, copy_size);
    image->data_size = copy_size;
  }

  // Queue buffer back to the device to prevent starvation
  if (-1 == xioctl(device->fd, VIDIOC_QBUF, &buffer_info)) {
    return KBX_STATUS_ERR_IO;
  }

  return KBX_STATUS_SUCCESS;
}

kbx_status_t kbx_v4l2_write(kbx_v4l2_device *device, const kbx_image *image) {
  if (!device || !image || !image->data) return KBX_STATUS_ERR_IO;
  
  // V4L2 writes (e.g. v4l2loopback or output devices) typically support native posix write
  int ret = write(device->fd, image->data, image->data_size);
  if (ret < 0) {
    return KBX_STATUS_ERR_IO;
  }
  
  return KBX_STATUS_SUCCESS;
}