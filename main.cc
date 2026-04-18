#include "kbx_mem.h"
#include "kbx_types.h"
#include "kbx_vulkan.h"

#include <fcntl.h>
#include <iostream>
#include <liburing.h>
#include <unistd.h>

int main() {

  std::cout << "[KBX] Initializing Runtime..." << std::endl;

  kbx_mem_manager mem_manager;
  if (kbx_mem_pool_init(&mem_manager, 1024) != KBX_STATUS_SUCCESS)
    std::cout << "[KBX] Failed to initialize memory manager" << std::endl;
  else
    std::cout << "[KBX] Memory manager initialized successfully" << std::endl;

  return 0;
}