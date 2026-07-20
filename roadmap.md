# KBX Vision Runtime — Expert Engineering Roadmap

> **Hardware:** Intel i7 11th Gen (Tiger Lake), Iris Xe Graphics (Gen12 LP, 96 EUs), 16GB DDR4, Ubuntu 26.04, kernel 6.11+  
> **Paradigm:** Procedural C++20, zero-copy, zero-syscall hot path, bare-metal Intel  
> **Target:** 1080p @ 60fps, photon-to-pixel < 16.67ms

---

## Hardware Reality for Tiger Lake

These are the critical differences between the document's Meteor Lake target and your machine. Misunderstanding them will cause silent correctness failures.

**No NPU.** 11th gen Tiger Lake has no Neural Processing Unit. Remove all `"NPU"` device strings from OpenVINO calls. Use `"GPU"` only. YOLOv8n INT8 runs at 3–8ms on Iris Xe via GPU path.

**Unified DDR4 memory.** Your iGPU has no separate VRAM — GPU and CPU share the same 16GB DDR4 pool through the same memory controller. This means:
- There is no PCIe bandwidth bottleneck
- DMA-BUF pages are accessible by CPU and GPU without cross-socket traffic
- The zero-copy argument is strictly stronger — you never cross a PCIe bus
- DDR4 total bandwidth (~51.2 GB/s dual-channel) is shared between CPU, GPU compute, GPU display scanout, and camera DMA simultaneously. You cannot saturate it with 1080p30, but don't add unnecessary concurrent access patterns

**Driver is `i915`, not `xe`.** Ubuntu 26.04 ships with kernel 6.11/6.12. `xe` driver exists for Tiger Lake on 6.8+ but `i915` remains the default. Confirm with `lspci -k | grep -A3 "VGA compatible"`. Stick with `i915` for the entire initial build. The `ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF` flag works on `i915`; if it fails use `ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_FD`.

**GEM handles.** On Tiger Lake iGPU, `/dev/dri/renderD128` (render node, no root) is used for GBM buffer allocation and Level Zero. `/dev/dri/card0` (needs `video` group or root) is used only for DRM/KMS display output.

**eBPF kprobe symbols.** The `i915_request_add` and `i915_request_retire` kprobe symbols are present in the 6.11 `i915` driver. If building with `xe`, symbol names differ. Verify with `grep i915_request /proc/kallsyms` before writing the BPF program.

---

## Project Structure

```
kbx_vision/
├── CMakeLists.txt                    # root build graph
├── CMakePresets.json                 # release/debug/asan/bpf presets
├── .clang-format                     # brace style, column limit 100
├── .clang-tidy                       # modernize, performance, readability
├── cmake/
│   ├── toolchain/
│   │   ├── x86_64-clang.cmake        # clang-18, -O3 -march=native -fno-exceptions -fno-rtti
│   │   └── bpf.cmake                 # clang -target bpf -g -O2, pahole BTF stripping
│   ├── modules/
│   │   ├── FindLevelZero.cmake       # pkg-config ze_loader → LevelZero::LevelZero
│   │   ├── FindOpenVINO.cmake        # find_package wrapper OpenVINO 2024
│   │   ├── FindLibDRM.cmake          # libdrm + libgbm, version >= 2.4.112
│   │   └── FindLibBPF.cmake          # libbpf >= 1.3 for ringbuf + CO-RE skeleton
│   └── targets/
│       ├── kernels.cmake             # ocloc PRE_BUILD: cl/ → spv/, device=tgllp
│       ├── shaders.cmake             # glslc PRE_BUILD: glsl/ → spv/
│       └── bpf.cmake                 # clang BPF compile, bpftool gen skeleton
├── include/
│   └── kbx/
│       ├── core/
│       │   ├── compiler.h            # LIKELY/UNLIKELY, ALWAYS_INLINE, NOINLINE, PACKED
│       │   ├── types.h               # u8/u16/u32/u64/i32/f32, kbx_status_t, KBX_CHECK
│       │   ├── assert.h              # KBX_ASSERT: __builtin_trap() release, abort+msg debug
│       │   └── log.h                 # lock-free ring-buffer logger, no heap, async drain
│       ├── mem/
│       │   ├── arena.h               # bump allocator over MAP_HUGETLB pool        [HOT PATH]
│       │   ├── ring.h                # SPSC queue: head/tail in separate alignas(64)[HOT PATH]
│       │   └── numa.h                # sysfs parse, libnuma preferred-node API
│       ├── io/
│       │   ├── dmabuf.h              # fd export, implicit fence wait, sync_file_merge
│       │   └── v4l2.h                # open, S_FMT NV12, REQBUFS DMABUF, STREAMON
│       ├── compute/
│       │   ├── l0_ctx.h              # ze init, immediate command list, events [HOT PATH]
│       │   └── l0_kernel.h           # SPIR-V module, kernel create, DMA-BUF import
│       ├── infer/
│       │   └── ov_session.h          # ov::Core, ClContext from L0, RemoteTensor, NMS
│       ├── gfx/
│       │   ├── vk_ctx.h              # VkInstance/Device + dma_buf + external_semaphore
│       │   ├── vk_pass.h             # render pass, push-const pipeline, semaphore export
│       │   └── drm_kms.h             # CRTC/plane/connector enum, property query, atomic
│       ├── reactor/
│       │   └── uring.h               # SQPOLL init, prep_ioctl VIDIOC_DQBUF, CQE peek [HOT]
│       └── telemetry/
│           ├── bpf_loader.h          # skeleton open/load, ring_buffer new+poll
│           └── pmu.h                 # perf_event_open, i915/render-busy/, read loop
├── src/
│   ├── core/
│   │   └── log.cc
│   ├── mem/
│   │   ├── arena.cc                  # mmap MAP_HUGETLB|MAP_LOCKED|MAP_POPULATE, bump ptr
│   │   ├── ring.cc                   # acquire/release atomics, padding verification
│   │   └── numa.cc                   # sysfs parse, numa_set_preferred
│   ├── io/
│   │   ├── dmabuf.cc                 # VIDIOC_EXPBUF, sync_file_merge, close lifecycle
│   │   └── v4l2.cc                   # full init: QUERYCAP, S_FMT, GBM bo alloc, QBUF loop
│   ├── compute/
│   │   ├── l0_ctx.cc                 # zeInit, zeDriverGet, zeDeviceGet GPU, immediate CL
│   │   └── l0_kernel.cc              # zeModuleCreate SPIR-V, zeKernelCreate, event chain
│   ├── infer/
│   │   └── ov_session.cc             # ClContext interop, RemoteTensor, infer, [1,84,8400] NMS
│   ├── gfx/
│   │   ├── vk_ctx.cc                 # instance layers, vkGetMemoryFdPropertiesKHR proc load
│   │   ├── vk_pass.cc                # DMA-BUF VkDeviceMemory import, render pass, push draw
│   │   └── drm_kms.cc                # drmModeObjectGetProperties, drmPrimeFDToHandle, atomic
│   ├── reactor/
│   │   └── uring.cc                  # SQPOLL, prep_ioctl VIDIOC_DQBUF, peek_cqe spin
│   ├── telemetry/
│   │   ├── bpf_loader.cc             # kbx_trace_bpf__open_and_load, ring_buffer__new, cb
│   │   └── pmu.cc                    # perf_event_open, rate calc
│   └── main.cc                       # subsystem init order, reactor loop, fence chain [HOT]
├── kernels/
│   ├── cl/
│   │   └── nv12_to_rgb.cl            # intel_reqd_sub_group_size(16), BT.709 math  [HOT]
│   └── spv/                          # GENERATED by ocloc PRE_BUILD — gitignore *.spv
├── shaders/
│   ├── glsl/
│   │   ├── bbox.vert.glsl            # push_constant boxes[32], NDC transform, 6v/box
│   │   └── bbox.frag.glsl            # flat color from push_constant color vec4
│   └── spv/                          # GENERATED by glslc PRE_BUILD — gitignore
├── bpf/
│   ├── kbx_trace.bpf.c               # kprobe i915_request_add/retire, ringbuf, hash latency
│   ├── vmlinux.h                     # GENERATED: bpftool btf dump vmlinux format c
│   └── kbx_trace.skel.h              # GENERATED: bpftool gen skeleton kbx_trace.bpf.o
├── models/
│   ├── README.md                     # omz_downloader --name yolov8n, pot INT8 steps
│   ├── yolov8n_int8.xml              # DOWNLOAD from OpenVINO model zoo
│   └── yolov8n_int8.bin              # weight blob companion
├── tests/
│   ├── CMakeLists.txt                # gtest targets, -DKBX_TEST_HW=ON for hardware tests
│   ├── unit/
│   │   ├── test_arena.cc             # hugepage alloc, alignment, overflow detection
│   │   ├── test_ring.cc              # SPSC correctness under producer/consumer stress
│   │   └── test_dmabuf.cc            # fd export/import/fence without real camera
│   └── integration/
│       ├── test_l0_kernel.cc         # synthetic DMA-BUF, run NV12→RGB, readback verify
│       ├── test_ov_infer.cc          # synthetic input tensor, decode YOLO, validate bbox
│       └── test_full_pipeline.cc     # v4l2loopback source, end-to-end latency timing
├── tools/
│   ├── gen_vmlinux.sh                # bpftool btf dump /sys/kernel/btf/vmlinux format c
│   ├── gen_bpf_skel.sh               # bpftool gen skeleton build/bpf/kbx_trace.bpf.o
│   ├── stream_loopback.sh            # modprobe v4l2loopback + ffmpeg → /dev/video0
│   └── benchmark.sh                  # perf stat i915/render-busy/ + intel_gpu_top
└── docs/
    ├── architecture.md               # data-flow diagram, fence chain, memory ownership
    ├── hardware_errata.md            # Tiger Lake i915 quirks, modifier compat
    └── bringup.md                    # stage-by-stage validation, sysctl, modetest
```

---

## Package Installation (Ubuntu 26.04)

```bash
sudo apt install -y \
    cmake ninja-build clang-18 lld-18 clang-tidy-18 clang-format-18 \
    libdrm-dev libgbm-dev \
    libvulkan-dev vulkan-tools glslang-tools \
    liburing-dev \
    libbpf-dev linux-headers-$(uname -r) pahole \
    libnuma-dev \
    intel-opencl-icd intel-ocloc \
    intel-level-zero-gpu level-zero-dev \
    bpftool \
    ffmpeg v4l-utils v4l2loopback-dkms \
    libgtest-dev

# OpenVINO 2024 — tarball from Intel (apt repo often lags)
wget https://storage.openvinotoolkit.org/repositories/openvino/packages/2024.5/linux/l_openvino_toolkit_ubuntu24_*.tgz
tar xf l_openvino*.tgz && cd l_openvino*
sudo ./install_dependencies/install_openvino_dependencies.sh
echo "source $(pwd)/setupvars.sh" >> ~/.bashrc
source setupvars.sh

# Verify GPU driver
lspci -k | grep -A3 "VGA compatible"
# Expect: Kernel driver in use: i915

# Verify Level Zero sees Iris Xe
ze_info
# Expect: Device: Intel(R) Iris(R) Xe Graphics ... [GPU]

# Verify huge pages
cat /proc/meminfo | grep HugePages_Total
# If 0: sudo sysctl -w vm.nr_hugepages=512
echo 'vm.nr_hugepages=512' | sudo tee /etc/sysctl.d/hugepages.conf

# Add yourself to video group for DRM/KMS
sudo usermod -aG video $USER && newgrp video
```

---

## System Data Flow (Photon → Pixel)

```
Camera DMA
    │ writes NV12 → GBM buffer (DDR4, GPU-accessible)
    │ VIDIOC_DQBUF signals io_uring CQE
    ▼
Level Zero NV12→RGB SPIR-V kernel
    │ waits: DMA-BUF implicit fence (camera write complete)
    │ imports: zeImageCreate(ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF)
    │ dispatches: 1920×1080 / 16 = 129,600 SIMD-16 waves
    │ signals: ze_event_handle_t conv_done
    ▼
OpenVINO YOLOv8n INT8 inference
    │ waits: ze_event conv_done
    │ input: ze_image_handle_t wrapped as ov::RemoteTensor (zero-copy)
    │ output: [1, 84, 8400] float32 → CPU decodes bbox + NMS
    │ signals: infer() returns (CPU unblocks)
    ▼
Vulkan bbox overlay
    │ waits: VkFence from previous frame (buffer not in-flight)
    │ imports: VkDeviceMemory from same DMA-BUF fd
    │ draws: 6 vertices × N boxes via push constants (no descriptor set)
    │ signals: VkSemaphore exported as sync_fd
    ▼
DRM/KMS atomic commit
    │ waits: IN_FENCE_FD = VkSemaphore sync_fd
    │ calls: drmModeAtomicCommit(NONBLOCK | PAGE_FLIP_EVENT)
    │ signals: DRM_EVENT_FLIP_COMPLETE at VSYNC (16.67ms boundary)
    ▼
Display scanout at VSYNC
    │ signals: DMA-BUF out-fence (buffer safe to reuse)
    ▼
VIDIOC_QBUF — return buffer to V4L2 for next frame
```

