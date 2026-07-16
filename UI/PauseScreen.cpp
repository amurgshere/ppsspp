// Copyright (c) 2014- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <algorithm>
#include <memory>

#include "Common/Render/DrawBuffer.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Context.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/PopupScreens.h"
#include "Common/GPU/thin3d.h"

#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/StringUtils.h"
#include "Common/System/Display.h"
#include "Common/System/OSD.h"
#include "Common/System/Request.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/UI/AsyncImageFileView.h"

#include "Core/KeyMap.h"
#include "Core/Reporting.h"
#include "Core/Dialog/PSPSaveDialog.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/RetroAchievements.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceUmd.h"
#include "Core/HLE/sceNet.h"
#include "Core/HLE/sceNetInet.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/Util/GameDB.h"
#include "Core/HLE/NetAdhocCommon.h"

#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"

#include "UI/EmuScreen.h"
#include "UI/PauseScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/ReportScreen.h"
#include "UI/CwCheatScreen.h"
#include "UI/MainScreen.h"
#include "UI/GameScreen.h"
#include "UI/OnScreenDisplay.h"
#include "UI/GameInfoCache.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/RetroAchievementScreens.h"
#include "UI/TouchControlLayoutScreen.h"
#include "UI/BackgroundAudio.h"
#include "UI/MiscViews.h"

static void AfterSaveStateAction(SaveState::Status status, std::string_view message) {
	if (!message.empty() && (!g_Config.bDumpFrames || !g_Config.bDumpVideoOutput)) {
		g_OSD.Show(status == SaveState::Status::SUCCESS ? OSDType::MESSAGE_SUCCESS : OSDType::MESSAGE_ERROR,
			message, status == SaveState::Status::SUCCESS ? 2.0 : 5.0);
	}
}

class ScreenshotViewScreen : public UI::PopupScreen {
public:
	ScreenshotViewScreen(const Path &filename, std::string title, int slot, Path gamePath)
		: PopupScreen(title), filename_(filename), slot_(slot), gamePath_(gamePath), title_(title) {}   // PopupScreen will translate Back on its own

	int GetSlot() const {
		return slot_;
	}

	const char *tag() const override { return "ScreenshotView"; }

protected:
	bool FillVertical() const override { return false; }
	UI::Size PopupWidth() const override { return 500; }
	bool ShowButtons() const override { return true; }

	void CreatePopupContents(UI::ViewGroup *parent) override {
		using namespace UI;
		auto pa = GetI18NCategory(I18NCat::PAUSE);
		auto di = GetI18NCategory(I18NCat::DIALOG);

		ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
		LinearLayout *content = new LinearLayout(ORIENT_VERTICAL);
		Margins contentMargins(10, 0);
		content->Add(new AsyncImageFileView(filename_, IS_KEEP_ASPECT, new LinearLayoutParams(480, 272, contentMargins)))->SetCanBeFocused(false);

		GridLayoutSettings gridsettings(240, 64, 5);
		gridsettings.fillCells = true;
		GridLayout *grid = content->Add(new GridLayoutList(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

		Choice *back = new Choice(di->T("Back"));

		const bool hasUndo = SaveState::HasUndoSaveInSlot(gamePath_, slot_);
		const bool undoEnabled = g_Config.bEnableStateUndo;

		Choice *undoButton = nullptr;
		if (undoEnabled || hasUndo) {
			// Show the undo button if state undo is enabled in settings, OR one is available. We can load it
			// even if making new undo states is not enabled.
			Choice *undoButton = new Choice(pa->T("Undo last save"));
			undoButton->SetEnabled(hasUndo);
		}

		grid->Add(new Choice(pa->T("Save State")))->OnClick.Handle(this, &ScreenshotViewScreen::OnSaveState);
		// We can unconditionally show the load state button, because you can only pop this dialog up if a state exists.
		grid->Add(new Choice(pa->T("Load State")))->OnClick.Handle(this, &ScreenshotViewScreen::OnLoadState);
		grid->Add(new Choice(pa->T("Delete State")))->OnClick.Handle(this, &ScreenshotViewScreen::OnDeleteState);
		if (undoButton) {
			grid->Add(undoButton)->OnClick.Handle(this, &ScreenshotViewScreen::OnUndoState);
		}
		grid->Add(back)->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

		scroll->Add(content);
		parent->Add(scroll);
	}

private:
	void OnSaveState(UI::EventParams &e);
	void OnLoadState(UI::EventParams &e);
	void OnUndoState(UI::EventParams &e);
	void OnDeleteState(UI::EventParams &e);

	Path filename_;
	Path gamePath_;
	std::string title_;
	int slot_;
};

void ScreenshotViewScreen::OnSaveState(UI::EventParams &e) {
	if (!NetworkWarnUserIfOnlineAndCantSavestate()) {
		g_Config.iCurrentStateSlot = slot_;
		SaveState::SaveSlot(gamePath_, slot_, &AfterSaveStateAction);
		TriggerFinish(DR_OK); //OK will close the pause screen as well
	}
}

void ScreenshotViewScreen::OnLoadState(UI::EventParams &e) {
	if (!NetworkWarnUserIfOnlineAndCantSavestate()) {
		g_Config.iCurrentStateSlot = slot_;
		SaveState::LoadSlot(gamePath_, slot_, &AfterSaveStateAction);
		TriggerFinish(DR_OK);
	}
}

void ScreenshotViewScreen::OnUndoState(UI::EventParams &e) {
	if (!NetworkWarnUserIfOnlineAndCantSavestate()) {
		SaveState::UndoSaveSlot(gamePath_, slot_);
		TriggerFinish(DR_CANCEL);
	}
}

void ScreenshotViewScreen::OnDeleteState(UI::EventParams &e) {
	auto di = GetI18NCategory(I18NCat::DIALOG);

	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, GameInfoFlags::PARAM_SFO);

	std::string_view title = di->T("Delete");
	std::string message = std::string(di->T("DeleteConfirmSaveState")) + "\n\n" + info->GetTitle() + " (" + info->id + ")";
	message += "\n\n" + title_;

	// TODO: Also show the screenshot on the confirmation screen?

	screenManager()->push(new UI::MessagePopupScreen(title, message, di->T("Delete"), di->T("Cancel"), [=](bool result) {
		if (result) {
			SaveState::DeleteSlot(gamePath_, slot_);
			TriggerFinish(DR_CANCEL);
		}
	}));
}

// Minimum width (dp) of the save-slot area required to lay out full-height
// Save State / Load State buttons to the right of the date, instead of the
// compact stacked layout. Below this, small-screen touch targets would end
// up cramped, so we fall back to the original layout.
static constexpr float kSaveSlotWideLayoutMinWidth = 900.0f;

// Explicit row height used for slots rendered with wide (full-height) Save/Load buttons.
// A LinearLayout only stretches FILL_PARENT children to fill it when it itself has an
// exact/bounded height to hand down - WRAP_CONTENT rows (the narrow layout) instead size
// themselves from their tallest child, so buttons can't "fill" a height that depends on them.
static constexpr float kSaveSlotWideRowHeight = 100.0f;

class SaveSlotView : public UI::LinearLayout {
public:
	SaveSlotView(const Path &gamePath, int slot, bool wideButtons, UI::LayoutParams *layoutParams = nullptr);

