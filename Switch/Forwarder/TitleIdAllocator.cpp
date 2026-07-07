#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include "Switch/Forwarder/TitleIdAllocator.h"

namespace Forwarder {

namespace {

// Reserved custom-title-id range: 0x0100000000AD0000 - 0x0100000000ADFFFF.
// Real retail application title IDs used by Nintendo do not fall in this
// range; the generic sentinel forwarder uses a fixed ID (0x...AD0FED) at the
// top of it, per-game IDs are hashed into the remaining space below it.
constexpr uint64_t kReservedRangeBase = 0x0100000000AD0000ULL;
constexpr uint64_t kReservedRangeSize = 0xF000ULL;  // Leaves 0xAD0FED reserved above.

// FNV-1a 64-bit. Only needs to be stable across runs for the same input, not
// cryptographically strong - this just picks a slot in the reserved title-id
// range so re-adding the same game reuses the same synthesized title ID.
uint64_t Fnv1a64(const std::string &data) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (unsigned char c : data) {
        hash ^= c;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

// Per-game title-id hash input, exactly: AppName + "+" + AppAuthor + "+" +
// ParameterContext (the game's path). AppAuthor (the caller-supplied
// InstallRequest::author) must stay byte-for-byte stable (NFC precomposed
// "å", UTF-8 where applicable) since changing it changes every previously-
// derived title ID for that author.
constexpr const char *kTitleIdHashAppName = "PPSSPP";

}  // namespace

uint64_t DeriveTitleId(const std::string &gameId, const std::string &author) {
    if (gameId == kGenericForwarderGameId) {
        return kGenericForwarderTitleId;
    }

    // Stable hash over AppName+AppAuthor+ParameterContext (gameId doubles as
    // ParameterContext here - SDLMain.cpp sets req.gameId to the game's full
    // path for a per-game forwarder, and the generic sentinel is already
    // handled above, so ParameterContext is never empty at this point).
    // Fixed-content hash (not memory address/time based) so the same inputs
    // always derive the same title ID across installs/reinstalls - there is
    // no registry to keep in sync, this is the only source of truth.
    std::string hashInput = std::string(kTitleIdHashAppName) + "+" + author + "+" + gameId;
    uint64_t hash = Fnv1a64(hashInput);
    return kReservedRangeBase + (hash % kReservedRangeSize);
}

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
