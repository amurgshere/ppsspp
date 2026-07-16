#include <algorithm>
#include <cmath>

#include "UI/Kiosk/KioskCarouselScreen.h"
#include "UI/Kiosk/KioskGridScreen.h"
#include "UI/Kiosk/KioskGameInfoPopup.h"
#include "UI/Kiosk/KioskHintBar.h"
#include "UI/Kiosk/KioskCommon.h"
#include "UI/Kiosk/KioskTile.h"
#include "UI/Kiosk/KioskCarouselView.h"

#include "UI/GameInfoCache.h"
#include "UI/GameSettingsScreen.h"
#include "UI/EmuScreen.h"

#include "Common/System/Request.h"

#include "Common/UI/Root.h"
#include "Common/UI/Context.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/StringUtils.h"

#include "Core/HLE/sceCtrl.h"

using namespace UI;

static constexpr float kTitleInset = 40.0f;

void KioskCarouselScreen::BuildLogo() {
	ImageView *logo = new ImageView(ImageID("I_ICON"), "", IS_DEFAULT, new AnchorLayoutParams(200, 200, 24, 24, NONE, NONE));
	logo->SetColor(alphaMul(0xFFFFFFFF, 0.16f));
	root_->Add(logo);
}

UI::TextView *KioskCarouselScreen::BuildTitleView(const Bounds &screenBounds, bool portrait, float tileH, float carouselCenterY) {
	const float kTitleGap = Kiosk::kFocusRingExtent + 20.0f;

	TextView *title;
	if (portrait) {
		title = new TextView("", ALIGN_HCENTER | FLAG_WRAP_TEXT, false, new AnchorLayoutParams(FILL_PARENT, WRAP_CONTENT, kTitleInset, 40, kTitleInset, NONE));
	} else {
		float tileTopY = carouselCenterY - tileH / 2.0f;
		titleBottomAnchorY_ = tileTopY - kTitleGap;
		title = new TextView("", ALIGN_HCENTER | FLAG_WRAP_TEXT, false, new AnchorLayoutParams(FILL_PARENT, WRAP_CONTENT, kTitleInset, NONE, kTitleInset, screenBounds.h - titleBottomAnchorY_));
		title->SetScale(Kiosk::kLandscapeTitleScale);
	}
	title->SetTextColor(0xFFFFFFFF);
	root_->Add(title);
	return title;
}

void KioskCarouselScreen::BuildCarouselView(bool portrait, float tileW, float tileH, float carouselCenterY) {
	UI::LayoutParams *carouselLayoutParams;
	if (portrait) {
		carouselLayoutParams = new AnchorLayoutParams(FILL_PARENT, FILL_PARENT, 0, 120, Kiosk::kHintBarThickness, 0);
	} else {
		float scrollHeight = tileH + Kiosk::kFocusRingExtent * 2;
		carouselLayoutParams = new AnchorLayoutParams(FILL_PARENT, scrollHeight, 0, carouselCenterY, 0, NONE, Centering::Vertical);
	}

	carouselView_ = new KioskCarouselView(portrait, tileW, tileH, carouselLayoutParams);
	root_->Add(carouselView_);

	carouselView_->PopulateFromEntries(entries_,
		[this](KioskTile *tile) { OnTileClick(tile); },
		[this](KioskTile *tile) { OnTileHold(tile); },
		[this](KioskTile *tile) { OnFocusedTileChanged(tile); },
		pendingFocusPath_);
	pendingFocusPath_ = Path();
}

void KioskCarouselScreen::CreateViews() {
	entries_ = gameList_.GetRecentRow();
	idPoller_.Reset((int)std::count_if(entries_.begin(), entries_.end(), [](const KioskGameEntry &e) { return e.gameId.empty(); }));
	int totalGames = (int)gameList_.GetAllGames().size();
	auto ki = GetI18NCategory(I18NCat::MAINMENU);
	romCountText_ = StringFromFormat(std::string(ki->T("Displaying %d of %d ROMs")).c_str(), (int)entries_.size(), totalGames);

	Bounds screenBounds = screenManager()->getUIContext()->GetBounds();
	bool portrait = Kiosk::IsPortrait(screenBounds);
	portrait_ = portrait;

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	BuildLogo();

	float tileW = portrait ? screenBounds.w * 0.7f : screenBounds.w / Kiosk::kTilesVisible - Kiosk::kTileGap;
	float tileH = tileW / Kiosk::kTileAspect;
	float carouselCenterY = Kiosk::ClientCenterY(screenBounds, portrait);

	if (!portrait) {
		float clientHeight = Kiosk::ClientHeight(screenBounds, portrait);
		Kiosk::GroundShadowBand band = Kiosk::ComputeGroundShadowBand(clientHeight);
		romCountBandCenterY_ = (band.bottom + clientHeight) * 0.5f;
	}

	titleView_ = BuildTitleView(screenBounds, portrait, tileH, carouselCenterY);

	hintBar_ = Kiosk::AddHintBar(root_, portrait ? ORIENT_VERTICAL : ORIENT_HORIZONTAL);
	BuildHintBar();

	BuildCarouselView(portrait, tileW, tileH, carouselCenterY);
}

