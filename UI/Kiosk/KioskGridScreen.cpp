#include <algorithm>

#include "UI/Kiosk/KioskGridScreen.h"
#include "UI/Kiosk/KioskGameInfoPopup.h"
#include "UI/Kiosk/KioskHintBar.h"
#include "UI/Kiosk/KioskCommon.h"
#include "UI/Kiosk/KioskTile.h"
#include "UI/Kiosk/KioskTileGridView.h"

#include "UI/GameSettingsScreen.h"
#include "UI/EmuScreen.h"
#include "UI/GameInfoCache.h"

#include "Common/UI/Root.h"
#include "Common/UI/Context.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Math/geom2d.h"
#include "Common/System/Display.h"
#include "Common/Data/Text/I18n.h"

#include "Core/Config.h"
#include "Core/HLE/sceCtrl.h"

using namespace UI;

static constexpr float kGridTopOffset = 64.0f;
static constexpr float kSelectionBannerHeight = 48.0f;
static constexpr float kSelectionBannerPaddingH = 20.0f;
static constexpr float kSelectionBannerPointerHeight = 12.0f;
static constexpr float kSelectionBannerPointerHalfWidth = 10.0f;
static constexpr uint32_t kSelectionBannerColor = 0xD0202020;

// g_Config.iKioskGridSortField is a plain int (Core can't depend on this UI-layer
// enum), so validate it into range rather than trusting an ini file to have kept
// it in bounds (e.g. hand-edited, or written by a future build with more fields).
static KioskSortField LoadSortFieldFromConfig() {
	int v = g_Config.iKioskGridSortField;
	if (v < 0 || v > (int)KioskSortField::TIME_PLAYED)
		return KioskSortField::LAST_USED;
	return (KioskSortField)v;
}

static std::string SortFieldLabel(KioskSortField f) {
	auto ki = GetI18NCategory(I18NCat::MAINMENU);
	switch (f) {
	case KioskSortField::LAST_USED: return std::string(ki->T("Last Used"));
	case KioskSortField::ALPHABETICAL: return std::string(ki->T("Alphabetical"));
	case KioskSortField::TIME_PLAYED: return std::string(ki->T("Time Played"));
	}
	return "";
}

static std::string FilterLabel(KioskFilter f) {
	auto ki = GetI18NCategory(I18NCat::MAINMENU);
	switch (f) {
	case KioskFilter::ALL: return std::string(ki->T("All ROMs"));
	case KioskFilter::PLAYED_AT_LEAST_ONCE: return std::string(ki->T("Played at least once"));
	}
	return "";
}

// Atlas glyph rather than a Unicode triangle character (U+25B2/25BC) - the Switch
// build's bundled TTF doesn't cover the Geometric Shapes block and renders those as tofu.
static ImageID SortDirectionGlyph(bool ascending) {
	return ascending ? ImageID("I_ARROW_UP") : ImageID("I_ARROW_DOWN");
}

KioskGridScreen::KioskGridScreen()
	: sort_(LoadSortFieldFromConfig()), ascending_(g_Config.bKioskGridSortAscending) {}

void KioskGridScreen::DrawForeground(UIContext &ui) {
	KioskTile *tile = tileGridView_->FocusedTile();
	// HasFocus(), not just the persisted/sticky selection - this banner is only
	// meaningful while the grid is the live input target, e.g. not while a popup
	// pushed on top (Settings, Game Info) has taken over focus.
	if (tile && tile->HasFocus())
		DrawSelectedGameNameBanner(ui, tile);
}