CPU touches exactly one thing per frame: the `[1, 84, 8400]` float32 output tensor decode + NMS, ~50μs of scalar work. Everything else is GPU-side or kernel-side.

---

## Phase 0 — Toolchain & C++20 Foundation

### Learn First

`alignas(std::hardware_destructive_interference_size)` compiles to `alignas(64)` on x86 — that is the L1 cache line size since Intel Nehalem (2008). Any two atomic variables sharing a 64-byte line cause *false sharing*: a write to variable A by core 0 invalidates the entire cache line in core 1, forcing core 1 to fetch the line over the L3 cache coherency bus even though it only reads variable B. Each such invalidation costs 40–100ns on Intel.

`std::atomic<uint32_t>` with `memory_order_relaxed` generates *no fence instruction* on x86 due to the Total Store Order (TSO) memory model — stores are globally visible in program order. `memory_order_seq_cst` generates a `MFENCE` instruction (~40 cycles full pipeline drain). `memory_order_acquire` on a load and `memory_order_release` on a store generate only a compiler barrier on x86 (the CPU already provides the ordering). On ARM these emit `ldar`/`stlr` instructions. Always use the weakest ordering that is correct.

`std::span<T>` is a non-owning view: `{pointer, length}` pair with zero overhead. Use it to pass buffer slices without raw pointer + size arguments.

Designated initializers on Vulkan/Level Zero structs prevent uninitialized padding bytes: `VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .format = VK_FORMAT_R8G8B8A8_UNORM, ...}`. Any field not listed is zero-initialized. Without this, struct padding can contain stack garbage that drivers read as flags — silent UB.

`-fno-exceptions` removes C++ exception unwind tables (saves ~5% binary size, eliminates hidden `__cxa_begin_catch` overhead in hot paths). `-fno-rtti` removes `dynamic_cast`/`typeid` support — neither is used in a POD pipeline. Both improve codegen predictability under `-O3`.

### Implement

**`CMakePresets.json`** — four presets: `release` (clang-18, `-O3 -march=native -fno-exceptions -fno-rtti`), `debug` (clang-18, `-O0 -g3`), `asan` (clang-18, `-O1 -fsanitize=address,undefined`), `bpf` (clang-18 with BPF target toolchain).

**`CMakeLists.txt`** root:
```cmake
cmake_minimum_required(VERSION 3.28)
project(kbx_vision LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 17)

include(cmake/modules/FindLevelZero.cmake)
include(cmake/modules/FindOpenVINO.cmake)
include(cmake/modules/FindLibDRM.cmake)
include(cmake/modules/FindLibBPF.cmake)
include(cmake/targets/kernels.cmake)   # ocloc PRE_BUILD
include(cmake/targets/shaders.cmake)   # glslc PRE_BUILD
include(cmake/targets/bpf.cmake)       # clang BPF + bpftool skeleton

add_executable(kbx_vision
    src/core/log.cc
    src/mem/arena.cc src/mem/ring.cc src/mem/numa.cc
    src/io/dmabuf.cc src/io/v4l2.cc
    src/compute/l0_ctx.cc src/compute/l0_kernel.cc
    src/infer/ov_session.cc
    src/gfx/vk_ctx.cc src/gfx/vk_pass.cc src/gfx/drm_kms.cc
    src/reactor/uring.cc
    src/telemetry/bpf_loader.cc src/telemetry/pmu.cc
    src/main.cc
)
target_include_directories(kbx_vision PRIVATE include)
target_link_libraries(kbx_vision PRIVATE
    LevelZero::LevelZero openvino::runtime
    drm gbm vulkan uring bpf numa
)
add_dependencies(kbx_vision kbx_kernels_spv kbx_shaders_spv kbx_bpf_skel)
```

**`cmake/targets/kernels.cmake`**:
```cmake
add_custom_command(
    OUTPUT  ${CMAKE_SOURCE_DIR}/kernels/spv/nv12_to_rgb.spv
    COMMAND ocloc compile
            -file  ${CMAKE_SOURCE_DIR}/kernels/cl/nv12_to_rgb.cl
            -device tgllp
            -options "-cl-std=CL3.0 -cl-intel-greater-64kb-buffer-required"
            -out_dir ${CMAKE_SOURCE_DIR}/kernels/spv
    DEPENDS ${CMAKE_SOURCE_DIR}/kernels/cl/nv12_to_rgb.cl
    COMMENT "ocloc: nv12_to_rgb.cl → nv12_to_rgb.spv"
)
add_custom_target(kbx_kernels_spv DEPENDS
    ${CMAKE_SOURCE_DIR}/kernels/spv/nv12_to_rgb.spv)
```

**`cmake/targets/shaders.cmake`**:
```cmake
foreach(SHADER bbox.vert bbox.frag)
    add_custom_command(
        OUTPUT  ${CMAKE_SOURCE_DIR}/shaders/spv/${SHADER}.spv
        COMMAND glslc
                ${CMAKE_SOURCE_DIR}/shaders/glsl/${SHADER}.glsl
                -o ${CMAKE_SOURCE_DIR}/shaders/spv/${SHADER}.spv
        DEPENDS ${CMAKE_SOURCE_DIR}/shaders/glsl/${SHADER}.glsl
    )
endforeach()
add_custom_target(kbx_shaders_spv DEPENDS
    ${CMAKE_SOURCE_DIR}/shaders/spv/bbox.vert.spv
    ${CMAKE_SOURCE_DIR}/shaders/spv/bbox.frag.spv)
```

**`cmake/targets/bpf.cmake`**:
```cmake
add_custom_command(
    OUTPUT  ${CMAKE_BINARY_DIR}/bpf/kbx_trace.bpf.o
    COMMAND clang-18 -target bpf -g -O2
            -I${CMAKE_SOURCE_DIR}/bpf
            -I/usr/include
            -c ${CMAKE_SOURCE_DIR}/bpf/kbx_trace.bpf.c
            -o ${CMAKE_BINARY_DIR}/bpf/kbx_trace.bpf.o
    DEPENDS ${CMAKE_SOURCE_DIR}/bpf/kbx_trace.bpf.c
            ${CMAKE_SOURCE_DIR}/bpf/vmlinux.h
)
add_custom_command(
    OUTPUT  ${CMAKE_SOURCE_DIR}/bpf/kbx_trace.skel.h
    COMMAND bpftool gen skeleton
            ${CMAKE_BINARY_DIR}/bpf/kbx_trace.bpf.o
            > ${CMAKE_SOURCE_DIR}/bpf/kbx_trace.skel.h
    DEPENDS ${CMAKE_BINARY_DIR}/bpf/kbx_trace.bpf.o
)
add_custom_target(kbx_bpf_skel DEPENDS ${CMAKE_SOURCE_DIR}/bpf/kbx_trace.skel.h)
```

**`include/kbx/core/types.h`**:
```cpp
#pragma once
#include <cstdint>
using u8  = uint8_t;  using u16 = uint16_t;
using u32 = uint32_t; using u64 = uint64_t;
using i32 = int32_t;  using f32 = float;

enum kbx_status_t : i32 {
    KBX_SUCCESS          =  0,
    KBX_ERR_MEM          = -1,
    KBX_ERR_V4L2         = -2,
    KBX_ERR_L0           = -3,
    KBX_ERR_OV           = -4,
    KBX_ERR_VK           = -5,
    KBX_ERR_DRM          = -6,
    KBX_ERR_URING        = -7,
    KBX_ERR_BPF          = -8,
    KBX_ERR_DMABUF       = -9,
    KBX_ERR_DEVICE_LOST  = -10,
};

#define KBX_CHECK(expr) do {                                          \
    kbx_status_t _s = (expr);                                         \
    if (__builtin_expect(_s != KBX_SUCCESS, 0)) {                     \
        fprintf(stderr, "[KBX FATAL] %s:%d  %s  →  %d\n",            \
                __FILE__, __LINE__, #expr, (int)_s);                  \
        __builtin_trap();                                             \
    }                                                                 \
} while(0)
```

**`include/kbx/core/compiler.h`**:
```cpp
#pragma once
#define LIKELY(x)       __builtin_expect(!!(x), 1)
#define UNLIKELY(x)     __builtin_expect(!!(x), 0)
#define ALWAYS_INLINE   __attribute__((always_inline)) inline
#define NOINLINE        __attribute__((noinline))
#define KBX_PACKED      __attribute__((packed))
#define CACHE_LINE      64u
#define KBX_ALIGN_CL    alignas(CACHE_LINE)
```

---

## Phase 1 — Memory Architecture

### Learn First

Linux translates virtual → physical address via a 4-level page table (PML4 → PDP → PD → PT). A TLB miss requires 4 sequential memory reads. With 4KB pages, a 3MB NV12 frame spans 768 potential TLB entries. With 2MB hugepages (`MAP_HUGETLB`), the same frame fits in 2 TLB entries. At 1080p/30fps this saves ~22,000 TLB misses per second from frame data alone.

`MAP_POPULATE` triggers `do_fault()` for every page during the `mmap()` call — page tables are populated, physical pages are allocated, TLB is primed. Zero first-touch page faults occur afterward. `MAP_LOCKED` calls `mlock2(MLOCK_ONFAULT)` under the hood — kernel cannot swap or NUMA-migrate the pages. On Tiger Lake (UMA), NUMA migration doesn't apply but swap avoidance still matters under memory pressure.

`malloc()` is catastrophic for GPU pipelines: every allocation potentially returns a new virtual address whose physical page is on an arbitrary NUMA node, whose TLB state is cold, and whose first access triggers a kernel interrupt for page allocation (~10μs worst case). The arena allocator eliminates all three failure modes for the lifetime of the process.

**SPSC false sharing:** In the ring buffer, `head` is written only by the producer thread and `tail` only by the consumer thread. If both reside on the same 64-byte cache line, the producer's `store(head)` marks the cache line as Modified (MESI state) in the producer's L1. The consumer's subsequent `load(tail)` causes a cache coherency request across the L3 interconnect — the consumer waits ~40–100ns for the line to transfer, even though it only needed `tail`. Putting them in separate `alignas(64)` structs places them on different cache lines, eliminating all cross-core traffic for the normal `push()/pop()` path.

### Implement

**`src/mem/arena.cc`**:
```cpp
#include "kbx/mem/arena.h"
#include "kbx/mem/numa.h"
#include <sys/mman.h>
#include <cstdio>
#include <cerrno>

kbx_status_t kbx_arena_init(kbx_arena* a, u64 bytes) {
    kbx_numa_prefer_gpu_node();   // must call before mmap

    a->base = (u8*)mmap(nullptr, bytes,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_LOCKED | MAP_POPULATE,
        -1, 0);

    if (a->base == MAP_FAILED) {
        // Fallback: THP (Transparent Huge Pages) — slower but works without root
        a->base = (u8*)mmap(nullptr, bytes,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1, 0);
        if (a->base == MAP_FAILED) return KBX_ERR_MEM;
        madvise(a->base, bytes, MADV_HUGEPAGE);
    }

    a->capacity = bytes;
    a->cursor   = 0;
    fprintf(stderr, "[MEM] arena @ %p, %zu MB\n", a->base, bytes >> 20);
    return KBX_SUCCESS;
}

void* kbx_arena_alloc(kbx_arena* a, u64 size, u64 align) {
    u64 pad     = (align - (a->cursor % align)) % align;
    u64 new_cur = a->cursor + pad + size;
    if (UNLIKELY(new_cur > a->capacity)) { __builtin_trap(); }
    void* ptr   = a->base + a->cursor + pad;
    a->cursor   = new_cur;
    return ptr;
}
```

**`include/kbx/mem/ring.h`**:
```cpp
#pragma once
#include "kbx/core/types.h"
#include "kbx/core/compiler.h"
#include <atomic>
#include <new>

// Single-producer single-consumer lock-free queue.
// head and tail on separate cache lines — zero false sharing.
struct kbx_ring {
    KBX_ALIGN_CL struct { std::atomic<u32> v; u8 pad[CACHE_LINE - 4]; } head;
    KBX_ALIGN_CL struct { std::atomic<u32> v; u8 pad[CACHE_LINE - 4]; } tail;
    u32   capacity;
    void** slots;   // pointer array, allocated from arena

    static_assert(CACHE_LINE == std::hardware_destructive_interference_size,
                  "cache line size mismatch");
};

// Returns true if slot was written. Call only from producer thread.
ALWAYS_INLINE bool kbx_ring_push(kbx_ring* r, void* item) {
    u32 h    = r->head.v.load(std::memory_order_relaxed);
    u32 next = (h + 1) % r->capacity;
    if (next == r->tail.v.load(std::memory_order_acquire)) return false; // full
    r->slots[h] = item;
    r->head.v.store(next, std::memory_order_release);
    return true;
}

// Returns item or nullptr if empty. Call only from consumer thread.
ALWAYS_INLINE void* kbx_ring_pop(kbx_ring* r) {
    u32 t = r->tail.v.load(std::memory_order_relaxed);
    if (t == r->head.v.load(std::memory_order_acquire)) return nullptr; // empty
    void* item = r->slots[t];
    r->tail.v.store((t + 1) % r->capacity, std::memory_order_release);
    return item;
}
```

