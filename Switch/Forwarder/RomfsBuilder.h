#pragma once

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Forwarder {

// Builds a minimal RomFS image containing exactly two files, matching the
// convention nx-hbloader itself reads at boot to know what to chain-load:
//   /nextArgv     - the argument string; first token is the NRO path itself,
//                   remainder (if any) are the args passed to it.
//   /nextNroPath  - the NRO path, duplicated as its own file per hbloader's
//                   expected layout.
// gameArgv empty means "plain launch": nextArgv is just nextNroPath with no
// extra arguments (used for the generic, no-game forwarder).
std::vector<uint8_t> BuildForwarderRomfs(const std::string &nextNroPath, const std::string &gameArgv);

// Builds a single-directory (root only) RomFS image containing the given
// named files as direct children of root. Generalizes the same binary-layout
// logic BuildForwarderRomfs uses for its fixed two-file case - also used for
// a real Switch Control NCA's content section, which (confirmed via hactool
// against a known-good reference forwarder) is a RomFS/IVFC section
// containing "control.nacp" and "icon_AmericanEnglish.dat", not a PFS0.
std::vector<uint8_t> BuildRomfsFromFiles(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &files);

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