	void Draw(UIContext &dc) override;

	int GetSlot() const {
		return slot_;
	}

	Path GetScreenshotFilename() const {
		return screenshotFilename_;
	}

	std::string GetScreenshotTitle() const {
		return SaveState::GetSlotDateAsString(gamePath_, slot_);
	}

	UI::Event OnStateLoaded;
	UI::Event OnStateSaved;
	UI::Event OnRequestClear;
	UI::Event OnScreenshotClicked;

private:
	UI::LinearLayout *AddThumbnailAndNumber();
	void AddNarrowDateAndButtons(UI::LinearLayout *lines, bool hasSave);
	void AddWideDateAndButtons(bool hasSave);
	UI::Button *AddSaveStateButton(UI::ViewGroup *parent, UI::LayoutParams *layoutParams);
	UI::Button *AddLoadStateButton(UI::ViewGroup *parent, UI::LayoutParams *layoutParams);
	UI::Button *AddClearStateButton(UI::ViewGroup *parent, UI::LayoutParams *layoutParams);

	void OnSaveState(UI::EventParams &e);
	void OnLoadState(UI::EventParams &e);
	void OnClearState(UI::EventParams &e);

	UI::Button *saveStateButton_ = nullptr;
	UI::Button *loadStateButton_ = nullptr;
	UI::Button *clearStateButton_ = nullptr;

	int slot_;
	Path gamePath_;
	Path screenshotFilename_;
	bool wideButtons_;
};

SaveSlotView::SaveSlotView(const Path &gameFilename, int slot, bool wideButtons, UI::LayoutParams *layoutParams)
	: UI::LinearLayout(ORIENT_HORIZONTAL, layoutParams), slot_(slot), gamePath_(gameFilename), wideButtons_(wideButtons) {
	using namespace UI;

	screenshotFilename_ = SaveState::GenerateSaveSlotFilename(gamePath_, slot, SaveState::SCREENSHOT_EXTENSION);

	const bool hasSave = SaveState::HasSaveInSlot(gamePath_, slot);

	LinearLayout *lines = AddThumbnailAndNumber();

	if (wideButtons_) {
		AddWideDateAndButtons(hasSave);
	} else {
		AddNarrowDateAndButtons(lines, hasSave);
	}
}

// Adds the slot number and screenshot thumbnail (common to both layouts), and
// returns the "lines" column (thumbnail caption area) used by the narrow layout.
UI::LinearLayout *SaveSlotView::AddThumbnailAndNumber() {
	using namespace UI;

	std::string number = StringFromFormat("%d", slot_ + 1);
	Add(new Spacer(5));

	Add(new TextView(number, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 0.0f, Gravity::G_VCENTER)))->SetBig(true);

	AsyncImageFileView *fv = Add(new AsyncImageFileView(screenshotFilename_, IS_DEFAULT, new UI::LayoutParams(82 * 2, 47 * 2)));
	fv->OnClick.Add([this](UI::EventParams &e) {
		e.v = this;
		OnScreenshotClicked.Trigger(e);
	});

	if (!SaveState::HasSaveInSlot(gamePath_, slot_)) {
		fv->SetFilename(Path());
	}

	LinearLayout *lines = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
	lines->SetSpacing(2.0f);
	Add(lines);
	return lines;
}

// Original compact layout: Save/Load buttons stacked in a row above the date,
// underneath the thumbnail. Used on narrow screens, or when there's no save yet.
void SaveSlotView::AddNarrowDateAndButtons(UI::LinearLayout *lines, bool hasSave) {
	using namespace UI;

	LinearLayout *buttons = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
	buttons->SetSpacing(10.0f);
	lines->Add(buttons);

	AddSaveStateButton(buttons, new LinearLayoutParams(0.0, Gravity::G_VCENTER));

	if (hasSave) {
		if (!Achievements::HardcoreModeActive()) {
			AddLoadStateButton(buttons, new LinearLayoutParams(0.0, Gravity::G_VCENTER));
		}
		AddClearStateButton(buttons, new LinearLayoutParams(0.0, Gravity::G_VCENTER));

		std::string dateStr = SaveState::GetSlotDateAsString(gamePath_, slot_);
		if (!dateStr.empty()) {
			TextView *dateView = new TextView(dateStr, new LinearLayoutParams(0.0, Gravity::G_VCENTER));
			dateView->SetSmall(true);
			lines->Add(dateView)->SetShadow(true);
		}
	}
}