**`src/mem/numa.cc`**:
```cpp
#include "kbx/mem/numa.h"
#include <numa.h>
#include <cstdio>
#include <cstdlib>

void kbx_numa_prefer_gpu_node() {
    // Intel GPU on Tiger Lake is always on NUMA node 0 (UMA).
    // On multi-socket systems, read the actual node:
    //   cat /sys/bus/pci/devices/0000:00:02.0/numa_node
    int gpu_node = 0;
    FILE* f = fopen("/sys/bus/pci/devices/0000:00:02.0/numa_node", "r");
    if (f) { fscanf(f, "%d", &gpu_node); fclose(f); }
    if (gpu_node < 0) gpu_node = 0; // -1 means no NUMA preference
    if (numa_available() >= 0) numa_set_preferred(gpu_node);
    fprintf(stderr, "[NUMA] preferring node %d for GPU-adjacent allocations\n", gpu_node);
}
```

---

## Phase 2 — DMA-BUF & V4L2 Camera Ingress

### Learn First

DMA-BUF (`drivers/dma-buf/dma-buf.c`) is a Linux framework for sharing GPU-accessible physical pages across subsystems via file descriptors. The kernel `struct dma_buf` holds a reference-counted set of physical pages plus an `ops` vtable. When you pass a DMA-BUF fd to a second subsystem (Level Zero, Vulkan, DRM), the kernel calls `dma_buf_attach()` + `dma_buf_map_attachment()` — the GPU's IOMMU maps those same physical pages into the GPU's address space. No copy, no staging buffer, no intermediate memcpy.

`V4L2_MEMORY_DMABUF` mode: you allocate camera buffers yourself (via GBM) and hand the DMA-BUF fds to V4L2 at `VIDIOC_QBUF` time. V4L2 programs the camera DMA engine to write the next captured frame directly into your GBM buffer. Contrast with `V4L2_MEMORY_MMAP`: kernel allocates the buffer, you receive a pointer via `mmap()`, but GPU access requires an additional copy.

GBM (`libgbm`): Generic Buffer Manager, part of Mesa. Creates GPU-accessible buffers aligned to hardware requirements. `gbm_device` is created from `/dev/dri/renderD128`. `gbm_bo_create(gbm, w, h, GBM_FORMAT_NV12, GBM_BO_USE_RENDERING|GBM_BO_USE_SCANOUT|GBM_BO_USE_LINEAR)` allocates a buffer that is simultaneously:
- Writeable by the camera ISP/DMA engine
- Importable by Level Zero, Vulkan, and DRM/KMS
- `LINEAR` modifier for cross-subsystem compatibility (tiled modifiers can fail on V4L2 → Vulkan boundary)

`GBM_FORMAT_NV12` layout: Y plane (1 byte/pixel, full resolution) followed immediately by UV plane (interleaved U+V, half resolution both dimensions). A 1920×1080 NV12 frame: Y plane = 1920×1080 = 2,073,600 bytes. UV plane = 960×540 pairs = 1,036,800 bytes. Total = 3,110,400 bytes ≈ 3MB.

**Implicit fencing:** every DMA-BUF has a `dma_resv` object tracking in-flight GPU operations via `dma_fence`. When you import a DMA-BUF into Level Zero, the i915 driver calls `dma_resv_wait()` — GPU waits for any pending writes (e.g., camera DMA still running) before allowing GPU reads. Cost: ~5–15μs per import. Accept this cost initially; explicit fencing can replace it later for ~2–5μs savings.

**NV12 vs YUYV:** USB webcams typically output MJPEG or YUYV (packed 4:2:2), not NV12. Check with `v4l2-ctl --list-formats-ext`. If NV12 is absent, you need a format conversion step (defeats zero-copy) or modify the SPIR-V kernel to accept YUYV. The v4l2loopback device used for Mac streaming *can* be configured for NV12.

### Implement

**`src/io/v4l2.cc`**:
```cpp
#include "kbx/io/v4l2.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <xf86drm.h>
#include <gbm.h>
#include <cstring>
#include <cstdio>

kbx_status_t kbx_v4l2_init(kbx_v4l2_ctx* ctx, const char* dev,
                             u32 w, u32 h, u32 n_bufs) {
    ctx->fd = open(dev, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (ctx->fd < 0) return KBX_ERR_V4L2;

    // Capability check
    struct v4l2_capability cap = {};
    if (ioctl(ctx->fd, VIDIOC_QUERYCAP, &cap) < 0) return KBX_ERR_V4L2;
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) return KBX_ERR_V4L2;
    if (!(cap.capabilities & V4L2_CAP_STREAMING))     return KBX_ERR_V4L2;

    // Set NV12 format — must match ISP native output to avoid kernel CSC insertion
    struct v4l2_format fmt = {};
    fmt.type                 = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width        = w;
    fmt.fmt.pix.height       = h;
    fmt.fmt.pix.pixelformat  = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix.field        = V4L2_FIELD_NONE;
    fmt.fmt.pix.colorspace   = V4L2_COLORSPACE_REC709;
    if (ioctl(ctx->fd, VIDIOC_S_FMT, &fmt) < 0) return KBX_ERR_V4L2;
    // fmt.fmt.pix.sizeimage is now the exact buffer size

    ctx->width     = w;
    ctx->height    = h;
    ctx->buf_size  = fmt.fmt.pix.sizeimage;
    ctx->n_bufs    = n_bufs;

    // Request DMABUF-mode buffers — WE provide the memory
    struct v4l2_requestbuffers req = {};
    req.count  = n_bufs;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_DMABUF;
    if (ioctl(ctx->fd, VIDIOC_REQBUFS, &req) < 0) return KBX_ERR_V4L2;

    // Open GBM device (renderD128 — no root required)
    int drm_fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) return KBX_ERR_DRM;
    ctx->gbm = gbm_create_device(drm_fd);

    // Allocate one GBM BO per buffer slot, export DMA-BUF fd, queue to V4L2
    for (u32 i = 0; i < n_bufs; i++) {
        ctx->bos[i] = gbm_bo_create(ctx->gbm, w, h, GBM_FORMAT_NV12,
            GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
        if (!ctx->bos[i]) return KBX_ERR_MEM;

        ctx->dmabuf_fds[i] = gbm_bo_get_fd(ctx->bos[i]);
        if (ctx->dmabuf_fds[i] < 0) return KBX_ERR_DMABUF;

        struct v4l2_buffer buf = {};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_DMABUF;
        buf.index  = i;
        buf.m.fd   = ctx->dmabuf_fds[i];
        buf.length = ctx->buf_size;
        if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) < 0) return KBX_ERR_V4L2;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx->fd, VIDIOC_STREAMON, &type) < 0) return KBX_ERR_V4L2;
    fprintf(stderr, "[V4L2] %s %ux%u NV12, %u buffers\n", dev, w, h, n_bufs);
    return KBX_SUCCESS;
}

// Call after io_uring CQE confirms VIDIOC_DQBUF completed
int kbx_v4l2_dmabuf_fd(kbx_v4l2_ctx* ctx, u32 buf_index) {
    return ctx->dmabuf_fds[buf_index];
}

kbx_status_t kbx_v4l2_requeue(kbx_v4l2_ctx* ctx, u32 buf_index) {
    struct v4l2_buffer buf = {};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_DMABUF;
    buf.index  = buf_index;
    buf.m.fd   = ctx->dmabuf_fds[buf_index];
    buf.length = ctx->buf_size;
    if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) < 0) return KBX_ERR_V4L2;
    return KBX_SUCCESS;
}
```

---

## Phase 3 — Level Zero GPU Compute

### Learn First

Level Zero is Intel's native GPU API — one layer below OpenCL, analogous to the CUDA Driver API or Vulkan Compute. The object hierarchy:

```
ze_driver_handle_t  (one per hardware vendor)
  └── ze_device_handle_t  (one per GPU)
        └── ze_context_handle_t  (isolated address space, owns all GPU resources)
              ├── ze_command_list_handle_t  (work submission stream)
              ├── ze_module_handle_t        (compiled SPIR-V kernel container)
              ├── ze_kernel_handle_t        (individual kernel function)
              ├── ze_image_handle_t         (2D tiled GPU image)
              └── ze_event_handle_t         (GPU-to-GPU synchronization token)
```

**Immediate command list:** A regular command list requires `zeCommandListClose()` then `zeCommandQueueExecuteCommandLists()` — internal batching window adds variable delay. An immediate command list (`zeCommandListCreateImmediate()`) maps directly to the Intel Command Streamer (CS) hardware ring buffer. `zeCommandListAppendLaunchKernel()` on an immediate list submits to hardware within microseconds of the call returning. This is the only acceptable mode for sub-16ms frame budgets.

**DMA-BUF import mechanism:** `ze_external_memory_import_fd_t` is chained into `ze_image_desc_t.pNext`. `zeImageCreate()` with this chain does not allocate GPU memory — it creates a GPU virtual address mapping over the existing DDR4 physical pages referenced by the DMA-BUF fd. The IOMMU maps those pages into the GPU's address space. On Tiger Lake (UMA), this is even simpler — the GPU already sees all DDR4 as its native memory; the import just creates a typed view.

**Intel EU architecture (Tiger Lake Iris Xe):** 96 EUs. Each EU is a 7-way SIMD processor with 128 General Register File registers × 32 bytes each. A work-group of 16 threads in SIMD-16 mode occupies one EU for one dispatch cycle. With `intel_reqd_sub_group_size(16)`, the compiler forces SIMD-16 mode — no divergence handling code is emitted, and the `intel_sub_group_block_read` intrinsics become legal. The 1920×1080 NV12→RGB conversion dispatches 1920×1080/16 = 129,600 SIMD-16 waves across 96 EUs.

### Implement

