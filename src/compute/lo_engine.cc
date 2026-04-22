
#include "kbx_compute.h"
#include "kbx_mem.h"
#include "kbx_types.h"
#include <cstdio>
#include <cstring>
#include <fcntl.h>

#include <CL/cl.h>
#include <CL/cl_ext.h>

kbx_status_t kbx_cl_init(kbx_cl_ctx *ctx, kbx_mem_manager *mem_manager) {
  if (!ctx || !mem_manager) {
    return KBX_STATUS_ERR_IO;
  }

  ctx->mem_manager = mem_manager;

  cl_int ret;
  cl_uint num_platforms = 0;
  ret = clGetPlatformIDs(0, nullptr, &num_platforms);
  if (ret != CL_SUCCESS || num_platforms == 0) {
    fprintf(stderr, "[KBX][CL] No OpenCL platforms found. (Error: %d)\n", ret);
    return KBX_STATUS_ERR_IO;
  }

  cl_platform_id *platforms = new cl_platform_id[num_platforms];
  clGetPlatformIDs(num_platforms, platforms, nullptr);

  cl_platform_id best_platform = nullptr;
  cl_device_id best_device = nullptr;
  int best_score = -1;

  for (cl_uint i = 0; i < num_platforms; ++i) {
    char p_name[256] = {0};
    clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(p_name), p_name,
                      nullptr);

    cl_uint num_devices = 0;
    ret = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, 0, nullptr,
                         &num_devices);
    if (ret != CL_SUCCESS || num_devices == 0)
      continue;

    cl_device_id *devices = new cl_device_id[num_devices];
    clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, num_devices, devices,
                   nullptr);

    for (cl_uint j = 0; j < num_devices; ++j) {
      cl_device_type type;
      clGetDeviceInfo(devices[j], CL_DEVICE_TYPE, sizeof(type), &type, nullptr);

      int score = 0;
      if (type & CL_DEVICE_TYPE_GPU) {
        score = 50;
        if (strstr(p_name, "Intel") != nullptr) {
          score += 50; // High priority for Intel GPU
        }
      } else if (type & CL_DEVICE_TYPE_ACCELERATOR) {
        score = 20;
      } else if (type & CL_DEVICE_TYPE_CPU) {
        score = 10;
      }

      if (score > best_score) {
        best_score = score;
        best_platform = platforms[i];
        best_device = devices[j];
      }
    }
    delete[] devices;
  }
  delete[] platforms;

  if (!best_device) {
    fprintf(stderr, "[KBX][CL] Failed to find any suitable OpenCL device.\n");
    return KBX_STATUS_ERR_IO;
  }

  // 4. Log selected hardware
  char d_name[256] = {0};
  char p_name[256] = {0};
  char d_version[256] = {0};
  clGetPlatformInfo(best_platform, CL_PLATFORM_NAME, sizeof(p_name), p_name,
                    nullptr);
  clGetDeviceInfo(best_device, CL_DEVICE_NAME, sizeof(d_name), d_name, nullptr);
  clGetDeviceInfo(best_device, CL_DEVICE_VERSION, sizeof(d_version), d_version,
                  nullptr);

  printf("[KBX][CL] Selected Platform: %s\n", p_name);
  printf("[KBX][CL] Selected Device:   %s\n", d_name);
  printf("[KBX][CL] Device Version:    %s\n", d_version);

  if (best_score < 100) {
    printf("[KBX][CL] Warning: High-performance Intel GPU not found. Using "
           "fallback: %s\n",
           d_name);
  }

  // 5. Create Context
  const cl_context_properties props[] = {
      CL_CONTEXT_PLATFORM, (cl_context_properties)best_platform, 0};
  cl_context context =
      clCreateContext(props, 1, &best_device, NULL, NULL, &ret);
  if (ret != CL_SUCCESS) {
    fprintf(stderr, "[KBX][CL] Failed to create OpenCL context. (Error: %d)\n",
            ret);
    return KBX_STATUS_ERR_IO;
  }

  // 6. Create Command Queue
  const cl_queue_properties queue_props[] = {
      CL_QUEUE_PROPERTIES, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, 0};

  cl_command_queue command_queue = clCreateCommandQueueWithProperties(
      context, best_device, queue_props, &ret);

  if (ret != CL_SUCCESS) {
    // Attempt fallback to synchronous queue if out-of-order is not supported
    cl_command_queue fallback_queue =
        clCreateCommandQueueWithProperties(context, best_device, nullptr, &ret);
    if (ret != CL_SUCCESS) {
      fprintf(stderr, "[KBX][CL] Failed to create command queue. (Error: %d)\n",
              ret);
      clReleaseContext(context);
      return KBX_STATUS_ERR_IO;
    }
    command_queue = fallback_queue;
    printf(
        "[KBX][CL] Note: Out-of-order execution disabled for this device.\n");
  }

  // 7. Store results in context
  ctx->platform = best_platform;
  ctx->device = best_device;
  ctx->context = context;
  ctx->queue = command_queue;

  return KBX_STATUS_SUCCESS;
}

