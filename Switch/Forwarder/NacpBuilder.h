#pragma once

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <cstdint>
#include <string>

#include <switch.h>

namespace Forwarder {

struct NacpBuildParams {
    std::string title;
    std::string author = "PPSSPP Team";
    std::string version = "1.0.0";
    uint64_t titleId = 0;
};

// Populates a NacpStruct (Control NCA's control.nacp) for the forwarder title.
// Only the AmericanEnglish language slot (and a couple of common duplicates)
// is filled in; save data / DLC / play-log fields are left at zero since the
// forwarder itself owns no save data.
void BuildNacp(NacpStruct *out, const NacpBuildParams &params);

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