**`src/compute/l0_ctx.cc`**:
```cpp
#include "kbx/compute/l0_ctx.h"
#include <level_zero/ze_api.h>
#include <cstdio>
#include <cstring>

kbx_status_t kbx_l0_init(kbx_l0_ctx* ctx) {
    if (zeInit(0) != ZE_RESULT_SUCCESS) return KBX_ERR_L0;

    u32 driver_count = 0;
    zeDriverGet(&driver_count, nullptr);
    if (driver_count == 0) return KBX_ERR_L0;

    ze_driver_handle_t driver;
    driver_count = 1;
    zeDriverGet(&driver_count, &driver);
    ctx->driver = driver;

    // Find GPU device (not integrated display, not sub-device)
    u32 dev_count = 0;
    zeDeviceGet(driver, &dev_count, nullptr);
    ze_device_handle_t devices[8];
    zeDeviceGet(driver, &dev_count, devices);

    ctx->device = nullptr;
    for (u32 i = 0; i < dev_count; i++) {
        ze_device_properties_t props = {ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES};
        zeDeviceGetProperties(devices[i], &props);
        if (props.type == ZE_DEVICE_TYPE_GPU) { ctx->device = devices[i]; break; }
    }
    if (!ctx->device) return KBX_ERR_L0;

    // Create isolated context
    ze_context_desc_t ctx_desc = {ZE_STRUCTURE_TYPE_CONTEXT_DESC};
    if (zeContextCreate(driver, &ctx_desc, &ctx->context) != ZE_RESULT_SUCCESS)
        return KBX_ERR_L0;

    // Immediate command list — bypasses scheduler batching, direct to CS hardware
    ze_command_queue_desc_t q_desc = {
        .stype    = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC,
        .ordinal  = 0,
        .index    = 0,
        .flags    = ZE_COMMAND_QUEUE_FLAG_EXPLICIT_ONLY,
        .mode     = ZE_COMMAND_QUEUE_MODE_ASYNCHRONOUS,
        .priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL,
    };
    if (zeCommandListCreateImmediate(ctx->context, ctx->device, &q_desc,
                                     &ctx->cmd_list) != ZE_RESULT_SUCCESS)
        return KBX_ERR_L0;

    // Event pool for GPU→GPU synchronization
    ze_event_pool_desc_t ep_desc = {
        .stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        .flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
        .count = 8,
    };
    zeEventPoolCreate(ctx->context, &ep_desc, 1, &ctx->device, &ctx->event_pool);

    fprintf(stderr, "[L0] initialized, immediate command list ready\n");
    return KBX_SUCCESS;
}

kbx_status_t kbx_l0_load_kernel(kbx_l0_ctx* ctx,
                                  const char* spv_path,
                                  const char* entry_name,
                                  ze_kernel_handle_t* out_kernel) {
    // Read SPIR-V binary
    FILE* f = fopen(spv_path, "rb");
    if (!f) return KBX_ERR_L0;
    fseek(f, 0, SEEK_END); size_t sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8* spv = (u8*)malloc(sz);
    fread(spv, 1, sz, f); fclose(f);

    ze_module_desc_t mod_desc = {
        .stype      = ZE_STRUCTURE_TYPE_MODULE_DESC,
        .format     = ZE_MODULE_FORMAT_IL_SPIRV,
        .inputSize  = sz,
        .pInputModule = spv,
        .pBuildFlags  = "",
    };
    ze_module_build_log_handle_t build_log;
    ze_result_t r = zeModuleCreate(ctx->context, ctx->device,
                                   &mod_desc, &ctx->module, &build_log);
    free(spv);
    if (r != ZE_RESULT_SUCCESS) {
        size_t log_sz; zeModuleBuildLogGetString(build_log, &log_sz, nullptr);
        char* log = (char*)alloca(log_sz);
        zeModuleBuildLogGetString(build_log, &log_sz, log);
        fprintf(stderr, "[L0] build log: %s\n", log);
        return KBX_ERR_L0;
    }

    ze_kernel_desc_t kern_desc = {
        .stype     = ZE_STRUCTURE_TYPE_KERNEL_DESC,
        .pKernelName = entry_name,
    };
    zeKernelCreate(ctx->module, &kern_desc, out_kernel);

    // Force SIMD-16 to match intel_reqd_sub_group_size(16) in kernel source
    zeKernelSetGroupSize(*out_kernel, 16, 1, 1);
    return KBX_SUCCESS;
}

kbx_status_t kbx_l0_import_dmabuf(kbx_l0_ctx* ctx, int dmabuf_fd,
                                    u32 w, u32 h, ze_image_handle_t* out_img) {
    ze_external_memory_import_fd_t import_desc = {
        .stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMPORT_FD,
        .pNext = nullptr,
        .flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF,
        .fd    = dmabuf_fd,
    };
    ze_image_desc_t img_desc = {
        .stype  = ZE_STRUCTURE_TYPE_IMAGE_DESC,
        .pNext  = &import_desc,
        .flags  = 0,
        .type   = ZE_IMAGE_TYPE_2D,
        .format = {
            .layout = ZE_IMAGE_FORMAT_LAYOUT_8_8_8_8,
            .type   = ZE_IMAGE_FORMAT_TYPE_UNORM,
            .x = ZE_IMAGE_FORMAT_SWIZZLE_R,
            .y = ZE_IMAGE_FORMAT_SWIZZLE_G,
            .z = ZE_IMAGE_FORMAT_SWIZZLE_B,
            .w = ZE_IMAGE_FORMAT_SWIZZLE_A,
        },
        .width  = w,
        .height = h,
        .depth  = 1,
        .arraylevels = 1,
        .miplevels   = 1,
    };
    // Does NOT allocate — creates a GPU mapping over existing DMA-BUF pages
    if (zeImageCreate(ctx->context, ctx->device, &img_desc, out_img)
        != ZE_RESULT_SUCCESS) return KBX_ERR_L0;
    return KBX_SUCCESS;
}

kbx_status_t kbx_l0_dispatch_nv12_rgb(kbx_l0_ctx* ctx,
                                        ze_kernel_handle_t kernel,
                                        ze_image_handle_t y_plane,
                                        ze_image_handle_t uv_plane,
                                        ze_image_handle_t rgb_out,
                                        ze_event_handle_t* signal_event) {
    zeKernelSetArgumentValue(kernel, 0, sizeof(ze_image_handle_t), &y_plane);
    zeKernelSetArgumentValue(kernel, 1, sizeof(ze_image_handle_t), &uv_plane);
    zeKernelSetArgumentValue(kernel, 2, sizeof(ze_image_handle_t), &rgb_out);

    ze_group_count_t group_count = {ctx->width / 16, ctx->height, 1};
    // signal_event fires when kernel completes — OpenVINO waits on this
    ze_result_t r = zeCommandListAppendLaunchKernel(
        ctx->cmd_list, kernel, &group_count,
        *signal_event, 0, nullptr);
    return r == ZE_RESULT_SUCCESS ? KBX_SUCCESS : KBX_ERR_L0;
}
```

---

## Phase 4 — SPIR-V Kernel Engineering

### Learn First

OpenCL C 3.0 is the shader language. The GPU compiler (`ocloc`) compiles it to SPIR-V binary (Khronos standard, magic `0x07230203`). Level Zero ingests SPIR-V via `zeModuleCreate`.

`__read_only image2d_t` uses the GPU sampler hardware — not a linear memory read. The sampler handles the NV12 interleaved layout natively and performs cache-line-aligned fetches. `read_imagef(image, sampler, (int2)(x,y))` returns `float4`, where the color components come from however the image format was declared.

`cl_intel_subgroups` extension provides `intel_sub_group_block_read_ui()` — maps to a single `send` instruction (Intel GPU ISA) that reads 16 consecutive 32-bit values in one cycle from cache. Without it, 16 separate scalar reads each require separate address generation and cache lookups. The `__attribute__((intel_reqd_sub_group_size(16)))` attribute forces the compiler to generate SIMD-16 code — it will fail with a compile error if the work-group geometry doesn't divide evenly into groups of 16 (prevents silent scalar fallback).

BT.709 full-range YCbCr → RGB matrix (camera sensor output for HD video):
- R = Y + 1.5748(V − 128)
- G = Y − 0.1873(U − 128) − 0.4681(V − 128)  
- B = Y + 1.8556(U − 128)

BT.601 (used by v4l2loopback/ffmpeg by default) has different coefficients. Set `V4L2_COLORSPACE_REC709` in V4L2 to force the camera to signal BT.709. Check actual colorspace metadata with `v4l2-ctl --all`.

UV chroma plane: in NV12, U and V are interleaved at half horizontal and half vertical resolution. For output pixel at `(x, y)`, the chroma sample is at UV coordinate `(x/2, y/2)`. The `read_imagef` call on the UV plane at that coordinate returns `.x = U`, `.y = V` due to the `RG8` format layout.

### Implement

**`kernels/cl/nv12_to_rgb.cl`**:
```c
#pragma OPENCL EXTENSION cl_intel_subgroups       : enable
#pragma OPENCL EXTENSION cl_intel_media_block_io  : enable

// BT.709 full-range YCbCr → RGB
#define YUV_R(Y,V)   clamp((Y) + 1.5748f * ((V) - 128.f), 0.f, 255.f)
#define YUV_G(Y,U,V) clamp((Y) - 0.1873f * ((U) - 128.f) - 0.4681f * ((V) - 128.f), 0.f, 255.f)
#define YUV_B(Y,U)   clamp((Y) + 1.8556f * ((U) - 128.f), 0.f, 255.f)

__kernel __attribute__((intel_reqd_sub_group_size(16)))
void convert_nv12_to_rgb(
    __read_only  image2d_t y_plane,    // LUMINANCE8 format
    __read_only  image2d_t uv_plane,   // RG8 format (U=R, V=G)
    __write_only image2d_t rgb_out     // RGBA8 output
) {
    const int x = get_global_id(0);
    const int y = get_global_id(1);

    const sampler_t s = CLK_NORMALIZED_COORDS_FALSE
                      | CLK_FILTER_NEAREST
                      | CLK_ADDRESS_CLAMP_TO_EDGE;

    // 16 threads in this subgroup each load one Y pixel.
    // The hardware fuses these into a single SIMD-16 load.
    float4 luma   = read_imagef(y_plane,  s, (int2)(x,   y));
    float4 chroma = read_imagef(uv_plane, s, (int2)(x/2, y/2));

    float Y = luma.x   * 255.f;
    float U = chroma.x * 255.f;  // interleaved U in R channel of UV plane
    float V = chroma.y * 255.f;  // interleaved V in G channel of UV plane

    write_imagef(rgb_out, (int2)(x, y), (float4)(
        YUV_R(Y,V)     / 255.f,
        YUV_G(Y,U,V)   / 255.f,
        YUV_B(Y,U)     / 255.f,
        1.f
    ));
}
```

Build manually to verify before CMake:
```bash
ocloc compile \
    -file kernels/cl/nv12_to_rgb.cl \
    -device tgllp \
    -options "-cl-std=CL3.0 -cl-intel-greater-64kb-buffer-required" \
    -out_dir kernels/spv/
# Produces: kernels/spv/nv12_to_rgb.spv
# Verify SPIR-V magic: hexdump -C kernels/spv/nv12_to_rgb.spv | head -1
# Expect: 03 02 23 07  (little-endian SPIR-V magic 0x07230203)
```

Dispatch geometry in `l0_ctx.cc`:
```cpp
// 1920 columns / 16 subgroup size = 120 groups in X
// 1080 rows, no subgrouping in Y
ze_group_count_t gc = { .groupCountX = 1920/16, .groupCountY = 1080, .groupCountZ = 1};
// Total SIMD-16 waves: 120 × 1080 = 129,600
// At 96 EUs, ~1350 waves per EU → ~0.3ms on Tiger Lake at 1.2GHz GPU clock
```

---

## Phase 5 — OpenVINO Inference with Zero-Copy

### Learn First

OpenVINO 2024 C++ API (not the deprecated `InferenceEngine::` namespace). `ov::Core core` is the entry point. `core.compile_model(path, device)` JIT-compiles the OpenVINO IR model for a specific device at startup (~200ms–2s first run, cached after).

`ov::intel_gpu::ocl::ClContext`: OpenVINO's GPU plugin uses OpenCL internally. This class wraps a user-provided OpenCL context handle, forcing OpenVINO to use *your* GPU memory allocations rather than its own internal ones. You obtain the OpenCL handle from Level Zero via `ze_cl_interop`:

```cpp
// Get OpenCL context that shares physical resources with your Level Zero context
cl_context cl_ctx_handle;
zeGetInteropHandleOf(ctx->context, &cl_ctx_handle);  // simplified — see actual API
auto ov_ctx = ov::intel_gpu::ocl::ClContext(core, cl_ctx_handle);
```

`ov_ctx.create_tensor(element_type, shape, ze_image_handle)`: wraps your `ze_image_handle_t` as an `ov::RemoteTensor`. The tensor's internal data pointer *is* the physical DDR4 address of the camera frame. OpenVINO stores a reference — no copy, no staging. `infer_req.set_input_tensor(0, remote_tensor)` stores the reference. `infer_req.infer()` dispatches the model on the GPU, reading directly from camera DDR4.

**YOLOv8n output tensor:** shape `[1, 84, 8400]`. Memory layout is column-major in the anchor dimension:
- `raw[attr * 8400 + anchor]` where attr ∈ [0..83]
- Attributes 0–3: `(cx, cy, w, h)` — center x, center y, width, height, all normalized 0..1 relative to 640×640 model input size
- Attributes 4–83: COCO class confidence scores (80 classes)

Decode box `i`: `cx = raw[0*8400+i]`, `cy = raw[1*8400+i]`, then scale to image coordinates. Max class = `argmax(raw[4*8400+i] .. raw[83*8400+i])`.

**NMS (greedy IoU-based):** Confidence threshold 0.25 pre-filters from 8400 to ~20–100 candidates. Sort by confidence. For each kept box, compute IoU with remaining candidates; suppress those with IoU > 0.45. IoU = intersection_area / union_area. This runs in ~50μs on CPU for typical scene density.

**INT8 quantization impact on Iris Xe:**
- 4× less weight memory bandwidth vs FP32 → fewer L3 cache misses
- Intel VNNI instruction processes 4 INT8 MACs per clock vs 1 FP32 MAC
- Typical latency: 3–6ms INT8 vs 12–18ms FP32 for YOLOv8n at 1080p

### Implement

**Model preparation (run once before coding):**
```bash
# Install OpenVINO model tools
pip install openvino-dev ultralytics --break-system-packages

# Export YOLOv8n to OpenVINO INT8 IR
python3 -c "
from ultralytics import YOLO
model = YOLO('yolov8n.pt')
model.export(format='openvino', imgsz=640, half=False, int8=True)
"
# Produces: yolov8n_openvino_model/yolov8n.xml + yolov8n.bin
cp yolov8n_openvino_model/yolov8n.xml models/yolov8n_int8.xml
cp yolov8n_openvino_model/yolov8n.bin models/yolov8n_int8.bin

# Benchmark on your GPU before integrating:
benchmark_app -m models/yolov8n_int8.xml -d GPU -nireq 1 -niter 100 -api sync
# Expect: Latency < 8ms on Iris Xe
```