kbx_status_t kbx_cl_load_kernels(kbx_cl_ctx *ctx, const char *source) {
  if (!ctx || !source || !ctx->mem_manager) {
    return KBX_STATUS_ERR_IO;
  }

  cl_int ret;
  size_t length = strlen(source);
  char *source_str = (char *)kbx_mem_alloc(ctx->mem_manager, length + 1);
  if (!source_str) {
    fprintf(stderr, "[KBX][CL] Failed to allocate memory for source string.\n");
    return KBX_STATUS_ERR_NOMEM;
  }
  memcpy(source_str, source, length + 1);

  ctx->program = clCreateProgramWithSource(
      ctx->context, 1, (const char **)&source_str, &length, &ret);
  if (ret != CL_SUCCESS) {
    fprintf(stderr, "[KBX][CL] Failed to create program. (Error: %d)\n", ret);
    kbx_mem_free(ctx->mem_manager, source_str);
    return KBX_STATUS_ERR_GPU;
  }

  ret =
      clBuildProgram(ctx->program, 1, &ctx->device, nullptr, nullptr, nullptr);
  if (ret != CL_SUCCESS) {
    fprintf(stderr, "[KBX][CL] Failed to build program. (Error: %d)\n", ret);

    size_t log_size;
    clGetProgramBuildInfo(ctx->program, ctx->device, CL_PROGRAM_BUILD_LOG, 0,
                          nullptr, &log_size);
    char *log = (char *)kbx_mem_alloc(ctx->mem_manager, log_size + 1);
    if (log) {
      clGetProgramBuildInfo(ctx->program, ctx->device, CL_PROGRAM_BUILD_LOG,
                            log_size, log, nullptr);
      fprintf(stderr, "[KBX][CL] Build Log:\n%s\n", log);
      kbx_mem_free(ctx->mem_manager, log);
    }

    kbx_mem_free(ctx->mem_manager, source_str);
    clReleaseProgram(ctx->program);
    ctx->program = nullptr;
    return KBX_STATUS_ERR_GPU;
  }

  // Create known kernels
  ctx->kernel_nv12_to_rgb = clCreateKernel(ctx->program, "nv12_to_rgb", &ret);
  if (ret != CL_SUCCESS) {
    fprintf(stderr,
            "[KBX][CL] Failed to create kernel 'nv12_to_rgb'. (Error: %d)\n",
            ret);
  }

  printf("[KBX][CL] Kernels loaded successfully.\n");
  kbx_mem_free(ctx->mem_manager, source_str);
  return KBX_STATUS_SUCCESS;
}

