#pragma once

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <cstdint>
#include <string>

namespace Forwarder {

// NCA header/key-area encryption needs raw AES key bytes (AES-XTS and
// AES-ECB primitives), which libnx's `spl` service does not expose - spl
// only offers sealed hardware-keyslot operations (CTR-mode crypt against a
// key that never leaves the secure engine), not a general "give me
// AES-ECB(key, block)" primitive. That makes a pure spl-only implementation
// infeasible for this specific part of NCA construction.
//
// Fallback (pre-approved): read the user's own /switch/prod.keys from the
// SD card at runtime, exactly like DBI/hactool do. This file is never
// embedded in source, never shipped in any build artifact, and is only
// ever read from the end user's own SD card on their own console - the
// same trust boundary as any other homebrew installer. If the file is
// absent, forwarder installation simply fails with a clear error rather
// than silently degrading.
//
// Only ever needs two specific entries (header_key, key_area_key_application_00)
// out of the ~230 lines a real prod.keys contains, so Load() does a single
// streaming pass and only keeps bytes for those two - no vector-of-all-entries
// table, to keep memory use (and the number of small heap allocations during
// the scan) to an absolute minimum.
class KeyProvider {
public:
    // Scans /switch/prod.keys (or the given path, for tests) for header_key
    // and key_area_key_application_00 only. Returns false if the file
    // doesn't exist or is unreadable; a missing/malformed individual key is
    // not an error here, GetKey() reports that.
    bool Load(const std::string &path = "/switch/prod.keys");

    // Looks up header_key (32 bytes) or key_area_key_application_00 (16
    // bytes). Returns false if that key wasn't found/valid in the file, or
    // if expectedSize doesn't match what Load() parsed for it.
    bool GetKey(const std::string &name, uint8_t *out, size_t expectedSize) const;

private:
    bool haveHeaderKey_ = false;
    uint8_t headerKey_[32] = {};
    bool haveKaek_ = false;
    uint8_t kaek_[16] = {};
};

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
