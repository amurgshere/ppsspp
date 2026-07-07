#pragma once

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <cstdint>
#include <cstddef>

namespace Forwarder {

// Thin wrapper around ext/libkirk's plain Rijndael block cipher (already
// linked into every PPSSPP target for PSP KIRK crypto), used here to build
// the couple of AES primitives NCA construction needs. No key material is
// hardcoded anywhere in this file - callers supply keys sourced elsewhere
// (see KeyProvider.h).

// Single 16-byte AES-128 ECB block encrypt/decrypt.
void Aes128EcbEncryptBlock(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
void Aes128EcbDecryptBlock(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

// AES-128-CTR, arbitrary length. counter is a 16-byte big-endian value
// incremented once per 16-byte block, matching the NCA section-body CTR
// convention. Encrypt and decrypt are the same operation.
void Aes128CtrCrypt(const uint8_t key[16], const uint8_t counter[16], const uint8_t *in, uint8_t *out, size_t size);

// Nintendo's NCA header encryption: AES-XTS-128 with a non-standard
// (big-endian) tweak, operating on whole 0x200-byte sectors. `data` must be
// sectorCount * 0x200 bytes; sectorIndex is the absolute sector number of
// data[0] (e.g. 0 for the main header's first half, 2 for the first FS
// header), matching Nintendo's per-NCA sector numbering.
void NcaHeaderXtsCrypt(const uint8_t key1[16], const uint8_t key2[16], uint64_t sectorIndex,
                       uint8_t *data, size_t sectorCount, bool encrypt);

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