// Sized to fit the selected game's title (plus a little padding), positioned directly
// above or below its tile (whichever direction has room within the grid's own area),
// centered over the tile but clamped within the client area (excluding the hint bar,
// which sits to the right in portrait) - mirrors the Switch's own home-screen selection
// label. The pointer always stays on the tile's own horizontal center, even when the
// banner itself has to shift off that center to stay on-screen.
void KioskGridScreen::DrawSelectedGameNameBanner(UIContext &ui, KioskTile *tile) {
	Bounds tileBounds = tile->GetBounds();
	float totalHeight = kSelectionBannerHeight + kSelectionBannerPointerHeight;
	bool showAbove = tileBounds.y - totalHeight >= kGridTopOffset;
	float bannerY = showAbove ? tileBounds.y - totalHeight : tileBounds.y2() + kSelectionBannerPointerHeight;

	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, tile->GamePath(), GameInfoFlags::PARAM_SFO);
	std::string title = info->GetTitle();
	ui.SetFontStyle(ui.GetTheme().uiFont);

	float pointerCenterX = tileBounds.centerX();
	Bounds bannerBounds = ComputeSelectionBannerBounds(ui, title, pointerCenterX, bannerY);

	{
		Kiosk::UntexturedFillScope fillScope(ui);
		ui.FillRect(UI::Drawable(kSelectionBannerColor), bannerBounds);
	}

	DrawSelectionBannerPointer(ui, pointerCenterX, bannerBounds, showAbove);

	const float titleScale = portrait_ ? 1.0f : Kiosk::kLandscapeTitleScale;
	ui.SetFontScale(titleScale, titleScale);
	ui.DrawTextRect(title, bannerBounds, 0xFFFFFFFF, ALIGN_CENTER);
	ui.SetFontScale(1.0f, 1.0f);
}

Bounds KioskGridScreen::ComputeSelectionBannerBounds(UIContext &ui, const std::string &title, float pointerCenterX, float bannerY) const {
	float clientRight = Kiosk::ClientWidth(ui.GetBounds(), portrait_);
	const float titleScale = portrait_ ? 1.0f : Kiosk::kLandscapeTitleScale;

	float textW, textH;
	ui.MeasureText(ui.GetTheme().uiFont, titleScale, titleScale, title, &textW, &textH);
	float bannerW = std::min(textW + kSelectionBannerPaddingH * 2.0f, clientRight);

	float bannerX = std::clamp(pointerCenterX - bannerW * 0.5f, 0.0f, clientRight - bannerW);
	return Bounds(bannerX, bannerY, bannerW, kSelectionBannerHeight);
}

void KioskGridScreen::DrawSelectionBannerPointer(UIContext &ui, float pointerCenterX, const Bounds &bannerBounds, bool showAbove) {
	float pointerBaseY = showAbove ? bannerBounds.y2() : bannerBounds.y;
	float pointerTipY = showAbove ? bannerBounds.y2() + kSelectionBannerPointerHeight : bannerBounds.y - kSelectionBannerPointerHeight;

	Kiosk::NoTexPipelineScope noTex(ui);
	ui.Draw()->V(pointerCenterX - kSelectionBannerPointerHalfWidth, pointerBaseY, kSelectionBannerColor, 0, 0);
	ui.Draw()->V(pointerCenterX + kSelectionBannerPointerHalfWidth, pointerBaseY, kSelectionBannerColor, 0, 0);
	ui.Draw()->V(pointerCenterX, pointerTipY, kSelectionBannerColor, 0, 0);
}

void KioskGridScreen::CreateViews() {
	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	Bounds screenBounds = screenManager()->getUIContext()->GetBounds();
	bool portrait = Kiosk::IsPortrait(screenBounds);
	portrait_ = portrait;

	LinearLayout *topBar = new LinearLayout(ORIENT_HORIZONTAL, new AnchorLayoutParams(FILL_PARENT, WRAP_CONTENT, 0, 0, 0, NONE));
	sortPill_ = topBar->Add(new Choice(SortFieldLabel(sort_)));
	sortPill_->SetIconLeft(SortDirectionGlyph(ascending_));
	sortPill_->OnClick.Add([this](UI::EventParams &e) { OnSortPillClick(e); });
	filterPill_ = topBar->Add(new Choice(FilterLabel(filter_)));
	filterPill_->OnClick.Add([this](UI::EventParams &e) { OnFilterPillClick(e); });
	root_->Add(topBar);

	hintBar_ = Kiosk::AddHintBar(root_, portrait ? ORIENT_VERTICAL : ORIENT_HORIZONTAL);
	BuildHintBar();

	const int kGridTileW = (int)(Kiosk::kTileNativeW * 1.5f);
	const int kGridTileH = (int)(Kiosk::kTileNativeH * 1.5f);
	UI::AnchorLayoutParams *gridLayoutParams = portrait
		? new AnchorLayoutParams(FILL_PARENT, FILL_PARENT, 0, kGridTopOffset, Kiosk::kHintBarThickness, 0)
		: new AnchorLayoutParams(FILL_PARENT, FILL_PARENT, 0, kGridTopOffset, 0, Kiosk::kHintBarThickness);
	float portraitRightMargin = portrait ? Kiosk::kHintBarThickness : 0.0f;
	tileGridView_ = new KioskTileGridView(kGridTileW, kGridTileH, (int)Kiosk::kTileGap, portraitRightMargin, screenBounds.w, gridLayoutParams);
	root_->Add(tileGridView_);

	// tileGridView_ above is always a fresh, empty view, so RebuildGrid()'s
	// "did the sequence actually change" skip (there to avoid flashing the grid
	// on every no-op idPoller update, see RebuildGrid()) must not apply here -
	// force it to repopulate by forgetting the previous screen's entries_.
	entries_.clear();
	RebuildGrid();
}

