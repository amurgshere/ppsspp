#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Common/File/Path.h"

// UI-agnostic game list builder for Kiosk Mode.
// Sources data from existing subsystems only (g_recentFiles, PathBrowser,
// PlayTimeTracker), with the one exception of GetAllGames()'s own directory
// listing, which this class caches per-instance - see GetAllGames()/Invalidate().

enum class KioskSortField {
	LAST_USED,
	ALPHABETICAL,
	TIME_PLAYED,
};

enum class KioskFilter {
	ALL,
	PLAYED_AT_LEAST_ONCE,
};

struct KioskGameEntry {
	Path path;
	std::string gameId;  // for PlayTimeTracker lookups
};

class KioskGameList {
public:
	// Recent row: up to g_Config.iKioskRecentRomsCount from g_recentFiles, padded
	// alphabetically from the full known-games set (deduped by path) if there are
	// fewer than that, unless g_Config.bKioskDisplayOnlyRecentRoms is set.
	std::vector<KioskGameEntry> GetRecentRow() const;

	// Full known-games set, scanned via PathBrowser over g_Config.currentDirectory,
	// the same source MainScreen's GameBrowser uses. The scan itself blocks on a
	// background thread (PathBrowser::GetListing), so the result is cached after
	// the first call within this instance's lifetime - see Invalidate().
	std::vector<KioskGameEntry> GetAllGames() const;

	// Drops the GetAllGames() cache so the next call re-scans the directory. Call
	// after an operation that can change which files are on disk (e.g. deleting a
	// ROM via KioskGameInfoPopup) - never needed for anything that only changes
	// sort order or playtime, since those aren't cached here.
	void Invalidate() const { cachedAllGames_.reset(); }

	// Sorted/filtered view over GetAllGames(), for the "All ROMs" grid.
	std::vector<KioskGameEntry> GetSortedFiltered(KioskSortField sort, bool ascending, KioskFilter filter) const;

	// Non-blocking: returns the id GameInfoCache has already resolved for path (the
	// same async PARAM.SFO load already kicked off to fetch tile icons - see
	// KioskTileGroup::PopulateFromEntries()), or empty if that load hasn't finished
	// yet. Never reads the file directly, so repeated calls are just a cache lookup.
	static std::string ResolveGameId(const Path &path);

	// Count of GetAllGames() entries ResolveGameId() can't answer yet - cheap to poll
	// (no disk I/O beyond the directory listing itself), so callers can detect when a
	// background id resolution completes and it's worth rebuilding a sorted/filtered
	// view or a PLAYED_AT_LEAST_ONCE filter that may have missed a not-yet-resolved entry.
	int CountUnresolvedGameIds() const;

private:
	mutable std::optional<std::vector<KioskGameEntry>> cachedAllGames_;
};
