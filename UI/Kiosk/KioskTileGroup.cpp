#include <algorithm>
#include <cmath>

#include "UI/Kiosk/KioskTileGroup.h"
#include "UI/GameInfoCache.h"

#include "Common/UI/Root.h"

using namespace UI;

// KioskTile::Draw() also requests the icon, but only draws (and thus only requests)
// tiles currently within the ScrollView's visible/scissor area - so without this,
// offscreen tiles wouldn't start loading their icon until scrolled into view. Kicking
// every request off up front here, in entries order, lets them all decode in the
// background from the moment the screen appears; KioskTile::Draw() still does the
// (by-then-fast) GPU upload once each tile actually becomes visible.
static void PrefetchTileIcons(const std::vector<KioskGameEntry> &entries) {
	for (const auto &entry : entries)
		g_gameInfoCache->GetInfo(nullptr, entry.path, GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON);
}

void KioskTileGroup::Clear() {
	tiles_.clear();
	focus_.Reset();
}

void KioskTileGroup::AddTile(UI::ViewGroup *container, KioskTile *tile, const TileCallback &onLaunch, const TileCallback &onHoldInfo) {
	if (onLaunch)
		tile->OnClick.Add([tile, onLaunch](UI::EventParams &e) { onLaunch(tile); });
	if (onHoldInfo)
		tile->OnHoldClick.Add([tile, onHoldInfo](UI::EventParams &e) { onHoldInfo(tile); });
	focus_.WireToHighlightEvent(tile, tile->OnHighlight, [this](KioskTile *t) {
		if (onFocusedTileChanged_)
			onFocusedTileChanged_(t);
	});
	container->Add(tile);
	tiles_.push_back(tile);
}

void KioskTileGroup::PopulateFromEntries(UI::ViewGroup *container, const std::vector<KioskGameEntry> &entries,
		const LayoutParamsFactory &makeLayoutParams, bool appendAllRomsTile,
		TileCallback onLaunch, TileCallback onHoldInfo, TileCallback onFocusedTileChanged, bool fadeInOnLoad,
		const Path &preferredFocusPath) {
	Clear();
	onFocusedTileChanged_ = std::move(onFocusedTileChanged);
	PrefetchTileIcons(entries);

	for (size_t i = 0; i < entries.size(); i++) {
		KioskTile *tile = new KioskTile(entries[i].path, makeLayoutParams(i, false));
		tile->SetFadeInOnLoad(fadeInOnLoad);
		AddTile(container, tile, onLaunch, onHoldInfo);
	}

	if (appendAllRomsTile) {
		KioskTile *allRomsTile = new KioskTile(makeLayoutParams(entries.size(), true));
		allRomsTile->SetFadeInOnLoad(fadeInOnLoad);
		AddTile(container, allRomsTile, onLaunch, onHoldInfo);
	}

	FocusPreferredOrFirstTile(preferredFocusPath);
}

void KioskTileGroup::RestoreFocus() {
	if (KioskTile *tile = focus_.Current())
		UI::SetFocusedView(tile);
}

void KioskTileGroup::FocusPreferredOrFirstTile(const Path &preferredFocusPath) {
	if (tiles_.empty())
		return;
	if (!preferredFocusPath.empty()) {
		for (KioskTile *tile : tiles_) {
			if (!tile->IsAllRoms() && tile->GamePath() == preferredFocusPath) {
				UI::SetFocusedView(tile);
				return;
			}
		}
	}
	UI::SetFocusedView(tiles_[0]);
}

Path KioskTileGroup::FocusedGamePath() const {
	KioskTile *tile = focus_.Current();
	if (!tile || tile->IsAllRoms())
		return Path();
	return tile->GamePath();
}

std::string KioskTileGroup::FocusedGameId(const std::vector<KioskGameEntry> &entries) const {
	KioskTile *tile = focus_.Current();
	if (!tile || tile->IsAllRoms())
		return std::string();
	for (const auto &entry : entries) {
		if (entry.path == tile->GamePath())
			return entry.gameId;
	}
	return std::string();
}

// Fails open (true) if not laid out yet, so callers gating directional focus
// navigation on these don't trap it.
bool KioskTileGroup::IsAtEdge(const KioskTile *tile, float referenceCoord, bool compareX) const {
	if (tiles_.empty() || !tile)
		return true;
	const Bounds &tileBounds = tile->GetBounds();
	float tileExtent = compareX ? tileBounds.w : tileBounds.h;
	if (tileExtent <= 0.0f)
		return true;
	float tileCoord = compareX ? tileBounds.x : tileBounds.y;
	return std::abs(tileCoord - referenceCoord) < 1.0f;
}

bool KioskTileGroup::IsInTopRow(const KioskTile *tile) const {
	if (tiles_.empty())
		return true;
	const Bounds &frontBounds = tiles_.front()->GetBounds();
	if (frontBounds.h <= 0.0f)
		return true;
	return IsAtEdge(tile, frontBounds.y, /* compareX = */ false);
}

bool KioskTileGroup::IsInBottomRow(const KioskTile *tile) const {
	if (tiles_.empty())
		return true;
	const Bounds &backBounds = tiles_.back()->GetBounds();
	if (backBounds.h <= 0.0f)
		return true;
	return IsAtEdge(tile, backBounds.y, /* compareX = */ false);
}

bool KioskTileGroup::IsInLastColumn(const KioskTile *tile) const {
	if (tiles_.empty() || !tile)
		return true;
	if (tile->GetBounds().w <= 0.0f)
		return true;
	float maxX = tile->GetBounds().x;
	for (const KioskTile *t : tiles_) {
		if (t->GetBounds().w > 0.0f)
			maxX = std::max(maxX, t->GetBounds().x);
	}
	return IsAtEdge(tile, maxX, /* compareX = */ true);
}
