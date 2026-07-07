#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <cstring>
#include <cstdlib>
#include <ctime>
#include <vector>

#include "Switch/Forwarder/RsaSign.h"
#include "Switch/Forwarder/NcaBuilder.h"
#include "Switch/Forwarder/embed/RsaKeyData.h"

namespace Forwarder {

namespace {

// --- 2048-bit bignum arithmetic, Barrett-reduction modexp ------------------
//
// A prior version of this file did RSASP1 (signature = message^d mod n) with
// a from-scratch schoolbook-multiply + BIT-SERIAL long division for the
// modular reduction inside ModPow's square-and-multiply loop. That bit-serial
// division (one bit of the ~4096-bit numerator processed per iteration, each
// doing O(n) work) was the entire reason a single RSA-2048-PSS signature took
// ~5.9 seconds on real hardware (confirmed via Log::Forwarder DEBUG timing) -
// RSA itself was never the bottleneck, and devkitPro's switch-mbedtls build
// turned out to have its bignum module compiled out entirely (only a debug
// stub symbol, no real mbedtls_mpi_* arithmetic), so that avenue was a dead
// end. Barrett reduction replaces the division with two schoolbook multiplies
// (using a modulus-derived constant, precomputed offline since the modulus is
// fixed at compile time - see kRsaBarrettMu in RsaKeyData.h) - verified
// correct and fast (single digit milliseconds) via a standalone host-native
// test harness cross-checked against Python's arbitrary-precision pow().

constexpr int kLimbs = 64;    // 2048 bits
constexpr int kMuLimbs = 65;  // mu = floor(b^(2k)/n) is just over 2048 bits

void BytesToLimbsBE(const uint8_t *be, size_t len, uint32_t *limbs, int nlimbs) {
    memset(limbs, 0, sizeof(uint32_t) * nlimbs);
    for (size_t i = 0; i < len; i++) {
        size_t byteFromEnd = len - 1 - i;
        int limbIdx = (int)(byteFromEnd / 4);
        int shift = (int)(byteFromEnd % 4) * 8;
        if (limbIdx < nlimbs) {
            limbs[limbIdx] |= (uint32_t)be[i] << shift;
        }
    }
}

void LimbsToBytesBE(const uint32_t *limbs, int nlimbs, uint8_t *be, size_t len) {
    memset(be, 0, len);
    for (size_t i = 0; i < len; i++) {
        size_t byteFromEnd = len - 1 - i;
        int limbIdx = (int)(byteFromEnd / 4);
        int shift = (int)(byteFromEnd % 4) * 8;
        if (limbIdx < nlimbs) {
            be[i] = (uint8_t)(limbs[limbIdx] >> shift);
        }
    }
}

// Returns true if a >= b (both n limbs, little-endian limb order).
bool GreaterEq(const uint32_t *a, const uint32_t *b, int n) {
    for (int i = n - 1; i >= 0; i--) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return true;
}

// a -= b, both n limbs. Wraps mod 2^(32n) via the borrow chain, which is
// exactly the semantics Barrett reduction's r1-r2 step needs (no need to
// check a >= b first).
void SubInPlace(uint32_t *a, const uint32_t *b, int n) {
    int64_t borrow = 0;
    for (int i = 0; i < n; i++) {
        int64_t diff = (int64_t)a[i] - (int64_t)b[i] - borrow;
        if (diff < 0) {
            diff += ((int64_t)1 << 32);
            borrow = 1;
        } else {
            borrow = 0;
        }
        a[i] = (uint32_t)diff;
    }
}

// result[na+nb] = a[na] * b[nb], little-endian limb order, schoolbook.
// Generalized (unequal lengths allowed) since Barrett reduction multiplies
// operands of varying limb counts (q1*mu, q3*modulus).
void MultiplyGeneric(const uint32_t *a, int na, const uint32_t *b, int nb, uint32_t *result) {
    memset(result, 0, sizeof(uint32_t) * (na + nb));
    for (int i = 0; i < na; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < nb; j++) {
            uint64_t prod = (uint64_t)a[i] * (uint64_t)b[j] + (uint64_t)result[i + j] + carry;
            result[i + j] = (uint32_t)prod;
            carry = prod >> 32;
        }
        int k = i + nb;
        while (carry != 0) {
            uint64_t sum = (uint64_t)result[k] + carry;
            result[k] = (uint32_t)sum;
            carry = sum >> 32;
            k++;
        }
    }
}

// Barrett reduction: remainder[kLimbs] = x[2*kLimbs] mod modulus[kLimbs],
// using a precomputed mu = floor(b^(2k) / modulus) (kMuLimbs limbs, base
// b = 2^32). Standard algorithm (see e.g. HAC 14.42): only needs two
// schoolbook multiplies plus a bounded number of trial subtractions, no
// division at all.
void BarrettReduce(const uint32_t *x, const uint32_t *modulus, const uint32_t *mu, uint32_t *remainder) {
    constexpr int k = kLimbs;

    // q1 = x >> (32*(k-1)) -> top (2k - (k-1)) = k+1 limbs of x.
    const uint32_t *q1 = x + (k - 1);
    constexpr int q1Limbs = k + 1;

    // q2 = q1 * mu
    std::vector<uint32_t> q2(q1Limbs + kMuLimbs);
    MultiplyGeneric(q1, q1Limbs, mu, kMuLimbs, q2.data());

    // q3 = q2 >> (32*(k+1))
    const uint32_t *q3 = q2.data() + (k + 1);
    int q3Limbs = (int)q2.size() - (k + 1);

    // r1 = x mod b^(k+1) -> lowest k+1 limbs of x.
    uint32_t r1[k + 1];
    memcpy(r1, x, sizeof(uint32_t) * (k + 1));

    // r2 = (q3 * modulus) mod b^(k+1) -> lowest k+1 limbs of the product.
    std::vector<uint32_t> q3m(q3Limbs + k);
    MultiplyGeneric(q3, q3Limbs, modulus, k, q3m.data());
    uint32_t r2[k + 1];
    memcpy(r2, q3m.data(), sizeof(uint32_t) * (k + 1));

    // r = r1 - r2 (mod b^(k+1)) - SubInPlace wraps correctly either way.
    uint32_t r[k + 1];
    memcpy(r, r1, sizeof(r));
    SubInPlace(r, r2, k + 1);

    // r is guaranteed to be < 3*modulus by the Barrett bound; a small,
    // bounded number of trial subtractions brings it into [0, modulus).
    uint32_t modExt[k + 1];
    memcpy(modExt, modulus, sizeof(uint32_t) * k);
    modExt[k] = 0;
    for (int i = 0; i < 3 && GreaterEq(r, modExt, k + 1); i++) {
        SubInPlace(r, modExt, k + 1);
    }
    memcpy(remainder, r, sizeof(uint32_t) * k);
}

// result[n] = (base[n] ^ exp[n]) mod modulus[n], square-and-multiply,
// MSB to LSB of the exponent, reducing via Barrett instead of division.
void ModPow(const uint32_t *base, const uint32_t *exp, const uint32_t *modulus, const uint32_t *mu,
            int n, uint32_t *result) {
    uint32_t acc[kLimbs];
    memset(acc, 0, sizeof(acc));
    acc[0] = 1;  // acc = 1

    uint32_t product[kLimbs * 2];
    bool started = false;
    for (int bitFromTop = n * 32 - 1; bitFromTop >= 0; bitFromTop--) {
        int limbIdx = bitFromTop / 32;
        int shift = bitFromTop % 32;
        if (started) {
            MultiplyGeneric(acc, n, acc, n, product);
            BarrettReduce(product, modulus, mu, acc);
        }
        uint32_t bit = (exp[limbIdx] >> shift) & 1;
        if (bit) {
            started = true;
            MultiplyGeneric(acc, n, base, n, product);
            BarrettReduce(product, modulus, mu, acc);
        }
    }
    memcpy(result, acc, sizeof(uint32_t) * n);
}

// MGF1 mask generation function (SHA-256 based), per RFC 8017 appendix B.2.1.
void Mgf1(const uint8_t *seed, size_t seedLen, uint8_t *out, size_t outLen) {
    size_t generated = 0;
    uint32_t counter = 0;
    while (generated < outLen) {
        uint8_t block[4] = {
            (uint8_t)(counter >> 24), (uint8_t)(counter >> 16),
            (uint8_t)(counter >> 8), (uint8_t)counter,
        };
        std::vector<uint8_t> input(seed, seed + seedLen);
        input.insert(input.end(), block, block + 4);
        uint8_t digest[32];
        Sha256(input.data(), input.size(), digest);
        size_t toCopy = outLen - generated < 32 ? outLen - generated : 32;
        memcpy(out + generated, digest, toCopy);
        generated += toCopy;
        counter++;
    }
}

}  // namespace

