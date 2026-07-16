#pragma once

#include <functional>
#include <vector>

#include "Common/UI/ViewGroup.h"
#include "Common/UI/ScrollView.h"

#include "UI/Kiosk/KioskGameList.h"
#include "UI/Kiosk/KioskTileGroup.h"
#include "UI/Kiosk/KioskTileContainerView.h"

class KioskTile;

class KioskCarouselView : public KioskTileContainerView<UI::LinearLayout> {
public:
	KioskCarouselView(bool isPortraitOrientation, float tileW, float tileH, UI::LayoutParams *layoutParams);

	void PopulateFromEntries(const std::vector<KioskGameEntry> &entries,
	                          KioskTileGroup::TileCallback onLaunch, KioskTileGroup::TileCallback onHoldInfo,
	                          KioskTileGroup::TileCallback onFocusedTileChanged = nullptr,
	                          const Path &preferredFocusPath = Path());
	void DrawGroundContactShadows(UIContext &dc);

private:
	void BuildScrollingRow();
	UI::LayoutParams *MakeTileLayoutParams(size_t index, bool isAllRomsTile);
	UI::Margins LeadingTileMargin() const;
	UI::Margins TrailingTileMargin() const;

	const bool isPortraitOrientation_;
	const float tileW_, tileH_;
	UI::ScrollView *scroll_ = nullptr;
	UI::LinearLayout *row_ = nullptr;
};
