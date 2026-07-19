#pragma once
#include <CL/cl.h>

#include "ZCVR_mem.h"
#include "ZCVR_types.h"

typedef struct {
  cl_platform_id platform;
  cl_device_id device;
  cl_context context;
  cl_command_queue queue;

  cl_program program;
  cl_kernel kernel_nv12_to_rgb;

  // image / memory
  cl_mem imported_image;
  struct ZCVR_mem_manager *mem_manager;
} ZCVR_cl_ctx;

ZCVR_status_t ZCVR_cl_init(ZCVR_cl_ctx *ctx, struct ZCVR_mem_manager *mem_manager);

ZCVR_status_t ZCVR_cl_load_kernels(ZCVR_cl_ctx *ctx, const char *source);

/**
import_dmabuf
imports dma buffer for opencl kernel

*/
ZCVR_status_t ZCVR_cl_import_dmabuf(ZCVR_cl_ctx *ctx, int dmabuf_fd, size_t size,
                                  cl_mem *out_buf);

/**
convert_nv12_to_rgb
converts nv12 image to rgb image

*/

ZCVR_status_t ZCVR_cl_convert_nv12_to_rgb(ZCVR_cl_ctx *ctx, cl_mem nv12_buf,
                                        cl_mem rgb_buf, uint32_t width,
                                        uint32_t height);

ZCVR_status_t ZCVR_cl_deinit(ZCVR_cl_ctx *ctx);