void KioskGridScreen::update() {
	UIScreen::update();
	if (!idPoller_.ShouldPoll())
		return;
	if (gameList_.CountUnresolvedGameIds() != idPoller_.UnresolvedCount())
		RebuildGrid();
}

// Whether a and b would produce the exact same grid of tiles in the same order - lets
// RebuildGrid() tell "nothing a viewer would notice actually changed" apart from "the
// grid genuinely needs rebuilding", since only the latter is worth the visible tile
// teardown/recreate flash.
static bool SameGameSequence(const std::vector<KioskGameEntry> &a, const std::vector<KioskGameEntry> &b) {
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); i++) {
		if (a[i].path != b[i].path)
			return false;
	}
	return true;
}

void KioskGridScreen::RebuildGrid() {
	// Captured before PopulateFromEntries() clears and rebuilds the tiles below, so
	// whichever game was focused (e.g. before returning from Game Info or Settings)
	// stays focused instead of resetting to the first tile.
	Path focusPath = tileGridView_->FocusedGamePath();

	std::vector<KioskGameEntry> newEntries = gameList_.GetSortedFiltered(sort_, ascending_, filter_);
	idPoller_.Reset(gameList_.CountUnresolvedGameIds());

	if (sortPill_) {
		sortPill_->SetText(SortFieldLabel(sort_));
		sortPill_->SetIconLeft(SortDirectionGlyph(ascending_));
	}
	if (filterPill_)
		filterPill_->SetText(FilterLabel(filter_));

	// update() calls this every time a background id resolution changes the unresolved
	// count, even when the resolved id didn't actually move its game in the sort/filter
	// order (the common case, since most games' positions are already settled well before
	// their id specifically finishes resolving) - so skip the tile rebuild entirely unless
	// the displayed sequence actually changed, or every one of those polls would flash the
	// whole grid for no visible reason.
	bool sameSequence = SameGameSequence(newEntries, entries_);
	entries_ = std::move(newEntries);
	if (sameSequence)
		return;

	tileGridView_->PopulateFromEntries(entries_,
		[this](KioskTile *tile) { OnTileClick(tile); },
		[this](KioskTile *tile) { OnTileHold(tile); },
		[this](KioskTile *tile) { OnFocusedTileChanged(tile); },
		focusPath);
}

void KioskGridScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	Base::HandleTileScreenDialogFinished(dialog, result,
		[this]() {
			gameList_.Invalidate();
			RebuildGrid();
		},
		[this]() {
			tileGridView_->RestoreFocus();
		});
}

void KioskGridScreen::OnFocusedTileChanged(KioskTile *tile) {
	if (!hintBar_)
		return;
	RefreshSettingsHintLabel(OnSettingsEvent, tileGridView_->FocusedGameId(entries_));
	UpdateGridBoundaryFocusability(tile);
}

// Keeps directional (D-pad/analog) navigation from jumping to the top sort/filter bar
// or the hint bar while there are still real tiles to reach first, by only letting the
// generic nearest-neighbor search consider them once focus is genuinely at that edge
// of the grid (see Clickable::SetCanBeFocused). The hint bar sits below the grid in
// landscape but to its right in portrait, so which edge check applies to it flips.
void KioskGridScreen::UpdateGridBoundaryFocusability(KioskTile *tile) {
	bool topRow = tileGridView_->IsInTopRow(tile);
	if (sortPill_)
		sortPill_->SetCanBeFocused(topRow);
	if (filterPill_)
		filterPill_->SetCanBeFocused(topRow);
	if (hintBar_) {
		bool hintBarEdge = portrait_ ? tileGridView_->IsInLastColumn(tile) : tileGridView_->IsInBottomRow(tile);
		hintBar_->SetButtonsCanBeFocused(hintBarEdge);
	}
}