kbx_status_t kbx_cl_import_dmabuf(kbx_cl_ctx *ctx, int dmabuf_fd, size_t size,

                                  cl_mem *out_buf) {
  if (!ctx || dmabuf_fd < 0 || size == 0 || !out_buf) {
    fprintf(stderr, "[KBX][CL] Invalid arguments for dmabuf import.\n");
    return KBX_STATUS_ERR_IO;
  }

  // Check at runtime that the platform actually exposes the extension
  char exts[1024];
  size_t ext_size = 0;
  cl_int ret =
      clGetDeviceInfo(ctx->device, CL_DEVICE_EXTENSIONS, 0, NULL, &ext_size);
  if (ret != CL_SUCCESS) {
    fprintf(stderr, "[KBX][CL] Failed to get device extensions. (Error: %d)\n",
            ret);
    return KBX_STATUS_ERR_GPU;
  }
  ret = clGetDeviceInfo(ctx->device, CL_DEVICE_EXTENSIONS, ext_size,
                        (void *)exts, NULL);
  if (ret != CL_SUCCESS) {
    fprintf(stderr, "[KBX][CL] Failed to get device extensions. (Error: %d)\n",
            ret);
    return KBX_STATUS_ERR_GPU;
  }
  exts[ext_size] = '\0'; // Ensure null-termination

  if (strstr(exts, "cl_arm_import_memory")) {
    printf("[KBX][CL] cl_arm_import_memory extension found.\n");
  } else {
    printf("[KBX][CL] cl_arm_import_memory extension not found.\n");
  }

  if (strstr(exts, "cl_khr_external_memory")) {
    printf("[KBX][CL] cl_khr_external_memory extension found.\n");
  } else {
    printf("[KBX][CL] cl_khr_external_memory extension not found.\n");
  }

#if defined(cl_arm_import_memory)

  /* Look up the extension entry-point at runtime */
  clImportMemoryARM_fn clImportMemoryARM =
      (clImportMemoryARM_fn)clGetExtensionFunctionAddressForPlatform(
          ctx->platform, "clImportMemoryARM");

  if (!clImportMemoryARM) {
    fprintf(stderr, "[KBX][CL] clImportMemoryARM not found.\n");
    return KBX_STATUS_ERR_IO;
  }

  cl_import_properties_arm props[] = {
      CL_IMPORT_TYPE_ARM, CL_IMPORT_TYPE_DMA_BUF_ARM,
      CL_IMPORT_DMA_BUF_DATA_CONSISTENCY_WITH_HOST_ARM,
      CL_IMPORT_DMA_BUF_DATA_CONSISTENCY_WITH_HOST_ARM, /* optional */
      0};

  cl_int err;
  cl_mem buf = clImportMemoryARM(ctx->context, CL_MEM_READ_WRITE, props,
                                 (void *)(intptr_t)dmabuf_fd, size, &err);
  if (err != CL_SUCCESS || !buf) {
    fprintf(stderr,
            "[KBX][CL] Failed to import dmabuf using clImportMemoryARM. "
            "(Error: %d)\n",
            err);
    return KBX_STATUS_CL_ERROR;
  }

#elif defined(cl_khr_external_memory)
  cl_int err;
  cl_mem_properties props[] = {CL_EXTERNAL_MEMORY_HANDLE_DMA_BUF_KHR,
                               (cl_mem_properties)dmabuf_fd, 0};

  cl_mem buf =
      clCreateBufferWithProperties(ctx->context, props, CL_MEM_READ_WRITE, size,
                                   NULL, /* host_ptr – unused   */
                                   &err);
  if (err != CL_SUCCESS || !buf) {
    fprintf(
        stderr,
        "[KBX][CL] Failed to import dmabuf using clCreateBufferWithProperties. "
        "(Error: %d)\n",
        err);
    return KBX_STATUS_CL_ERROR;
  }

#else
  /* Neither extension is available at compile time */
  (void)dmabuf_fd;
  (void)size;
  return KBX_STATUS_NOT_SUPPORTED;
#endif

  if (ctx->imported_image) {
    clReleaseMemObject(ctx->imported_image);
  }

  ctx->imported_image = buf;
  *out_buf = buf;

  return KBX_STATUS_SUCCESS;
}

/**
NV12 is a common 8-bit YUV 4:2:0 pixel format used in video processing and
machine vision, storing data in two planes: a full-resolution Y (luminance)
plane followed by an interleaved U/V (chrominance) plane. It uses 12 bits per
pixel, with chroma subsampled by a factor of 2 horizontally and vertically
*/