// Wide layout: date shown inline, with Save State then Load State as large
// full-height buttons pinned to the right of the row for easier touch targets.
void SaveSlotView::AddWideDateAndButtons(bool hasSave) {
	using namespace UI;

	std::string dateStr = SaveState::GetSlotDateAsString(gamePath_, slot_);
	if (!dateStr.empty()) {
		TextView *dateView = Add(new TextView(dateStr, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 0.0f, Gravity::G_VCENTER)));
		dateView->SetSmall(true);
		dateView->SetShadow(true);
	}

	Add(new Spacer(new LinearLayoutParams(1.0f, WRAP_CONTENT)));

	AddSaveStateButton(this, new LinearLayoutParams(150, FILL_PARENT, Margins(5, 0)));

	if (hasSave) {
		if (!Achievements::HardcoreModeActive()) {
			AddLoadStateButton(this, new LinearLayoutParams(150, FILL_PARENT, Margins(5, 0)));
		}
		AddClearStateButton(this, new LinearLayoutParams(150, FILL_PARENT, Margins(5, 0)));
	}
}

UI::Button *SaveSlotView::AddSaveStateButton(UI::ViewGroup *parent, UI::LayoutParams *layoutParams) {
	auto pa = GetI18NCategory(I18NCat::PAUSE);
	saveStateButton_ = parent->Add(new UI::Button(pa->T("Save State"), layoutParams));
	saveStateButton_->OnClick.Handle(this, &SaveSlotView::OnSaveState);
	return saveStateButton_;
}

UI::Button *SaveSlotView::AddLoadStateButton(UI::ViewGroup *parent, UI::LayoutParams *layoutParams) {
	auto pa = GetI18NCategory(I18NCat::PAUSE);
	loadStateButton_ = parent->Add(new UI::Button(pa->T("Load State"), layoutParams));
	loadStateButton_->OnClick.Handle(this, &SaveSlotView::OnLoadState);
	return loadStateButton_;
}

UI::Button *SaveSlotView::AddClearStateButton(UI::ViewGroup *parent, UI::LayoutParams *layoutParams) {
	auto pa = GetI18NCategory(I18NCat::PAUSE);
	clearStateButton_ = parent->Add(new UI::Button(pa->T("Clear"), layoutParams));
	clearStateButton_->OnClick.Handle(this, &SaveSlotView::OnClearState);
	return clearStateButton_;
}

void SaveSlotView::Draw(UIContext &dc) {
	if (g_Config.iCurrentStateSlot == slot_) {
		dc.FillRect(UI::Drawable(0x70000000), GetBounds().Expand(3));
		dc.FillRect(UI::Drawable(0x70FFFFFF), GetBounds().Expand(3));
	}
	UI::LinearLayout::Draw(dc);
}

void SaveSlotView::OnLoadState(UI::EventParams &e) {
	if (!NetworkWarnUserIfOnlineAndCantSavestate()) {
		g_Config.iCurrentStateSlot = slot_;
		SaveState::LoadSlot(gamePath_, slot_, &AfterSaveStateAction);
		UI::EventParams e2{};
		e2.v = this;
		OnStateLoaded.Trigger(e2);
	}
}

void SaveSlotView::OnSaveState(UI::EventParams &e) {
	if (!NetworkWarnUserIfOnlineAndCantSavestate()) {
		g_Config.iCurrentStateSlot = slot_;
		SaveState::SaveSlot(gamePath_, slot_, &AfterSaveStateAction);
		UI::EventParams e2{};
		e2.v = this;
		OnStateSaved.Trigger(e2);
	}
}

void SaveSlotView::OnClearState(UI::EventParams &e) {
	UI::EventParams e2{};
	e2.v = this;
	OnRequestClear.Trigger(e2);
}

void GamePauseScreen::resized() {
	UIBaseDialogScreen::resized();
	// Window resize can change whether there's room for the wide Save/Load button
	// layout (see HasRoomForWideSaveButtons) - rebuild so save slots switch layout live.
	RecreateViews();
}

void GamePauseScreen::update() {
	UpdateUIState(UISTATE_PAUSEMENU);
	UIScreen::update();

	if (finishNextFrame_) {
		TriggerFinish(finishNextFrameResult_);
		finishNextFrame_ = false;
	}

	if (pendingProceedWithExit_) {
		pendingProceedWithExit_ = false;
		ProceedWithExit(pendingProceedExitsEmulator_);
	}

	const bool networkConnected = IsNetworkConnected();
	const InfraDNSConfig &dnsConfig = GetInfraDNSConfig();
	if (g_netInited != lastNetInited_ || netInetInited != lastNetInetInited_ || lastAdhocServerConnected_ != g_adhocServerConnected || lastOnline_ != networkConnected || lastDNSConfigLoaded_ != dnsConfig.loaded) {
		INFO_LOG(Log::sceNet, "Network status changed (or pause dialog just popped up), recreating views");
		RecreateViews();
		lastNetInetInited_ = netInetInited;
		lastNetInited_ = g_netInited;
		lastAdhocServerConnected_ = g_adhocServerConnected;
		lastOnline_ = networkConnected;
		lastDNSConfigLoaded_ = dnsConfig.loaded;
	}

	if (playButton_) {
		const bool mustRunBehind = MustRunBehind();
		playButton_->SetVisibility(mustRunBehind ? UI::V_GONE : UI::V_VISIBLE);
	}

	SetVRAppMode(VRAppMode::VR_MENU_MODE);
}

GamePauseScreen::GamePauseScreen(const Path &filename, bool bootPending)
	: UIBaseDialogScreen(filename), bootPending_(bootPending) {
	// So we can tell if something blew up while on the pause screen.
	std::string assertStr = "PauseScreen: " + filename.GetFilename();
	SetExtraAssertInfo(assertStr.c_str());
}

GamePauseScreen::~GamePauseScreen() {
	__DisplaySetWasPaused();
}

bool GamePauseScreen::key(const KeyInput &key) {
	if (!UIScreen::key(key) && (key.flags & KEY_DOWN)) {
		// Special case to be able to unpause with a bound pause key.
		// Normally we can't bind keys used in the UI.
		InputMapping mapping(key.deviceId, key.keyCode);
		std::vector<int> pspButtons;
		KeyMap::InputMappingToPspButton(mapping, &pspButtons);
		for (auto button : pspButtons) {
			if (button == VIRTKEY_PAUSE) {
				TriggerFinish(DR_CANCEL);
				return true;
			}
		}
		return false;
	}
	return false;
}

