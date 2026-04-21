#pragma once
#include <CL/cl.h>

#include "kbx_mem.h"
#include "kbx_types.h"

typedef struct {
  cl_platform_id platform;
  cl_device_id device;
  cl_context context;
  cl_command_queue queue;

  cl_program program;
  cl_kernel kernel_nv12_to_rgb;

  // image / memory
  cl_mem imported_image;
  kbx_mem_manager *mem_manager;
} kbx_cl_ctx;

kbx_status_t kbx_cl_init(kbx_cl_ctx *ctx, kbx_mem_manager *mem_manager);

kbx_status_t kbx_cl_load_kernels(kbx_cl_ctx *ctx, const char *source);

kbx_status_t kbx_cl_import_dmabuf(kbx_cl_ctx *ctx, int dmabuf_fd, size_t size,
                                  cl_mem *out_buf);

kbx_status_t kbx_cl_convert_nv12_to_rgb(kbx_cl_ctx *ctx, cl_mem nv12_image,
                                        cl_mem rgb_image, uint32_t width,
                                        uint32_t height);

kbx_status_t kbx_cl_deinit(kbx_cl_ctx *ctx);