kbx_status_t kbx_cl_convert_nv12_to_rgb(kbx_cl_ctx *ctx, cl_mem nv12_buf,
                                        cl_mem rgb_buf, uint32_t width,
                                        uint32_t height) {
  if (!ctx || !nv12_buf || !rgb_buf || width == 0 || height == 0) {
    fprintf(stderr, "[KBX][CL] Invalid arguments for NV12->RGB conversion.\n");
    return KBX_STATUS_ERR_IO;
  }

  if (!ctx->kernel_nv12_to_rgb) {
    fprintf(stderr, "[KBX][CL] kernel_nv12_to_rgb not loaded.\n");
    return KBX_STATUS_ERR_GPU;
  }

  cl_int ret;

  ret = clSetKernelArg(ctx->kernel_nv12_to_rgb, 0, sizeof(cl_mem), &nv12_buf);
  if (ret != CL_SUCCESS) {
    fprintf(stderr, "[KBX][CL] Failed to set arg 0 for kernel. (Error: %d)\n",
            ret);
    return KBX_STATUS_ERR_GPU;
  }

  ret = clSetKernelArg(ctx->kernel_nv12_to_rgb, 1, sizeof(cl_mem), &rgb_buf);
  if (ret != CL_SUCCESS) {
    fprintf(stderr, "[KBX][CL] Failed to set arg 1 for kernel. (Error: %d)\n",
            ret);
    return KBX_STATUS_ERR_GPU;
  }

  ret = clSetKernelArg(ctx->kernel_nv12_to_rgb, 2, sizeof(uint32_t), &width);
  if (ret != CL_SUCCESS) {
    fprintf(stderr, "[KBX][CL] Failed to set arg 2 for kernel. (Error: %d)\n",
            ret);
    return KBX_STATUS_ERR_GPU;
  }

  ret = clSetKernelArg(ctx->kernel_nv12_to_rgb, 3, sizeof(uint32_t), &height);
  if (ret != CL_SUCCESS) {
    fprintf(stderr, "[KBX][CL] Failed to set arg 3 for kernel. (Error: %d)\n",
            ret);
    return KBX_STATUS_ERR_GPU;
  }

  const size_t local[2] = {16, 16};
  const size_t global[2] = {((width + local[0] - 1) / local[0]) * local[0],
                            ((height + local[1] - 1) / local[1]) * local[1]};

  cl_event event;
  ret = clEnqueueNDRangeKernel(ctx->queue, ctx->kernel_nv12_to_rgb,
                               2,       // dimensions
                               nullptr, // global offset
                               global,
                               local, // local work size
                               0,       // num_events_in_wait_list
                               nullptr, // event_wait_list
                               &event); // event
  if (ret != CL_SUCCESS) {
    fprintf(stderr, "[KBX][CL] Failed to enqueue kernel. (Error: %d)\n", ret);
    return KBX_STATUS_ERR_GPU;
  }

  ret = clFinish(ctx->queue);
  if (ret != CL_SUCCESS) {
    fprintf(stderr, "[KBX][CL] Failed to finish queue. (Error: %d)\n", ret);
    return KBX_STATUS_ERR_GPU;
  }

  ret = clWaitForEvents(1, &event);
  clReleaseEvent(event);

  return KBX_STATUS_SUCCESS;
}
void ManualNV12ToRGB(unsigned char *nv12, unsigned char *rgb, int width,
                     int height) {
  unsigned char *y_plane = nv12;
  unsigned char *uv_plane = nv12 + (width * height);

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int y_idx = y * width + x;
      // UV values are shared for every 2x2 block of pixels
      int uv_idx = (y / 2) * width + (x & ~1);

      int Y = y_plane[y_idx];
      int U = uv_plane[uv_idx] - 128;
      int V = uv_plane[uv_idx + 1] - 128;

      // Apply conversion formulas and clamp to [0, 255]
      // Using standard BT.601 limited range
      int r = (int)(1.164 * (Y - 16) + 1.596 * V);
      int g = (int)(1.164 * (Y - 16) - 0.813 * V - 0.391 * U);
      int b = (int)(1.164 * (Y - 16) + 2.018 * U);

      rgb[y_idx * 3 + 0] = std::max(0, std::min(255, r));
      rgb[y_idx * 3 + 1] = std::max(0, std::min(255, g));
      rgb[y_idx * 3 + 2] = std::max(0, std::min(255, b));
    }
  }
}
