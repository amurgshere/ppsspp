#pragma once

#include <string>
#include <vector>

#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/Math/geom2d.h"

struct KeyInput;

// Choice button that can optionally composite two atlas images into one
// glyph (icon + overlayIcon), same technique as MultiTouchButton. Icon and
// label are always laid out and measured in unrotated "logical" (icon-left/
// text-right) space, then drawn rotated about the button's own center - 0
// degrees in ORIENT_HORIZONTAL, 270 degrees in ORIENT_VERTICAL so the content
// reads bottom-to-top - so sizing and spacing can't drift apart between the
// two orientations.
class KioskHintButton : public UI::Choice {
public:
	KioskHintButton(ImageID icon, ImageID overlayIcon, std::string_view label, bool flipIconH,
		Orientation orientation, float iconScale, UI::LayoutParams *layoutParams);

	void GetContentDimensionsBySpec(const UIContext &dc, UI::MeasureSpec horiz, UI::MeasureSpec vert, float &w, float &h) const override;
	void Draw(UIContext &dc) override;

private:
	bool vertical() const { return orientation_ == ORIENT_VERTICAL; }
	float IconSlotSize() const;
	void DrawIcon(UIContext &dc, const Bounds &logicalBounds, float angle, const UI::Style &style);
	void DrawLabel(UIContext &dc, const Bounds &logicalBounds, float angle, const UI::Style &style);

	ImageID overlayIcon_ = ImageID::invalid();
	bool flipIconH_ = false;
	Orientation orientation_ = ORIENT_HORIZONTAL;
	// Per-glyph multiplier on top of kHintIconScale - some atlas glyphs (the
	// L/R shoulder composite, Start) read as visually smaller than the face
	// buttons even at the same bounding-box size, so they need a bit more.
	float iconScale_ = 1.0f;
};

// Horizontal (bottom-anchored) in landscape, vertical (right-anchored) in
// portrait - orientation is fixed at construction and drives the internal
// LinearLayout orientation, shade-gradient direction, and button layout.
class KioskHintBar : public UI::LinearLayout {
public:
	struct Entry {
		ImageID glyph;
		std::string label;
		UI::Event *event;
		ImageID overlayGlyph = ImageID::invalid();
		std::vector<int> pspButtons;
		bool flipGlyphH = false;  // mirrors just the base icon, for the R shoulder glyph
		float iconScale = 1.0f;  // per-glyph size multiplier, see KioskHintButton::iconScale_
	};

	explicit KioskHintBar(Orientation orientation, UI::LayoutParams *layoutParams = nullptr);

	void SetEntries(const std::vector<Entry> &entries);
	// Updates one entry's label in place, matched by event pointer, without rebuilding the whole bar.
	void UpdateEntryLabel(const UI::Event *event, std::string_view label);
	bool RouteKey(const KeyInput &key) const;
	// Buttons stay clickable/press-routable (RouteKey, pspButtons) regardless - this only
	// gates whether directional (D-pad/analog) focus navigation can land on them, for
	// screens that want their own content exhausted first (see Clickable::SetCanBeFocused).
	void SetButtonsCanBeFocused(bool canBeFocused);

	void Draw(UIContext &dc) override;

private:
	bool vertical() const { return orientation_ == ORIENT_VERTICAL; }

	Orientation orientation_;
	std::vector<Entry> entries_;
	std::vector<KioskHintButton *> buttons_;
};

namespace Kiosk {
KioskHintBar *AddHintBar(UI::ViewGroup *root, Orientation orientation);
}  // namespace Kiosk
