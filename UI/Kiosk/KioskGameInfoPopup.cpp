#include <algorithm>
#include <cstdint>
#include <ctime>
#include <vector>

#include "UI/Kiosk/KioskGameInfoPopup.h"
#include "UI/Kiosk/KioskCommon.h"
#include "UI/Kiosk/KioskHintBar.h"
#include "UI/Kiosk/KioskFocusTracker.h"
#include "UI/Kiosk/KioskSaveThumbnail.h"
#include "UI/Kiosk/KioskSaveSlot.h"
#include "UI/Kiosk/KioskInfoCard.h"
#include "UI/GameSettingsScreen.h"
#include "UI/EmuScreen.h"

#include "Common/UI/Root.h"
#include "Common/UI/Context.h"
#include "Common/UI/ScrollView.h"
#include "Common/UI/PopupScreens.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Text/I18n.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/System/Display.h"

#include "Common/File/FileUtil.h"

#include "Core/Config.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/SaveState.h"
#include "Core/Util/RecentFiles.h"

#if PPSSPP_PLATFORM(SWITCH)
#include "Switch/Forwarder/ForwarderInstaller.h"
#endif

using namespace UI;

namespace {

constexpr float kCardPadding = 28.0f;
constexpr float kCardSpacing = 28.0f;
constexpr float kBodyRowSpacing = 14.0f;

constexpr float kTitleRowH = 40.0f;
constexpr float kBadgesRowH = 28.0f;
constexpr float kStatsRowH = 40.0f;
constexpr float kSaveLabelRowH = 24.0f;
constexpr float kOneSaveRowH = Kiosk::kSaveThumbH + Kiosk::kSaveSlotTextH + Kiosk::kSaveSlotMargin * 2.0f;

// Card/body sizing fractions - tuned by hand against the mockup, so named
// here rather than left as bare literals in the arithmetic that uses them.
constexpr float kCardWidthMaxFraction = 0.97f;  // Never edge-to-edge, either orientation.
constexpr float kPortraitCardWidthMinFraction = 0.88f;
constexpr float kPortraitCardHeightFraction = 0.94f;
constexpr float kLandscapeCardWidthMinFraction = 0.72f;
constexpr float kLandscapeCardHeightFraction = 0.82f;
// Back-solve desiredCardW so the resulting card leaves exactly desiredBodyW
// for the body column once padding/icon/spacing overhead is subtracted.
constexpr float kLandscapeCardPaddingFactor = 1.04f;
constexpr float kLandscapeBodyWidthFraction = 0.68f;
constexpr float kLandscapeIconWidthFraction = 0.32f;

constexpr float kMinBodyContentH = kTitleRowH + kBadgesRowH + kStatsRowH + kSaveLabelRowH + kOneSaveRowH + kBodyRowSpacing * 4.0f;

TextView *AddStat(LinearLayout *parent, std::string_view label, const std::string &value) {
	LinearLayout *col = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
	col->Add(new TextView(label, ALIGN_LEFT, true, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
	return col->Add(new TextView(value, ALIGN_LEFT, false, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
}

void AddHomeScreenBadge(LinearLayout *badges, std::string_view label) {
	static constexpr uint32_t kHomeScreenBadgeColor = 0xFF2E7D32;
	static constexpr float kHomeScreenBadgePaddingH = 10.0f;
	static constexpr float kHomeScreenBadgePaddingV = 4.0f;

	LinearLayout *badge = badges->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
	badge->SetBG(UI::Drawable(kHomeScreenBadgeColor));
	badge->padding = Margins(kHomeScreenBadgePaddingH, kHomeScreenBadgePaddingV);
	badge->Add(new TextView(label, ALIGN_LEFT, true, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
}

std::string FormatPlayedDuration(int totalSeconds) {
	int seconds = totalSeconds % 60;
	totalSeconds /= 60;
	int minutes = totalSeconds % 60;
	totalSeconds /= 60;
	int hours = totalSeconds;
	char buf[32];
	snprintf(buf, sizeof(buf), "%dh %dm %ds", hours, minutes, seconds);
	return buf;
}

std::string FormatLastPlayed(uint64_t unixTime) {
	if (unixTime == 0)
		return "-";
	time_t t = (time_t)unixTime;
	struct tm localTm;
#if PPSSPP_PLATFORM(WINDOWS)
	localtime_s(&localTm, &t);
#else
	localtime_r(&t, &localTm);
#endif
	char buf[32];
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &localTm);
	return buf;
}

const char *RegionLabel(GameRegion region) {
	switch (region) {
	case GameRegion::JAPAN: return "Japan";
	case GameRegion::USA: return "USA";
	case GameRegion::EUROPE: return "Europe";
	case GameRegion::HONGKONG: return "Hong Kong";
	case GameRegion::ASIA: return "Asia";
	case GameRegion::KOREA: return "Korea";
	case GameRegion::HOMEBREW: return "Homebrew";
	default: return "Unknown";
	}
}

}  // namespace

KioskGameInfoPopup::KioskGameInfoPopup(const Path &gamePath) : KioskScreenBase<UIBaseDialogScreen>(gamePath) {}

void KioskGameInfoPopup::update() {
	UIScreen::update();
	if (pendingDeleteFinish_) {
		pendingDeleteFinish_ = false;
		TriggerFinish(DR_OK);
		return;
	}
	info_ = g_gameInfoCache->GetInfo(nullptr, gamePath_, GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON | GameInfoFlags::SIZE, &knownFlags_);
	if (knownFlags_ != lastCreateViewsFlags_)
		RecreateViews();
}

float KioskGameInfoPopup::ComputeCardWidth(const Bounds &screenBounds, bool portrait) const {
	const int kDesiredSaveColumns = portrait ? 2 : 3;
	float desiredBodyW = kDesiredSaveColumns * (Kiosk::kSaveThumbW + Kiosk::kSaveSlotMargin * 2.0f) + (kDesiredSaveColumns - 1) * Kiosk::kSaveGridSpacing + Kiosk::kScrollbarGutter;

	float cardW;
	if (portrait) {
		float availableW = Kiosk::ClientWidth(screenBounds, portrait);
		float desiredCardW = desiredBodyW + kCardPadding * 2.0f;
		cardW = std::max(availableW * kPortraitCardWidthMinFraction, desiredCardW);
		return std::min(cardW, availableW * kCardWidthMaxFraction);
	} else {
		float desiredCardW = (desiredBodyW + kCardPadding * kLandscapeCardPaddingFactor + kCardSpacing) / kLandscapeBodyWidthFraction;
		cardW = std::max(screenBounds.w * kLandscapeCardWidthMinFraction, desiredCardW);
	}
	return std::min(cardW, screenBounds.w * kCardWidthMaxFraction);
}

float KioskGameInfoPopup::LandscapeIconWidth(float availableW) const {
	return (availableW - kCardPadding) * kLandscapeIconWidthFraction;
}

float KioskGameInfoPopup::ComputeBodyWidth(bool portrait, float cardW) const {
	float availableW = cardW - kCardPadding * 2.0f;
	if (portrait)
		return availableW;
	return availableW - LandscapeIconWidth(availableW) - kCardSpacing;
}

UI::LinearLayout *KioskGameInfoPopup::BuildCardBody(UI::ViewGroup *card, bool portrait, float cardW, float cardH) {
	float availableW = cardW - kCardPadding * 2.0f;
	float iconW = portrait ? availableW : LandscapeIconWidth(availableW);
	float iconH = iconW / Kiosk::kTileAspect;
	if (portrait) {
		// The body below the icon (title/badges/stats/label/one save row)
		// needs a guaranteed minimum, or the icon can grow to cover it -
		// reserve that space directly instead of hoping a flat fraction of
		// cardH happens to leave enough room at every window size.
		float maxIconH = cardH - kCardSpacing - kMinBodyContentH;
		maxIconH = std::max(maxIconH, availableW / Kiosk::kTileAspect * 0.5f);
		if (iconH > maxIconH) {
			iconH = maxIconH;
			iconW = std::min(iconH * Kiosk::kTileAspect, availableW);
		}
	}
	Gravity iconGravity = portrait ? Gravity::G_HCENTER : Gravity::G_TOPLEFT;
	card->Add(new KioskInfoIcon(info_, new LinearLayoutParams(iconW, iconH, 0.0f, iconGravity)));

	// A concrete height here (not FILL_PARENT+weight) keeps body->ssScroll a
	// single level of weight propagation - two nested weighted levels don't
	// reliably stay EXACTLY-typed, and ssScroll silently grows to fit its
	// content instead of clipping/scrolling it when that happens.
	float availableH = cardH - kCardPadding * 2.0f;
	float bodyH = portrait ? (availableH - kCardSpacing - iconH) : availableH;

	float bodyW = ComputeBodyWidth(portrait, cardW);
	LinearLayout *body = card->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(bodyW, bodyH)));
	body->SetSpacing(kBodyRowSpacing);
	return body;
}

void KioskGameInfoPopup::AddTitleBadgesStats(UI::LinearLayout *body) {
	auto ga = GetI18NCategory(I18NCat::GAME);

	std::string title = info_ ? info_->GetTitle() : gamePath_.GetFilename();
	TextView *titleView = body->Add(new TextView(title, ALIGN_LEFT, false, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
	titleView->SetBig(true);

	LinearLayout *badges = body->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
	badges->SetSpacing(16.0f);
	if (info_ && !info_->id.empty())
		badges->Add(new TextView(info_->id, ALIGN_LEFT, true, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
	if (info_)
		badges->Add(new TextView(std::string(ga->T("Region")) + ": " + RegionLabel(info_->region), ALIGN_LEFT, true, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
#if PPSSPP_PLATFORM(SWITCH)
	if (forwarderInstalled_)
		AddHomeScreenBadge(badges, ga->T("ON HOMESCREEN"));
#endif

	LinearLayout *stats = body->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
	stats->SetSpacing(32.0f);
	PlayTimeTracker::PlayTime playTime{};
	bool hasPlayTime = info_ && g_Config.TimeTracker().GetPlayTime(info_->id, &playTime);
	AddStat(stats, ga->T("Total Played"), hasPlayTime ? FormatPlayedDuration(playTime.totalTimePlayed) : "");
	if (hasPlayTime)
		AddStat(stats, ga->T("Last Played"), FormatLastPlayed(playTime.lastTimePlayed));
	if (info_ && (knownFlags_ & GameInfoFlags::SIZE)) {
		char buf[64];
		snprintf(buf, sizeof(buf), "%.1f MB", info_->gameSizeOnDisk / (1024.0 * 1024.0));
		AddStat(stats, ga->T("Size"), buf);
	}
}

UI::View *KioskGameInfoPopup::BuildSaveSlotGrid(UI::LinearLayout *body, float bodyW) {
	auto ga = GetI18NCategory(I18NCat::GAME);

	TextView *saveStatesLabel = body->Add(new TextView(std::string(ga->T("Save States")), ALIGN_LEFT, true, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));

	GridLayoutSettings gridSettings = Kiosk::MakeReflowGridSettings((int)(Kiosk::kSaveThumbW + Kiosk::kSaveSlotMargin * 2), (int)(Kiosk::kSaveThumbH + Kiosk::kSaveSlotTextH + Kiosk::kSaveSlotMargin * 2), (int)Kiosk::kSaveGridSpacing);
	ScrollView *ssScroll = body->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f)));
	// Fixed, margin-free grid width to avoid ScrollView's margin double-subtraction
	// bug - see the fuller explanation in KioskTileGridView::KioskTileGridView().
	float gridW = bodyW - Kiosk::kScrollbarGutter;
	GridLayout *ssGrid = new GridLayout(gridSettings, new LinearLayoutParams(gridW, WRAP_CONTENT));
	ssScroll->Add(ssGrid);

	Draw::Texture *iconTexture = (info_ && info_->Ready(GameInfoFlags::ICON)) ? info_->icon.texture : nullptr;
	std::string inGameLabel = std::string(ga->T("In-game"));

	auto slotFocus = std::make_shared<KioskFocusTracker<KioskSaveSlot>>();
	saveSlotFocusKeepAlive_ = slotFocus;
	auto trackSlotFocus = [slotFocus](KioskSaveSlot *slotView) {
		slotFocus->WireToHighlightEvent(slotView, slotView->OnHighlight);
	};

	for (const KioskSlotInfo &info : KioskSaveSlotRepository::GatherSlots(info_)) {
		if (info.isInGameSave) {
			KioskSaveSlot *slotView = ssGrid->Add(KioskSaveSlot::ForInGameSave(iconTexture, inGameLabel, info.dateStr, info.timeStr, new GridLayoutParams(Gravity::G_CENTER)));
			slotView->OnClick.Add([this](UI::EventParams &e) { OnInGameSaveClick(); });
			trackSlotFocus(slotView);
			continue;
		}
		KioskSaveSlot *slotView = ssGrid->Add(KioskSaveSlot::ForSavestate(info.slot + 1, info.hasSave, info.screenshotPath, info.dateStr, info.timeStr, new GridLayoutParams(Gravity::G_CENTER)));
		int slot = info.slot;
		slotView->OnClick.Add([this, slot](UI::EventParams &e) { OnSaveSlotClick(slot); });
		trackSlotFocus(slotView);
	}

	if (ssGrid->GetNumSubviews() > 0)
		SetFocusedView(ssGrid->GetViewByIndex(0));

	return saveStatesLabel;
}

void KioskGameInfoPopup::CreateViews() {
	info_ = g_gameInfoCache->GetInfo(nullptr, gamePath_, GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON | GameInfoFlags::SIZE, &knownFlags_);
	lastCreateViewsFlags_ = knownFlags_;

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	Bounds screenBounds = screenManager()->getUIContext()->GetBounds();
	bool portrait = Kiosk::IsPortrait(screenBounds);
	portrait_ = portrait;
	float cardW = ComputeCardWidth(screenBounds, portrait);
	float clientCenterY = Kiosk::ClientCenterY(screenBounds, portrait);
	float availableCardH = Kiosk::ClientHeight(screenBounds, portrait);
	float cardH = portrait ? availableCardH * kPortraitCardHeightFraction : availableCardH * kLandscapeCardHeightFraction;

	hintBar_ = Kiosk::AddHintBar(root_, portrait ? ORIENT_VERTICAL : ORIENT_HORIZONTAL);
	BuildHintBar();

	// AnchorLayout centers left=NONE/right=NONE views across the FULL parent
	// width with no awareness of sibling views, so in portrait the card must
	// be given an explicit left offset centered within the region the
	// vertical hint bar doesn't occupy, or it would overlap the bar.
	float cardLeft = NONE;
	if (portrait) {
		cardLeft = std::max(0.0f, (Kiosk::ClientWidth(screenBounds, portrait) - cardW) * 0.5f);
	}

	KioskInfoCard *card = new KioskInfoCard(portrait ? ORIENT_VERTICAL : ORIENT_HORIZONTAL,
		new AnchorLayoutParams(cardW, cardH, cardLeft, clientCenterY, NONE, NONE, Centering::Vertical));
	card->padding = Margins(kCardPadding);
	card->SetSpacing(kCardSpacing);

	LinearLayout *body = BuildCardBody(card, portrait, cardW, cardH);
	AddTitleBadgesStats(body);
	View *saveStatesLabel = BuildSaveSlotGrid(body, ComputeBodyWidth(portrait, cardW));
	card->SetDividerAbove(saveStatesLabel);

	root_->Add(card);
}

void KioskGameInfoPopup::BuildHintBar() {
	RefreshHintBarEntries();

	if (hintBarEventsBound_)
		return;
	hintBarEventsBound_ = true;

	OnLaunchEvent.Add([this](UI::EventParams &e) { OnLaunch(e); });
	OnSettingsEvent.Add([this](UI::EventParams &e) { OnGameSettings(e); });
	OnBackEvent.Add([this](UI::EventParams &e) { OnBack(e); });
	OnDeleteRomEvent.Add([this](UI::EventParams &e) { OnDeleteRom(e); });
#if PPSSPP_PLATFORM(SWITCH)
	OnAddToHomeEvent.Add([this](UI::EventParams &e) { OnAddToHome(e); });
#endif
}

// Rebuilds just the entry list, safe to call again later (e.g. after adding a Switch
// Home Screen forwarder) without re-registering the once-only event bindings above.
void KioskGameInfoPopup::RefreshHintBarEntries() {
	auto ga = GetI18NCategory(I18NCat::GAME);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	std::string gameId = info_ ? info_->id : "";

	std::vector<KioskHintBar::Entry> entries;
	entries.push_back(Kiosk::LaunchEntry(OnLaunchEvent, std::string(ga->T("Launch"))));
	entries.push_back(Kiosk::BackEntry(OnBackEvent, std::string(di->T("Back"))));
#if PPSSPP_PLATFORM(SWITCH)
	if ((knownFlags_ & GameInfoFlags::PARAM_SFO) && System_GetPropertyBool(SYSPROP_CAN_CREATE_SWITCH_HOME_FORWARDER)) {
		forwarderInstalled_ = Forwarder::IsForwarderInstalled(gamePath_.ToString());
		if (!forwarderInstalled_)
			entries.push_back({ ImageID("I_SQUARE"), std::string(ga->T("Add to Home Screen")), &OnAddToHomeEvent, ImageID::invalid(), { CTRL_SQUARE } });
	}
#endif
	entries.push_back(Kiosk::SettingsEntry(OnSettingsEvent, gameId));
	entries.push_back({ ImageID("I_SELECT"), std::string(ga->T("Delete Game")), &OnDeleteRomEvent, ImageID::invalid(), { CTRL_SELECT }, /* flipGlyphH = */ false, Kiosk::kSmallGlyphIconScale });
	hintBar_->SetEntries(entries);
}

void KioskGameInfoPopup::OnLaunch(UI::EventParams &e) {
	screenManager()->switchScreen(new EmuScreen(gamePath_));
}

void KioskGameInfoPopup::OnGameSettings(UI::EventParams &e) {
	std::string gameId = info_ ? info_->id : "";
	screenManager()->push(Kiosk::MakeSettingsScreen(gamePath_, gameId));
}

void KioskGameInfoPopup::OnBack(UI::EventParams &e) {
	TriggerFinish(DR_BACK);
}

void KioskGameInfoPopup::OnAddToHome(UI::EventParams &e) {
#if PPSSPP_PLATFORM(SWITCH)
	auto di = GetI18NCategory(I18NCat::DIALOG);
	std::string title = info_ ? info_->GetTitle() : gamePath_.GetFilename();
	System_CreateSwitchHomeForwarder(GetRequesterToken(), gamePath_, title, [this, di](const char *responseString, int responseValue) {
		if (responseValue) {
			forwarderInstalled_ = true;
			RefreshHintBarEntries();
			g_OSD.Show(OSDType::MESSAGE_SUCCESS, di->T("Added to Switch Home Screen"), 2.0f);
		} else {
			g_OSD.Show(OSDType::MESSAGE_ERROR, (responseString && responseString[0]) ? responseString : "Failed to add to Switch Home Screen", 3.0f);
		}
	});
#endif
}

void KioskGameInfoPopup::OnDeleteRom(UI::EventParams &e) {
	if (!info_ || !info_->Ready(GameInfoFlags::PARAM_SFO))
		return;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ga = GetI18NCategory(I18NCat::GAME);
	std::string prompt = std::string(di->T("DeleteConfirmGame", "Do you really want to delete this game\nfrom your device? You can't undo this."));
	prompt += "\n\n" + gamePath_.ToVisualString(g_Config.memStickDirectory.c_str());
	const bool trashAvailable = System_GetPropertyBool(SYSPROP_HAS_TRASH_BIN);
	Path gamePath = gamePath_;
	screenManager()->push(
		new UI::MessagePopupScreen(ga->T("Delete Game"), prompt, trashAvailable ? di->T("Move to trash") : di->T("Delete"), di->T("Cancel"),
			[this, gamePath](bool yes) {
		if (yes) {
			std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath, GameInfoFlags::PARAM_SFO);
			// The popup's own GameInfoFlags::ICON|SIZE fetch (for the Size stat)
			// opens a file loader that's only ever disposed by the async work
			// item that first populated it - by now that's long gone, so the
			// handle is still open and Delete() would fail with a sharing
			// violation (e.g. Windows "File In Use") unless closed here first.
			info->DisposeFileLoader();
			info->Delete();
			g_gameInfoCache->Clear();
			g_recentFiles.Remove(gamePath.ToString());
			// Can't TriggerFinish() here - this callback runs while the
			// MessagePopupScreen we were shown from is still stack_.back(),
			// and ScreenManager::finishDialog() silently rejects a finish
			// from any screen that isn't currently on top. Deferred to update().
			pendingDeleteFinish_ = true;
		}
	}));
}

void KioskGameInfoPopup::OnSaveSlotClick(int slot) {
	if (!info_ || info_->id_version.empty())
		return;
	Path fn = Kiosk::SaveSlotFilename(info_->id_version, slot);
	if (!File::Exists(fn))
		return;
	Path gamePath = gamePath_;
	SaveState::Load(fn, slot, SaveState::Callback());
	screenManager()->switchScreen(new EmuScreen(gamePath));
}

void KioskGameInfoPopup::OnInGameSaveClick() {
	screenManager()->switchScreen(new EmuScreen(gamePath_, /* skipAutoLoad = */ true));
}
