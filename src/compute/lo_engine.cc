
#include "kbx_compute.h"
#include "kbx_mem.h"
#include "kbx_types.h"
#include <CL/cl.h>
#include <cstdio>
#include <cstring>

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