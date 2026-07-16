#include <algorithm>
#include <set>
#include <unordered_map>

#include "Common/Common.h"
#include "UI/Kiosk/KioskGameList.h"
#include "UI/Kiosk/KioskCommon.h"
#include "UI/GameInfoCache.h"

#include "Common/File/PathBrowser.h"
#include "Core/Config.h"
#include "Core/Util/RecentFiles.h"

// hasFlags reflects what GameInfoCache has actually finished, independent of whether
// the resolved id came back empty (e.g. a corrupt file) - callers that need to know
// "is this still loading" (CountUnresolvedGameIds) must check this, not id.empty(),
// or a permanently-unresolvable entry would make them poll forever.
//
// Requests ICON here too, even though only the id is read back, so this never becomes
// a second, narrower GetInfo() request racing the tile's own PARAM_SFO|ICON request for
// the same path (see KioskTileGroup's PrefetchTileIcons/KioskTile::Draw()). GameInfoCache
// dedupes by path but not by which caller's request "wins" the underlying worker item -
// two concurrent work items for the same path can run before either has determined the
// file's type, so the second one's flag-specific load (e.g. icon decode) silently comes
// up empty and gets marked ready anyway, permanently skipping that data for this path.
// Requesting the same superset of flags everywhere collapses all these calls onto a single
// work item per path regardless of call order, so the race can't happen at all.
static bool GameIdReady(const Path &path, std::string *outId) {
	GameInfoFlags hasFlags;
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, path, GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON, &hasFlags);
	bool ready = hasFlags & GameInfoFlags::PARAM_SFO;
	if (outId)
		*outId = ready ? info->id : std::string();
	return ready;
}

static bool PathVisualLess(const KioskGameEntry &a, const KioskGameEntry &b) {
	return a.path.ToVisualString() < b.path.ToVisualString();
}

std::string KioskGameList::ResolveGameId(const Path &path) {
	std::string id;
	GameIdReady(path, &id);
	return id;
}

int KioskGameList::CountUnresolvedGameIds() const {
	int unresolved = 0;
	for (const auto &entry : GetAllGames()) {
		if (!GameIdReady(entry.path, nullptr))
			unresolved++;
	}
	return unresolved;
}

std::vector<KioskGameEntry> KioskGameList::GetAllGames() const {
	if (cachedAllGames_)
		return *cachedAllGames_;

	std::vector<KioskGameEntry> result;
	if (g_Config.currentDirectory.empty())
		return result;

	PathBrowser browser;
	browser.SetPath(Path(g_Config.currentDirectory));

	std::vector<File::FileInfo> fileInfo;
	// Same filter GameBrowser uses to enumerate playable ROMs/homebrew.
	browser.GetListing(fileInfo, "iso:cso:chd:pbp:elf:prx:ppdmp:");

	result.reserve(fileInfo.size());
	for (const auto &file : fileInfo) {
		if (file.isDirectory)
			continue;
		result.push_back(KioskGameEntry{ Path(file.fullName), "" });
	}

	std::sort(result.begin(), result.end(), PathVisualLess);
	cachedAllGames_ = result;
	return result;
}

std::vector<KioskGameEntry> KioskGameList::GetRecentRow() const {
	std::vector<KioskGameEntry> result;
	std::set<std::string> seen;
	const int maxTiles = g_Config.iKioskRecentRomsCount;

	for (const auto &filename : g_recentFiles.GetRecentFiles()) {
		if ((int)result.size() >= maxTiles)
			break;
		Path p(filename);
		std::string key = p.ToVisualString();
		if (seen.count(key))
			continue;
		seen.insert(key);
		result.push_back(KioskGameEntry{ p, ResolveGameId(p) });
	}

	if (!g_Config.bKioskDisplayOnlyRecentRoms && (int)result.size() < maxTiles) {
		for (const auto &entry : GetAllGames()) {
			if ((int)result.size() >= maxTiles)
				break;
			std::string key = entry.path.ToVisualString();
			if (seen.count(key))
				continue;
			seen.insert(key);
			result.push_back(KioskGameEntry{ entry.path, ResolveGameId(entry.path) });
		}
	}

	return result;
}

std::vector<KioskGameEntry> KioskGameList::GetSortedFiltered(KioskSortField sort, bool ascending, KioskFilter filter) const {
	std::vector<KioskGameEntry> games = GetAllGames();

	for (auto &entry : games) {
		if (entry.gameId.empty())
			entry.gameId = ResolveGameId(entry.path);
	}

	if (filter == KioskFilter::PLAYED_AT_LEAST_ONCE) {
		std::vector<KioskGameEntry> filtered;
		for (const auto &entry : games) {
			std::string str;
			if (g_Config.TimeTracker().GetPlayedTimeString(entry.gameId, &str)) {
				filtered.push_back(entry);
			}
		}
		games = filtered;
	}

	// LAST_USED/TIME_PLAYED are genuinely different metrics (when a game was last
	// opened vs. how long it's been played in total) - PlayTimeTracker::GetPlayTime()
	// exposes both directly, so look each entry's stats up once (keyed by path, so
	// the lookup stays correct as std::sort below reorders games) rather than using
	// last-opened as a stand-in for total playtime.
	std::unordered_map<std::string, PlayTimeTracker::PlayTime> playTimeByPath;
	for (const auto &entry : games) {
		PlayTimeTracker::PlayTime pt{};
		if (!entry.gameId.empty())
			g_Config.TimeTracker().GetPlayTime(entry.gameId, &pt);
		playTimeByPath[entry.path.ToVisualString()] = pt;
	}
	auto playTimeOf = [&](const KioskGameEntry &e) -> const PlayTimeTracker::PlayTime & {
		return playTimeByPath.at(e.path.ToVisualString());
	};

	switch (sort) {
	case KioskSortField::ALPHABETICAL:
		std::sort(games.begin(), games.end(), PathVisualLess);
		break;
	case KioskSortField::LAST_USED:
		// Unreversed result is oldest-first, matching every other field's
		// ascending-native baseline (so "descending", the default, shows the
		// most-recently-opened game first, as a user expects from "Last Used").
		std::sort(games.begin(), games.end(), [&](const KioskGameEntry &a, const KioskGameEntry &b) {
			uint64_t ta = playTimeOf(a).lastTimePlayed, tb = playTimeOf(b).lastTimePlayed;
			if (ta != tb)
				return ta < tb;
			return PathVisualLess(a, b);
		});
		break;
	case KioskSortField::TIME_PLAYED:
		// Unreversed result is longest-first here (opposite native polarity from
		// Last Used, by design), so "descending", the default, shows the
		// shortest total playtime first.
		std::sort(games.begin(), games.end(), [&](const KioskGameEntry &a, const KioskGameEntry &b) {
			int pa = playTimeOf(a).totalTimePlayed, pb = playTimeOf(b).totalTimePlayed;
			if (pa != pb)
				return pa > pb;
			return PathVisualLess(a, b);
		});
		break;
	}

	// ALPHABETICAL's unreversed sort (A-Z) is already what "descending" (the
	// default, down arrow) should show, so it needs the opposite reverse
	// condition from LAST_USED/TIME_PLAYED, whose unreversed baselines are the
	// other way around (oldest-first / longest-first respectively - see the
	// comments above) precisely so descending reverses *them* into what a user
	// expects instead.
	bool needsReverse = (sort == KioskSortField::ALPHABETICAL) ? ascending : !ascending;
	if (needsReverse)
		std::reverse(games.begin(), games.end());

	return games;
}
