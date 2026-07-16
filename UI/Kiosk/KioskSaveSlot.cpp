#include <algorithm>
#include <cctype>
#include <cstdio>

#include "UI/Kiosk/KioskSaveSlot.h"
#include "UI/Kiosk/KioskCommon.h"
#include "UI/GameInfoCache.h"

#include "Common/UI/Context.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/StringUtils.h"
#include "Common/File/FileUtil.h"

#include "Core/Util/PathUtil.h"

namespace Kiosk {

Path SaveSlotFilename(const std::string &idVersion, int slot, const char *extension) {
	std::string filename = StringFromFormat("%s_%d.%s", idVersion.c_str(), slot, extension);
	return GetSysDirectory(DIRECTORY_SAVESTATE) / filename;
}

}  // namespace Kiosk

KioskSlotInfo KioskSaveSlotRepository::MakeRealSlotInfo(const std::string &idVersion, int slot) {
	KioskSlotInfo info;
	info.slot = slot;
	if (idVersion.empty())
		return info;
	Path fn = Kiosk::SaveSlotFilename(idVersion, slot);
	info.hasSave = File::Exists(fn);
	if (!info.hasSave)
		return info;
	File::GetModifTimeT(fn, &info.modTime);
	tm modTm;
	if (File::GetModifTime(fn, modTm)) {
		char buf[16];
		strftime(buf, sizeof(buf), "%d/%m/%Y", &modTm);
		info.dateStr = buf;
		strftime(buf, sizeof(buf), "%I:%M:%S %p", &modTm);
		info.timeStr = (buf[0] == '0') ? buf + 1 : buf;
	}
	info.screenshotPath = Kiosk::SaveSlotFilename(idVersion, slot, SaveState::SCREENSHOT_EXTENSION);
	return info;
}

bool KioskSaveSlotRepository::TryMakeInGameSaveSlotInfo(const std::string &gameId, KioskSlotInfo *out) {
	if (gameId.empty())
		return false;
	Path inGameSaveFile = SaveState::GetNewestGameSaveFile(gameId);
	if (inGameSaveFile.empty())
		return false;
	out->hasSave = true;
	out->isInGameSave = true;
	File::GetModifTimeT(inGameSaveFile, &out->modTime);
	tm modTm;
	if (File::GetModifTime(inGameSaveFile, modTm)) {
		char dateBuf[16], timeBuf[16], ampmBuf[4];
		strftime(dateBuf, sizeof(dateBuf), "%d/%m/%Y", &modTm);
		out->dateStr = dateBuf;
		strftime(timeBuf, sizeof(timeBuf), "%I:%M:%S", &modTm);
		strftime(ampmBuf, sizeof(ampmBuf), "%p", &modTm);
		for (char *c = ampmBuf; *c; c++)
			*c = (char)tolower((unsigned char)*c);
		out->timeStr = (timeBuf[0] == '0' ? timeBuf + 1 : timeBuf) + std::string(ampmBuf);
	}
	return true;
}

std::vector<KioskSlotInfo> KioskSaveSlotRepository::GatherSlots(const std::shared_ptr<GameInfo> &info) {
	std::vector<KioskSlotInfo> slots;
	std::string idVersion = info ? info->id_version : std::string();
	for (int slot = 0; slot < Kiosk::kMaxSaveSlots; slot++)
		slots.push_back(MakeRealSlotInfo(idVersion, slot));

	if (info && !info->id.empty()) {
		KioskSlotInfo inGame;
		if (TryMakeInGameSaveSlotInfo(info->id, &inGame))
			slots.push_back(inGame);
	}

	std::sort(slots.begin(), slots.end(), [](const KioskSlotInfo &a, const KioskSlotInfo &b) {
		if (a.hasSave != b.hasSave)
			return a.hasSave;
		if (a.hasSave)
			return a.modTime > b.modTime;
		return a.slot < b.slot;
	});
	return slots;
}

