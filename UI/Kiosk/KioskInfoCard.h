#pragma once

#include <memory>

#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

class GameInfo;

// Bordered container for the game-info popup's content (icon + body), with an optional
// divider line drawn just above a given child view (the "Save States" label, in practice)
// once the layout knows where that child landed.
class KioskInfoCard : public UI::LinearLayout {
public:
	using UI::LinearLayout::LinearLayout;

	void SetDividerAbove(UI::View *v) { dividerAnchor_ = v; }

	void Draw(UIContext &dc) override;

private:
	UI::View *dividerAnchor_ = nullptr;
};

// Draws a game's box-art icon, letterboxed to the tile aspect ratio, with no interaction
// of its own.
class KioskInfoIcon : public UI::InertView {
public:
	KioskInfoIcon(std::shared_ptr<GameInfo> info, UI::LayoutParams *layoutParams);

	void Draw(UIContext &dc) override;

private:
	std::shared_ptr<GameInfo> info_;
};
