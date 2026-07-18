#include <atomic>
#include <cstdio>
#include <mutex>

#include "Common/StutterMonitor.h"

namespace StutterMonitor {

// All accumulators are reset every frame by Tick(), win or lose, so they only ever
// need to represent one frame's worth of activity. Written from whichever thread
// (display, IO workers, texture-loader threads) observes the event; read/reset from
// the display thread in Tick(). Times are kept as whole microseconds so plain integer
// atomics suffice (no std::atomic<double> before C++20).
static std::atomic<int64_t> g_ioBytes{0};
static std::atomic<int64_t> g_ioTimeUs{0};
static std::atomic<int> g_ioOpCount{0};
static std::atomic<int64_t> g_ioThreadBusyUs{0};
static std::atomic<int64_t> g_texAsyncWaitUs{0};
static std::atomic<int> g_texStillPendingCount{0};
static std::atomic<int64_t> g_texBuildUs{0};
static std::atomic<int> g_texBuildCount{0};
static std::atomic<int64_t> g_gpuSyncWaitUs{0};

// NOT reset per-frame by ResetFrameAccumulators() -- this reflects live decode-queue depth,
// not a single frame's activity. See AdjustPendingDecodeCount's doc comment in the header.
static std::atomic<int> g_pendingDecodeCount{0};

// The worst single lock wait this frame. Lock name pointers are always to
// string-literal storage (see call sites), so it's safe to store and log the raw
// pointer without a mutex -- only the numeric "is this the new worst" comparison
// needs to be atomic.
static std::atomic<int64_t> g_worstLockWaitUs{0};
static std::atomic<const char *> g_worstLockName{""};

static int64_t MsToUs(double ms) {
	return (int64_t)(ms * 1000.0);
}

void AddIOTime(uint32_t handle, bool isWrite, size_t bytes, double elapsedMs) {
	if (!IsEnabled())
		return;
	g_ioBytes.fetch_add((int64_t)bytes, std::memory_order_relaxed);
	g_ioTimeUs.fetch_add(MsToUs(elapsedMs), std::memory_order_relaxed);
	g_ioOpCount.fetch_add(1, std::memory_order_relaxed);
}

void AddIOThreadBusy(double elapsedMs) {
	if (!IsEnabled())
		return;
	g_ioThreadBusyUs.fetch_add(MsToUs(elapsedMs), std::memory_order_relaxed);
}

void AddTextureAsyncWait(double elapsedMs, bool stillPending) {
	if (!IsEnabled())
		return;
	g_texAsyncWaitUs.fetch_add(MsToUs(elapsedMs), std::memory_order_relaxed);
	if (stillPending)
		g_texStillPendingCount.fetch_add(1, std::memory_order_relaxed);
}

void AddTextureBuild(double elapsedMs) {
	if (!IsEnabled())
		return;
	g_texBuildUs.fetch_add(MsToUs(elapsedMs), std::memory_order_relaxed);
	g_texBuildCount.fetch_add(1, std::memory_order_relaxed);
}

void AddGpuSyncWait(double elapsedMs) {
	if (!IsEnabled())
		return;
	g_gpuSyncWaitUs.fetch_add(MsToUs(elapsedMs), std::memory_order_relaxed);
}

void AdjustPendingDecodeCount(int delta) {
	if (!IsEnabled())
		return;
	g_pendingDecodeCount.fetch_add(delta, std::memory_order_relaxed);
}

void AddLockWait(const char *lockName, double elapsedMs) {
	if (!IsEnabled())
		return;
	int64_t us = MsToUs(elapsedMs);
	int64_t prevWorst = g_worstLockWaitUs.load(std::memory_order_relaxed);
	while (us > prevWorst) {
		if (g_worstLockWaitUs.compare_exchange_weak(prevWorst, us, std::memory_order_relaxed)) {
			g_worstLockName.store(lockName, std::memory_order_relaxed);
			break;
		}
	}
}

static void ResetFrameAccumulators() {
	g_ioBytes.store(0, std::memory_order_relaxed);
	g_ioTimeUs.store(0, std::memory_order_relaxed);
	g_ioOpCount.store(0, std::memory_order_relaxed);
	g_ioThreadBusyUs.store(0, std::memory_order_relaxed);
	g_texAsyncWaitUs.store(0, std::memory_order_relaxed);
	g_texStillPendingCount.store(0, std::memory_order_relaxed);
	g_texBuildUs.store(0, std::memory_order_relaxed);
	g_texBuildCount.store(0, std::memory_order_relaxed);
	g_gpuSyncWaitUs.store(0, std::memory_order_relaxed);
	g_worstLockWaitUs.store(0, std::memory_order_relaxed);
	g_worstLockName.store("", std::memory_order_relaxed);
}

void Tick(bool didDrop, double budgetMs, double actualMs, double gpuMs, double cpuMs) {
	if (!IsEnabled()) {
		return;
	}

	if (didDrop) {
		WARN_LOG(Log::Stutter,
			"Frame %.1fms (target %.1fms): IO=%lldB/%.1fms(%dops) ioThreadBusy=%.1fms texWait=%.1fms(%d still pending) texBuild=%.1fms(%dtex) asyncDecodesPending=%d gpuSyncWait=%.1fms worstLock=%.1fms(%s) gpu=%.1fms cpu=%.1fms",
			actualMs, budgetMs,
			(long long)g_ioBytes.load(std::memory_order_relaxed), g_ioTimeUs.load(std::memory_order_relaxed) / 1000.0, g_ioOpCount.load(std::memory_order_relaxed),
			g_ioThreadBusyUs.load(std::memory_order_relaxed) / 1000.0,
			g_texAsyncWaitUs.load(std::memory_order_relaxed) / 1000.0, g_texStillPendingCount.load(std::memory_order_relaxed),
			g_texBuildUs.load(std::memory_order_relaxed) / 1000.0, g_texBuildCount.load(std::memory_order_relaxed),
			g_pendingDecodeCount.load(std::memory_order_relaxed),
			g_gpuSyncWaitUs.load(std::memory_order_relaxed) / 1000.0,
			g_worstLockWaitUs.load(std::memory_order_relaxed) / 1000.0, g_worstLockName.load(std::memory_order_relaxed),
			gpuMs, cpuMs);
	}

	ResetFrameAccumulators();
}

}  // namespace StutterMonitor
