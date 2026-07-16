#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Common/UI/ViewGroup.h"
#include "Common/File/Path.h"

#include "UI/Kiosk/KioskGameList.h"
#include "UI/Kiosk/KioskFocusTracker.h"
#include "UI/Kiosk/KioskTile.h"

class KioskTileGroup {
public:
	using LayoutParamsFactory = std::function<UI::LayoutParams *(size_t index, bool isAllRomsTile)>;
	using TileCallback = std::function<void(KioskTile *)>;

	// preferredFocusPath, if a tile with that game path exists in entries, is focused
	// instead of the first tile - lets a caller restore whichever game was focused
	// before a screen this group belongs to got rebuilt (e.g. after returning from
	// Game Info or Settings). Falls back to the first tile otherwise, same as before.
	void PopulateFromEntries(UI::ViewGroup *container, const std::vector<KioskGameEntry> &entries,
	                          const LayoutParamsFactory &makeLayoutParams, bool appendAllRomsTile,
	                          TileCallback onLaunch, TileCallback onHoldInfo,
	                          TileCallback onFocusedTileChanged = nullptr, bool fadeInOnLoad = true,
	                          const Path &preferredFocusPath = Path());

	// Drops the group's own bookkeeping (focused tile, tracked tile list)
	// without touching the container - call before the container's own
	// Clear() so nothing can observe a tile pointer that's about to be freed.
	void Clear();

	KioskTile *FocusedTile() const { return focus_.Current(); }
	Path FocusedGamePath() const;
	std::string FocusedGameId(const std::vector<KioskGameEntry> &entries) const;

	// Re-asserts real UI focus onto whichever tile this group still considers current,
	// without touching the tile list. Needed after a dialog (Game Info, Settings) pushed
	// on top of this group's screen closes: UI::SetFocusedView() is a single global
	// pointer, so opening that dialog moved it to the dialog's own views, and closing it
	// doesn't hand it back on its own. This group's own tile still looks selected in the
	// meantime (KioskFocusHighlightable's sticky highlight persists independently), which
	// is exactly what made the framework's focus loss easy to miss - the tile appears
	// selected right up until the next d-pad move, which starts from the framework's
	// default focus view instead.
	void RestoreFocus();

	// Bounds-based (not index/column-math-based, since GridLayout's reflow column
	// count isn't exposed) boundary checks - fail open (true) if not laid out yet,
	// so callers gating directional focus navigation on these don't trap it.
	// IsInLastColumn compares against the layout's furthest-right tile (not just the
	// last entry), since a partial final row may not reach that column itself.
	bool IsInTopRow(const KioskTile *tile) const;
	bool IsInBottomRow(const KioskTile *tile) const;
	bool IsInLastColumn(const KioskTile *tile) const;

private:
	void AddTile(UI::ViewGroup *container, KioskTile *tile, const TileCallback &onLaunch, const TileCallback &onHoldInfo);
	void FocusPreferredOrFirstTile(const Path &preferredFocusPath);
	bool IsAtEdge(const KioskTile *tile, float referenceCoord, bool compareX) const;

	std::vector<KioskTile *> tiles_;
	KioskFocusTracker<KioskTile> focus_;
	TileCallback onFocusedTileChanged_;
};
