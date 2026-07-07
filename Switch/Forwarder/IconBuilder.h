#pragma once

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <cstdint>
#include <vector>

namespace Forwarder {

// Decodes an arbitrary-size PNG and encodes a 256x256 baseline JPEG suitable
// for a Switch Control NCA's icon section.
//
// isPspGameIcon selects the compositing style:
//  - true:  the source is a PSP game's ~16:9 icon (GameInfo::icon). Stretching
//           that directly to a square tile looks distorted, so instead it's
//           letterboxed into the top portion of the square canvas with a
//           small bitmap "PSP" wordmark underneath, on a black background.
//  - false: the source is already square (PPSSPP's own bundled icon, used for
//           the generic/no-game forwarder) and is resized to fill the tile
//           directly, no compositing.
//
// Returns an empty vector on failure.
std::vector<uint8_t> BuildForwarderIcon(const uint8_t *pngData, size_t pngSize, bool isPspGameIcon);

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