void KioskCarouselScreen::BuildHintBar() {
	auto ki = GetI18NCategory(I18NCat::MAINMENU);

	std::vector<KioskHintBar::Entry> entries;
	entries.push_back(Kiosk::LaunchEntry(OnLaunchEvent, std::string(ki->T("Launch"))));
	entries.push_back({ ImageID("I_SQUARE"), std::string(ki->T("All ROMs")), &OnAllRomsEvent, ImageID::invalid(), { CTRL_SQUARE } });
	entries.push_back({ ImageID("I_TRIANGLE"), std::string(ki->T("Settings")), &OnSettingsEvent, ImageID::invalid(), { CTRL_TRIANGLE } });
	entries.push_back({ ImageID("I_START"), std::string(ki->T("Game Info")), &OnInfoEvent, ImageID::invalid(), { CTRL_START, CTRL_SELECT }, /* flipGlyphH = */ false, Kiosk::kSmallGlyphIconScale });
	entries.push_back(Kiosk::BackEntry(OnExitEvent, std::string(ki->T("Exit")), { Kiosk::BackButton() }));
	hintBar_->SetEntries(entries);

	OnLaunchEvent.Add([this](UI::EventParams &e) { OnLaunch(e); });
	OnInfoEvent.Add([this](UI::EventParams &e) { OnGameInfo(e); });
	OnSettingsEvent.Add([this](UI::EventParams &e) { OnSettings(e); });
	OnAllRomsEvent.Add([this](UI::EventParams &e) { OnAllRoms(e); });
	OnExitEvent.Add([this](UI::EventParams &e) { OnExit(e); });
}

Path KioskCarouselScreen::FocusedGamePath() const {
	return carouselView_ ? carouselView_->FocusedGamePath() : Path();
}

// pendingFocusPath_ must be captured here, before RecreateViews() tears down
// carouselView_ (see BuildCarouselView(), which consumes and clears it).
void KioskCarouselScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	Base::HandleTileScreenDialogFinished(dialog, result,
		[this]() {
			gameList_.Invalidate();
			pendingFocusPath_ = FocusedGamePath();
			RecreateViews();
		},
		[this]() {
			if (carouselView_)
				carouselView_->RestoreFocus();
		});
}

void KioskCarouselScreen::OnTileClick(KioskTile *tile) {
	if (!tile)
		return;
	if (tile->IsAllRoms())
		screenManager()->push(new KioskGridScreen());
	else
		screenManager()->switchScreen(new EmuScreen(tile->GamePath()));
}

void KioskCarouselScreen::OnLaunch(UI::EventParams &e) {
	OnTileClick(carouselView_ ? carouselView_->FocusedTile() : nullptr);
}

void KioskCarouselScreen::OnTileHold(KioskTile *tile) {
	if (tile && !tile->IsAllRoms())
		screenManager()->push(new KioskGameInfoPopup(tile->GamePath()));
}

void KioskCarouselScreen::OnFocusedTileChanged(KioskTile *tile) {
	RefreshSettingsHintLabel(OnSettingsEvent, carouselView_->FocusedGameId(entries_));

	if (!titleView_)
		return;
	if (tile->IsAllRoms()) {
		auto ki = GetI18NCategory(I18NCat::MAINMENU);
		titleView_->SetText(ki->T("All ROMs"));
		return;
	}
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(nullptr, tile->GamePath(), GameInfoFlags::PARAM_SFO);
	titleView_->SetText(ginfo->GetTitle());
}

void KioskCarouselScreen::OnSettings(UI::EventParams &e) {
	std::string gameId = carouselView_ ? carouselView_->FocusedGameId(entries_) : std::string();
	screenManager()->push(Kiosk::MakeSettingsScreen(FocusedGamePath(), gameId));
}

void KioskCarouselScreen::OnGameInfo(UI::EventParams &e) {
	Path path = FocusedGamePath();
	if (!path.empty())
		screenManager()->push(new KioskGameInfoPopup(path));
}

void KioskCarouselScreen::OnAllRoms(UI::EventParams &e) {
	screenManager()->push(new KioskGridScreen());
}

void KioskCarouselScreen::OnExit(UI::EventParams &e) {
	System_ExitApp();
}

void KioskCarouselScreen::RetryTitleTextUntilAsyncSfoLoadCompletes(KioskTile *focusedTile) {
	if (focusedTile->IsAllRoms())
		return;
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(nullptr, focusedTile->GamePath(), GameInfoFlags::PARAM_SFO);
	titleView_->SetText(ginfo->GetTitle());
}

