#pragma once

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <cstdint>
#include <vector>

namespace Forwarder {

// Signs `message` with RSA-2048-PSS-SHA256 using the same self-generated
// keypair hacbrewpack/the real nsp-forwarder tooling embeds (not a Nintendo
// key - see embed/RsaKeyData.h and embed/NOTICE-ISC.txt for provenance).
// Returns a 256-byte big-endian signature. The salt is randomly generated
// per call (per the PSS spec), so repeated calls on identical input produce
// different, equally-valid signatures - matching the reference tool's own
// behavior exactly.
std::vector<uint8_t> RsaPssSignSha256(const uint8_t *message, size_t messageLen);

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
