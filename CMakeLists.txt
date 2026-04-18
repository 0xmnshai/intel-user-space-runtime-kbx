cmake_minimum_required(VERSION 4.2.3)
project(kbx_vision_runtime CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER clang++-18)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "-D_GNU_SOURCE -std=c++20 -Wall -Wextra -Wpedantic -Werror -march=native -mtune=native")

# Find Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(DRM REQUIRED libdrm)
pkg_check_modules(GBM REQUIRED gbm)
pkg_check_modules(LIBURING REQUIRED liburing)
pkg_check_modules(LIBBPF REQUIRED libbpf)
pkg_check_modules(NUMA REQUIRED numa)
find_package(OpenCL REQUIRED)
find_package(Vulkan REQUIRED)
set(OpenVINO_DIR "${CMAKE_SOURCE_DIR}/openvino/runtime/cmake")
find_package(OpenVINO REQUIRED)
find_library(ZE_LOADER ze_loader REQUIRED)

include_directories(include ${DRM_INCLUDE_DIRS} ${GBM_INCLUDE_DIRS} ${LIBURING_INCLUDE_DIRS} ${LIBBPF_INCLUDE_DIRS} ${NUMA_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS})

file(GLOB_RECURSE SRC_FILES "src/*.cc")

add_executable(${PROJECT_NAME} main.cc ${SRC_FILES})

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    ${DRM_LIBRARIES}
    ${GBM_LIBRARIES}
    ${LIBURING_LIBRARIES}
    ${LIBBPF_LIBRARIES}
    ${NUMA_LIBRARIES}
    ${Vulkan_LIBRARIES}
    ${ZE_LOADER}
    Vulkan::Vulkan
    OpenCL::OpenCL
    Threads::Threads
    openvino::runtime)

# # Compile OpenCL to SPIR-V offline
# add_custom_command(
#     TARGET ${PROJECT_NAME} PRE_BUILD
#     COMMAND ocloc compile -file ${CMAKE_SOURCE_DIR}/src/kernels/nv12_to_rgb.cl -device "*" -out_dir ${CMAKE_SOURCE_DIR}/src/kernels/
#     VERBATIM
# )

# # Compile eBPF program
# add_custom_command(
#     TARGET ${PROJECT_NAME} PRE_BUILD
#     COMMAND clang -O2 -g -target bpf -c ${CMAKE_SOURCE_DIR}/src/telemetry/i915_trace.bpf.cc -o ${CMAKE_SOURCE_DIR}/src/telemetry/i915_trace.bpf.o
#     COMMAND bpftool gen skeleton ${CMAKE_SOURCE_DIR}/src/telemetry/i915_trace.bpf.o
# )


# sed -i 's/find_package(OpenVINO REQUIRED)/set(OpenVINO_DIR "${CMAKE_SOURCE_DIR}\/openvino\/runtime\/cmake")\nfind_package(OpenVINO REQUIRED)/' CMakeLists.txt && cd build && cmake .. && make && ./kbx_vision_runtime