bool KioskCarouselScreen::HideTitleWhileCarouselMoving(KioskTile *focusedTile) {
	const float kStillThreshold = 1.0f;
	const int kStableFramesRequired = 3;

	float anchorX = focusedTile->GetBounds().centerX();
	if (std::abs(anchorX - lastTileAnchorX_) > kStillThreshold) {
		stableAnchorFrameCount_ = 0;
	} else {
		stableAnchorFrameCount_++;
	}
	lastTileAnchorX_ = anchorX;

	bool settled = stableAnchorFrameCount_ >= kStableFramesRequired;
	titleView_->SetVisibility(settled ? V_VISIBLE : V_INVISIBLE);
	return !settled;
}

void KioskCarouselScreen::WrapTitleAroundFocusedTile(KioskTile *focusedTile) {
	const float kEdgeInset = Kiosk::kTileGap * 0.5f;

	UIContext *dc = screenManager()->getUIContext();
	Bounds screenBounds = dc->GetBounds();

	float anchorX = focusedTile->GetBounds().centerX();
	float halfW = std::min(anchorX, screenBounds.w - anchorX) - kEdgeInset;
	float w = halfW * 2.0f;

	float measuredW, measuredH;
	titleView_->GetContentDimensionsBySpec(*dc, MeasureSpec(EXACTLY, w), MeasureSpec(AT_MOST, screenBounds.h), measuredW, measuredH);
	titleView_->SetBounds(Bounds(anchorX - halfW, titleBottomAnchorY_ - measuredH, w, measuredH));
}

void KioskCarouselScreen::update() {
	UIScreen::update();
	RefreshGameIdsIfResolved();

	KioskTile *focusedTile = carouselView_ ? carouselView_->FocusedTile() : nullptr;
	if (!titleView_ || !focusedTile)
		return;

	RetryTitleTextUntilAsyncSfoLoadCompletes(focusedTile);
}

void KioskCarouselScreen::RefreshGameIdsIfResolved() {
	if (!idPoller_.ShouldPoll())
		return;

	for (auto &entry : entries_) {
		if (entry.gameId.empty())
			entry.gameId = KioskGameList::ResolveGameId(entry.path);
	}
	idPoller_.SetUnresolvedCount((int)std::count_if(entries_.begin(), entries_.end(), [](const KioskGameEntry &e) { return e.gameId.empty(); }));

	if (hintBar_ && carouselView_)
		hintBar_->UpdateEntryLabel(&OnSettingsEvent, Kiosk::SettingsHintLabel(carouselView_->FocusedGameId(entries_)));
}

void KioskCarouselScreen::DrawRomCountText(UIContext &ui, const Bounds &bounds, int align) {
	if (romCountText_.empty())
		return;
	const float kScale = 0.75f;
	ui.SetFontStyle(ui.GetTheme().uiFont);
	ui.SetFontScale(kScale, kScale);
	ui.DrawTextRect(romCountText_, bounds, 0x91FFFFFF, align);
	ui.SetFontScale(1.0f, 1.0f);
	ui.SetFontStyle(ui.GetTheme().uiFont);
}

void KioskCarouselScreen::PositionTitleAndCountPortrait(UIContext &ui) {
	if (!titleView_)
		return;
	const float kGap = 8.0f;
	const float kTopSectionHeight = 120.0f;
	const float kScale = 0.75f;
	float width = Kiosk::ClientWidth(ui.GetBounds(), /* portrait = */ true) - kTitleInset * 2.0f;

	float titleW, titleH;
	titleView_->GetContentDimensionsBySpec(ui, MeasureSpec(EXACTLY, width), MeasureSpec(AT_MOST, kTopSectionHeight), titleW, titleH);

	float countW, countH;
	ui.MeasureTextRect(ui.GetTheme().uiFont, kScale, kScale, romCountText_, width, &countW, &countH, ALIGN_HCENTER | FLAG_WRAP_TEXT);

	float blockH = titleH + kGap + countH;
	float top = (kTopSectionHeight - blockH) * 0.5f;

	titleView_->SetBounds(Bounds(kTitleInset, top, width, titleH));
	romCountBoundsPortrait_ = Bounds(kTitleInset, top + titleH + kGap, width, countH);
}

void KioskCarouselScreen::DrawBackground(UIContext &ui) {
	Base::DrawBackground(ui);

	if (carouselView_)
		carouselView_->DrawGroundContactShadows(ui);

	if (portrait_) {
		PositionTitleAndCountPortrait(ui);
		return;
	}

	KioskTile *focusedTile = carouselView_ ? carouselView_->FocusedTile() : nullptr;
	if (!titleView_ || !focusedTile)
		return;

	if (HideTitleWhileCarouselMoving(focusedTile))
		return;

	WrapTitleAroundFocusedTile(focusedTile);
}

void KioskCarouselScreen::DrawForeground(UIContext &ui) {
	if (portrait_) {
		DrawRomCountText(ui, romCountBoundsPortrait_, ALIGN_HCENTER | FLAG_WRAP_TEXT);
	} else {
		Bounds bounds(0, romCountBandCenterY_ - 20.0f, ui.GetBounds().w, 40.0f);
		DrawRomCountText(ui, bounds, ALIGN_HCENTER | ALIGN_VCENTER);
	}
}
