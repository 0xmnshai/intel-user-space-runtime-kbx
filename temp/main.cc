#include "ZCVR_compute.h"
#include "ZCVR_io_v4l2.h"
#include "ZCVR_mem.h"
#include <cstdio>
#include <cstdlib>

#include <cstring>
#include <linux/videodev2.h>

int main(int argc, char **argv) {
  if (argc < 5) {
    fprintf(stderr, "usage: %s <device> <width> <height> <count> [jpeg]\n",
            argv[0]);
    exit(1);
  }

  // Core Memeory
  ZCVR_mem_manager mem_manager;
  ZCVR_mem_pool_init(&mem_manager, 1024);

  ZCVR_cl_ctx ctx;
  ZCVR_cl_init(&ctx, &mem_manager);

  // Telemetry

  // // Compute & Vision
  // ZCVR_v4l2_device device;
  // const char *device_name = argv[1];
  // int width = atoi(argv[2]);
  // int height = atoi(argv[3]);
  // int count = atoi(argv[4]);
  // (void)count;

  // ZCVR_v4l2_init_params_t params;
  // params.device_name = (char *)device_name;
  // params.width = width;
  // params.height = height;
  // params.format = V4L2_PIX_FMT_UYVY;

  // if (argc >= 6 && strcmp(argv[5], "jpeg") == 0) {
  //   params.format = V4L2_PIX_FMT_MJPEG;
  // }

  // params.interval = 1;
  // params.io_type = V4L2_MEMORY_DMABUF;
  // params.frame_count = count;

  // ZCVR_v4l2_init(&device, &params);
  // ZCVR_v4l2_start_capture(&device, &params);

  // Vulkan & DRM

  // io_uring Reactor

  printf("[KBX] Entering Zero-Syscall Reactor Loop.\n");

  while (false) {
    // A. Hardware Ingress
    // int dmabuf_fd;
    // ZCVR_v4l2_export_dmabuf(&device, 0, &dmabuf_fd);

    // B. Preprocessing (Level Zero SPIR-V)
    // ze_image_handle_t l0_img;
    // ZCVR_l0_import_dmabuf(&l0, dmabuf_fd, device.width, device.height,
    // &l0_img); zeCommandListAppendLaunchKernel(l0.immediate_cmd_list,
    // l0.kernel_nv12_to_rgb, ...)

    // ZCVR_cl_ctx ctx;
    // ZCVR_cl_init(&ctx);

    // C. Inference (OpenVINO Zero-Copy)
    // ZCVR_ov_set_zero_copy_input(&ov, &l0, l0_img);
    // std::vector<ZCVR_bbox> boxes = ZCVR_ov_infer_and_get_boxes(&ov);

    // D. Vulkan Overlay Rendering (Hardware Rasterization)
    // ZCVR_vk_import_dmabuf(&vk, dmabuf_fd, device.width, device.height);
    // ZCVR_vk_draw_boxes(&vk, boxes);

    // E. Bare-Metal Scanout
    // uint32_t fb_id = 0; // Wrap dmabuf_fd into DRM FB
    // ZCVR_drm_atomic_commit(&drm, fb_id);

    // F. Stream kernel metrics without blocking
    // ZCVR_bpf_poll(&bpf);

    // G. Event Polling (io_uring)
    // struct io_uring_cqe* cqe;
    // io_uring_peek_cqe(&ring, &cqe);
    // if (cqe) io_uring_cqe_seen(&ring, cqe);
  }
}