**`src/infer/ov_session.cc`**:
```cpp
#include "kbx/infer/ov_session.h"
#include <openvino/openvino.hpp>
#include <openvino/runtime/intel_gpu/ocl/ocl.hpp>
#include <cstdio>
#include <algorithm>
#include <vector>

kbx_status_t kbx_ov_init(kbx_ov_ctx* ctx, kbx_l0_ctx* l0, const char* xml_path) {
    try {
        ctx->core = new ov::Core();

        // Get OpenCL context from Level Zero context (ze-ocl interop)
        // Tiger Lake: both L0 and OV GPU plugin target the same i915 device
        cl_context cl_ctx_h = nullptr;
        // ze_intel_get_default_context() or driver-specific interop
        // Simplified: let OpenVINO select GPU automatically if interop is complex
        // For full zero-copy, bind to the same device:
        ctx->ov_ctx = new ov::intel_gpu::ocl::ClContext(
            *ctx->core,
            /* cl_context from ze interop — driver specific, see note below */
            cl_ctx_h);

        ctx->compiled = new ov::CompiledModel(
            ctx->core->compile_model(xml_path, *ctx->ov_ctx,
                ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY),
                ov::hint::num_requests(1)));

        ctx->infer_req = new ov::InferRequest(ctx->compiled->create_infer_request());
        fprintf(stderr, "[OV] YOLOv8n INT8 compiled on GPU\n");
        return KBX_SUCCESS;
    } catch (const ov::Exception& e) {
        fprintf(stderr, "[OV] init failed: %s\n", e.what());
        return KBX_ERR_OV;
    }
}

// Note on Level Zero ↔ OpenVINO interop:
// OpenVINO 2024 accepts ze_context_handle_t directly:
//   ov::intel_gpu::ocl::ClContext(core, ze_ctx_handle, ze_dev_handle)
// This variant is available in OpenVINO 2024.1+ for L0 interop.
// Check OpenVINO release notes for exact API signature.

kbx_status_t kbx_ov_infer(kbx_ov_ctx* ctx, ze_image_handle_t input_img,
                            kbx_bbox* out_boxes, u32* out_count, u32 max_boxes) {
    try {
        // Wrap L0 image as OV remote tensor — ZERO COPY
        auto remote = ctx->ov_ctx->create_tensor(
            ov::element::u8,
            ov::Shape{1, 3, 1080, 1920},
            input_img);
        ctx->infer_req->set_input_tensor(0, remote);
        ctx->infer_req->infer();  // blocks until GPU finishes

        // Decode output — CPU reads ~201,600 floats (8400 × 24 bytes)
        auto out_tensor = ctx->infer_req->get_output_tensor(0);
        float* raw = out_tensor.data<float>();
        // Shape: [1, 84, 8400]

        *out_count = 0;
        // Pre-filter by confidence (attr 4..83 = class scores)
        for (u32 i = 0; i < 8400 && *out_count < max_boxes; i++) {
            float max_score = 0.f;
            u32   max_class = 0;
            for (u32 c = 0; c < 80; c++) {
                float s = raw[(4 + c) * 8400 + i];
                if (s > max_score) { max_score = s; max_class = c; }
            }
            if (max_score < 0.25f) continue;  // confidence threshold

            float cx = raw[0 * 8400 + i];
            float cy = raw[1 * 8400 + i];
            float bw = raw[2 * 8400 + i];
            float bh = raw[3 * 8400 + i];

            out_boxes[*out_count] = {
                .x1    = cx - bw/2.f,
                .y1    = cy - bh/2.f,
                .x2    = cx + bw/2.f,
                .y2    = cy + bh/2.f,
                .conf  = max_score,
                .cls   = max_class,
            };
            (*out_count)++;
        }

        // Greedy NMS — O(n²) acceptable for n < 100
        for (u32 i = 0; i < *out_count; i++) {
            for (u32 j = i + 1; j < *out_count; ) {
                // IoU of boxes[i] and boxes[j]
                float ix1 = std::max(out_boxes[i].x1, out_boxes[j].x1);
                float iy1 = std::max(out_boxes[i].y1, out_boxes[j].y1);
                float ix2 = std::min(out_boxes[i].x2, out_boxes[j].x2);
                float iy2 = std::min(out_boxes[i].y2, out_boxes[j].y2);
                float inter = std::max(0.f, ix2-ix1) * std::max(0.f, iy2-iy1);
                float area_i = (out_boxes[i].x2-out_boxes[i].x1) * (out_boxes[i].y2-out_boxes[i].y1);
                float area_j = (out_boxes[j].x2-out_boxes[j].x1) * (out_boxes[j].y2-out_boxes[j].y1);
                float iou = inter / (area_i + area_j - inter);
                if (iou > 0.45f) {
                    out_boxes[j] = out_boxes[--(*out_count)]; // remove j
                } else { j++; }
            }
        }
        return KBX_SUCCESS;
    } catch (const ov::Exception& e) {
        fprintf(stderr, "[OV] infer failed: %s\n", e.what());
        return KBX_ERR_OV;
    }
}
```

---

## Phase 6 — Vulkan Rendering Pipeline

### Learn First

Vulkan has zero implicit state. Every GPU operation requires an explicit dependency. Three sync primitives: `VkFence` (CPU blocks waiting for GPU), `VkSemaphore` (GPU-to-GPU sync across queue submissions), `VkEvent` (fine-grained within a command buffer).

For the DRM handoff, export a `VkSemaphore` as a Linux sync fd via `vkExportSemaphoreFdKHR` with `VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT`. This fd is passed to DRM as the `IN_FENCE_FD` plane property. DRM waits for that fence to signal (Vulkan rendering complete) before scanning out the framebuffer. Without this, DRM might scanout a buffer mid-render — tearing or corruption.

**DMA-BUF memory type query — critical and non-skippable:**
`vkGetMemoryFdPropertiesKHR(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &props)` returns `props.memoryTypeBits` — a bitmask of compatible Vulkan memory type indices. You must iterate `VkPhysicalDeviceMemoryProperties.memoryTypes` and find an index that is (a) set in `memoryTypeBits` AND (b) has `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`. Hardcoding index 0 will crash on real hardware.

`VkRenderPass` with `VK_ATTACHMENT_LOAD_OP_LOAD` + `VK_ATTACHMENT_STORE_OP_STORE`: loads existing framebuffer content (camera frame) before rendering, stores result after. Never use `CLEAR` (destroys frame) or `DONT_CARE` (GPU may garbage-fill). Initial layout `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL`, final layout `VK_IMAGE_LAYOUT_GENERAL` for DRM import.

**Push constants for bbox:** `vkCmdPushConstants` copies data from CPU to GPU registers in < 1μs — no descriptor set allocation, no buffer upload, no staging. Vulkan guarantees at minimum 128 bytes push constant space = 32 × `vec4` = 32 bounding boxes. Sufficient for typical detection scenes.

**Image layout transitions:** DMA-BUF imported image starts in `VK_IMAGE_LAYOUT_UNDEFINED`. Must transition to `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` before `vkCmdBeginRenderPass`. Insert a `VkImageMemoryBarrier` in the command buffer with appropriate `srcAccessMask`/`dstAccessMask`.

### Implement

**`src/gfx/vk_ctx.cc`** — device and extension initialization:
```cpp
// Required instance extensions:
const char* inst_exts[] = {
    VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
};
// Required device extensions:
const char* dev_exts[] = {
    VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
};

// Select physical device: Intel GPU (vendorID 0x8086)
VkPhysicalDeviceProperties props;
vkGetPhysicalDeviceProperties(pdev, &props);
// props.vendorID == 0x8086 → Intel
// props.deviceName contains "Iris Xe" on Tiger Lake

// Load extension function pointers (not in core loader):
PFN_vkGetMemoryFdPropertiesKHR vkGetMemoryFdPropertiesKHR =
    (PFN_vkGetMemoryFdPropertiesKHR)
    vkGetDeviceProcAddr(device, "vkGetMemoryFdPropertiesKHR");
PFN_vkExportSemaphoreFdKHR vkExportSemaphoreFdKHR =
    (PFN_vkExportSemaphoreFdKHR)
    vkGetDeviceProcAddr(device, "vkExportSemaphoreFdKHR");
```

**`src/gfx/vk_pass.cc`** — DMA-BUF import with correct memory type query:
```cpp
kbx_status_t kbx_vk_import_dmabuf(kbx_vk_ctx* ctx, int dmabuf_fd,
                                    u32 w, u32 h) {
    // Step 1: Query which Vulkan memory types are compatible with this DMA-BUF
    VkMemoryFdPropertiesKHR fd_props = {VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
    ctx->fn.vkGetMemoryFdPropertiesKHR(ctx->device,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, dmabuf_fd, &fd_props);
    // fd_props.memoryTypeBits: bitmask of compatible memory type indices

    // Step 2: Find compatible + DEVICE_LOCAL memory type
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(ctx->pdev, &mem_props);
    u32 mem_idx = UINT32_MAX;
    for (u32 i = 0; i < mem_props.memoryTypeCount; i++) {
        if (!((fd_props.memoryTypeBits >> i) & 1)) continue;
        if (!(mem_props.memoryTypes[i].propertyFlags &
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) continue;
        mem_idx = i;
        break;
    }
    if (mem_idx == UINT32_MAX) return KBX_ERR_VK;

    // Step 3: Allocate Vulkan memory object over the DMA-BUF pages
    off_t buf_size = lseek(dmabuf_fd, 0, SEEK_END); // get actual size
    lseek(dmabuf_fd, 0, SEEK_SET);

    VkImportMemoryFdInfoKHR import_info = {
        .sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        .fd         = dmabuf_fd,
    };
    VkMemoryAllocateInfo alloc_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = &import_info,
        .allocationSize  = (VkDeviceSize)buf_size,
        .memoryTypeIndex = mem_idx,
    };
    if (vkAllocateMemory(ctx->device, &alloc_info, nullptr, &ctx->mem) != VK_SUCCESS)
        return KBX_ERR_VK;

    // Step 4: Create VkImage view into the imported memory
    VkExternalMemoryImageCreateInfo ext_img = {
        .sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    VkImageCreateInfo img_ci = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext         = &ext_img,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_R8G8B8A8_UNORM,
        .extent        = {w, h, 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_LINEAR,
        .usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    vkCreateImage(ctx->device, &img_ci, nullptr, &ctx->image);
    vkBindImageMemory(ctx->device, ctx->image, ctx->mem, 0);

    // Create image view for render pass attachment
    VkImageViewCreateInfo view_ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = ctx->image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCreateImageView(ctx->device, &view_ci, nullptr, &ctx->image_view);
    return KBX_SUCCESS;
}

kbx_status_t kbx_vk_draw_boxes(kbx_vk_ctx* ctx,
                                 kbx_bbox* boxes, u32 n_boxes,
                                 int* out_semaphore_fd) {
    vkBeginCommandBuffer(ctx->cmd_buf, &(VkCommandBufferBeginInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    });

    // Transition: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
    VkImageMemoryBarrier barrier = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask    = 0,
        .dstAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image            = ctx->image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(ctx->cmd_buf,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkRenderPassBeginInfo rp_begin = {
        .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass  = ctx->render_pass,
        .framebuffer = ctx->framebuffer,
        .renderArea  = {{0,0},{ctx->width, ctx->height}},
    };
    vkCmdBeginRenderPass(ctx->cmd_buf, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(ctx->cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeline);

    // Push all bbox data at once — no descriptor set, no buffer upload
    struct { float boxes[32][4]; float color[4]; } pc_data;
    for (u32 i = 0; i < std::min(n_boxes, 32u); i++) {
        pc_data.boxes[i][0] = boxes[i].x1;
        pc_data.boxes[i][1] = boxes[i].y1;
        pc_data.boxes[i][2] = boxes[i].x2;
        pc_data.boxes[i][3] = boxes[i].y2;
    }
    pc_data.color[0] = 1.f; pc_data.color[1] = 0.f;
    pc_data.color[2] = 0.f; pc_data.color[3] = 1.f;
    vkCmdPushConstants(ctx->cmd_buf, ctx->pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_data), &pc_data);

    // 6 vertices per box (2 triangles), all generated in vertex shader from gl_VertexIndex
    vkCmdDraw(ctx->cmd_buf, 6 * std::min(n_boxes, 32u), 1, 0, 0);
    vkCmdEndRenderPass(ctx->cmd_buf);
    vkEndCommandBuffer(ctx->cmd_buf);

    // Export semaphore so DRM can wait for rendering to complete
    VkExportSemaphoreCreateInfo exp_sem_ci = {
        .sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    };
    VkSemaphoreCreateInfo sem_ci = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &exp_sem_ci};
    VkSemaphore render_done;
    vkCreateSemaphore(ctx->device, &sem_ci, nullptr, &render_done);

    VkSubmitInfo submit = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &ctx->cmd_buf,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &render_done,
    };
    vkQueueSubmit(ctx->queue, 1, &submit, VK_NULL_HANDLE);

    // Export as Linux sync fd for DRM IN_FENCE_FD
    VkSemaphoreGetFdInfoKHR get_fd = {
        .sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .semaphore  = render_done,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    };
    ctx->fn.vkExportSemaphoreFdKHR(ctx->device, &get_fd, out_semaphore_fd);
    return KBX_SUCCESS;
}
```

