// Apple Metal backend for the GpuDevice interface. Compiled only on
// macOS (see src/CMakeLists.txt) and only consumed via factory.cc.
//
// Memory management: this file is compiled as Objective-C++ with ARC
// enabled. We bridge each retained id<MTLxxx> out as a +1-retained
// `void *` (__bridge_retained) so the C++ destructor can release it
// later (__bridge_transfer in Destroy()). The header keeps these as
// `void *` so the rest of the project does not need to be ObjC++.

#include "metal_device.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "metal_kernel.h"

#include "core/constants.h"
#include "util/log.h"

namespace gpu::apple {

namespace {

// Helpers that turn a +0 NSString into a UTF-8 std::string. Returns
// empty string on nil input.
std::string NsStringToStd(NSString *s) {
  if (s == nil) {
    return std::string();
  }
  const char *cstr = [s UTF8String];
  return cstr != nullptr ? std::string(cstr) : std::string();
}

// Enumerate MTLDevices in a stable order. `MTLCopyAllDevices()` is only
// available on macOS, but it covers both Apple Silicon (returns the
// single integrated GPU) and Intel Macs (returns integrated + discrete
// + any eGPUs in deterministic order).
NSArray<id<MTLDevice>> *CopyAllDevices() {
  NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
  if (devices != nil && devices.count > 0) {
    return devices;
  }
  // Fallback: very old Metal feature sets returned nil. Try the system
  // default device instead.
  id<MTLDevice> def = MTLCreateSystemDefaultDevice();
  if (def != nil) {
    return @[ def ];
  }
  return @[];
}

}  // namespace

int AppleMetalDevice::DeviceCount() {
  @autoreleasepool {
    NSArray<id<MTLDevice>> *devices = CopyAllDevices();
    return static_cast<int>(devices.count);
  }
}

AppleMetalDevice::AppleMetalDevice()
    : device_(nullptr),
      queue_(nullptr),
      library_(nullptr),
      pipeline_(nullptr),
      out_buffer_(nullptr),
      mem_buffer_(nullptr),
      mem_bytes_(0),
      device_index_(-1) {}

AppleMetalDevice::~AppleMetalDevice() { Destroy(); }

void AppleMetalDevice::Destroy() {
  // Reclaim each +1 retain we bridged out. Setting the void* to nullptr
  // after the transfer makes Destroy() idempotent.
  if (mem_buffer_ != nullptr) {
    id<MTLBuffer> b = (__bridge_transfer id<MTLBuffer>)mem_buffer_;
    (void)b;
    mem_buffer_ = nullptr;
    mem_bytes_ = 0;
  }
  if (out_buffer_ != nullptr) {
    id<MTLBuffer> b = (__bridge_transfer id<MTLBuffer>)out_buffer_;
    (void)b;
    out_buffer_ = nullptr;
  }
  if (pipeline_ != nullptr) {
    id<MTLComputePipelineState> p = (__bridge_transfer id<MTLComputePipelineState>)pipeline_;
    (void)p;
    pipeline_ = nullptr;
  }
  if (library_ != nullptr) {
    id<MTLLibrary> l = (__bridge_transfer id<MTLLibrary>)library_;
    (void)l;
    library_ = nullptr;
  }
  if (queue_ != nullptr) {
    id<MTLCommandQueue> q = (__bridge_transfer id<MTLCommandQueue>)queue_;
    (void)q;
    queue_ = nullptr;
  }
  if (device_ != nullptr) {
    id<MTLDevice> d = (__bridge_transfer id<MTLDevice>)device_;
    (void)d;
    device_ = nullptr;
  }
}

bool AppleMetalDevice::Init(int device_index) {
  @autoreleasepool {
    device_index_ = device_index;

    NSArray<id<MTLDevice>> *devices = CopyAllDevices();
    if (device_index < 0 || static_cast<NSUInteger>(device_index) >= devices.count) {
      LOG_WARN("Apple Metal: device index %d out of range (have %lu)", device_index,
               static_cast<unsigned long>(devices.count));
      return false;
    }
    id<MTLDevice> device = devices[static_cast<NSUInteger>(device_index)];
    if (device == nil) {
      LOG_WARN("Apple Metal: nil MTLDevice at index %d", device_index);
      return false;
    }
    name_ = NsStringToStd([device name]);
    if (name_.empty()) {
      name_ = "Apple GPU";
    }

    // Compile the embedded MSL source into a MTLLibrary. This is the
    // host-side equivalent of NVIDIA's cuModuleLoadData(PTX) and AMD's
    // hiprtcCompileProgram() -- it is run once at startup.
    NSError *err = nil;
    NSString *src = [NSString stringWithUTF8String:kBusyKernelMsl];
    id<MTLLibrary> library = [device newLibraryWithSource:src options:nil error:&err];
    if (library == nil) {
      LOG_WARN("Apple Metal: newLibraryWithSource failed: %s",
               NsStringToStd([err localizedDescription]).c_str());
      return false;
    }
    id<MTLFunction> function = [library newFunctionWithName:@"busy_kernel"];
    if (function == nil) {
      LOG_WARN("Apple Metal: newFunctionWithName(busy_kernel) returned nil");
      return false;
    }
    err = nil;
    id<MTLComputePipelineState> pipeline =
        [device newComputePipelineStateWithFunction:function error:&err];
    if (pipeline == nil) {
      LOG_WARN("Apple Metal: newComputePipelineStateWithFunction failed: %s",
               NsStringToStd([err localizedDescription]).c_str());
      return false;
    }

    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (queue == nil) {
      LOG_WARN("Apple Metal: newCommandQueue returned nil");
      return false;
    }

    // Per-thread scratch buffer. Sized to match the dispatch grid so each
    // thread has exactly one uint32_t to write to (mirrors the CUDA/HIP
    // backends).
    const NSUInteger threads =
        static_cast<NSUInteger>(kGpuKernelGridSize) * static_cast<NSUInteger>(kGpuKernelBlockSize);
    const NSUInteger out_bytes = threads * sizeof(uint32_t);
    id<MTLBuffer> out_buffer =
        [device newBufferWithLength:out_bytes options:MTLResourceStorageModePrivate];
    if (out_buffer == nil) {
      LOG_WARN("Apple Metal: newBufferWithLength(%lu, scratch) returned nil",
               static_cast<unsigned long>(out_bytes));
      return false;
    }

    // Bridge-retain each handle into our void* slots. From this point on
    // Destroy() owns them.
    device_ = (__bridge_retained void *)device;
    queue_ = (__bridge_retained void *)queue;
    library_ = (__bridge_retained void *)library;
    pipeline_ = (__bridge_retained void *)pipeline;
    out_buffer_ = (__bridge_retained void *)out_buffer;
    return true;
  }
}

std::string AppleMetalDevice::Name() const {
  char buf[320];
  std::snprintf(buf, sizeof(buf), "%s (#%d, Metal)", name_.c_str(), device_index_);
  return std::string(buf);
}

bool AppleMetalDevice::AllocateMemory(size_t bytes) {
  if (device_ == nullptr || queue_ == nullptr) {
    return false;
  }
  if (bytes == 0) {
    ReleaseMemory();
    return true;
  }
  @autoreleasepool {
    // Drop any previous allocation first.
    if (mem_buffer_ != nullptr) {
      id<MTLBuffer> b = (__bridge_transfer id<MTLBuffer>)mem_buffer_;
      (void)b;
      mem_buffer_ = nullptr;
      mem_bytes_ = 0;
    }

    id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)queue_;

    // Storage mode: on Apple Silicon (UMA) every storage mode lives in
    // system RAM. MTLResourceStorageModePrivate forces a GPU-only
    // allocation and skips any CPU-side caching, which matches what
    // CUDA's cuMemAlloc / HIP's hipMalloc give us on discrete GPUs.
    id<MTLBuffer> buffer =
        [device newBufferWithLength:bytes options:MTLResourceStorageModePrivate];
    if (buffer == nil) {
      LOG_WARN("Apple Metal: newBufferWithLength(%zu) returned nil", bytes);
      return false;
    }

    // Force physical commit by filling the buffer once. On UMA systems
    // this means the kernel actually pages the memory in.
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit fillBuffer:buffer range:NSMakeRange(0, bytes) value:0xA5];
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];

    mem_buffer_ = (__bridge_retained void *)buffer;
    mem_bytes_ = bytes;
    return true;
  }
}