// Estimates the width (dp) available to the save-slot list, to decide whether there's
// room for full-height Save/Load buttons beside the date instead of the compact
// stacked layout. Mirrors the column widths GamePauseScreen::CreateViews() lays out
// (see the non-portrait branch: save-slot scroll gets whatever's left after the fixed-
// width middle and button columns).
static bool HasRoomForWideSaveButtons(bool portrait) {
	if (portrait) {
		// The save-slot list gets the full screen width in portrait mode.
		return g_display.dp_xres >= kSaveSlotWideLayoutMinWidth;
	}
	const float middleColumnWidth = UI::ITEM_HEIGHT;
	const float buttonColumnWidth = 320.0f;
	const float availableWidth = g_display.dp_xres - middleColumnWidth - buttonColumnWidth;
	return availableWidth >= kSaveSlotWideLayoutMinWidth;
}

void GamePauseScreen::CreateSavestateControls(UI::LinearLayout *leftColumnItems, bool wideButtons) {
	auto pa = GetI18NCategory(I18NCat::PAUSE);

	static const int NUM_SAVESLOTS = 5;

	using namespace UI;

	leftColumnItems->SetSpacing(10.0);
	// SaveSlotView's selection highlight draws slightly outside its own bounds (see
	// SaveSlotView::Draw's GetBounds().Expand(3)). Without this, the very first slot has
	// no gap above it, so that top sliver of highlight gets clipped by the scroll view's
	// scissor rect - only the bottom edge (which has the inter-slot spacing to draw into)
	// ends up visible when slot 1 is selected.
	leftColumnItems->Add(new Spacer(3.0f));
	float rowHeight = wideButtons ? kSaveSlotWideRowHeight : WRAP_CONTENT;
	for (int i = 0; i < NUM_SAVESLOTS; i++) {
		SaveSlotView *slot = leftColumnItems->Add(new SaveSlotView(gamePath_, i, wideButtons, new LinearLayoutParams(FILL_PARENT, rowHeight, Gravity::G_HCENTER, Margins(0,0,0,0))));
		slot->OnStateLoaded.Handle(this, &GamePauseScreen::OnState);
		slot->OnStateSaved.Handle(this, &GamePauseScreen::OnState);
		slot->OnRequestClear.Handle(this, &GamePauseScreen::OnRequestClearState);
		slot->OnScreenshotClicked.Handle(this, &GamePauseScreen::OnScreenshotClicked);
	}
	leftColumnItems->Add(new Spacer(0.0));

	LinearLayout *buttonRow = leftColumnItems->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(Margins(10, 0, 0, 0))));
	if (g_Config.bEnableStateUndo && !Achievements::HardcoreModeActive() && NetworkAllowSaveState()) {
		UI::Choice *loadUndoButton = buttonRow->Add(new Choice(pa->T("Undo last load")));
		loadUndoButton->SetEnabled(SaveState::HasUndoLoad(gamePath_));
		loadUndoButton->OnClick.Handle(this, &GamePauseScreen::OnLoadUndo);

		UI::Choice *saveUndoButton = buttonRow->Add(new Choice(pa->T("Undo last save")));
		saveUndoButton->SetEnabled(SaveState::HasUndoLastSave(gamePath_));
		saveUndoButton->OnClick.Handle(this, &GamePauseScreen::OnLastSaveUndo);
	}

	if (g_Config.iRewindSnapshotInterval > 0 && !Achievements::HardcoreModeActive() && NetworkAllowSaveState()) {
		UI::Choice *rewindButton = buttonRow->Add(new Choice(pa->T("Rewind")));
		rewindButton->SetEnabled(SaveState::CanRewind());
		rewindButton->OnClick.Handle(this, &GamePauseScreen::OnRewind);
	}
}

UI::Margins GamePauseScreen::RootMargins() const {
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE && GetDeviceOrientation() == DeviceOrientation::Landscape) {
		// Add some top margin on mobile so it isn't too close to the status bar, as we place buttons
		// very close to the top of the screen.
		return UI::Margins(0, 30, 0, 0);
	} else {
		return UI::Margins(0);
	}
}