std::vector<uint8_t> RsaPssSignSha256(const uint8_t *message, size_t messageLen) {
    constexpr size_t kEmLen = 256;  // modulus size in bytes (2048 bits)
    constexpr size_t kHashLen = 32;
    constexpr size_t kSaltLen = 32;

    uint8_t mHash[kHashLen];
    Sha256(message, messageLen, mHash);

    uint8_t salt[kSaltLen];
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned)time(nullptr));
        seeded = true;
    }
    for (size_t i = 0; i < kSaltLen; i++) {
        salt[i] = (uint8_t)(rand() & 0xFF);
    }

    // M' = 8 zero bytes || mHash || salt
    std::vector<uint8_t> mPrime(8, 0);
    mPrime.insert(mPrime.end(), mHash, mHash + kHashLen);
    mPrime.insert(mPrime.end(), salt, salt + kSaltLen);
    uint8_t hHash[kHashLen];
    Sha256(mPrime.data(), mPrime.size(), hHash);

    // DB = PS (zeros) || 0x01 || salt, total length emLen - hLen - 1
    size_t dbLen = kEmLen - kHashLen - 1;
    std::vector<uint8_t> db(dbLen, 0);
    db[dbLen - kSaltLen - 1] = 0x01;
    memcpy(db.data() + dbLen - kSaltLen, salt, kSaltLen);

    std::vector<uint8_t> dbMask(dbLen);
    Mgf1(hHash, kHashLen, dbMask.data(), dbLen);

    std::vector<uint8_t> maskedDb(dbLen);
    for (size_t i = 0; i < dbLen; i++) {
        maskedDb[i] = db[i] ^ dbMask[i];
    }
    // modBits = 2048 is a multiple of 8, so emBits = modBits-1 = 2047 leaves
    // exactly 1 extra high bit in the leftmost byte that must be masked off.
    maskedDb[0] &= 0x7F;

    std::vector<uint8_t> em(kEmLen);
    memcpy(em.data(), maskedDb.data(), dbLen);
    memcpy(em.data() + dbLen, hHash, kHashLen);
    em[kEmLen - 1] = 0xBC;

    // signature = em^d mod n (RSASP1)
    uint32_t emLimbs[kLimbs], nLimbs[kLimbs], dLimbs[kLimbs], muLimbs[kMuLimbs], sigLimbs[kLimbs];
    BytesToLimbsBE(em.data(), em.size(), emLimbs, kLimbs);
    BytesToLimbsBE(kRsaModulus, sizeof(kRsaModulus), nLimbs, kLimbs);
    BytesToLimbsBE(kRsaPrivateExponent, sizeof(kRsaPrivateExponent), dLimbs, kLimbs);
    BytesToLimbsBE(kRsaBarrettMu, sizeof(kRsaBarrettMu), muLimbs, kMuLimbs);
    ModPow(emLimbs, dLimbs, nLimbs, muLimbs, kLimbs, sigLimbs);

    std::vector<uint8_t> signature(kEmLen);
    LimbsToBytesBE(sigLimbs, kLimbs, signature.data(), kEmLen);
    return signature;
}

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
