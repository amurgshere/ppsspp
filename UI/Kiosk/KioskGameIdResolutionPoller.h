#pragma once

#include "Common/TimeUtil.h"

// Game IDs resolve asynchronously in the background - screens that show
// something derived from them (a hint bar's Settings label, a filtered/sorted
// grid) need to periodically re-check and refresh once resolution progresses,
// without polling every single frame forever after everything's resolved.
// This just owns the "should I poll now" gate; callers decide what polling
// actually means for them (re-resolve entries in place, rebuild a view, ...).
class KioskGameIdResolutionPoller {
public:
	void Reset(int unresolvedCount) {
		unresolvedCount_ = unresolvedCount;
		lastPollTime_ = time_now_d();
	}
	void SetUnresolvedCount(int count) { unresolvedCount_ = count; }
	int UnresolvedCount() const { return unresolvedCount_; }

	bool ShouldPoll() {
		if (unresolvedCount_ <= 0)
			return false;
		double now = time_now_d();
		if (now - lastPollTime_ < kPollIntervalSeconds)
			return false;
		lastPollTime_ = now;
		return true;
	}

private:
	static constexpr double kPollIntervalSeconds = 0.25;
	int unresolvedCount_ = 0;
	double lastPollTime_ = 0.0;
};