void AppleMetalDevice::ReleaseMemory() {
  if (mem_buffer_ != nullptr) {
    id<MTLBuffer> b = (__bridge_transfer id<MTLBuffer>)mem_buffer_;
    (void)b;
    mem_buffer_ = nullptr;
    mem_bytes_ = 0;
  }
}

bool AppleMetalDevice::RunKernel(int loop_count) {
  if (queue_ == nullptr || pipeline_ == nullptr || out_buffer_ == nullptr) {
    return false;
  }
  if (loop_count < 1) {
    loop_count = 1;
  }
  @autoreleasepool {
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)queue_;
    id<MTLComputePipelineState> pipeline = (__bridge id<MTLComputePipelineState>)pipeline_;
    id<MTLBuffer> out_buffer = (__bridge id<MTLBuffer>)out_buffer_;

    id<MTLCommandBuffer> cb = [queue commandBuffer];
    if (cb == nil) {
      LOG_WARN("Apple Metal: commandBuffer returned nil");
      return false;
    }
    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
    if (enc == nil) {
      LOG_WARN("Apple Metal: computeCommandEncoder returned nil");
      return false;
    }
    [enc setComputePipelineState:pipeline];
    [enc setBuffer:out_buffer offset:0 atIndex:0];

    const uint32_t loop_count_u32 = static_cast<uint32_t>(loop_count);
    [enc setBytes:&loop_count_u32 length:sizeof(loop_count_u32) atIndex:1];

    // Match the CUDA/HIP launch shape: kGpuKernelGridSize threadgroups
    // of kGpuKernelBlockSize threads each.
    MTLSize grid = MTLSizeMake(static_cast<NSUInteger>(kGpuKernelGridSize), 1, 1);
    MTLSize tg = MTLSizeMake(static_cast<NSUInteger>(kGpuKernelBlockSize), 1, 1);
    [enc dispatchThreadgroups:grid threadsPerThreadgroup:tg];
    [enc endEncoding];

    [cb commit];
    [cb waitUntilCompleted];

    NSError *err = [cb error];
    if (err != nil) {
      LOG_WARN("Apple Metal: command buffer error: %s",
               NsStringToStd([err localizedDescription]).c_str());
      return false;
    }
    return true;
  }
}

