#pragma once

#include "Common/UI/View.h"
#include "Common/File/Path.h"

#include "UI/Kiosk/KioskFocusTracker.h"

// A single game tile shared by the carousel and grid screens: draws the
// PSP-icon-aspect-ratio artwork and fires OnHoldClick after a short
// touch-and-hold (kiosk is touch-first, so the threshold is shorter than
// MainScreen's GameButton).
class KioskTile : public KioskFocusHighlightable<UI::Clickable> {
public:
	KioskTile(const Path &gamePath, UI::LayoutParams *layoutParams);
	// "All ROMs" tile: no backing game, draws a 3x3 grid icon instead of artwork.
	explicit KioskTile(UI::LayoutParams *layoutParams);

	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	bool Touch(const TouchInput &input) override;
	void Update() override;

	const Path &GamePath() const { return gamePath_; }
	bool IsAllRoms() const { return isAllRoms_; }

	void SetFadeInOnLoad(bool enable) { fadeInOnLoad_ = enable; }

	UI::Event OnHoldClick;

protected:
	std::string HighlightEventPayload() const override { return gamePath_.ToString(); }

private:
	Path gamePath_;
	double holdStart_ = 0.0;
	bool isAllRoms_ = false;
	bool fadeInOnLoad_ = true;
};
