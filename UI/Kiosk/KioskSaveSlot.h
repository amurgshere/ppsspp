#pragma once

#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "UI/Kiosk/KioskFocusTracker.h"
#include "UI/Kiosk/KioskSaveThumbnail.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/File/Path.h"
#include "Core/SaveState.h"

class GameInfo;
namespace Draw {
class Texture;
}

namespace Kiosk {

constexpr float kSaveSlotTextH = 58.0f;
constexpr float kSaveSlotMargin = 4.0f;
constexpr float kSaveGridSpacing = 12.0f;

// SaveState::HasSaveInSlot/GenerateSaveSlotFilename resolve the save filename from the
// globally active game's PARAM.SFO, not a passed-in gameFilename - only valid while that
// specific game is already booted. The kiosk popup inspects games that aren't running, so
// slot paths are built here from GameInfo's own per-path id_version instead.
Path SaveSlotFilename(const std::string &idVersion, int slot, const char *extension = SaveState::STATE_EXTENSION);

}  // namespace Kiosk

// Resolved info for one save slot: whether it has a save, and (if so) when it was made and
// where its screenshot lives - either a numbered savestate slot or the special in-game-save
// entry (isInGameSave, slot left at its default).
struct KioskSlotInfo {
	int slot = 0;
	bool hasSave = false;
	bool isInGameSave = false;
	time_t modTime = 0;
	std::string dateStr;
	std::string timeStr;
	Path screenshotPath;
};

// Looks up every save slot (savestates + the PSP's own in-game save, if any) for a game,
// sorted newest-first with empty slots last.
class KioskSaveSlotRepository {
public:
	static std::vector<KioskSlotInfo> GatherSlots(const std::shared_ptr<GameInfo> &info);

private:
	static KioskSlotInfo MakeRealSlotInfo(const std::string &idVersion, int slot);
	static bool TryMakeInGameSaveSlotInfo(const std::string &gameId, KioskSlotInfo *out);
};

// One tile in the game-info popup's save-state grid: either a numbered savestate slot
// (possibly empty) or the special "In-game" slot showing the PSP's own native save data.
class KioskSaveSlot : public KioskFocusHighlightable<UI::Choice> {
public:
	static KioskSaveSlot *ForSavestate(int slotNumber, bool hasSave, const Path &screenshotPath, std::string_view dateStr, std::string_view timeStr, UI::LayoutParams *layoutParams);
	static KioskSaveSlot *ForInGameSave(Draw::Texture *iconTexture, std::string_view inGameLabel, std::string_view dateStr, std::string_view timeStr, UI::LayoutParams *layoutParams);

	void Draw(UIContext &dc) override;
	void DeviceLost() override;

private:
	KioskSaveSlot(int slotNumber, bool hasSave, const Path &screenshotPath, std::string_view dateStr, std::string_view timeStr, UI::LayoutParams *layoutParams);
	KioskSaveSlot(Draw::Texture *iconTexture, std::string_view inGameLabel, std::string_view dateStr, std::string_view timeStr, UI::LayoutParams *layoutParams);

	int slotNumber_ = 0;
	bool hasSave_ = false;
	bool isInGameSave_ = false;
	Draw::Texture *iconTexture_ = nullptr;
	std::string inGameLabel_;
	std::string dateStr_;
	std::string timeStr_;
	std::unique_ptr<KioskSaveThumbnail> thumbnail_;
	static constexpr uint32_t dimColor_ = 0x80FFFFFF;
};