KioskSaveSlot *KioskSaveSlot::ForSavestate(int slotNumber, bool hasSave, const Path &screenshotPath, std::string_view dateStr, std::string_view timeStr, UI::LayoutParams *layoutParams) {
	return new KioskSaveSlot(slotNumber, hasSave, screenshotPath, dateStr, timeStr, layoutParams);
}

KioskSaveSlot *KioskSaveSlot::ForInGameSave(Draw::Texture *iconTexture, std::string_view inGameLabel, std::string_view dateStr, std::string_view timeStr, UI::LayoutParams *layoutParams) {
	return new KioskSaveSlot(iconTexture, inGameLabel, dateStr, timeStr, layoutParams);
}

KioskSaveSlot::KioskSaveSlot(int slotNumber, bool hasSave, const Path &screenshotPath, std::string_view dateStr, std::string_view timeStr, UI::LayoutParams *layoutParams)
	: KioskFocusHighlightable<UI::Choice>("", layoutParams), slotNumber_(slotNumber), hasSave_(hasSave), dateStr_(dateStr), timeStr_(timeStr) {
	if (hasSave_)
		thumbnail_ = std::make_unique<KioskSaveThumbnail>(screenshotPath);
}

KioskSaveSlot::KioskSaveSlot(Draw::Texture *iconTexture, std::string_view inGameLabel, std::string_view dateStr, std::string_view timeStr, UI::LayoutParams *layoutParams)
	: KioskFocusHighlightable<UI::Choice>("", layoutParams), hasSave_(true), isInGameSave_(true), iconTexture_(iconTexture), inGameLabel_(inGameLabel), dateStr_(dateStr), timeStr_(timeStr) {}

void KioskSaveSlot::Draw(UIContext &dc) {
	bool focused = IsHighlighted();
	UI::Style style = focused ? dc.GetTheme().itemFocusedStyle : dc.GetTheme().itemStyle;
	DrawBG(dc, style);

	Bounds thumbBounds(bounds_.x + Kiosk::kSaveSlotMargin, bounds_.y + Kiosk::kSaveSlotMargin, Kiosk::kSaveThumbW, Kiosk::kSaveThumbH);
	if (isInGameSave_ && iconTexture_) {
		float artH = thumbBounds.w / Kiosk::kTileAspect;
		float artY = thumbBounds.y + (thumbBounds.h - artH) * 0.5f;
		Kiosk::DrawGameArt(dc, iconTexture_, Bounds(thumbBounds.x, artY, thumbBounds.w, artH));
	} else if (hasSave_ && thumbnail_) {
		thumbnail_->Draw(dc, thumbBounds);
	} else {
		dc.FillRect(UI::Drawable(0x50202020), thumbBounds);
		dc.DrawTextRect("-", thumbBounds, dimColor_, ALIGN_CENTER);
	}

	Bounds textBounds(bounds_.x + Kiosk::kSaveSlotMargin, thumbBounds.y2(), Kiosk::kSaveThumbW, Kiosk::kSaveSlotTextH);
	dc.SetFontStyle(dc.GetTheme().uiFontSmall);
	if (isInGameSave_) {
		std::string label = inGameLabel_ + ": " + dateStr_ + "\n" + timeStr_;
		dc.DrawTextRect(label, textBounds, style.fgColor, ALIGN_HCENTER | ALIGN_TOP);
	} else if (hasSave_) {
		char line1[64];
		snprintf(line1, sizeof(line1), "%d. %s", slotNumber_, dateStr_.c_str());
		std::string label = std::string(line1) + "\n" + timeStr_;
		dc.DrawTextRect(label, textBounds, style.fgColor, ALIGN_HCENTER | ALIGN_TOP);
	} else {
		char label[16];
		snprintf(label, sizeof(label), "%d.", slotNumber_);
		dc.DrawTextRect(label, textBounds, dimColor_, ALIGN_HCENTER | ALIGN_TOP);
	}
}

void KioskSaveSlot::DeviceLost() {
	UI::Choice::DeviceLost();
	if (thumbnail_)
		thumbnail_->DeviceLost();
}
