# ZCVR — zero-copy vision runtime

**status: pre-alpha.** toolchain, core types, and the lock-free ring buffer are built. camera ingress, GPU compute, inference, display, and telemetry are designed — see below — but not wired up yet. there is no working binary at time of writing.

camera-to-display edge inference on Intel integrated graphics, built to avoid every copy the obvious implementation would make. a frame moves from V4L2 capture through GPU-side color conversion, OpenVINO inference, and Vulkan overlay rendering to DRM/KMS scanout as one DMA-BUF-backed set of physical pages — no `memcpy`, no userspace roundtrip, no syscall on the steady-state path.

internally still namespaced `kbx::`. the name predates the pipeline actually running end to end, and renaming headers across seven subsystems is a worse use of time than finishing phase 2.

---

## the pipeline

```
              camera (V4L2, NV12)
                      |
                      v   DMA-BUF, GBM-backed
        +--------------------------------+
        |      GPU color convert         |  OpenCL/SPIR-V kernel,
        |      NV12 -> RGB (BT.709)      |  Level Zero immediate cmdlist
        +--------------------------------+
                      |
                      v   ze_image_handle_t, no copy
        +--------------------------------+
        |      OpenVINO inference        |  INT8 YOLOv8n, remote tensor
        |                                 |  via OCL / Level Zero interop
        +--------------------------------+
                      |
                      v   bbox list (CPU, a few hundred bytes)
        +--------------------------------+
        |     Vulkan overlay render      |  push-constant boxes,
        |                                 |  LOAD_OP_LOAD onto the frame
        +--------------------------------+
                      |
                      v   exported VkSemaphore -> plane IN_FENCE_FD
        +--------------------------------+
        |      DRM/KMS scanout           |  atomic commit, page flip
        |                                 |  at vsync
        +--------------------------------+
```

the bounding-box list is the only thing that touches the CPU per frame. everything upstream of it stays on the GPU's side of the page table.

## why zero-copy, specifically, on this hardware

Tiger Lake's Iris Xe has no dedicated VRAM — it's a slice of the same DDR4 pool the CPU uses, over the same ~51.2GB/s dual-channel bus. that's usually described as a limitation. here it works the other way: camera DMA, GPU compute, and display scanout can all reference the *same physical pages*. the copy a naive `V4L2 -> cv::Mat -> upload -> download -> display` path does three or four times over was never a PCIe transfer being saved — there's no PCIe hop on an iGPU to save. it was redundant walks over pages that were never going anywhere.

what's actually left after removing the copies is a `dma_resv` fence lookup on every import (on the order of 5–15µs) and TLB pressure — a 1080p NV12 frame is 768 entries at 4KB pages, 2 at 2MB hugepages. neither of those shows up in a "look, no copies" diagram, and in practice both matter more than the copy that got removed.

## requirements

**hardware** — Intel Gen12+ iGPU (Tiger Lake / Alder Lake / Arc). development machine is an i7-1165G7: Iris Xe, 96 EU, Gen12LP, 16GB DDR4 shared with the CPU. no dGPU, no NPU.

**kernel** — 5.15+ covers `i915` + `io_uring` + DMA-BUF. 6.8+ if you want to try the `xe` path — untested here, see known issues below. developed on 6.11/6.12, Ubuntu 26.04.

**libraries** — OpenVINO runtime, oneAPI Level Zero loader, Vulkan SDK + loader, `liburing`, `libdrm`, `libgbm`, `libbpf`, `libnuma`.

**tools** — `clang-18`, `ocloc`, `bpftool`, cmake ≥ 3.24.

## build

```
cmake --preset release      # clang-18, -O3 -march=native -fno-exceptions -fno-rtti
cmake --build --preset release -j$(nproc)
```

`--preset asan` gives the same flags minus `-O3`, plus `-fsanitize=address,undefined`. SPIR-V kernels and the eBPF skeleton are generated as part of the build graph, not checked in pre-compiled:

```cmake
add_custom_command(TARGET zcvr PRE_BUILD
    COMMAND ocloc compile -file ${CMAKE_SOURCE_DIR}/kernels/cl/nv12_to_rgb.cl
            -device tgllp -options "-cl-std=CL3.0"
            -out_dir ${CMAKE_BINARY_DIR}/kernels/spv
    COMMENT "compiling SPIR-V kernels")
```