void KioskGridScreen::BuildHintBar() {
	auto ki = GetI18NCategory(I18NCat::MAINMENU);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	std::vector<KioskHintBar::Entry> entries;
	entries.push_back(Kiosk::LaunchEntry(OnLaunchEvent, std::string(ki->T("Launch"))));
	entries.push_back(Kiosk::BackEntry(OnBackEvent, std::string(di->T("Back"))));
	entries.push_back({ ImageID("I_TRIANGLE"), std::string(ki->T("Settings")), &OnSettingsEvent, ImageID::invalid(), { CTRL_TRIANGLE } });
	entries.push_back({ ImageID("I_SHOULDER_LINE"), std::string(ki->T("Sort By")), &OnCycleSortEvent, ImageID("I_L"), { CTRL_LTRIGGER }, /* flipGlyphH = */ false, Kiosk::kSmallGlyphIconScale });
	entries.push_back({ ImageID("I_SHOULDER_LINE"), std::string(ki->T("Sort Order")), &OnToggleAscEvent, ImageID("I_R"), { CTRL_RTRIGGER }, /* flipGlyphH = */ true, Kiosk::kSmallGlyphIconScale });
	entries.push_back({ ImageID("I_START"), std::string(ki->T("Game Info")), &OnGameInfoEvent, ImageID::invalid(), { CTRL_START, CTRL_SELECT }, /* flipGlyphH = */ false, Kiosk::kSmallGlyphIconScale });
	hintBar_->SetEntries(entries);

	OnLaunchEvent.Add([this](UI::EventParams &e) { OnLaunchClick(e); });
	OnBackEvent.Add([this](UI::EventParams &e) { OnBackClick(e); });
	OnSettingsEvent.Add([this](UI::EventParams &e) { OnSettingsClick(e); });
	OnGameInfoEvent.Add([this](UI::EventParams &e) { OnGameInfoClick(e); });

	OnCycleSortEvent.Add([this](UI::EventParams &e) { CycleSortField(); });
	OnToggleAscEvent.Add([this](UI::EventParams &e) { ToggleAscending(); });
}

void KioskGridScreen::CycleSortField() {
	switch (sort_) {
	case KioskSortField::LAST_USED: sort_ = KioskSortField::ALPHABETICAL; break;
	case KioskSortField::ALPHABETICAL: sort_ = KioskSortField::TIME_PLAYED; break;
	case KioskSortField::TIME_PLAYED: sort_ = KioskSortField::LAST_USED; break;
	}
	g_Config.iKioskGridSortField = (int)sort_;
	RebuildGrid();
}

void KioskGridScreen::ToggleAscending() {
	ascending_ = !ascending_;
	g_Config.bKioskGridSortAscending = ascending_;
	RebuildGrid();
}

void KioskGridScreen::OnSortPillClick(UI::EventParams &e) {
	CycleSortField();
}

void KioskGridScreen::OnFilterPillClick(UI::EventParams &e) {
	filter_ = (filter_ == KioskFilter::ALL) ? KioskFilter::PLAYED_AT_LEAST_ONCE : KioskFilter::ALL;
	RebuildGrid();
}

void KioskGridScreen::OnTileClick(KioskTile *tile) {
	if (tile)
		screenManager()->switchScreen(new EmuScreen(tile->GamePath()));
}

void KioskGridScreen::OnLaunchClick(UI::EventParams &e) {
	OnTileClick(tileGridView_->FocusedTile());
}

void KioskGridScreen::OnTileHold(KioskTile *tile) {
	if (tile)
		screenManager()->push(new KioskGameInfoPopup(tile->GamePath()));
}

void KioskGridScreen::OnGameInfoClick(UI::EventParams &e) {
	KioskTile *tile = tileGridView_->FocusedTile();
	if (!tile || tile->IsAllRoms())
		return;
	screenManager()->push(new KioskGameInfoPopup(tile->GamePath()));
}

void KioskGridScreen::OnBackClick(UI::EventParams &e) {
	TriggerFinish(DR_BACK);
}

void KioskGridScreen::OnSettingsClick(UI::EventParams &e) {
	std::string gameId = tileGridView_->FocusedGameId(entries_);
	Path gamePath = tileGridView_->FocusedGamePath();
	screenManager()->push(Kiosk::MakeSettingsScreen(gamePath, gameId));
}
