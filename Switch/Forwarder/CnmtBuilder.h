#pragma once

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <array>
#include <cstdint>
#include <vector>

#include <switch.h>

namespace Forwarder {

struct CnmtContentEntry {
    std::array<uint8_t, 32> hash;    // SHA-256 of the full NCA file.
    NcmContentId contentId;          // First 16 bytes of hash, per NCA convention.
    uint64_t size = 0;
    NcmContentType type = NcmContentType_Program;
};

// Builds a PackagedContentMeta (CNMT) buffer describing a standalone
// Application content set (Program + Control), with no patch/addon content
// metas, matching a ticketless/unsigned homebrew forwarder install.
std::vector<uint8_t> BuildApplicationCnmt(uint64_t titleId, uint32_t version,
                                          const std::vector<CnmtContentEntry> &entries);

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
