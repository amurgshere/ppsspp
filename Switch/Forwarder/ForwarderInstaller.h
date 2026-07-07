#pragma once

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include "Switch/Forwarder/ForwarderTypes.h"

namespace Forwarder {

InstallResult BuildAndInstallForwarder(const InstallRequest &req);
bool UninstallForwarder(const std::string &gameId, std::string *errorMessage);
bool IsForwarderInstalled(const std::string &gameId, uint64_t *outTitleId = nullptr);

// Called once at startup, from main()'s own argv[0] - the path this NRO was
// actually launched from - so newly-created forwarders point at wherever the
// user really placed PPSSPP_GL.nro, instead of always assuming the
// documented default install location. Path should be bare (no "sdmc:"
// prefix - that's added back at the point of use) matching InstallRequest::
// nextNroPath's own convention. A no-op if path is empty.
void SetRunningNroPath(const std::string &path);

// Returns the path recorded by SetRunningNroPath, or "" if never called
// (e.g. not launched as an NRO at all, or argv[0] was unavailable).
std::string GetRunningNroPath();

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