**`shaders/glsl/bbox.vert.glsl`**:
```glsl
#version 450
layout(push_constant) uniform PC {
    vec4 boxes[32];   // (x1, y1, x2, y2) normalized [0,1]
    vec4 color;
} pc;

void main() {
    int box_idx  = gl_VertexIndex / 6;
    int vert_idx = gl_VertexIndex % 6;
    vec4 b = pc.boxes[box_idx]; // x1, y1, x2, y2

    // 4 corners: TL, TR, BL, BR
    vec2 corners[4] = vec2[](
        vec2(b.x, b.y), vec2(b.z, b.y),
        vec2(b.x, b.w), vec2(b.z, b.w));
    // Two triangles: (TL,TR,BL), (TR,BR,BL)
    int idx[6] = int[](0, 1, 2, 1, 3, 2);
    // Transform [0,1] → NDC [-1,1]
    gl_Position = vec4(corners[idx[vert_idx]] * 2.0 - 1.0, 0.0, 1.0);
}
```

**`shaders/glsl/bbox.frag.glsl`**:
```glsl
#version 450
layout(push_constant) uniform PC {
    vec4 boxes[32];
    vec4 color;
} pc;
layout(location = 0) out vec4 out_color;
void main() { out_color = pc.color; }
```

---

## Phase 7 — DRM/KMS Bare-Metal Display

### Learn First

**DRM property IDs are not stable.** The numeric values passed to `drmModeAtomicAddProperty(req, object_id, prop_id, value)` vary per system and per boot. Never hardcode them. Always query at runtime via `drmModeObjectGetProperties(fd, object_id, DRM_MODE_OBJECT_PLANE)`, iterate `drmModePropertyPtr` array by name, cache the IDs in your `kbx_drm_ctx` struct. Names are stable strings: `"FB_ID"`, `"CRTC_ID"`, `"SRC_X"`, `"SRC_Y"`, `"SRC_W"`, `"SRC_H"`, `"CRTC_X"`, `"CRTC_Y"`, `"CRTC_W"`, `"CRTC_H"`, `"IN_FENCE_FD"`.

**Atomic KMS object chain:** Connector → Encoder → CRTC → Plane. The primary plane is the main framebuffer. `drmModeGetResources()` gives connectors, encoders, CRTCs. `drmModeGetPlaneResources()` gives planes. For each CRTC, find its primary plane by matching `possible_crtcs` bitmask.

**`drmPrimeFDToHandle`:** converts a DMA-BUF fd to a DRM GEM handle internal to the DRM subsystem. Required before `drmModeAddFB2WithModifiers()` because DRM works with GEM handles, not raw fds. `pitches[0] = width * 4` for XRGB8888 (4 bytes/pixel). Modifier `DRM_FORMAT_MOD_LINEAR` is mandatory for cross-subsystem compatibility — X-tiling or Y-tiling can fail at the V4L2 → DRM boundary.

**`DRM_MODE_ATOMIC_NONBLOCK`:** `drmModeAtomicCommit` returns immediately. The actual buffer swap happens at the next VSYNC hardware interrupt. The kernel writes a `DRM_EVENT_FLIP_COMPLETE` event to the DRM fd (`card0`). Poll this fd with `io_uring_prep_poll_add` in the reactor loop to know when the previous buffer is safe to reuse.

**`IN_FENCE_FD` property:** set to the Vulkan semaphore sync fd exported in Phase 6. DRM hardware waits for that fence to signal before initiating scanout. This is the cross-subsystem explicit fence that replaces the 5–20μs implicit fence overhead.

### Implement

**`src/gfx/drm_kms.cc`**:
```cpp
#include "kbx/gfx/drm_kms.h"
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <cstring>
#include <cstdio>

// Query and cache a property ID by name for a DRM object
static u32 find_prop_id(int fd, u32 object_id, u32 object_type, const char* name) {
    drmModeObjectProperties* props = drmModeObjectGetProperties(fd, object_id, object_type);
    u32 id = 0;
    for (u32 i = 0; i < props->count_props; i++) {
        drmModePropertyRes* p = drmModeGetProperty(fd, props->props[i]);
        if (p && strcmp(p->name, name) == 0) { id = p->prop_id; drmModeFreeProperty(p); break; }
        drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);
    return id;
}

kbx_status_t kbx_drm_init(kbx_drm_ctx* ctx) {
    ctx->fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (ctx->fd < 0) return KBX_ERR_DRM;

    drmSetClientCap(ctx->fd, DRM_CLIENT_CAP_ATOMIC, 1);
    drmSetClientCap(ctx->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

    drmModeRes* res = drmModeGetResources(ctx->fd);

    // Find first connected connector
    ctx->connector_id = 0;
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector* c = drmModeGetConnector(ctx->fd, res->connectors[i]);
        if (c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            ctx->connector_id = c->connector_id;
            ctx->mode = c->modes[0];  // preferred mode (first = highest res)
            drmModeFreeConnector(c);
            break;
        }
        drmModeFreeConnector(c);
    }
    if (!ctx->connector_id) return KBX_ERR_DRM;

    // Walk encoder → CRTC chain
    drmModeConnector* conn = drmModeGetConnector(ctx->fd, ctx->connector_id);
    drmModeEncoder* enc    = drmModeGetEncoder(ctx->fd, conn->encoder_id);
    ctx->crtc_id           = enc->crtc_id;
    drmModeFreeEncoder(enc);
    drmModeFreeConnector(conn);

    // Find primary plane for this CRTC
    u32 crtc_idx = 0;
    for (int i = 0; i < res->count_crtcs; i++)
        if (res->crtcs[i] == ctx->crtc_id) { crtc_idx = i; break; }

    drmModePlaneRes* plane_res = drmModeGetPlaneResources(ctx->fd);
    ctx->plane_id = 0;
    for (u32 i = 0; i < plane_res->count_planes; i++) {
        drmModePlane* p = drmModeGetPlane(ctx->fd, plane_res->planes[i]);
        if (p->possible_crtcs & (1 << crtc_idx)) {
            // Check it's a PRIMARY plane via type property
            u32 type_id = find_prop_id(ctx->fd, p->plane_id, DRM_MODE_OBJECT_PLANE, "type");
            drmModeObjectProperties* oprops = drmModeObjectGetProperties(ctx->fd, p->plane_id, DRM_MODE_OBJECT_PLANE);
            for (u32 j = 0; j < oprops->count_props; j++) {
                if (oprops->props[j] == type_id && oprops->prop_values[j] == DRM_PLANE_TYPE_PRIMARY) {
                    ctx->plane_id = p->plane_id;
                }
            }
            drmModeFreeObjectProperties(oprops);
        }
        drmModeFreePlane(p);
        if (ctx->plane_id) break;
    }
    drmModeFreePlaneResources(plane_res);
    drmModeFreeResources(res);

    // Cache property IDs — MUST be queried, not hardcoded
    ctx->prop_fb_id     = find_prop_id(ctx->fd, ctx->plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
    ctx->prop_crtc_id   = find_prop_id(ctx->fd, ctx->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
    ctx->prop_src_x     = find_prop_id(ctx->fd, ctx->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X");
    ctx->prop_src_y     = find_prop_id(ctx->fd, ctx->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y");
    ctx->prop_src_w     = find_prop_id(ctx->fd, ctx->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W");
    ctx->prop_src_h     = find_prop_id(ctx->fd, ctx->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H");
    ctx->prop_crtc_x    = find_prop_id(ctx->fd, ctx->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X");
    ctx->prop_crtc_y    = find_prop_id(ctx->fd, ctx->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
    ctx->prop_crtc_w    = find_prop_id(ctx->fd, ctx->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W");
    ctx->prop_crtc_h    = find_prop_id(ctx->fd, ctx->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H");
    ctx->prop_in_fence  = find_prop_id(ctx->fd, ctx->plane_id, DRM_MODE_OBJECT_PLANE, "IN_FENCE_FD");

    fprintf(stderr, "[DRM] connector=%u crtc=%u plane=%u mode=%ux%u@%dHz\n",
        ctx->connector_id, ctx->crtc_id, ctx->plane_id,
        ctx->mode.hdisplay, ctx->mode.vdisplay, ctx->mode.vrefresh);
    return KBX_SUCCESS;
}

kbx_status_t kbx_drm_commit(kbx_drm_ctx* ctx, int dmabuf_fd, int in_fence_fd) {
    // Convert DMA-BUF fd → GEM handle
    u32 gem_handle;
    drmPrimeFDToHandle(ctx->fd, dmabuf_fd, &gem_handle);

    u32 handles[4]   = {gem_handle, 0, 0, 0};
    u32 pitches[4]   = {ctx->mode.hdisplay * 4, 0, 0, 0};
    u32 offsets[4]   = {0};
    u64 modifiers[4] = {DRM_FORMAT_MOD_LINEAR, 0, 0, 0};

    u32 fb_id;
    if (drmModeAddFB2WithModifiers(ctx->fd,
            ctx->mode.hdisplay, ctx->mode.vdisplay,
            DRM_FORMAT_XRGB8888,
            handles, pitches, offsets, modifiers,
            &fb_id, DRM_MODE_FB_MODIFIERS) < 0) return KBX_ERR_DRM;

    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    #define ADDP(prop, val) drmModeAtomicAddProperty(req, ctx->plane_id, ctx->prop_##prop, (val))
    ADDP(fb_id,    fb_id);
    ADDP(crtc_id,  ctx->crtc_id);
    ADDP(src_x,    0);
    ADDP(src_y,    0);
    ADDP(src_w,    (u64)ctx->mode.hdisplay << 16);  // 16.16 fixed-point
    ADDP(src_h,    (u64)ctx->mode.vdisplay << 16);
    ADDP(crtc_x,   0);
    ADDP(crtc_y,   0);
    ADDP(crtc_w,   ctx->mode.hdisplay);
    ADDP(crtc_h,   ctx->mode.vdisplay);
    if (in_fence_fd >= 0)
        ADDP(in_fence, in_fence_fd);                // Vulkan semaphore sync fd
    #undef ADDP

    // Non-blocking — returns before VSYNC. DRM signals flip event when done.
    int r = drmModeAtomicCommit(ctx->fd, req,
        DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, nullptr);
    drmModeAtomicFree(req);

    if (ctx->prev_fb_id) drmModeRmFB(ctx->fd, ctx->prev_fb_id);
    ctx->prev_fb_id = fb_id;
    return r < 0 ? KBX_ERR_DRM : KBX_SUCCESS;
}
```

---

## Phase 8 — io_uring Reactor

### Learn First

`poll()` and `epoll_wait()` are syscalls — each crosses the kernel boundary (user → kernel → user mode). On Linux 6.x this costs 200–400ns of context switch overhead per call.

`io_uring` with `IORING_SETUP_SQPOLL`: a kernel thread (`io_uring-sq`, visible in `ps`) continuously polls the Submission Queue (SQ) in shared mmap'd memory. User space writes a `struct io_uring_sqe` to the SQ ring buffer and the kthread picks it up — zero syscalls when the kthread is awake. The kthread sleeps after `sq_thread_idle` ms of inactivity; `io_uring_submit()` then issues `io_uring_enter(IORING_ENTER_SQ_WAKEUP)` to wake it (one syscall, rare event).

`io_uring_prep_ioctl(sqe, fd, VIDIOC_DQBUF, &buf)`: submits a V4L2 dequeue as an async operation (Linux 5.11+ `IORING_OP_IOCTL`). The reactor loop submits this and immediately continues doing other work (BPF telemetry drain, stats update) rather than blocking on the camera. When the camera has a frame, the CQE appears.

`io_uring_peek_cqe`: reads the Completion Queue (CQ) directly from shared mmap — no syscall. Combined with `__builtin_ia32_pause()` (x86 `PAUSE` instruction, opcode `F3 90`) in the spin loop:
- Without `PAUSE`: tight spin aggressively speculates on the branch → 15–30 cycles of branch misprediction penalty on every actual completion
- With `PAUSE`: signals the CPU that this is a spin-wait → drains out-of-order speculation, reduces power, yields pipeline resources to the Hyperthreading sibling logical core

CPU affinity: pin the SQ kthread (`IORING_SETUP_SQ_AFF`) to CPU 0 and the reactor to CPU 1 via `pthread_setaffinity_np`. This prevents migration overhead and keeps the two threads on separate physical cores with warm private caches.

### Implement

