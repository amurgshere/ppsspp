#pragma once

#include <string>
#include <vector>

#include "Common/UI/ViewGroup.h"

#include "UI/Kiosk/KioskGameList.h"
#include "UI/Kiosk/KioskTileGroup.h"

// Shared "tile group wrapped in some scrolling container" contract for
// KioskCarouselView (LinearLayout wrapping its own internal ScrollView) and
// KioskTileGridView (a ScrollView directly): both own a KioskTileGroup and
// forward the same focus/navigation queries to it.
template<typename Base>
class KioskTileContainerView : public Base {
public:
	using Base::Base;

	KioskTile *FocusedTile() const { return tiles_.FocusedTile(); }
	Path FocusedGamePath() const { return tiles_.FocusedGamePath(); }
	std::string FocusedGameId(const std::vector<KioskGameEntry> &entries) const { return tiles_.FocusedGameId(entries); }
	void RestoreFocus() { tiles_.RestoreFocus(); }

	bool IsInTopRow(const KioskTile *tile) const { return tiles_.IsInTopRow(tile); }
	bool IsInBottomRow(const KioskTile *tile) const { return tiles_.IsInBottomRow(tile); }
	bool IsInLastColumn(const KioskTile *tile) const { return tiles_.IsInLastColumn(tile); }

protected:
	KioskTileGroup tiles_;
};
