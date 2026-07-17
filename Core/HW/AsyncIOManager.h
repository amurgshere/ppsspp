// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <chrono>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <map>
#include <set>

#include "Core/Core.h"

#include "Core/System.h"
#include "Core/CoreTiming.h"

enum AsyncIOEventType {
	IO_EVENT_INVALID,
	IO_EVENT_SYNC,
	IO_EVENT_FINISH,
	IO_EVENT_READ,
	IO_EVENT_WRITE,
};

// True for event types that operate on a specific file handle and thus need to be
// serialized against other events for that same handle when running on more than
// one worker thread. SYNC/FINISH are barriers with no associated handle.
inline bool AsyncIOEventHasHandle(AsyncIOEventType t) {
	return t == IO_EVENT_READ || t == IO_EVENT_WRITE;
}

struct AsyncIOEvent {
	AsyncIOEvent(AsyncIOEventType t) : type(t) {}
	AsyncIOEventType type;
	u32 handle;
	u8 *buf;
	size_t bytes;
	u32 invalidateAddr;

	operator AsyncIOEventType() const {
		return type;
	}
};

struct AsyncIOResult {
	AsyncIOResult() : result(0), finishTicks(0), invalidateAddr(0) {}

	explicit AsyncIOResult(s64 r) : result(r), finishTicks(0), invalidateAddr(0) {}

	AsyncIOResult(s64 r, int usec, u32 addr = 0) : result(r), invalidateAddr(addr) {
		finishTicks = CoreTiming::GetTicks() + usToCycles(usec);
	}

	void DoState(PointerWrap &p) {
		auto s = p.Section("AsyncIOResult", 1, 2);
		if (!s)
			return;

		Do(p, result);
		Do(p, finishTicks);
		if (s >= 2) {
			Do(p, invalidateAddr);
		} else {
			invalidateAddr = 0;
		}
	}

	s64 result;
	u64 finishTicks;
	u32 invalidateAddr;
};

// Services async file I/O on a small pool of background worker threads (sized per-game
// via the IOThreadCount compat flag; defaults to a single worker, which behaves
// identically to the historical single-I/O-thread design).
//
// Events for different file handles may run concurrently across workers. Events for
// the *same* handle are never run concurrently -- GetNextEvent() will skip over an
// event whose handle is already being serviced by another worker, preserving
// per-handle ordering. IO_EVENT_SYNC/IO_EVENT_FINISH act as full barriers: a worker
// will only pick one up once every event scheduled before it (including in-flight
// ones on other workers) has finished.
class AsyncIOManager {
public:
	void DoState(PointerWrap &p);

	bool HasOperation(u32 handle);
	void ScheduleOperation(const AsyncIOEvent &ev);
	void Shutdown();

	bool HasResult(u32 handle);
	bool WaitResult(u32 handle, AsyncIOResult &result);
	u64 ResultFinishTicks(u32 handle);

	void SetThreadEnabled(bool threadEnabled) {
		threadEnabled_ = threadEnabled;
	}

	bool ThreadEnabled() {
		return threadEnabled_;
	}

	void ScheduleEvent(AsyncIOEvent ev) {
		if (threadEnabled_) {
			std::lock_guard<std::recursive_mutex> guard(eventsLock_);
			events_.push_back(ev);
			// Multiple workers may be idle waiting for eligible work, so wake them all
			// to re-scan -- only one will actually claim this event.
			eventsWait_.notify_all();
		} else {
			events_.push_back(ev);
		}

		if (!threadEnabled_) {
			RunEventsUntil(0);
		}
	}

	bool HasEvents() {
		if (threadEnabled_) {
			std::lock_guard<std::recursive_mutex> guard(eventsLock_);
			// busyHandles_ tracks events currently being serviced by a worker (and thus
			// no longer sitting in events_), which still count as outstanding work.
			return !events_.empty() || !busyHandles_.empty();
		} else {
			return !events_.empty();
		}
	}

	void NotifyDrain() {
		if (threadEnabled_) {
			std::lock_guard<std::recursive_mutex> guard(eventsLock_);
			eventsDrain_.notify_one();
		}
	}

	// Finds and removes the next event this worker may legally process, honoring
	// per-handle serialization and barrier ordering. Returns IO_EVENT_INVALID if
	// nothing is currently eligible (caller should wait and retry).
	AsyncIOEvent GetNextEvent() {
		if (threadEnabled_) {
			std::lock_guard<std::recursive_mutex> guard(eventsLock_);
			for (auto it = events_.begin(); it != events_.end(); ++it) {
				if (AsyncIOEventHasHandle(it->type)) {
					if (busyHandles_.find(it->handle) != busyHandles_.end()) {
						// Another worker already has this handle in flight -- keep scanning
						// for other, independent work.
						continue;
					}
					AsyncIOEvent ev = *it;
					events_.erase(it);
					busyHandles_.insert(ev.handle);
					return ev;
				}

				// SYNC/FINISH: only resolvable once nothing scheduled before it remains
				// outstanding, whether still queued (implied by not being at the front)
				// or currently in flight on another worker.
				bool earlierEventsRemain = it != events_.begin();
				if (earlierEventsRemain || !busyHandles_.empty()) {
					break;
				}
				AsyncIOEvent ev = *it;
				events_.erase(it);
				return ev;
			}

			NotifyDrain();
			return IO_EVENT_INVALID;
		} else {
			if (events_.empty()) {
				return IO_EVENT_INVALID;
			}
			AsyncIOEvent ev = events_.front();
			events_.pop_front();
			return ev;
		}
	}