**`src/reactor/uring.cc`**:
```cpp
#include "kbx/reactor/uring.h"
#include <liburing.h>
#include <linux/videodev2.h>
#include <pthread.h>
#include <cstdio>

kbx_status_t kbx_uring_init(kbx_uring_ctx* ctx) {
    struct io_uring_params params = {};
    params.flags = IORING_SETUP_SQPOLL
                 | IORING_SETUP_SQ_AFF;  // pin SQ kthread to CPU 0
    params.sq_thread_cpu  = 0;
    params.sq_thread_idle = 10;  // ms before kthread sleeps

    if (io_uring_queue_init_params(32, &ctx->ring, &params) < 0)
        return KBX_ERR_URING;

    // Pin reactor thread to CPU 1
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset); CPU_SET(1, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);

    fprintf(stderr, "[URING] SQPOLL init, SQ kthread on CPU0, reactor on CPU1\n");
    return KBX_SUCCESS;
}

// Submit V4L2 DQBUF as async io_uring operation — no syscall if kthread awake
void kbx_uring_submit_dqbuf(kbx_uring_ctx* ctx, int v4l2_fd,
                              struct v4l2_buffer* buf) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx->ring);
    io_uring_prep_ioctl(sqe, v4l2_fd, VIDIOC_DQBUF, buf);
    io_uring_sqe_set_data(sqe, (void*)(uintptr_t)buf->index);
    io_uring_sqe_set_flags(sqe, IOSQE_ASYNC);
    io_uring_submit(&ctx->ring);  // may syscall if kthread asleep (rare)
}

// Spin until CQE arrives — no syscall. Do other work between pause iterations.
struct io_uring_cqe* kbx_uring_wait_cqe(kbx_uring_ctx* ctx) {
    struct io_uring_cqe* cqe;
    while (io_uring_peek_cqe(&ctx->ring, &cqe) != 0) {
        __builtin_ia32_pause();  // x86 PAUSE — yields to HT sibling, reduces power
    }
    return cqe;
}
```

**`src/main.cc`** — integration reactor loop:
```cpp
#include "kbx/core/types.h"
#include "kbx/mem/arena.h"
#include "kbx/mem/ring.h"
#include "kbx/io/v4l2.h"
#include "kbx/compute/l0_ctx.h"
#include "kbx/infer/ov_session.h"
#include "kbx/gfx/vk_ctx.h"
#include "kbx/gfx/vk_pass.h"
#include "kbx/gfx/drm_kms.h"
#include "kbx/reactor/uring.h"
#include "kbx/telemetry/bpf_loader.h"
#include <linux/videodev2.h>
#include <cstdio>

int main() {
    // ── INIT ORDER IS STRICT ──────────────────────────────────────────────
    // 1. Memory (NUMA preference must be set before any GPU allocation)
    kbx_arena arena;
    KBX_CHECK(kbx_arena_init(&arena, 1ULL << 30)); // 1GB HugeTLB pool

    // 2. eBPF (attach kprobes before any GPU work — avoids missing early events)
    kbx_bpf_ctx bpf = {};
    KBX_CHECK(kbx_bpf_init(&bpf));

    // 3. Level Zero (GPU compute context)
    kbx_l0_ctx l0 = {};
    KBX_CHECK(kbx_l0_init(&l0));
    ze_kernel_handle_t kern_nv12_rgb;
    KBX_CHECK(kbx_l0_load_kernel(&l0, "kernels/spv/nv12_to_rgb.spv",
                                   "convert_nv12_to_rgb", &kern_nv12_rgb));

    // 4. OpenVINO (compile on L0 context — must happen after L0 init)
    kbx_ov_ctx ov = {};
    KBX_CHECK(kbx_ov_init(&ov, &l0, "models/yolov8n_int8.xml"));

    // 5. Vulkan (rendering context)
    kbx_vk_ctx vk = {};
    KBX_CHECK(kbx_vk_init(&vk));

    // 6. DRM/KMS (display output)
    kbx_drm_ctx drm = {};
    KBX_CHECK(kbx_drm_init(&drm));

    // 7. V4L2 (camera — last because it starts DMA immediately on STREAMON)
    kbx_v4l2_ctx cam = {};
    KBX_CHECK(kbx_v4l2_init(&cam, "/dev/video0", 1920, 1080, 4));

    // 8. io_uring (reactor)
    kbx_uring_ctx uring = {};
    KBX_CHECK(kbx_uring_init(&uring));

    // ── REACTOR HOT LOOP ──────────────────────────────────────────────────
    struct v4l2_buffer v4l2_buf = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
                                    .memory = V4L2_MEMORY_DMABUF};
    ze_event_handle_t conv_done;
    {   // Create conv_done event from pool
        ze_event_desc_t ed = {.sType = ZE_STRUCTURE_TYPE_EVENT_DESC, .index = 0,
                               .signal = ZE_EVENT_SCOPE_FLAG_DEVICE,
                               .wait   = ZE_EVENT_SCOPE_FLAG_HOST};
        zeEventCreate(l0.event_pool, &ed, &conv_done);
    }

    while (true) {
        // A: Submit async V4L2 DQBUF (no syscall if SQ kthread is awake)
        kbx_uring_submit_dqbuf(&uring, cam.fd, &v4l2_buf);

        // B: Do non-blocking work while waiting for frame
        kbx_bpf_poll(&bpf);   // drain eBPF telemetry ring (zero-syscall)

        // C: Spin-wait for camera frame CQE (no syscall, PAUSE between checks)
        struct io_uring_cqe* cqe = kbx_uring_wait_cqe(&uring);
        u32 buf_idx = (u32)(uintptr_t)io_uring_cqe_get_data(cqe);
        io_uring_cqe_seen(&uring.ring, cqe);
        int dmabuf_fd = kbx_v4l2_dmabuf_fd(&cam, buf_idx);

        // D: Import DMA-BUF into Level Zero, dispatch NV12→RGB
        ze_image_handle_t l0_frame;
        KBX_CHECK(kbx_l0_import_dmabuf(&l0, dmabuf_fd, 1920, 1080, &l0_frame));
        // Allocate separate RGB output image from arena (or pre-allocate)
        ze_image_handle_t l0_rgb_out; // pre-allocated at init
        zeEventHostReset(conv_done);
        KBX_CHECK(kbx_l0_dispatch_nv12_rgb(&l0, kern_nv12_rgb,
                                             l0_frame, l0_frame, l0_rgb_out, &conv_done));

        // E: Wait for GPU conv to finish, then run inference
        // zeEventHostSynchronize() blocks CPU until GPU signals conv_done
        zeEventHostSynchronize(conv_done, UINT64_MAX);

        kbx_bbox boxes[32];
        u32 n_boxes = 0;
        KBX_CHECK(kbx_ov_infer(&ov, l0_rgb_out, boxes, &n_boxes, 32));

        // F: Vulkan overlay — import same DMA-BUF, draw boxes
        KBX_CHECK(kbx_vk_import_dmabuf(&vk, dmabuf_fd, 1920, 1080));
        int vk_fence_fd = -1;
        KBX_CHECK(kbx_vk_draw_boxes(&vk, boxes, n_boxes, &vk_fence_fd));

        // G: Commit to display — DRM waits for vk_fence_fd before scanout
        KBX_CHECK(kbx_drm_commit(&drm, dmabuf_fd, vk_fence_fd));

        // H: Return buffer to V4L2 for next frame capture
        KBX_CHECK(kbx_v4l2_requeue(&cam, buf_idx));

        // Cleanup per-frame GPU handles
        zeImageDestroy(l0_frame);
    }
}
```

---

## Phase 9 — eBPF Telemetry

### Learn First

**BPF CO-RE (Compile Once, Run Everywhere):** `vmlinux.h` is generated from your running kernel's BTF (BPF Type Format) metadata. BTF is built into the kernel at compile time and exposed at `/sys/kernel/btf/vmlinux`. `bpftool btf dump file /sys/kernel/btf/vmlinux format c > bpf/vmlinux.h` generates C declarations for every kernel struct, with exact field offsets for your specific kernel. The BPF CO-RE verifier then patches field access instructions at load time to handle offset differences across kernels — the same `.bpf.o` file works on any kernel that has the target function.

`BPF_MAP_TYPE_RINGBUF`: lock-free MPSC ring buffer in the kernel, shared via `mmap` with user space. Producer side: `bpf_ringbuf_reserve(rb, size, 0)` returns a slot pointer, you fill it, `bpf_ringbuf_submit(ptr, 0)` makes it visible. Consumer side: `ring_buffer__poll(rb, timeout_ms=0)` reads all available events and calls your callback synchronously — reads from shared mmap, no syscall.

`kprobe/i915_request_add`: fires when the i915 driver adds a GPU command to the hardware ring buffer (submission). `PT_REGS_PARM1(ctx)` is the first function argument — `struct i915_request*`. `BPF_CORE_READ(rq, context, hw_id)` reads `rq->context->hw_id` using CO-RE relocation — handles struct layout changes across kernel versions.

`kprobe/i915_request_retire`: fires when the GPU hardware signals completion and the driver retires the request. Subtract submit timestamp from retire timestamp = GPU execution time for that request.

### Implement

**Generate required files first:**
```bash
# Must run as root, uses running kernel's BTF
sudo bpftool btf dump file /sys/kernel/btf/vmlinux format c > bpf/vmlinux.h

# Build BPF object, then generate skeleton header
cmake --build build --target kbx_bpf_skel
# Skeleton header: bpf/kbx_trace.skel.h
# Contains: kbx_trace_bpf__open(), __load(), __attach(), __destroy()
```

**`bpf/kbx_trace.bpf.c`**:
```c
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

struct telemetry_event {
    u64 ts_ns;
    u64 gpu_exec_ns;
    u32 hw_ctx;
    u32 _pad;
};

// Lock-free ring buffer — user space reads via mmap, no syscall
struct {
    __uint(type,       BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);  // 256KB
} perf_events SEC(".maps");

// Per-context in-flight submission timestamp
struct {
    __uint(type,       BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key,   u32);   // hw_context_id
    __type(value, u64);   // submit timestamp (ns)
} inflight_ts SEC(".maps");

SEC("kprobe/i915_request_add")
int BPF_KPROBE(i915_submit_hook, struct i915_request* rq) {
    u32 hw_ctx = BPF_CORE_READ(rq, context, hw_id);
    u64 ts     = bpf_ktime_get_ns();
    bpf_map_update_elem(&inflight_ts, &hw_ctx, &ts, BPF_ANY);
    return 0;
}

SEC("kprobe/i915_request_retire")
int BPF_KPROBE(i915_retire_hook, struct i915_request* rq) {
    u32  hw_ctx    = BPF_CORE_READ(rq, context, hw_id);
    u64  now       = bpf_ktime_get_ns();
    u64* submit_ts = bpf_map_lookup_elem(&inflight_ts, &hw_ctx);

    struct telemetry_event* e = bpf_ringbuf_reserve(&perf_events, sizeof(*e), 0);
    if (!e) return 0;

    e->ts_ns      = now;
    e->hw_ctx     = hw_ctx;
    e->gpu_exec_ns = submit_ts ? (now - *submit_ts) : 0;
    bpf_ringbuf_submit(e, 0);
    bpf_map_delete_elem(&inflight_ts, &hw_ctx);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
```

**`src/telemetry/bpf_loader.cc`**:
```cpp
#include "kbx/telemetry/bpf_loader.h"
#include "kbx_trace.skel.h"   // generated by bpftool gen skeleton
#include <bpf/libbpf.h>
#include <cstdio>

struct telemetry_event {
    u64 ts_ns;
    u64 gpu_exec_ns;
    u32 hw_ctx;
    u32 _pad;
};

static int handle_event(void* ctx, void* data, size_t sz) {
    auto* e = (telemetry_event*)data;
    fprintf(stderr, "[BPF] ctx=%u gpu_exec=%.3fms at ts=%.3fs\n",
        e->hw_ctx, e->gpu_exec_ns / 1e6, e->ts_ns / 1e9);
    return 0;
}

kbx_status_t kbx_bpf_init(kbx_bpf_ctx* ctx) {
    ctx->skel = kbx_trace_bpf__open_and_load();
    if (!ctx->skel) return KBX_ERR_BPF;
    if (kbx_trace_bpf__attach(ctx->skel) < 0) return KBX_ERR_BPF;

    ctx->rb = ring_buffer__new(
        bpf_map__fd(ctx->skel->maps.perf_events),
        handle_event, nullptr, nullptr);
    if (!ctx->rb) return KBX_ERR_BPF;

    fprintf(stderr, "[BPF] kprobes attached: i915_request_add, i915_request_retire\n");
    return KBX_SUCCESS;
}

// Non-blocking drain — call in reactor loop between submissions
void kbx_bpf_poll(kbx_bpf_ctx* ctx) {
    ring_buffer__poll(ctx->rb, 0);  // timeout=0: returns immediately if empty
}
```

---

## Complete Fence & Synchronization Chain

