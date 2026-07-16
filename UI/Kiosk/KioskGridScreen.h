#pragma once

#include "UI/BaseScreens.h"
#include "UI/Kiosk/KioskGameList.h"
#include "UI/Kiosk/KioskScreenBase.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

class KioskTileGridView;
class KioskTile;

// Uses UIBaseDialogScreen rather than UIBaseScreen so Escape/Circle
// automatically pops back to the carousel via UIDialogScreen::key().
class KioskGridScreen : public KioskScreenBase<UIBaseDialogScreen> {
public:
	using Base = KioskScreenBase<UIBaseDialogScreen>;

	KioskGridScreen();

	void update() override;
	void resized() override { RecreateViews(); }
	const char *tag() const override { return "KioskGrid"; }
	void dialogFinished(const Screen *dialog, DialogResult result) override;

protected:
	void CreateViews() override;
	void DrawForeground(UIContext &ui) override;

private:
	void BuildHintBar();
	void RebuildGrid();
	void CycleSortField();
	void ToggleAscending();
	void DrawSelectedGameNameBanner(UIContext &ui, KioskTile *tile);
	Bounds ComputeSelectionBannerBounds(UIContext &ui, const std::string &title, float pointerCenterX, float bannerY) const;
	void DrawSelectionBannerPointer(UIContext &ui, float pointerCenterX, const Bounds &bannerBounds, bool showAbove);

	void OnTileClick(KioskTile *tile);
	void OnTileHold(KioskTile *tile);
	void OnFocusedTileChanged(KioskTile *tile);
	void UpdateGridBoundaryFocusability(KioskTile *tile);
	void OnLaunchClick(UI::EventParams &e);
	void OnSortPillClick(UI::EventParams &e);
	void OnFilterPillClick(UI::EventParams &e);
	void OnSettingsClick(UI::EventParams &e);
	void OnGameInfoClick(UI::EventParams &e);
	void OnBackClick(UI::EventParams &e);

	KioskGameList gameList_;
	std::vector<KioskGameEntry> entries_;

	// Initialized from g_Config in the constructor and written back to it
	// whenever the user changes them, so the grid remembers sort field/order
	// across sessions (same idea as MainScreen's bGridView1-4).
	KioskSortField sort_;
	bool ascending_;
	KioskFilter filter_ = KioskFilter::ALL;

	KioskTileGridView *tileGridView_ = nullptr;
	UI::Choice *sortPill_ = nullptr;
	UI::Choice *filterPill_ = nullptr;

	UI::Event OnLaunchEvent;
	UI::Event OnCycleSortEvent;
	UI::Event OnToggleAscEvent;
	UI::Event OnSettingsEvent;
	UI::Event OnGameInfoEvent;
	UI::Event OnBackEvent;
};
