#pragma once

#include <functional>
#include <string>

#include "Common/UI/View.h"

// Sticky-highlightable focus item contract required by KioskFocusTracker<T>
// below: keeps a focus ring visually highlighted even after UI focus moves
// elsewhere (e.g. to the hint bar), and fires OnHighlight so a tracker or
// hint bar can react. Derived can override HighlightEventPayload() to put
// something in the fired event's EventParams::s (e.g. KioskTile puts its
// game path there so listeners don't need a second lookup).
template<typename Base>
class KioskFocusHighlightable : public Base {
public:
	using Base::Base;

	void SetStickyHighlight(bool sticky) { stickyHighlighted_ = sticky; }
	bool IsHighlighted() const { return this->HasFocus() || stickyHighlighted_; }

	UI::Event OnHighlight;

	void FocusChanged(int focusFlags) override {
		Base::FocusChanged(focusFlags);
		UI::EventParams e{};
		e.v = this;
		e.a = focusFlags;
		e.s = HighlightEventPayload();
		OnHighlight.Trigger(e);
	}

protected:
	virtual std::string HighlightEventPayload() const { return std::string(); }

private:
	bool stickyHighlighted_ = false;
};

template<typename T>
class KioskFocusTracker {
public:
	void Highlight(T *item, bool focused) {
		if (!focused)
			return;
		if (current_ && current_ != item)
			current_->SetStickyHighlight(false);
		current_ = item;
		current_->SetStickyHighlight(true);
	}

	void WireToHighlightEvent(T *item, UI::Event &highlightEvent, std::function<void(T *)> onHighlighted = nullptr) {
		highlightEvent.Add([this, item, onHighlighted](UI::EventParams &e) {
			if (!(e.a & UI::FF_GOTFOCUS))
				return;
			Highlight(item, true);
			if (onHighlighted)
				onHighlighted(item);
		});
	}

	T *Current() const { return current_; }
	void Reset() { current_ = nullptr; }

private:
	T *current_ = nullptr;
};
