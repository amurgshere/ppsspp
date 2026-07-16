#include "UI/Kiosk/KioskTileGridView.h"
#include "UI/Kiosk/KioskCommon.h"

using namespace UI;

KioskTileGridView::KioskTileGridView(int columnWidth, int rowHeight, int spacing, float portraitRightMargin, float screenWidth, UI::LayoutParams *layoutParams)
	: KioskTileContainerView<ScrollView>(ORIENT_VERTICAL, layoutParams) {
	GridLayoutSettings settings = Kiosk::MakeReflowGridSettings(columnWidth, rowHeight, spacing);

	// A reflowing GridLayout inside a vertical ScrollView needs a fixed, margin-free
	// width computed here rather than via LinearLayoutParams margins on the grid
	// itself: ScrollView::Layout() re-subtracts a child's margins from a width its
	// Measure() may already have reduced by those same margins whenever a
	// FILL_PARENT child hits the AT_MOST cap (which a reflowing grid always does),
	// so margins here would double-subtract. This width must also never exceed the
	// AT_MOST cap the ScrollView chain actually hands the grid, or GridLayout::Measure()'s
	// AT_MOST branch (which compares against the *previous frame's* measured width)
	// oscillates between two states every frame instead of clamping once. See also
	// the matching fixed-width computation in KioskGameInfoPopup::BuildSaveSlotGrid().
	float gridW = screenWidth - Kiosk::kFocusRingExtent * 2.0f - Kiosk::kScrollbarGutter - portraitRightMargin;
	grid_ = new GridLayout(settings, new LinearLayoutParams(gridW, WRAP_CONTENT));

	LinearLayout *wrapper = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	wrapper->padding = Margins((int8_t)Kiosk::kFocusRingExtent);
	wrapper->Add(grid_);
	Add(wrapper);
}

void KioskTileGridView::PopulateFromEntries(const std::vector<KioskGameEntry> &entries,
		KioskTileGroup::TileCallback onLaunch, KioskTileGroup::TileCallback onHoldInfo,
		KioskTileGroup::TileCallback onFocusedTileChanged, const Path &preferredFocusPath) {
	tiles_.Clear();
	grid_->Clear();
	tiles_.PopulateFromEntries(grid_, entries,
		[](size_t, bool) { return new GridLayoutParams(Gravity::G_CENTER); },
		/* appendAllRomsTile = */ false, std::move(onLaunch), std::move(onHoldInfo), std::move(onFocusedTileChanged),
		/* fadeInOnLoad = */ true, preferredFocusPath);
}