	// This is the threadfunc, really. Although it can also run on the main thread if threadEnabled_ is set.
	// Safe to call from more than one worker thread concurrently.
	void RunEventsUntil(u64 globalticks) {
		if (!threadEnabled_) {
			do {
				for (AsyncIOEvent ev = GetNextEvent(); AsyncIOEventType(ev) != IO_EVENT_INVALID; ev = GetNextEvent()) {
					ProcessEventIfApplicable(ev, globalticks);
				}
			} while (CoreTiming::GetTicks() < globalticks);
			return;
		}

		std::unique_lock<std::recursive_mutex> guard(eventsLock_);
		++activeWorkers_;
		eventsHaveRun_ = true;
		do {
			AsyncIOEvent ev = GetNextEvent();
			while (AsyncIOEventType(ev) == IO_EVENT_INVALID) {
				// Bounded by real wall-clock time rather than an unconditional wait, so
				// this worker periodically re-checks for eligible work even if it's never
				// notified again -- e.g. after RequestWorkerExit(), a FINISH marker may be
				// claimed by a different worker than intended (any worker may claim any
				// marker; see RequestWorkerExit), leaving this one with nothing left to
				// wake it. Falling all the way back to the ~1 second globalticks deadline
				// below isn't enough either, since CoreTiming ticks freeze while the game
				// is paused. A short, harmless poll interval means this worker (and thus
				// the __IoManagerThread loop that owns it) notices a reduced thread-count
				// target within a bounded, real-time interval no matter what.
				if (eventsWait_.wait_for(guard, std::chrono::milliseconds(100)) == std::cv_status::timeout) {
					NotifyDrain();
					--activeWorkers_;
					return;
				}
				ev = GetNextEvent();
			}

			while (AsyncIOEventType(ev) != IO_EVENT_INVALID) {
				guard.unlock();
				ProcessEventIfApplicable(ev, globalticks);
				guard.lock();
				ev = GetNextEvent();
			}
		} while (CoreTiming::GetTicks() < globalticks);

		// This will force the waiter to check coreState, even if we didn't actually drain.
		NotifyDrain();
		--activeWorkers_;
	}

	void SyncBeginFrame() {
		if (threadEnabled_) {
			std::lock_guard<std::recursive_mutex> guard(eventsLock_);
			eventsHaveRun_ = false;
		} else {
			eventsHaveRun_ = false;
		}
	}

	inline bool ShouldSyncThread(bool force) {
		if (!HasEvents())
			return false;
		if (coreState != CORE_RUNNING_CPU && !force)
			return false;

		// Don't run if it's not running, but wait for startup.
		if (activeWorkers_ == 0) {
			if (eventsHaveRun_ || coreState == CORE_RUNTIME_ERROR || coreState == CORE_POWERDOWN) {
				return false;
			}
		}

		return true;
	}

	// Force ignores coreState.
	void SyncThread(bool force = false) {
		if (!threadEnabled_) {
			return;
		}

		std::unique_lock<std::recursive_mutex> guard(eventsLock_);
		// While processing the last event, HasEvents() will be false even while not done.
		// So we schedule a nothing event and wait for that to finish.
		ScheduleEvent(IO_EVENT_SYNC);
		while (ShouldSyncThread(force)) {
			eventsDrain_.wait(guard);
		}
	}

	void FinishEventLoop() {
		RequestWorkerExit(activeWorkers_);
	}

	// Schedules `count` FINISH barrier markers, causing up to `count` worker
	// RunEventsUntil() calls to return promptly instead of waiting out their current
	// ~1 second deadline. Used both for shutdown (FinishEventLoop(), one per active
	// worker) and for shrinking the worker pool at runtime (see IOThreadCount): the
	// caller is expected to also be checking its own exit condition (e.g. a
	// per-worker index against a target count) each time RunEventsUntil() returns, so
	// it doesn't matter which specific worker happens to claim which FINISH event --
	// any worker returning early gets a chance to notice it should exit.
	void RequestWorkerExit(int count) {
		if (!threadEnabled_) {
			return;
		}

		std::lock_guard<std::recursive_mutex> guard(eventsLock_);
		for (int i = 0; i < count; ++i) {
			ScheduleEvent(IO_EVENT_FINISH);
		}
	}

protected:
	void ProcessEvent(AsyncIOEvent ref);

	inline void ProcessEventIfApplicable(AsyncIOEvent &ev, u64 &globalticks) {
		switch (AsyncIOEventType(ev)) {
		case IO_EVENT_FINISH:
			// Stop waiting.
			globalticks = 0;
			break;

		case IO_EVENT_SYNC:
			// Nothing special to do, this event it just to wait on, see SyncThread.
			break;

		default:
			ProcessEvent(ev);
		}
	}

private:
	bool PopResult(u32 handle, AsyncIOResult &result);
	bool ReadResult(u32 handle, AsyncIOResult &result);
	void Read(u32 handle, u8 *buf, size_t bytes, u32 invalidateAddr);
	void Write(u32 handle, const u8 *buf, size_t bytes);

	void EventResult(u32 handle, const AsyncIOResult &result);

	bool threadEnabled_ = false;
	int activeWorkers_ = 0;
	bool eventsHaveRun_ = false;
	std::deque<AsyncIOEvent> events_;
	std::set<u32> busyHandles_;  // Handles currently being serviced by a worker thread.
	std::recursive_mutex eventsLock_;  // TODO: Should really make this non-recursive - condition_variable_any is dangerous
	std::condition_variable_any eventsWait_;
	std::condition_variable_any eventsDrain_;

	std::mutex resultsLock_;
	std::condition_variable resultsWait_;
	std::set<u32> resultsPending_;
	std::map<u32, AsyncIOResult> results_;
};
