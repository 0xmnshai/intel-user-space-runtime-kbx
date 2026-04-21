#include "kbx_vulkan.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <vulkan/vulkan_core.h>

kbx_status_t kbx_vulkan_init(kbx_vulkan_context_t *ctx) {
  // Vulkan Instance
  VkApplicationInfo app_info = {};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "KBX Vulkan Renderer";
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "KBX";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.apiVersion = VK_API_VERSION_1_2;

  VkInstanceCreateInfo instance_create_info = {};
  instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_create_info.pApplicationInfo = &app_info;
  instance_create_info.enabledExtensionCount = 0;
  instance_create_info.ppEnabledExtensionNames = NULL;

  // Try enabling validation layers
  const char* validation_layer = "VK_LAYER_KHRONOS_validation";
  bool validation_layer_supported = false;

  uint32_t layer_count;
  vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
  std::vector<VkLayerProperties> available_layers(layer_count);
  vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

  for (const auto& layer_properties : available_layers) {
    if (strcmp(validation_layer, layer_properties.layerName) == 0) {
      validation_layer_supported = true;
      break;
    }
  }

  std::vector<const char*> enabled_layers;
#ifndef NDEBUG
  if (validation_layer_supported) {
    enabled_layers.push_back(validation_layer);
    printf("Enabling Vulkan validation layer: %s\n", validation_layer);
  } else {
    printf("Vulkan validation layer requested but not available: %s\n", validation_layer);
  }
#endif

  instance_create_info.enabledLayerCount = static_cast<uint32_t>(enabled_layers.size());
  instance_create_info.ppEnabledLayerNames = enabled_layers.empty() ? nullptr : enabled_layers.data();

  if (vkCreateInstance(&instance_create_info, NULL, &ctx->instance) !=
      VK_SUCCESS) {
    printf("Failed to create Vulkan instance\n");
    return KBX_STATUS_ERR_GFX;
  }

  // Physical Device
  uint32_t physical_device_count = 0;
  vkEnumeratePhysicalDevices(ctx->instance, &physical_device_count, NULL);
  if (physical_device_count == 0) {
    printf("No physical devices found\n");
    vkDestroyInstance(ctx->instance, NULL);
    return KBX_STATUS_ERR_GFX;
  }

  std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
  vkEnumeratePhysicalDevices(ctx->instance, &physical_device_count,
                             physical_devices.data());

  ctx->physical_device = VK_NULL_HANDLE;
  for (uint32_t i = 0; i < physical_device_count; i++) {
    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(physical_devices[i], &device_properties);

    if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
        device_properties.deviceType ==
            VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
      ctx->physical_device = physical_devices[i];
      printf("Selected Physical Device: %s\n", device_properties.deviceName);
      break;
    }
  }

  // Fallback to the first available if no discrete or integrated GPU was found.
  if (ctx->physical_device == VK_NULL_HANDLE) {
    ctx->physical_device = physical_devices[0];
  }

  // Queue Families
  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical_device,
                                           &queue_family_count, NULL);
  std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(
      ctx->physical_device, &queue_family_count, queue_families.data());

  uint32_t graphics_queue_family_index = UINT32_MAX;
  for (uint32_t i = 0; i < queue_family_count; i++) {
    if (queue_families[i].queueCount > 0 &&
        (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
      graphics_queue_family_index = i;
      break;
    }
  }

  if (graphics_queue_family_index == UINT32_MAX) {
    printf("Failed to find graphics queue family\n");
    vkDestroyInstance(ctx->instance, NULL);
    return KBX_STATUS_ERR_GFX;
  }

  // Device
  float queue_priority = 1.0f;
  VkDeviceQueueCreateInfo queue_create_info = {};
  queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_create_info.queueFamilyIndex = graphics_queue_family_index;
  queue_create_info.queueCount = 1;
  queue_create_info.pQueuePriorities = &queue_priority;

  const char *device_extensions[] = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME, // Kept to match original extension list
      VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
      VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
      VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
      VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
      VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
  };

  VkDeviceCreateInfo device_create_info = {};
  device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_create_info.queueCreateInfoCount = 1;
  device_create_info.pQueueCreateInfos = &queue_create_info;
  device_create_info.enabledExtensionCount =
      sizeof(device_extensions) / sizeof(device_extensions[0]);
  device_create_info.ppEnabledExtensionNames = device_extensions;
  device_create_info.enabledLayerCount = 0;
  device_create_info.ppEnabledLayerNames = NULL;

  if (vkCreateDevice(ctx->physical_device, &device_create_info, NULL,
                     &ctx->device) != VK_SUCCESS) {
    printf("Failed to create Vulkan device\n");
    vkDestroyInstance(ctx->instance, NULL);
    return KBX_STATUS_ERR_GFX;
  }

  // Graphics Queue
  vkGetDeviceQueue(ctx->device, graphics_queue_family_index, 0,
                   &ctx->graphics_queue);

  // Command Pool
  VkCommandPoolCreateInfo command_pool_create_info = {};
  command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_create_info.queueFamilyIndex = graphics_queue_family_index;
  command_pool_create_info.flags =
      VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  if (vkCreateCommandPool(ctx->device, &command_pool_create_info, NULL,
                          &ctx->cmd_pool) != VK_SUCCESS) {
    printf("Failed to create Vulkan command pool\n");
    vkDestroyDevice(ctx->device, NULL);
    vkDestroyInstance(ctx->instance, NULL);
    return KBX_STATUS_ERR_GFX;
  }

  // Command Buffer
  VkCommandBufferAllocateInfo command_buffer_allocate_info = {};
  command_buffer_allocate_info.sType =
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_buffer_allocate_info.commandPool = ctx->cmd_pool;
  command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_buffer_allocate_info.commandBufferCount = 1;

  if (vkAllocateCommandBuffers(ctx->device, &command_buffer_allocate_info,
                               &ctx->cmd_buffer) != VK_SUCCESS) {
    printf("Failed to allocate Vulkan command buffer\n");
    vkDestroyCommandPool(ctx->device, ctx->cmd_pool, NULL);
    vkDestroyDevice(ctx->device, NULL);
    vkDestroyInstance(ctx->instance, NULL);
    return KBX_STATUS_ERR_GFX;
  }

  return KBX_STATUS_SUCCESS;
}

kbx_status_t kbx_vulkan_deinit(kbx_vulkan_context_t *ctx) {
  if (ctx->device) {
    vkDeviceWaitIdle(ctx->device);
  }

  if (ctx->cmd_pool && ctx->cmd_buffer) {
    vkFreeCommandBuffers(ctx->device, ctx->cmd_pool, 1, &ctx->cmd_buffer);
  }
  if (ctx->cmd_pool) {
    vkDestroyCommandPool(ctx->device, ctx->cmd_pool, NULL);
  }
  if (ctx->device) {
    vkDestroyDevice(ctx->device, NULL);
  }
  if (ctx->instance) {
    vkDestroyInstance(ctx->instance, NULL);
  }

  printf("Vulkan deinitialized\n");
  return KBX_STATUS_SUCCESS;
}