void GamePauseScreen::CreateViews() {
	using namespace UI;

	bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	Margins scrollMargins(0, 10, 0, 0);
	Margins actionMenuMargins(0, 10, 15, 0);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);
	auto pa = GetI18NCategory(I18NCat::PAUSE);
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	auto nw = GetI18NCategory(I18NCat::NETWORKING);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto co = GetI18NCategory(I18NCat::CONTROLS);

	root_ = new LinearLayout(portrait ? ORIENT_VERTICAL : ORIENT_HORIZONTAL);

	if (portrait) {
		((LinearLayout *)root_)->SetSpacing(0);
	}

	if (portrait) {
		// We have room for a title bar. Use the game DB title if available.
		std::string title;
		std::vector<GameDBInfo> dbInfos;
		const bool inGameDB = g_gameDB.GetGameInfos(g_paramSFO.GetDiscID(), &dbInfos);
		if (inGameDB) {
			title = dbInfos[0].title;
		} else {
			title = g_paramSFO.GetValueString("TITLE");
		}
		TopBar *topBar = new TopBar(*screenManager()->getUIContext(), TopBarFlags::ContextMenuButton, title);
		root_->Add(topBar);

		topBar->OnContextMenuClick.Add([this, portrait](UI::EventParams &e) {
			UI::View *srcView = e.v;
			ShowContextMenu(srcView, portrait);
		});
	}

	ViewGroup *saveStateScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f, scrollMargins));
	root_->Add(saveStateScroll);

	LinearLayout *saveDataScrollItems = new LinearLayoutList(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT));
	saveStateScroll->Add(saveDataScrollItems);

	saveDataScrollItems->SetSpacing(5.0f);
	if (Achievements::IsActive()) {
		saveDataScrollItems->Add(new GameAchievementSummaryView());

		char buf[512];
		size_t sz = Achievements::GetRichPresenceMessage(buf, sizeof(buf));
		if (sz != (size_t)-1) {
			saveDataScrollItems->Add(new TextView(std::string_view(buf, sz), FLAG_WRAP_TEXT, true, new UI::LinearLayoutParams(Margins(5, 5))));
		}
	}

	if (IsNetworkConnected()) {
		saveDataScrollItems->Add(new NoticeView(NoticeLevel::INFO, nw->T("Network connected"), ""));

		const InfraDNSConfig &dnsConfig = GetInfraDNSConfig();
		if (dnsConfig.loaded && __NetApctlConnected()) {
			saveDataScrollItems->Add(new NoticeView(NoticeLevel::INFO, nw->T("Infrastructure"), ""));

			if (dnsConfig.state == InfraGameState::NotWorking) {
				saveDataScrollItems->Add(new NoticeView(NoticeLevel::WARN, nw->T("Some network functionality in this game is not working"), ""));
				if (!dnsConfig.workingIDs.empty()) {
					std::string str(nw->T("Other versions of this game that should work:"));
					for (auto &id : dnsConfig.workingIDs) {
						str.append("\n - ");
						str += id;
					}
					saveDataScrollItems->Add(new TextView(str));
				}
			} else if (dnsConfig.state == InfraGameState::Unknown) {
				saveDataScrollItems->Add(new NoticeView(NoticeLevel::WARN, nw->T("Network functionality in this game is not guaranteed"), ""));
			}
			if (!dnsConfig.revivalTeam.empty()) {
				saveDataScrollItems->Add(new TextView(std::string(nw->T("Infrastructure server provided by:"))));
				saveDataScrollItems->Add(new TextView(dnsConfig.revivalTeam));
				if (!dnsConfig.revivalTeamURL.empty()) {
					saveDataScrollItems->Add(new Button(dnsConfig.revivalTeamURL))->OnClick.Add([&dnsConfig](UI::EventParams &e) {
						if (!dnsConfig.revivalTeamURL.empty()) {
							System_LaunchUrl(LaunchUrlType::BROWSER_URL, dnsConfig.revivalTeamURL.c_str());
						}
					});
				}
			}
		}

		if (NetAdhocctl_GetState() >= ADHOCCTL_STATE_CONNECTED) {
			// Awkwardly re-using a string here
			saveDataScrollItems->Add(new TextView(std::string(nw->T("AdHoc server")) + ": " + std::string(nw->T("Connected"))));
		}
	}

	bool achievementsAllowSavestates = !Achievements::HardcoreModeActive() || g_Config.bAchievementsSaveStateInHardcoreMode;
	bool showSavestateControls = achievementsAllowSavestates;
	if (IsNetworkConnected() && !g_Config.bAllowSavestateWhileConnected) {
		showSavestateControls = false;
	}

	if (showSavestateControls) {
		if (PSP_CoreParameter().compat.flags().SaveStatesNotRecommended) {
			LinearLayout *horiz = new LinearLayout(ORIENT_HORIZONTAL);
			saveDataScrollItems->Add(horiz);
			horiz->Add(new NoticeView(NoticeLevel::WARN, pa->T("Using save states is not recommended in this game"), "", new LinearLayoutParams(1.0f)));
			horiz->Add(new Button(di->T("More info")))->OnClick.Add([](UI::EventParams &e) {
				System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/docs/troubleshooting/save-state-time-warps");
			});
		}
		CreateSavestateControls(saveDataScrollItems, HasRoomForWideSaveButtons(portrait));
	} else {
		// Let's show the active challenges.
		std::set<uint32_t> ids = Achievements::GetActiveChallengeIDs();
		if (!ids.empty()) {
			saveDataScrollItems->Add(new ItemHeader(ac->T("Active Challenges")));
			for (auto id : ids) {
				const rc_client_achievement_t *achievement = rc_client_get_achievement_info(Achievements::GetClient(), id);
				if (!achievement)
					continue;
				saveDataScrollItems->Add(new AchievementView(achievement));
			}
		}

		// And tack on an explanation for why savestate options are not available.
		if (!achievementsAllowSavestates) {
			saveDataScrollItems->Add(new NoticeView(NoticeLevel::INFO, ac->T("Save states not available in Hardcore Mode"), ""));
		}
	}

	LinearLayout *middleColumn = nullptr;
	ViewGroup *buttonColumn = nullptr;
	if (portrait) {
		buttonColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));

		middleColumn = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, ITEM_HEIGHT, Margins(10, 10, 10, 10)));
		root_->Add(middleColumn);
		root_->Add(buttonColumn);
	} else {
		middleColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(ITEM_HEIGHT, FILL_PARENT, Margins(0, 10, 0, 15)));
		root_->Add(middleColumn);
		middleColumn->SetSpacing(0.0f);

		ViewGroup *buttonColumnScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(320, FILL_PARENT, actionMenuMargins));
		buttonColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
		buttonColumnScroll->Add(buttonColumn);
		root_->Add(buttonColumnScroll);
	}

	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);
	buttonColumn->Add(rightColumnItems);

	rightColumnItems->SetSpacing(0.0f);
	if (getUMDReplacePermit()) {
		rightColumnItems->Add(new Choice(pa->T("Switch UMD"), ImageID("I_UMD")))->OnClick.Add([=](UI::EventParams &) {
			screenManager()->push(new UmdReplaceScreen());
		});
	}

	if (!portrait) {
		Choice *continueChoice = rightColumnItems->Add(new Choice(pa->T("Continue"), ImageID("I_PLAY")));
		root_->SetDefaultFocusView(continueChoice);
		continueChoice->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
		rightColumnItems->Add(new Spacer(20.0));
	}

	if (g_paramSFO.IsValid() && g_Config.HasGameConfig(g_paramSFO.GetDiscID())) {
		rightColumnItems->Add(new Choice(pa->T("Game Settings"), ImageID("I_GEAR")))->OnClick.Handle(this, &GamePauseScreen::OnGameSettings);
	} else if (PSP_CoreParameter().fileType != IdentifiedFileType::PPSSPP_GE_DUMP) {
		rightColumnItems->Add(new Choice(pa->T("Settings"), ImageID("I_GEAR")))->OnClick.Handle(this, &GamePauseScreen::OnGameSettings);
		Choice *createGameConfig = rightColumnItems->Add(new Choice(pa->T("Create Game Config"), ImageID("I_GEAR_STAR")));
		createGameConfig->OnClick.Handle(this, &GamePauseScreen::OnCreateConfig);
		createGameConfig->SetEnabled(!bootPending_);
	}

	if (g_Config.bAchievementsEnable && Achievements::HasAchievementsOrLeaderboards()) {
		rightColumnItems->Add(new Choice(ac->T("Achievements"), ImageID("I_ACHIEVEMENT")))->OnClick.Add([&](UI::EventParams &e) {
			screenManager()->push(new RetroAchievementsListScreen(gamePath_));
		});
	}

	rightColumnItems->Add(new Choice(gr->T("Display layout & effects"), ImageID("I_DISPLAY")))->OnClick.Add([&](UI::EventParams &) -> void {
		screenManager()->push(new DisplayLayoutScreen(gamePath_));
	});
	if (g_Config.bShowTouchControls) {
		rightColumnItems->Add(new Choice(co->T("Edit touch control layout..."), ImageID("I_CONTROLLER")))->OnClick.Add([&](UI::EventParams &) -> void {
			screenManager()->push(new TouchControlLayoutScreen(gamePath_));
		});
	}

	if (g_Config.bEnableCheats && PSP_CoreParameter().fileType != IdentifiedFileType::PPSSPP_GE_DUMP) {
		rightColumnItems->Add(new Choice(pa->T("Cheats"), ImageID("I_CHEAT")))->OnClick.Add([&](UI::EventParams &e) {
			screenManager()->push(new CwCheatScreen(gamePath_));
		});
	}

	// TODO, also might be nice to show overall compat rating here?
	// Based on their platform or even cpu/gpu/config.  Would add an API for it.
	if (!portrait && Reporting::IsSupported() && g_paramSFO.GetValueString("DISC_ID").size()) {
		auto rp = GetI18NCategory(I18NCat::REPORTING);
		rightColumnItems->Add(new Choice(rp->T("ReportButton", "Report Feedback")))->OnClick.Handle(this, &GamePauseScreen::OnReportFeedback);
	}
	rightColumnItems->Add(new Spacer(20.0));

	// --pause-menu-exit always overrides the per-game setting when passed on
	// the CLI. Otherwise, when we were launched directly into a specific ROM
	// (forwarder/CLI/file-association launch), the per-game setting decides
	// which exit behavior (or none at all) is offered. A normal launch into
	// the game browser is unaffected by the setting.
	enum { PMEO_EXIT_TO_MENU = 0, PMEO_EXIT_PPSSPP = 1, PMEO_NONE = 2 };
	int effectiveExitOption = PMEO_EXIT_TO_MENU;
	if (g_Config.bPauseMenuExitsEmulator) {
		effectiveExitOption = PMEO_EXIT_PPSSPP;
	} else if (g_Config.bLoadedViaDirectLaunch) {
		effectiveExitOption = g_Config.iPauseMenuExitOption;
	}

	Choice *exit = nullptr;
	if (effectiveExitOption != PMEO_NONE) {
		if (effectiveExitOption == PMEO_EXIT_PPSSPP) {
			auto mm = GetI18NCategory(I18NCat::MAINMENU);
			exit = new Choice(mm->T("Exit"), ImageID("I_EXIT"));
		} else {
			exit = new Choice(pa->T("Exit to menu"), ImageID("I_EXIT"));
		}
	}

	if (portrait) {
		Choice *continueChoice = new Choice(pa->T("Continue"), ImageID("I_PLAY"));
		continueChoice->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
		root_->SetDefaultFocusView(continueChoice);
		if (exit) {
			UI::LinearLayout *exitRow = new UI::LinearLayout(ORIENT_HORIZONTAL, new UI::LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(0, 0, 0, 0)));
			rightColumnItems->Add(exitRow);
			exitRow->Add(exit);
			exit->ReplaceLayoutParams(new UI::LinearLayoutParams(1.0f, Gravity::G_VCENTER));
			exitRow->Add(continueChoice);
			continueChoice->ReplaceLayoutParams(new UI::LinearLayoutParams(1.0f, Gravity::G_VCENTER));
		} else {
			rightColumnItems->Add(continueChoice);
		}
	} else if (exit) {
		rightColumnItems->Add(exit);
	}

	if (exit) {
		exit->OnClick.Handle(this, &GamePauseScreen::OnExit);
		exit->SetEnabled(!bootPending_);
	}

	if (middleColumn) {
		middleColumn->SetSpacing(portrait ? 8.0f : 20.0f);
		playButton_ = middleColumn->Add(new Choice(g_Config.bRunBehindPauseMenu ? ImageID("I_PAUSE") : ImageID("I_PLAY"), new LinearLayoutParams(64, 64)));
		playButton_->OnClick.Add([=](UI::EventParams &e) {
			g_Config.bRunBehindPauseMenu = !g_Config.bRunBehindPauseMenu;
			playButton_->SetIconLeft(g_Config.bRunBehindPauseMenu ? ImageID("I_PAUSE") : ImageID("I_PLAY"));
		});

		bool mustRunBehind = MustRunBehind();
		playButton_->SetVisibility(mustRunBehind ? UI::V_GONE : UI::V_VISIBLE);

		Choice *infoButton = middleColumn->Add(new Choice(ImageID("I_INFO"), new LinearLayoutParams(64, 64)));
		infoButton->OnClick.Add([=](UI::EventParams &e) {
			screenManager()->push(new GameScreen(gamePath_, true));
		});

		if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE) {
			AddRotationPicker(screenManager(), middleColumn, false);
		}

		if (!portrait) {
			Choice *menuButton = middleColumn->Add(new Choice("", ImageID("I_THREE_DOTS"), new LinearLayoutParams(64, 64)));
			menuButton->OnClick.Add([this, menuButton, portrait](UI::EventParams &e) {
				ShowContextMenu(menuButton, portrait);
			});
		}
	} else {
		playButton_ = nullptr;
	}
}