## running

needs `/dev/dri/renderD128` for GBM + Level Zero (no root), and `/dev/dri/card0` + `/dev/video0` for DRM/KMS + V4L2 (root, or `video`/`render` group membership). hugepages have to exist before the arena tries to mmap them:

```
sudo sysctl vm.nr_hugepages=64
ulimit -l unlimited   # or set memlock in /etc/security/limits.conf — MAP_LOCKED fails silently otherwise
./build/zcvr
```

## layout

```
.
├── CMakeLists.txt
├── CMakePresets.json
├── include/
│   └── kbx/core/
│       ├── compiler.h        # LIKELY/UNLIKELY, ALWAYS_INLINE, alignas helpers
│       ├── types.h           # ZCVR_status_t, fixed-width aliases
│       └── assert.h          # ZCVR_CHECK -> __builtin_trap(), no unwinding
├── src/
│   ├── mem/
│   │   ├── arena.cc          # hugepage bump allocator
│   │   ├── ring.cc           # SPSC ring, cache-line-separated head/tail
│   │   └── numa.cc
│   ├── io/
│   │   └── v4l2.cc
│   ├── compute/
│   │   ├── l0_ctx.cc
│   │   └── l0_kernel.cc
│   ├── infer/
│   │   └── ov_session.cc
│   ├── gfx/
│   │   ├── vk_ctx.cc
│   │   ├── vk_pass.cc
│   │   └── drm_kms.cc
│   ├── reactor/
│   │   └── uring.cc
│   ├── telemetry/
│   │   └── bpf_loader.cc
│   └── main.cc
├── kernels/cl/
│   └── nv12_to_rgb.cl
├── shaders/glsl/
│   ├── bbox.vert.glsl
│   └── bbox.frag.glsl
├── bpf/
│   ├── ZCVR_trace.bpf.c
│   ├── vmlinux.h             # generated, tools/gen_vmlinux.sh
│   └── ZCVR_trace.skel.h      # generated, tools/gen_bpf_skel.sh
├── cmake/targets/
│   ├── kernels.cmake
│   └── bpf.cmake
└── tools/
    ├── gen_vmlinux.sh
    └── gen_bpf_skel.sh
```

## status

| phase | subsystem | state |
|---|---|---|
| 0 | toolchain, core types, trap-based assert | done |
| 1 | hugepage arena / SPSC ring / NUMA binding | ring buffer done, arena + NUMA not started |
| 2 | V4L2 + DMA-BUF camera ingress | not started |
| 3 | Level Zero context + immediate command lists | not started |
| 4 | SPIR-V color conversion kernel | not started |
| 5 | OpenVINO zero-copy inference | not started |
| 6 | Vulkan overlay renderer | not started |
| 7 | DRM/KMS atomic display | not started |
| 8 | io_uring reactor | not started |
| 9 | eBPF telemetry | not started |
| 10 | integration / main loop | not started |

## known issues / non-goals

- USB webcams are inconsistent about exposing NV12 over V4L2 — check `v4l2-ctl --list-formats-ext` before assuming the DMA-BUF path just works. there's no software-conversion fallback; if the camera only speaks MJPEG/YUYV, the zero-copy claim doesn't hold.
- `i915` only. `xe` is the eventual target on newer kernels, but the DMA-BUF import path and a couple of kprobe symbols differ — not a supported configuration yet.
- DRM property IDs are queried at runtime and cached at init rather than hardcoded, but that cache lives for the process's lifetime — a display hot-unplug mid-run isn't handled.
- single camera device. no hot-plug, no multi-stream.
- no CI, no test suite beyond manual per-phase checks. this is a systems-learning project before it's anything else — treat it like one.

## why not gstreamer / deepstream

because they already solve this, better, for production. `msdk`/`va` on GStreamer, or DeepStream on Jetson-class hardware, gets a working pipeline with less code and far fewer driver-quirk debugging sessions. this exists because I wanted to see what's actually underneath a "zero-copy" claim — the fence chain, the page tables, the exact ioctls — not to replace tools that already do this well. if something needs to work in production tomorrow, use those instead.

---
