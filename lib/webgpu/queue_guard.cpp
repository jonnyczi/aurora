#include "queue_guard.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>

#include <webgpu/webgpu.h>

namespace aurora::webgpu {
namespace {

std::atomic_bool g_shared{false};
std::recursive_mutex g_mutex;
std::atomic<int64_t> g_waitMaxNs{0};
std::atomic<uint32_t> g_waitsOver1ms{0};

} // namespace

void set_queue_shared(bool shared) noexcept { g_shared.store(shared, std::memory_order_release); }
bool queue_shared() noexcept { return g_shared.load(std::memory_order_acquire); }

QueueGuard::QueueGuard(bool timed) noexcept {
  if (!queue_shared())
    return;
  if (!timed) {
    g_mutex.lock();
  } else if (!g_mutex.try_lock()) {
    const auto start = std::chrono::steady_clock::now();
    g_mutex.lock();
    const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    int64_t prev = g_waitMaxNs.load(std::memory_order_relaxed);
    while (ns > prev && !g_waitMaxNs.compare_exchange_weak(prev, ns, std::memory_order_relaxed)) {
    }
    if (ns > 1000000)
      g_waitsOver1ms.fetch_add(1, std::memory_order_relaxed);
  }
  m_locked = true;
}

QueueGuard::~QueueGuard() {
  if (m_locked)
    g_mutex.unlock();
}

void queue_guard_waits(int64_t& maxNs, uint32_t& over1ms) noexcept {
  maxNs = g_waitMaxNs.exchange(0, std::memory_order_relaxed);
  over1ms = g_waitsOver1ms.exchange(0, std::memory_order_relaxed);
}

} // namespace aurora::webgpu

#ifdef AURORA_QUEUE_GUARD_WRAP
// The Dawn entry points that can reach the Vulkan queue, wrapped at link time
// (ld --wrap, cmake/aurora_xr.cmake): every caller's reference to wgpuX
// resolves to __wrap_wgpuX, which holds the guard around Dawn's (__real_).
using aurora::webgpu::QueueGuard;

#define AURORA_WRAP_VOID(name, params, args)                                                                           \
  extern "C" void __real_##name params;                                                                                \
  extern "C" void __wrap_##name params {                                                                               \
    const QueueGuard guard;                                                                                            \
    __real_##name args;                                                                                                \
  }
#define AURORA_WRAP(ret, name, params, args)                                                                           \
  extern "C" ret __real_##name params;                                                                                 \
  extern "C" ret __wrap_##name params {                                                                                \
    const QueueGuard guard;                                                                                            \
    return __real_##name args;                                                                                         \
  }

AURORA_WRAP_VOID(wgpuQueueSubmit, (WGPUQueue q, size_t n, WGPUCommandBuffer const* c), (q, n, c))
AURORA_WRAP_VOID(wgpuQueueWriteBuffer, (WGPUQueue q, WGPUBuffer b, uint64_t o, void const* d, size_t s),
                 (q, b, o, d, s))
AURORA_WRAP_VOID(wgpuQueueWriteTexture,
                 (WGPUQueue q, WGPUTexelCopyTextureInfo const* dst, void const* d, size_t s,
                  WGPUTexelCopyBufferLayout const* l, WGPUExtent3D const* e),
                 (q, dst, d, s, l, e))
AURORA_WRAP(WGPUFuture, wgpuQueueOnSubmittedWorkDone, (WGPUQueue q, WGPUQueueWorkDoneCallbackInfo i), (q, i))
AURORA_WRAP(WGPUFuture, wgpuBufferMapAsync,
            (WGPUBuffer b, WGPUMapMode m, size_t o, size_t s, WGPUBufferMapCallbackInfo i), (b, m, o, s, i))
AURORA_WRAP_VOID(wgpuBufferUnmap, (WGPUBuffer b), (b))
AURORA_WRAP_VOID(wgpuBufferDestroy, (WGPUBuffer b), (b))
AURORA_WRAP_VOID(wgpuInstanceProcessEvents, (WGPUInstance i), (i))
AURORA_WRAP_VOID(wgpuDeviceTick, (WGPUDevice d), (d))
AURORA_WRAP_VOID(wgpuDeviceDestroy, (WGPUDevice d), (d))
AURORA_WRAP_VOID(wgpuDeviceRelease, (WGPUDevice d), (d))
AURORA_WRAP(WGPUStatus, wgpuSurfacePresent, (WGPUSurface s), (s))
AURORA_WRAP_VOID(wgpuSurfaceGetCurrentTexture, (WGPUSurface s, WGPUSurfaceTexture* t), (s, t))
AURORA_WRAP_VOID(wgpuSurfaceConfigure, (WGPUSurface s, WGPUSurfaceConfiguration const* c), (s, c))
AURORA_WRAP_VOID(wgpuSurfaceUnconfigure, (WGPUSurface s), (s))
AURORA_WRAP(WGPUStatus, wgpuSharedTextureMemoryBeginAccess,
            (WGPUSharedTextureMemory m, WGPUTexture t, WGPUSharedTextureMemoryBeginAccessDescriptor const* d), (m, t, d))
AURORA_WRAP(WGPUStatus, wgpuSharedTextureMemoryEndAccess,
            (WGPUSharedTextureMemory m, WGPUTexture t, WGPUSharedTextureMemoryEndAccessState* d), (m, t, d))

// A wait holds the guard only while it submits what it waits for (a zero
// timeout flushes the awaited serials); the wait itself then touches no
// queue, and other threads, the XR thread above all, may use it meanwhile.
extern "C" WGPUWaitStatus __real_wgpuInstanceWaitAny(WGPUInstance i, size_t n, WGPUFutureWaitInfo* f, uint64_t t);
extern "C" WGPUWaitStatus __wrap_wgpuInstanceWaitAny(WGPUInstance i, size_t n, WGPUFutureWaitInfo* f, uint64_t t) {
  if (!aurora::webgpu::queue_shared())
    return __real_wgpuInstanceWaitAny(i, n, f, t);
  WGPUWaitStatus status;
  {
    const QueueGuard guard;
    status = __real_wgpuInstanceWaitAny(i, n, f, 0);
  }
  if (t == 0 || status != WGPUWaitStatus_TimedOut ||
      std::any_of(f, f + n, [](const WGPUFutureWaitInfo& w) { return w.completed != 0; }))
    return status;
  return __real_wgpuInstanceWaitAny(i, n, f, t);
}
#endif