void GamePauseScreen::ShowContextMenu(UI::View *menuButton, bool portrait) {
	using namespace UI;
	PopupCallbackScreen *contextMenu = new UI::PopupCallbackScreen([this, portrait](UI::ViewGroup *parent) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		parent->Add(new Choice(di->T("Reset")))->OnClick.Add([this](UI::EventParams &e) {
			std::string confirmMessage = GetConfirmExitMessage();
			if (!confirmMessage.empty()) {
				auto di = GetI18NCategory(I18NCat::DIALOG);
				screenManager()->push(new UI::MessagePopupScreen(di->T("Reset"), confirmMessage, di->T("Reset"), di->T("Cancel"), [=](bool result) {
					if (result) {
						System_PostUIMessage(UIMessage::REQUEST_GAME_RESET);
					}
				}));
			} else {
				System_PostUIMessage(UIMessage::REQUEST_GAME_RESET);
			}
		});

		auto pa = GetI18NCategory(I18NCat::PAUSE);

		Choice *delGameConfig = parent->Add(new Choice(pa->T("Delete Game Config")));
		delGameConfig->OnClick.Handle(this, &GamePauseScreen::OnDeleteConfig);
		delGameConfig->SetEnabled(!bootPending_);

		if (portrait) {
			// Add some other options that are removed from the main screen in portrait mode.
			if (Reporting::IsSupported() && g_paramSFO.GetValueString("DISC_ID").size()) {
				auto rp = GetI18NCategory(I18NCat::REPORTING);
				parent->Add(new Choice(rp->T("ReportButton", "Report Feedback")))->OnClick.Handle(this, &GamePauseScreen::OnReportFeedback);
			}
		}
	}, menuButton);
	screenManager()->push(contextMenu);
}

