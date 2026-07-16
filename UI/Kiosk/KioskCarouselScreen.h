#pragma once

#include "UI/BaseScreens.h"
#include "UI/Kiosk/KioskGameList.h"
#include "UI/Kiosk/KioskScreenBase.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

class KioskTile;
class KioskCarouselView;

// Screen 1: gamepad-first recent-games carousel + "All ROMs" tile.
// Horizontal carousel in landscape, vertical column in portrait; both reflow
// the same KioskGameList data through the same tile/hint components.
class KioskCarouselScreen : public KioskScreenBase<UIBaseScreen> {
public:
	using Base = KioskScreenBase<UIBaseScreen>;

	KioskCarouselScreen() = default;

	void update() override;
	void resized() override { RecreateViews(); }
	const char *tag() const override { return "KioskCarousel"; }
	void dialogFinished(const Screen *dialog, DialogResult result) override;

protected:
	void CreateViews() override;
	void DrawBackground(UIContext &ui) override;
	void DrawForeground(UIContext &ui) override;

private:
	void BuildLogo();
	UI::TextView *BuildTitleView(const Bounds &screenBounds, bool portrait, float tileH, float carouselCenterY);
	void BuildHintBar();
	void BuildCarouselView(bool portrait, float tileW, float tileH, float carouselCenterY);

	void OnTileClick(KioskTile *tile);
	void OnTileHold(KioskTile *tile);
	void OnFocusedTileChanged(KioskTile *tile);
	void OnLaunch(UI::EventParams &e);
	void OnSettings(UI::EventParams &e);
	void OnGameInfo(UI::EventParams &e);
	void OnAllRoms(UI::EventParams &e);
	void OnExit(UI::EventParams &e);

	void RetryTitleTextUntilAsyncSfoLoadCompletes(KioskTile *focusedTile);
	void RefreshGameIdsIfResolved();
	bool HideTitleWhileCarouselMoving(KioskTile *focusedTile);
	void WrapTitleAroundFocusedTile(KioskTile *focusedTile);
	void DrawRomCountText(UIContext &ui, const Bounds &bounds, int align);
	void PositionTitleAndCountPortrait(UIContext &ui);

	Path FocusedGamePath() const;

	KioskGameList gameList_;
	std::vector<KioskGameEntry> entries_;
	Path pendingFocusPath_;
	float titleBottomAnchorY_ = 0.0f;
	float lastTileAnchorX_ = -1.0f;
	int stableAnchorFrameCount_ = 0;

	std::string romCountText_;
	float romCountBandCenterY_ = 0.0f;
	Bounds romCountBoundsPortrait_;

	KioskCarouselView *carouselView_ = nullptr;
	UI::TextView *titleView_ = nullptr;

	UI::Event OnLaunchEvent;
	UI::Event OnSettingsEvent;
	UI::Event OnInfoEvent;
	UI::Event OnAllRomsEvent;
	UI::Event OnExitEvent;
};