int AppleMetalDevice::ProbeBaseLoopCount(int target_ms) {
  if (pipeline_ == nullptr) {
    return 1;
  }
  // Same doubling-then-bisection algorithm as the NVIDIA / AMD backends
  // so the three implementations behave identically from the worker's
  // point of view.
  int low = 1;
  int high = 1024;
  for (int probe = 0; probe < 24; ++probe) {
    const auto start = std::chrono::high_resolution_clock::now();
    if (!RunKernel(high)) {
      return std::max(1, low);
    }
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::high_resolution_clock::now() - start)
                                .count();
    if (elapsed_ms >= target_ms) {
      break;
    }
    low = high;
    if (high >= kGpuKernelLoopBaseMax / 2) {
      high = kGpuKernelLoopBaseMax;
      break;
    }
    high *= 2;
  }

  int best = low;
  int64_t best_diff = static_cast<int64_t>(target_ms);
  for (int i = 0; i < kGpuKernelLoopBaseTestIteration && low + 1 < high; ++i) {
    const int mid = low + (high - low) / 2;
    const auto start = std::chrono::high_resolution_clock::now();
    if (!RunKernel(mid)) {
      break;
    }
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::high_resolution_clock::now() - start)
                                .count();
    const int64_t diff =
        elapsed_ms > target_ms ? (elapsed_ms - target_ms) : (target_ms - elapsed_ms);
    if (diff < best_diff) {
      best = mid;
      best_diff = diff;
    }
    if (elapsed_ms > target_ms) {
      high = mid;
    } else {
      low = mid;
    }
  }
  LOG_DEBUG("AppleMetalDevice #%d probed base_loop_count=%d (target=%dms)", device_index_, best,
            target_ms);
  return std::max(1, best);
}

}  // namespace gpu::apple
