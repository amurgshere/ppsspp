#pragma once

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <cstdint>
#include <string>

#include "Switch/Forwarder/ForwarderTypes.h"

namespace Forwarder {

// Deterministically derives a title ID from AppName+author+gameId (stable
// across calls and reboots - reinstalling a forwarder re-derives the exact
// same ID rather than allocating a new one, so there is nothing to persist).
// Falls in a reserved custom-title range chosen to avoid collision with real
// retail title IDs. The generic sentinel gameId always maps to the fixed
// kGenericForwarderTitleId instead of being hashed. `author` must be the
// same InstallRequest::author string used for the NACP (see
// ForwarderAuthorForGameId), since it's part of the hash input - per-game
// forwarders use a different author string than the generic one, so they
// occupy a distinct region of the hash space.
uint64_t DeriveTitleId(const std::string &gameId, const std::string &author);

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
