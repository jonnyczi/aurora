#pragma once

// One Vulkan queue shared by Dawn, the OpenXR runtime and the XR thread.
//
// The XR presenter gives the runtime (and its own submits) a queue of their
// own in Dawn's queue family when the family has a spare one (the Quest's
// Adreno driver does). Mesa's Turnip (the Steam Frame) and RADV have one
// queue for graphics, so there all three share Dawn's, and Vulkan requires
// every use of a queue to be externally synchronized. With sharing on
// (lib/xr, xr::device_created), this lock is held around:
//   - every Dawn entry point that may reach the queue: submits, writes and
//     buffer maps that can flush, event processing and waits, presents,
//     shared texture memory access, device destruction. On Linux these are
//     wrapped at link time (cmake/aurora_xr.cmake, queue_guard.cpp), so every
//     caller is covered: aurora, the ImGui and RmlUi backends, the game.
//   - the XR thread's own submits, and the OpenXR calls that may use the
//     queue (xrBeginFrame, xrEndFrame, swapchain acquire and release).
// Off (the queue isn't shared), the guard does nothing.
//
// Lock order: this lock, then Dawn's device lock, never the other way. Dawn
// calls its callbacks with its own lock released, but they can run with this
// one held (inside a wrapped event processing call), so a callback must not
// wait for another thread that is waiting for this lock.

#include <cstdint>

namespace aurora::webgpu {

// Sharing on or off; set once, when the device is created, before any
// thread but the creating one uses it.
void set_queue_shared(bool shared) noexcept;
bool queue_shared() noexcept;

class QueueGuard {
public:
  // timed: the wait for the lock is counted (queue_guard_waits), for the
  // XR thread, whose frames it can make late.
  explicit QueueGuard(bool timed = false) noexcept;
  ~QueueGuard();
  QueueGuard(const QueueGuard&) = delete;
  QueueGuard& operator=(const QueueGuard&) = delete;

private:
  bool m_locked = false;
};

// The timed waits since the last call: the longest, in nanoseconds, and how
// many took over a millisecond. Resets both.
void queue_guard_waits(int64_t& maxNs, uint32_t& over1ms) noexcept;

} // namespace aurora::webgpu
