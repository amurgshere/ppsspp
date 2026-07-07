#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <cstring>

#include "Switch/Forwarder/AesPrimitives.h"

extern "C" {
#include "ext/libkirk/AES.h"
}

namespace Forwarder {

namespace {

void GfDouble(uint8_t block[16]) {
    // GF(2^128) "double" (multiply by x) per the XTS spec, used to advance
    // the tweak from one 16-byte block to the next within a sector. Standard
    // XTS treats the 128-bit tweak as little-endian (byte 0 = least
    // significant), so the shift propagates from byte 0 upward with any
    // overflow out of byte 15 folded back into byte 0 - confirmed against a
    // known-good reference NCA (a mismatched shift direction here decrypted
    // sector 1 - the single-block main header - correctly, since no
    // doubling is needed there, but corrupted every subsequent 16-byte block
    // within multi-block sectors like FS headers).
    uint8_t carryIn = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t carryOut = block[i] >> 7;
        block[i] = (uint8_t)((block[i] << 1) | carryIn);
        carryIn = carryOut;
    }
    if (carryIn) {
        block[0] ^= 0x87;
    }
}

void XorBlock(uint8_t *dst, const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 16; i++) {
        dst[i] = a[i] ^ b[i];
    }
}

}  // namespace

void Aes128EcbEncryptBlock(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
    AES_ctx ctx;
    AES_set_key(&ctx, key, 128);
    AES_encrypt(&ctx, in, out);
}

void Aes128EcbDecryptBlock(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
    AES_ctx ctx;
    AES_set_key(&ctx, key, 128);
    AES_decrypt(&ctx, in, out);
}

void Aes128CtrCrypt(const uint8_t key[16], const uint8_t counter[16], const uint8_t *in, uint8_t *out, size_t size) {
    AES_ctx ctx;
    AES_set_key(&ctx, key, 128);

    uint8_t ctr[16];
    memcpy(ctr, counter, 16);

    size_t offset = 0;
    while (offset < size) {
        uint8_t keystream[16];
        AES_encrypt(&ctx, ctr, keystream);

        size_t chunk = size - offset < 16 ? size - offset : 16;
        for (size_t i = 0; i < chunk; i++) {
            out[offset + i] = in[offset + i] ^ keystream[i];
        }
        offset += chunk;

        // Big-endian 128-bit increment.
        for (int i = 15; i >= 0; i--) {
            if (++ctr[i] != 0) {
                break;
            }
        }
    }
}

void NcaHeaderXtsCrypt(const uint8_t key1[16], const uint8_t key2[16], uint64_t sectorIndex,
                       uint8_t *data, size_t sectorCount, bool encrypt) {
    constexpr size_t kSectorSize = 0x200;
    constexpr size_t kBlocksPerSector = kSectorSize / 16;

    for (size_t s = 0; s < sectorCount; s++) {
        // Nintendo's NCA XTS tweak departs from the standard: the sector
        // number is written big-endian (not little-endian) into the 128-bit
        // tweak block before the initial AES-ECB encryption with key2.
        uint8_t tweakSeed[16] = {0};
        uint64_t sector = sectorIndex + s;
        for (int i = 0; i < 8; i++) {
            tweakSeed[15 - i] = (uint8_t)(sector >> (8 * i));
        }

        uint8_t tweak[16];
        Aes128EcbEncryptBlock(key2, tweakSeed, tweak);

        uint8_t *sectorData = data + s * kSectorSize;
        for (size_t b = 0; b < kBlocksPerSector; b++) {
            uint8_t *block = sectorData + b * 16;
            uint8_t tmp[16];
            XorBlock(tmp, block, tweak);
            if (encrypt) {
                Aes128EcbEncryptBlock(key1, tmp, tmp);
            } else {
                Aes128EcbDecryptBlock(key1, tmp, tmp);
            }
            XorBlock(block, tmp, tweak);
            GfDouble(tweak);
        }
    }
}

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
