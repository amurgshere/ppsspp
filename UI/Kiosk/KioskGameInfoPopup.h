#pragma once

#include "ppsspp_config.h"
#include "UI/BaseScreens.h"
#include "UI/GameInfoCache.h"
#include "UI/Kiosk/KioskScreenBase.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

// Kiosk-styled game info overlay: icon, title, region/id badges, playtime,
// size, up to 5 save-state slots, and a bottom hint bar for actions (Launch,
// Game Settings, Add to Home, Back) - matches the mockup's card layout.
class KioskGameInfoPopup : public KioskScreenBase<UIBaseDialogScreen> {
public:
	explicit KioskGameInfoPopup(const Path &gamePath);

	void update() override;
	void resized() override { RecreateViews(); }
	const char *tag() const override { return "KioskGameInfoPopup"; }

protected:
	void CreateViews() override;

private:
	void BuildHintBar();
	void RefreshHintBarEntries();
	void OnLaunch(UI::EventParams &e);
	void OnGameSettings(UI::EventParams &e);
	void OnBack(UI::EventParams &e);
	void OnAddToHome(UI::EventParams &e);
	void OnDeleteRom(UI::EventParams &e);
	void OnSaveSlotClick(int slot);
	void OnInGameSaveClick();

	float ComputeCardWidth(const Bounds &screenBounds, bool portrait) const;
	float LandscapeIconWidth(float availableW) const;
	float ComputeBodyWidth(bool portrait, float cardW) const;
	UI::LinearLayout *BuildCardBody(UI::ViewGroup *card, bool portrait, float cardW, float cardH);
	void AddTitleBadgesStats(UI::LinearLayout *body);
	UI::View *BuildSaveSlotGrid(UI::LinearLayout *body, float bodyW);

	GameInfoFlags knownFlags_ = GameInfoFlags::EMPTY;
	// Snapshot of knownFlags_ as of the last CreateViews() - update() compares against this
	// each frame and recreates the views once GetInfo() resolves more flags asynchronously,
	// so fields like size (and the icon/region/homescreen badges) appear without the user
	// having to close and reopen the popup.
	GameInfoFlags lastCreateViewsFlags_ = GameInfoFlags::EMPTY;
	std::shared_ptr<GameInfo> info_;
	// Set by OnDeleteRom()'s confirm callback, consumed by update() - see the
	// comment at the set site for why this can't just call TriggerFinish() directly.
	bool pendingDeleteFinish_ = false;

	// BuildHintBar() runs on every CreateViews() (including the async-refresh recreate above
	// and window resize), but these Event objects live for the screen's whole lifetime and
	// assert if Add() is called on one twice - so the bindings themselves are one-time only.
	bool hintBarEventsBound_ = false;
	UI::Event OnLaunchEvent;
	UI::Event OnSettingsEvent;
	UI::Event OnBackEvent;
	UI::Event OnDeleteRomEvent;

	// Keeps the save-slot grid's KioskFocusTracker alive for as long as this screen is -
	// each save slot's OnHighlight event holds a raw pointer to it, so it must outlive
	// BuildSaveSlotGrid() rather than being destroyed when that function returns.
	// Type-erased to avoid pulling KioskSaveSlot.h into this widely-included header.
	std::shared_ptr<void> saveSlotFocusKeepAlive_;

#if PPSSPP_PLATFORM(SWITCH)
	bool forwarderInstalled_ = false;
	UI::Event OnAddToHomeEvent;
#endif
};