```
CAMERA DMA ENGINE (hardware)
    │  writes NV12 → GBM DDR4 pages
    │  registers: dma_resv exclusive fence on the DMA-BUF
    ▼
VIDIOC_DQBUF (confirms DMA complete)
    │  signaled via: io_uring CQE (async ioctl)
    │  wait: io_uring_peek_cqe spin (no syscall)
    ▼
LEVEL ZERO NV12→RGB KERNEL
    │  waits: dma_resv_wait() automatically on zeImageCreate import
    │         (implicit fence — ~5–15μs, camera DMA must be done)
    │  dispatches: zeCommandListAppendLaunchKernel (immediate cmd list)
    │  signals: ze_event_handle_t conv_done (GPU-side event)
    ▼
HOST SYNC POINT
    │  waits: zeEventHostSynchronize(conv_done, UINT64_MAX)
    │         CPU blocks here ~0.3ms (GPU NV12→RGB duration)
    ▼
OPENVINO INFERENCE
    │  input: ze_image_handle_t wrapped as ov::RemoteTensor
    │         GPU reads directly from camera DDR4 (zero-copy)
    │  executes: YOLOv8n INT8 on Iris Xe EUs (~3–8ms)
    │  signals: infer_req.infer() returns (CPU unblocks)
    ▼
VULKAN RENDERING
    │  waits: VkFence from previous frame submission
    │         (ensures we don't write to buffer while DRM is scanning it out)
    │  imports: VkDeviceMemory over same DMA-BUF fd
    │  records + submits: vkQueueSubmit (bbox draw commands)
    │  signals: VkSemaphore exported as sync_fd (Linux sync file fd)
    ▼
DRM ATOMIC COMMIT
    │  IN_FENCE_FD = Vulkan sync_fd
    │  DRM plane hardware waits for fence before initiating scanout
    │  commits: drmModeAtomicCommit(NONBLOCK | PAGE_FLIP_EVENT)
    │  CPU returns immediately
    │  signals: DRM_EVENT_FLIP_COMPLETE at VSYNC (kernel writes to drm fd)
    ▼
DISPLAY SCANOUT (VSYNC boundary)
    │  display engine reads framebuffer pixels
    │  signals: out-fence on previous buffer (safe to reuse)
    ▼
VIDIOC_QBUF — requeue buffer for next capture
```

**Implicit vs explicit fencing:**
- Implicit (default): `dma_resv` framework handles automatically; adds ~5–20μs per DMA-BUF import
- Explicit (optimization): export `OUT_FENCE_FD` from DRM, import as `IN_FENCE_FD` for next stage; saves ~2–5μs; implement after pipeline is stable

---

## Mac → Server Development Streaming

For development without a hardware Intel camera, stream from Mac camera to `/dev/video0` via v4l2loopback.

```bash
# Server: load loopback module — creates /dev/video0 as virtual V4L2 device
sudo modprobe v4l2loopback \
    devices=1 video_nr=0 \
    card_label="KBX-Virtual" \
    exclusive_caps=1

# Verify
v4l2-ctl --device=/dev/video0 --all | grep -E "Card|Bus|Format"

# Mac: list capture devices
ffmpeg -f avfoundation -list_devices true -i "" 2>&1 | grep '\[0\]'

# Mac: stream camera → server (replace IP)
ffmpeg -f avfoundation -framerate 30 -video_size 1280x720 -i "0" \
    -vf scale=1920:1080 \
    -pix_fmt yuv420p \
    -f mjpeg -q:v 3 \
    -listen 1 "http://0.0.0.0:8080"

# Server: receive and feed /dev/video0 (run while Mac ffmpeg is listening)
ffmpeg -i http://192.168.X.X:8080 \
    -f v4l2 -pix_fmt yuv420p /dev/video0

# Server: verify frames arrive (captures 5 frames to files)
v4l2-ctl --device=/dev/video0 \
    --set-fmt-video=width=1920,height=1080,pixelformat=NV12 \
    --stream-mmap --stream-count=5

# Or quick sanity check
ffmpeg -f v4l2 -i /dev/video0 -frames:v 1 /tmp/test.jpg && xdg-open /tmp/test.jpg
```

---

## Bring-Up Validation Sequence

Run each stage independently before integrating. Every stage must pass before proceeding.

**Stage 0: System prerequisites**
```bash
# Huge pages
grep HugePages_Total /proc/meminfo  # must be >= 256
sudo sysctl -w vm.nr_hugepages=512

# Level Zero
ze_info | grep -E "Device|Type|EU"
# Expect: Intel(R) Iris(R) Xe Graphics, GPU, EU count: 96

# Vulkan DMA-BUF extensions
vulkaninfo 2>/dev/null | grep -i "dma_buf\|external_memory"
# Expect: VK_EXT_external_memory_dma_buf, VK_KHR_external_memory_fd

# DRM resources
sudo modetest -M i915 | head -80
# Note: connector_id, crtc_id, plane_id for debugging
```

**Stage 1: Memory pool**
```bash
./kbx_vision --test-mem
# [MEM] arena @ 0x7f... 1024MB OK — HugeTLB
# [MEM] ring push/pop 1M iterations — OK
```

**Stage 2: V4L2 camera ingress**
```bash
# Verify NV12 is available
v4l2-ctl --device=/dev/video0 --list-formats-ext | grep NV12
# If absent: check yuv420p from ffmpeg matches NV12 in kernel

# Stream 30 frames, check no errors
v4l2-ctl --device=/dev/video0 \
    --set-fmt-video=width=1920,height=1080,pixelformat=NV12 \
    --stream-mmap --stream-count=30 --stream-poll
```

**Stage 3: Level Zero kernel**
```bash
./kbx_vision --test-l0
# [L0] Device init OK
# [L0] Immediate CmdList OK
# [L0] Module load: nv12_to_rgb.spv OK
# [L0] Kernel dispatch 1920x1080 OK — conv time: 0.3ms
```

**Stage 4: OpenVINO inference**
```bash
benchmark_app -m models/yolov8n_int8.xml -d GPU \
    -nireq 1 -niter 200 -api sync 2>&1 | grep Latency
# Expect: Latency: 4.xx ms (vary by GPU clock state; run twice to warm up)

./kbx_vision --test-ov
# [OV] compiled YOLOv8n INT8 on GPU OK
# [OV] dummy inference OK — 0 boxes (blank input)
```

**Stage 5: Vulkan rendering**
```bash
./kbx_vision --test-vk
# [VK] instance + device OK (Intel 0x8086)
# [VK] DMA-BUF import OK (mem_type_idx=N)
# [VK] render pass OK
# [VK] bbox draw 3 boxes OK

# Check extensions
vulkaninfo --summary 2>/dev/null | grep -i "dma\|external\|semaphore"
```

**Stage 6: DRM display**
```bash
# Full property dump for your display
sudo modetest -M i915 -p
# Identify: FB_ID, CRTC_ID, SRC_*, CRTC_*, IN_FENCE_FD property IDs

./kbx_vision --test-drm
# [DRM] connector=N crtc=M plane=P mode=1920x1080@60Hz
# [DRM] atomic commit OK (should see display flip briefly)
```

**Stage 7: eBPF**
```bash
# Verify kprobe symbols exist
grep "i915_request_add\|i915_request_retire" /proc/kallsyms | head -5
# If empty: i915 module may not be loaded or symbol is not exported

./kbx_vision --test-bpf
# [BPF] kprobes attached: i915_request_add, i915_request_retire
# Run any GPU workload in another terminal, check events arrive
```

**Stage 8: Full pipeline**
```bash
# Build release
cmake --preset release
cmake --build build --parallel $(nproc)

# Run with GPU profiling
sudo ./build/kbx_vision &
sudo intel_gpu_top    # separate terminal — watch Render% during pipeline
sudo perf stat -e i915/render-busy/,i915/rcs-requests/,cache-misses \
    -I 1000 --pid $(pgrep kbx_vision)
```

---

## Performance Targets

| Stage | Target | Tool |
|---|---|---|
| Camera DMA (NV12 write) | 1–2 ms | V4L2 buf.timestamp delta |
| L0 NV12→RGB kernel | < 0.5 ms | zeEventHostSynchronize timing |
| OpenVINO YOLOv8n INT8 | 3–8 ms | ov::InferRequest profiling_info |
| Vulkan bbox rasterization | < 0.2 ms | Vulkan timestamp query extension |
| DRM atomic commit | < 1 ms CPU; fires at VSYNC | DRM page flip event timestamp |
| **Total frame budget** | **< 16.67 ms** (60 Hz) | eBPF wall clock (ts_ns delta) |

**Profiling commands:**
```bash
# GPU busy percentage (run while pipeline is active)
sudo intel_gpu_top -d drm:/dev/dri/card0

# CPU hot path flame graph
sudo perf record -g --call-graph=dwarf -p $(pgrep kbx_vision) -- sleep 10
sudo perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg

# Memory bandwidth usage
sudo perf mem -t load,store record -p $(pgrep kbx_vision) -- sleep 5
sudo perf mem report --sort=mem,symbol

# Full counter set
sudo perf stat -e \
    i915/render-busy/,i915/rcs-requests/,\
    cache-misses,LLC-load-misses,\
    dTLB-load-misses,instructions,cycles \
    -I 500 --pid $(pgrep kbx_vision)

# Per-stage GPU timing (add to main.cc)
# ze_event timestamps: zeCommandListAppendQueryKernelTimestamps
# after conv_done — gives kernel start/end in GPU clock ticks
```

---

## Known Hardware Errata (Tiger Lake Specific)

**`i915` vs `xe` driver confusion:** Ubuntu 26.04 may offer `xe` for Tiger Lake. `i915` is stable and tested with Level Zero. If you see `xe` active, blacklist it during development: `echo "blacklist xe" | sudo tee /etc/modprobe.d/kbx.conf && sudo update-initramfs -u`. Verify with `lsmod | grep -E "^xe|^i915"`.

**GBM format modifier incompatibility:** `GBM_BO_USE_LINEAR` forces `DRM_FORMAT_MOD_LINEAR`. If you accidentally allocate with a tiled modifier (X-tile, Y-tile), V4L2 will accept it but DRM `drmModeAddFB2WithModifiers` may fail with `EINVAL` if the plane doesn't support that modifier. Always use `DRM_FORMAT_MOD_LINEAR` until all three subsystems (V4L2, Vulkan, DRM) are confirmed working, then experiment with tiling for cache performance.

**v4l2loopback NV12:** The loopback module does not natively announce NV12 in `VIDIOC_ENUM_FMT` unless the feeding process has already set that format. FFmpeg with `-pix_fmt yuv420p` feeds `YUV420P` which v4l2loopback may expose as `YU12` (planar) not `NV12` (semi-planar). If `v4l2-ctl --list-formats-ext` shows `YU12` instead of `NV12`, either: (a) add a SPIR-V kernel variant for YU12 input, or (b) convert in ffmpeg: `-vf "format=nv12"` before piping to `/dev/video0`.

**OpenVINO + Level Zero interop API changes:** The `ov::intel_gpu::ocl::ClContext` constructor accepting `ze_context_handle_t` directly was introduced in OpenVINO 2024.1. Verify with `python3 -c "import openvino; print(openvino.__version__)"`. If older, use the `cl_context` handle path via `clCreateContextFromType` with the i915 GPU as the target device.

**DRM mode setting on Ubuntu with display manager:** If GNOME/KDE is running, it owns `/dev/dri/card0` exclusively via DRM master. `drmSetMaster()` will fail with `EPERM`. Either: (a) run in a TTY (`Ctrl+Alt+F2`, stop display manager: `sudo systemctl stop gdm3`), or (b) use `drmModeSetMaster()` only after grabbing the fd in `DRM_CLIENT_CAP_ATOMIC` mode. For development, running from a virtual console without display manager is simplest.

**`i915_request_add` renamed in kernel >= 6.9:** The function may be `__i915_request_add` or wrapped internally. Check with `sudo bpftool btf dump file /sys/kernel/btf/vmlinux | grep i915_request` to find the actual symbol name and update the `SEC("kprobe/...")` string accordingly.

**ECC memory on i7-11xxx:** Some i7-11th gen variants enable ECC on iGPU memory paths, reducing effective bandwidth by ~8%. This affects the NV12→RGB kernel throughput. `intel_gpu_top` will show correct bandwidth numbers; adjust latency targets accordingly.

**Page flip event loss:** If `drmModeAtomicCommit` returns `EBUSY`, the previous flip has not completed (DRM is still waiting for the previous `IN_FENCE_FD` to signal). Do not submit another commit. Wait for the DRM page flip event by polling `drm.fd` with `select()` or a separate `io_uring_prep_poll_add` submission before retrying.

---

*Architecture: Intel Tiger Lake (i7-11th Gen, Iris Xe Gen12 LP) | Ubuntu 26.04 | Linux 6.11+ | C++20/C17*  
*Paradigm: zero-copy DMA-BUF, zero-syscall reactor, bare-metal KMS, GPU-native inference*