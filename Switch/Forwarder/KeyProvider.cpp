#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include "Switch/Forwarder/KeyProvider.h"

#include <cstdio>
#include <cctype>
#include <cstring>

#include "Common/Log.h"

namespace Forwarder {

namespace {

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decodes exactly outSize bytes of hex from [hex, hex+hexLen). Returns false
// (leaving out untouched) if there aren't enough valid hex digits.
bool DecodeHexExact(const char *hex, size_t hexLen, uint8_t *out, size_t outSize) {
    if (hexLen < outSize * 2)
        return false;
    for (size_t i = 0; i < outSize; i++) {
        int hi = HexVal(hex[i * 2]);
        int lo = HexVal(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

}  // namespace

bool KeyProvider::Load(const std::string &path) {
    haveHeaderKey_ = false;
    haveKaek_ = false;

    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        ERROR_LOG(Log::Forwarder, "KeyProvider::Load failed to open '%s'", path.c_str());
        return false;
    }

    // Single fixed-size line buffer, reused for every line - no per-line
    // heap allocations. Real prod.keys lines (even the long RSA keypair
    // ones) are well under 2KB; anything longer is truncated (harmless,
    // since we only care about two short, fixed-size keys).
    constexpr size_t kLineBufSize = 2048;
    char lineBuf[kLineBufSize];
    size_t lineLen = 0;

    auto processLine = [&]() {
        if (lineLen == 0)
            return;
        // Find '='.
        size_t eq = (size_t)-1;
        for (size_t i = 0; i < lineLen; i++) {
            if (lineBuf[i] == '=') { eq = i; break; }
        }
        if (eq == (size_t)-1)
            return;

        size_t nameEnd = eq;
        while (nameEnd > 0 && isspace((unsigned char)lineBuf[nameEnd - 1])) nameEnd--;

        size_t valueStart = eq + 1;
        while (valueStart < lineLen && isspace((unsigned char)lineBuf[valueStart])) valueStart++;

        size_t nameLen = nameEnd;
        size_t valueLen = lineLen - valueStart;

        if (nameLen == strlen("header_key") && memcmp(lineBuf, "header_key", nameLen) == 0) {
            if (DecodeHexExact(lineBuf + valueStart, valueLen, headerKey_, sizeof(headerKey_))) {
                haveHeaderKey_ = true;
            }
        } else if (nameLen == strlen("key_area_key_application_00") &&
                   memcmp(lineBuf, "key_area_key_application_00", nameLen) == 0) {
            if (DecodeHexExact(lineBuf + valueStart, valueLen, kaek_, sizeof(kaek_))) {
                haveKaek_ = true;
            }
        }
    };

    int ch;
    size_t charCount = 0;
    while ((ch = fgetc(f)) != EOF) {
        charCount++;
        if (ch == '\n' || ch == '\r') {
            processLine();
            lineLen = 0;
        } else if (lineLen < kLineBufSize) {
            lineBuf[lineLen++] = (char)ch;
        }
        // else: line longer than kLineBufSize - silently truncated, fine
        // since neither key we care about is anywhere near that long.

        if (haveHeaderKey_ && haveKaek_) {
            break;
        }
    }
    processLine();

    fclose(f);
    if (!haveHeaderKey_ || !haveKaek_) {
        ERROR_LOG(Log::Forwarder, "KeyProvider::Load missing key(s) in '%s', haveHeaderKey=%d haveKaek=%d",
                  path.c_str(), haveHeaderKey_ ? 1 : 0, haveKaek_ ? 1 : 0);
    }
    return true;
}

bool KeyProvider::GetKey(const std::string &name, uint8_t *out, size_t expectedSize) const {
    if (name == "header_key" && haveHeaderKey_ && expectedSize == sizeof(headerKey_)) {
        memcpy(out, headerKey_, sizeof(headerKey_));
        return true;
    }
    if (name == "key_area_key_application_00" && haveKaek_ && expectedSize == sizeof(kaek_)) {
        memcpy(out, kaek_, sizeof(kaek_));
        return true;
    }
    return false;
}

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
