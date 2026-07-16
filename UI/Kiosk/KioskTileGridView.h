#pragma once

#include <string>
#include <vector>

#include "Common/UI/ViewGroup.h"
#include "Common/UI/ScrollView.h"

#include "UI/Kiosk/KioskGameList.h"
#include "UI/Kiosk/KioskTileGroup.h"
#include "UI/Kiosk/KioskTileContainerView.h"

class KioskTileGridView : public KioskTileContainerView<UI::ScrollView> {
public:
	// portraitRightMargin should equal whatever right-side margin layoutParams
	// itself reserves for a vertical hint bar (0 in landscape) - the grid's
	// own fixed-width child needs to match that margin exactly, or GridLayout's
	// AT_MOST measurement oscillates every frame (see MeasureBySpec). screenWidth
	// is the caller's current UIContext bounds width, not read from g_display here -
	// see KioskCommon.h's IsPortrait()/ClientWidth() comment for why.
	KioskTileGridView(int columnWidth, int rowHeight, int spacing, float portraitRightMargin, float screenWidth, UI::LayoutParams *layoutParams);

	void PopulateFromEntries(const std::vector<KioskGameEntry> &entries,
	                          KioskTileGroup::TileCallback onLaunch, KioskTileGroup::TileCallback onHoldInfo,
	                          KioskTileGroup::TileCallback onFocusedTileChanged = nullptr,
	                          const Path &preferredFocusPath = Path());

private:
	UI::GridLayout *grid_ = nullptr;
};