void GamePauseScreen::OnGameSettings(UI::EventParams &e) {
	std::string gameId = g_paramSFO.IsValid() ? g_paramSFO.GetDiscID() : "";
	bool hasConfig = !gameId.empty() && g_Config.HasGameConfig(gameId);
	screenManager()->push(new GameSettingsScreen(gamePath_, gameId, hasConfig));
}

void GamePauseScreen::OnState(UI::EventParams &e) {
	TriggerFinish(DR_CANCEL);
}

void GamePauseScreen::dialogFinished(const Screen *dialog, DialogResult dr) {
	std::string tag = dialog->tag();
	if (tag == "ScreenshotView" && dr == DR_OK) {
		finishNextFrame_ = true;
	} else {
		if (tag == "Game") {
			g_BackgroundAudio.SetGame(Path());
		} else if (tag != "Prompt" && tag != "ContextMenuPopup") {
			// There may have been changes to our savestates, so let's recreate.
			RecreateViews();
		}
	}
}

void GamePauseScreen::OnRequestClearState(UI::EventParams &e) {
	SaveSlotView *v = static_cast<SaveSlotView *>(e.v);
	int slot = v->GetSlot();
	Path gamePath = gamePath_;

	auto pa = GetI18NCategory(I18NCat::PAUSE);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	std::string message = ApplySafeSubstitutions(pa->T("Delete savestate in slot %1?"), StringFromFormat("%d", slot + 1));
	screenManager()->push(new UI::MessagePopupScreen(pa->T("Clear"), message, di->T("Delete"), di->T("Cancel"), [gamePath, slot](bool yes) {
		if (yes) {
			SaveState::DeleteSlot(gamePath, slot);
		}
	}));
}

void GamePauseScreen::OnScreenshotClicked(UI::EventParams &e) {
	SaveSlotView *v = static_cast<SaveSlotView *>(e.v);
	int slot = v->GetSlot();
	g_Config.iCurrentStateSlot = v->GetSlot();
	if (SaveState::HasSaveInSlot(gamePath_, slot)) {
		Path fn = v->GetScreenshotFilename();
		std::string title = v->GetScreenshotTitle();
		Screen *screen = new ScreenshotViewScreen(fn, title, v->GetSlot(), gamePath_);
		screenManager()->push(screen);
	}
}

int GetUnsavedProgressSeconds() {
	const double timeSinceSaveState = SaveState::SecondsSinceLastSavestate();
	const double timeSinceGameSave = SecondsSinceLastGameSave();

	return (int)std::min(timeSinceSaveState, timeSinceGameSave);
}

bool ShouldAskBeforeAutoSave(const Path &gamePath, int *outSlot) {
	if (!g_Config.bAutoSaveSaveStateAlwaysAsk || g_Config.iAutoSaveSaveState == 0)
		return false;

	int unsavedSeconds = GetUnsavedProgressSeconds();
	if (g_Config.iAutoSaveSaveStateAfterSeconds > 0 && unsavedSeconds >= 0 &&
	    unsavedSeconds < g_Config.iAutoSaveSaveStateAfterSeconds) {
		return false;
	}

	int slot = SaveState::ResolveAutoSaveSlot(gamePath);
	if (slot == -1)
		return false;

	if (outSlot)
		*outSlot = slot;
	return true;
}

void PerformAutoSaveNow(const Path &gamePath, int slot) {
	SaveState::SaveSlot(gamePath, slot, &AfterSaveStateAction);
	SaveState::Process(); // Force the queued save to run now, synchronously.
}

