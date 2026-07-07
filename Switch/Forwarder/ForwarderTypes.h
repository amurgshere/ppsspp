#pragma once

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <cstdint>
#include <string>
#include <vector>

namespace Forwarder {

// Reserved sentinel gameId for the "bare PPSSPP, no game" forwarder installed
// from Settings > Tools, distinct from any real game's DISC_ID.
constexpr const char *kGenericForwarderGameId = "__PPSSPP_GENERIC__";

// Fixed title ID reserved for the generic forwarder so it never collides with
// a per-game derived title ID. Falls within the same reserved custom-title
// range used by DeriveTitleId (TitleIdAllocator.h) for per-game IDs.
// TEMP DEBUG: bumped from 0x0100000000AD0FEDULL to rule out a Home Menu/
// qlaunch icon-cache poisoning theory - that title ID has been repeatedly
// installed/removed across many broken debug builds, and a corrupted-icon/
// no-context-menu symptom persisted even after independently confirming (via
// hactool) that the NCA/NACP/icon content itself is now byte-correct. Revert
// to the original ID once this is confirmed/ruled out.
constexpr uint64_t kGenericForwarderTitleId = 0x0100000000AD0FEEULL;

// Author strings are part of the Title ID hash input (see
// TitleIdAllocator.h's DeriveTitleId), so both the generic and per-game
// forwarders occupy distinct, deterministic slots in the reserved title-id
// range. Treat these as effectively fixed once shipped: changing either
// changes every previously-derived Title ID for that category.
constexpr const char *kGenericForwarderAuthor = "Henrik Rydgård & Aaron Murgatroyd";
constexpr const char *kPerGameForwarderAuthor = "Emulated by PPSSPP";

// The author string is a pure function of which category of forwarder this
// is - used both when building a new InstallRequest and when re-deriving an
// existing forwarder's Title ID for a lookup/uninstall (no gameId->author
// mapping is persisted anywhere; there's nothing to keep in sync).
inline const char *ForwarderAuthorForGameId(const std::string &gameId) {
    return gameId == kGenericForwarderGameId ? kGenericForwarderAuthor : kPerGameForwarderAuthor;
}

struct InstallRequest {
    std::string gameId;        // DISC_ID for a real game, or kGenericForwarderGameId.
    std::string displayName;   // NACP title shown on the Home Menu tile.
    // Default covers the generic PPSSPP-only forwarder. Per-game forwarders
    // (SDLMain.cpp) override this to kPerGameForwarderAuthor instead, since
    // the game's actual publisher isn't recoverable from PARAM.SFO and
    // crediting PPSSPP's own authors as the "author" of someone else's game
    // is wrong.
    std::string author = kGenericForwarderAuthor;
    std::string version;       // NACP version string; empty means "use PPSSPP_GIT_VERSION".
    std::vector<uint8_t> iconPng;   // Raw PNG bytes: GameInfo icon, or PPSSPP's own icon.
    std::string nextNroPath = "/switch/ppsspp/PPSSPP_GL.nro";
    std::string gameArgv;       // ISO/EBOOT path (+ args) to boot into, or "" for a plain launch.
};

struct InstallResult {
    bool success = false;
    std::string errorMessage;
    uint64_t titleId = 0;
};

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
