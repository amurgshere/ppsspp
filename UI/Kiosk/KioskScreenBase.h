#pragma once

#include <functional>
#include <string>

#include "UI/Kiosk/KioskCommon.h"
#include "UI/Kiosk/KioskGameIdResolutionPoller.h"
#include "UI/Kiosk/KioskHintBar.h"

// Shared skeleton for the Kiosk carousel/grid screens: both own a hint bar,
// route unhandled input to it, poll for asynchronously-resolved game ids, and
// draw the same landscape-only ground shade. Base is UIBaseScreen for the
// carousel (a top-level screen) or UIBaseDialogScreen for the grid (a dialog
// screen that Escape/Circle pops), which is also what determines whether
// key() falls back to UIScreen::key() or UIDialogScreen::key().
template<typename Base>
class KioskScreenBase : public Base {
public:
	using Base::Base;

	bool key(const KeyInput &key) override {
		return Kiosk::RouteKeyToHintBar(Base::key(key), key, hintBar_);
	}

protected:
	bool TouchDisablesFocusMovement() const override { return false; }

	void DrawBackground(UIContext &ui) override {
		if (!portrait_)
			Kiosk::DrawLandscapeGroundShade(ui);
	}

	// Game Info and Settings are both pushed as overlays on top of these screens, which stay
	// alive underneath - so the only reason to disturb their tiles (and thus focus/scroll) is
	// if the closed dialog actually changed which ROMs exist (onGameDeleted). Otherwise, just
	// restore focus (onRestoreFocus): pushing a dialog moves the framework's single global
	// focused-view pointer to the dialog's own views, and closing it doesn't hand that back on
	// its own, even though the tile's own sticky highlight (independent of real focus) never
	// stopped showing it as selected. Restoring focus re-triggers OnFocusedTileChanged, which
	// already refreshes the hint bar label - covering the case where a Settings visit toggled
	// whether this game now has a game-specific config.
	void HandleTileScreenDialogFinished(const Screen *dialog, DialogResult result,
			const std::function<void()> &onGameDeleted, const std::function<void()> &onRestoreFocus) {
		if (Kiosk::WasGameDeleted(dialog, result))
			onGameDeleted();
		else
			onRestoreFocus();
	}

	void RefreshSettingsHintLabel(UI::Event &onSettingsEvent, const std::string &gameId) {
		if (hintBar_)
			hintBar_->UpdateEntryLabel(&onSettingsEvent, Kiosk::SettingsHintLabel(gameId));
	}

	KioskHintBar *hintBar_ = nullptr;
	bool portrait_ = false;
	KioskGameIdResolutionPoller idPoller_;
};
