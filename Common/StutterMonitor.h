#pragma once

#include <cstddef>
#include <cstdint>

#include "Common/Log.h"
#include "Common/TimeUtil.h"

// Lightweight, always-present frame-drop diagnostics feeding the Log::Stutter channel.
// Every entry point starts by checking IsEnabled() (the same cheap check any WARN_LOG
// call already does), so the feature costs effectively nothing when the STUTTER log
// channel is disabled.
namespace StutterMonitor {

inline bool IsEnabled() {
	return GenericLogEnabled(Log::Stutter, LogLevel::LWARNING);
}

void AddIOTime(uint32_t handle, bool isWrite, size_t bytes, double elapsedMs);
void AddIOThreadBusy(double elapsedMs);
void AddTextureAsyncWait(double elapsedMs, bool stillPending);
void AddLockWait(const char *lockName, double elapsedMs);
// Time spent synchronously decoding/uploading a texture that wasn't already cached
// (TextureCacheCommon::BuildTexture) -- distinct from AddTextureAsyncWait, which only
// covers the background-thread replacement-texture path.
void AddTextureBuild(double elapsedMs);
// Time the CPU/emu thread spent blocked waiting for the GPU backend's render thread to
// catch up (e.g. GLRenderManager::BeginFrame's fence wait). This is backpressure from
// *whatever* the render thread is doing -- shader compile, texture upload, heavy draw
// processing, or actual vsync/present -- none of which gpuStats' CPU-side display-list
// timing can see, since that only measures time spent enqueuing commands, not the
// render thread executing them.
void AddGpuSyncWait(double elapsedMs);
// Adjust the persistent (not per-frame -- see Tick()) count of textures currently being
// decoded in the background. +1 when a PendingTextureDecode starts, -1 when it's destroyed
// (whether by finishing normally or by the owning entry being evicted mid-decode), so the
// count is always accurate regardless of which path retires it. See AsyncTextureDecode.h.
void AdjustPendingDecodeCount(int delta);

// Called once per displayed frame (from sceDisplay's existing frame timing/drop
// detection), regardless of whether this particular frame dropped. gpuMs/cpuMs are
// the caller's own GPU display-list and kernel syscall time for this frame (the
// caller is responsible for keeping debug-stat collection enabled while the STUTTER
// channel is on, since that's a Core-layer concern). If `didDrop` is true, composes
// and emits one Log::Stutter line summarizing everything accumulated by the Add*
// functions above since the previous call. Always resets the accumulators afterward
// so the next report reflects only its own frame's window.
void Tick(bool didDrop, double budgetMs, double actualMs, double gpuMs, double cpuMs);

// RAII helper for timing a specific lock's hold time. Adds nothing but a cheap
// IsEnabled() check when the monitor is disabled.
class ScopedLockTimer {
public:
	explicit ScopedLockTimer(const char *lockName) : lockName_(lockName), enabled_(IsEnabled()) {
		if (enabled_)
			startTime_ = time_now_d();
	}
	~ScopedLockTimer() {
		if (enabled_)
			AddLockWait(lockName_, (time_now_d() - startTime_) * 1000.0);
	}

private:
	const char *lockName_;
	bool enabled_;
	double startTime_ = 0.0;
};

}  // namespace StutterMonitor
