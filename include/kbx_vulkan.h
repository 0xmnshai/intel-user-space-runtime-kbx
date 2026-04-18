#include "kbx_types.h"
#include <vulkan/vulkan.h>

typedef struct kbx_bbox {
  uint32_t x1;
  uint32_t y1;
  uint32_t x2;
  uint32_t y2;
} kbx_bbox_t;

typedef struct {
  VkInstance instance;
  VkDevice device;
  VkPhysicalDevice physical_device;
  VkQueue graphics_queue;
  VkCommandPool cmd_pool;
  VkCommandBuffer cmd_buffer;
  VkImage imported_dmabuf_image;
  VkDeviceMemory imported_memory;
} kbx_vulkan_context_t;

kbx_status_t kbx_vulkan_init(kbx_vulkan_context_t *ctx);

kbx_status_t kbx_vulkan_deinit(kbx_vulkan_context_t *ctx);

kbx_status_t kbx_vulkan_import_dma(kbx_vulkan_context_t *ctx,
                                   struct dma_buf_import_sync_fd dma_buf_import,
                                   uint32_t width, uint32_t height,
                                   VkFormat format, VkImageView *view);

kbx_status_t kbx_vulkan_export_dma(kbx_vulkan_context_t *ctx, VkImageView view);

kbx_status_t kbx_vulkan_draw_boxes(kbx_vulkan_context_t *ctx, VkImageView image,
                                   uint32_t width, uint32_t height,
                                   kbx_bbox_t *boxes, uint32_t num_boxes,
                                   VkSemaphore *sem);

kbx_status_t kbx_vulkan_detect_objects(kbx_vulkan_context_t *ctx,
                                       VkImageView image, uint32_t width,
                                       uint32_t height);