// If empty, no confirmation dialog should be shown.
std::string GetConfirmExitMessage() {
	std::string confirmMessage;

	int unsavedSeconds = GetUnsavedProgressSeconds();

	// If RAIntegration has dirty info, ask for confirmation.
	if (Achievements::RAIntegrationDirty()) {
		auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
		confirmMessage = ac->T("You have unsaved RAIntegration changes.");
		confirmMessage += '\n';
	}

	if (coreState == CORE_RUNTIME_ERROR) {
		// The game crashed, or similar. Don't bother checking for timeout or network.
		return confirmMessage;
	}

	if (IsNetworkConnected()) {
		auto nw = GetI18NCategory(I18NCat::NETWORKING);
		confirmMessage += nw->T("Network connected");
		confirmMessage += '\n';
	} else if (g_Config.iAskForExitConfirmationAfterSeconds > 0 && unsavedSeconds > g_Config.iAskForExitConfirmationAfterSeconds) {
		if (PSP_CoreParameter().fileType == IdentifiedFileType::PPSSPP_GE_DUMP) {
			// No need to ask for this type of confirmation for dumps.
			return confirmMessage;
		}
		auto di = GetI18NCategory(I18NCat::DIALOG);
		confirmMessage = ApplySafeSubstitutions(di->T("You haven't saved your progress for %1."), NiceTimeFormat((int)unsavedSeconds));
		confirmMessage += '\n';
	}

	return confirmMessage;
}

void GamePauseScreen::ProceedWithExit(bool exitsEmulator) {
	std::string confirmExitMessage = GetConfirmExitMessage();

	if (!confirmExitMessage.empty()) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		std::string_view title = di->T("Are you sure you want to exit?");
		screenManager()->push(new UI::MessagePopupScreen(title, confirmExitMessage, di->T("Exit"), di->T("Cancel"), [=](bool result) {
			if (result) {
				if (exitsEmulator) {
					System_ExitApp();
				} else {
					finishNextFrameResult_ = DR_OK;  // exit game
					finishNextFrame_ = true;
				}
			}
		}));
	} else {
		if (exitsEmulator) {
			System_ExitApp();
		} else {
			TriggerFinish(DR_OK);
		}
	}
}

void GamePauseScreen::OnExit(UI::EventParams &e) {
	// --pause-menu-exit always overrides the per-game setting; otherwise use
	// the per-game setting only when we were launched directly into a ROM.
	bool exitsEmulator = g_Config.bPauseMenuExitsEmulator ||
		(g_Config.bLoadedViaDirectLaunch && g_Config.iPauseMenuExitOption == 1 /* Exit PPSSPP */);

	// If configured, ask for explicit confirmation before auto-saving, ahead
	// of (and instead of, if they agree) the normal exit-confirmation dialog.
	int autoSaveSlot = -1;
	if (ShouldAskBeforeAutoSave(gamePath_, &autoSaveSlot)) {
		auto pa = GetI18NCategory(I18NCat::PAUSE);
		auto di = GetI18NCategory(I18NCat::DIALOG);
		bool overwriting = SaveState::HasSaveInSlot(gamePath_, autoSaveSlot);
		std::string message = ApplySafeSubstitutions(
			overwriting ? pa->T("This will overwrite savestate slot %1.") : pa->T("This will save to slot %1."),
			StringFromFormat("%d", autoSaveSlot + 1));
		Path gamePath = gamePath_;
		screenManager()->push(new UI::MessagePopupScreen(pa->T("Auto save savestate"), message, di->T("Yes"), di->T("No"), [this, gamePath, autoSaveSlot, exitsEmulator](bool yes) {
			if (yes) {
				PerformAutoSaveNow(gamePath, autoSaveSlot);
				// Progress was just saved - go straight to exiting rather than
				// re-checking/showing the "unsaved progress" exit dialog.
				if (exitsEmulator) {
					System_ExitApp();
				} else {
					finishNextFrameResult_ = DR_OK;
					finishNextFrame_ = true;
				}
			} else {
				// Don't push a new popup synchronously from within this one's
				// own finish callback - the screen stack is still mid-teardown
				// at this point, which corrupts input routing. Defer instead.
				pendingProceedWithExit_ = true;
				pendingProceedExitsEmulator_ = exitsEmulator;
			}
		}));
	} else {
		ProceedWithExit(exitsEmulator);
	}
}

void GamePauseScreen::OnReportFeedback(UI::EventParams &e) {
	screenManager()->push(new ReportScreen(gamePath_));
}

void GamePauseScreen::OnRewind(UI::EventParams &e) {
	SaveState::Rewind(&AfterSaveStateAction);

	TriggerFinish(DR_CANCEL);
}

void GamePauseScreen::OnLoadUndo(UI::EventParams &e) {
	SaveState::UndoLoad(gamePath_, &AfterSaveStateAction);

	TriggerFinish(DR_CANCEL);
}

void GamePauseScreen::OnLastSaveUndo(UI::EventParams &e) {
	SaveState::UndoLastSave(gamePath_);

	RecreateViews();
}

void GamePauseScreen::OnCreateConfig(UI::EventParams &e) {
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, GameInfoFlags::PARAM_SFO);
	if (info->Ready(GameInfoFlags::PARAM_SFO)) {
		std::string gameId = info->id;
		g_Config.CreateGameConfig(gameId);
		g_Config.SaveGameConfig(gameId, info->GetTitle());
		if (info) {
			info->hasConfig = true;
		}
		screenManager()->topScreen()->RecreateViews();
	}
}

void GamePauseScreen::OnDeleteConfig(UI::EventParams &e) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	const bool trashAvailable = System_GetPropertyBool(SYSPROP_HAS_TRASH_BIN);
	screenManager()->push(
		new UI::MessagePopupScreen(di->T("Delete"), di->T("DeleteConfirmGameConfig", "Do you really want to delete the settings for this game?"),
			trashAvailable ? di->T("Move to trash") : di->T("Delete"), di->T("Cancel"), [this](bool yes) {
		if (!yes) {
			return;
		}
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, GameInfoFlags::PARAM_SFO);
		if (info->Ready(GameInfoFlags::PARAM_SFO)) {
			g_Config.UnloadGameConfig();
			g_Config.DeleteGameConfig(info->id);
			info->hasConfig = false;
			screenManager()->RecreateAllViews();
		}
	}));
}
