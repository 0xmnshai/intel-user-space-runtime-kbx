#include "ZCVR_types.h"
#include <vulkan/vulkan.h>

typedef struct ZCVR_bbox {
  uint32_t x1;
  uint32_t y1;
  uint32_t x2;
  uint32_t y2;
} ZCVR_bbox_t;

typedef struct {
  VkInstance instance;
  VkDevice device;
  VkPhysicalDevice physical_device;
  VkQueue graphics_queue;
  VkCommandPool cmd_pool;
  VkCommandBuffer cmd_buffer;
  VkImage imported_dmabuf_image;
  VkDeviceMemory imported_memory;
} ZCVR_vulkan_context_t;

ZCVR_status_t ZCVR_vulkan_init(ZCVR_vulkan_context_t *ctx);

ZCVR_status_t ZCVR_vulkan_deinit(ZCVR_vulkan_context_t *ctx);

ZCVR_status_t ZCVR_vulkan_import_dma(ZCVR_vulkan_context_t *ctx,
                                   struct dma_buf_import_sync_fd dma_buf_import,
                                   uint32_t width, uint32_t height,
                                   VkFormat format, VkImageView *view);

ZCVR_status_t ZCVR_vulkan_export_dma(ZCVR_vulkan_context_t *ctx, VkImageView view);

ZCVR_status_t ZCVR_vulkan_draw_boxes(ZCVR_vulkan_context_t *ctx, VkImageView image,
                                   uint32_t width, uint32_t height,
                                   ZCVR_bbox_t *boxes, uint32_t num_boxes,
                                   VkSemaphore *sem);

ZCVR_status_t ZCVR_vulkan_detect_objects(ZCVR_vulkan_context_t *ctx,
                                       VkImageView image, uint32_t width,
                                       uint32_t height);