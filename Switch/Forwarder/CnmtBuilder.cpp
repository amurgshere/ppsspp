#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <cstring>

#include "Switch/Forwarder/CnmtBuilder.h"

namespace Forwarder {

namespace {

#pragma pack(push, 1)
// 32 bytes total - verified byte-for-byte against a known-good reference
// forwarder's CNMT via hactool/hexdump. An earlier 26-byte version of this
// struct (missing 2 reserved bytes after `attributes` and the entire
// `requiredDownloadSystemVersion` field) shifted every following offset
// (extended header, content entries) 6 bytes early, corrupting the CNMT.
struct PackagedContentMetaHeader {
    uint64_t titleId;
    uint32_t version;
    uint8_t type;                    // NcmContentMetaType_Application = 0x80
    uint8_t reserved1;
    uint16_t extendedHeaderSize;
    uint16_t contentCount;
    uint16_t contentMetaCount;
    uint8_t attributes;
    uint8_t reserved2[3];
    uint32_t requiredDownloadSystemVersion;
    uint8_t reserved3[4];
};

struct PackagedContentMetaExtendedHeaderApplication {
    uint64_t patchId;                  // titleId | 0x800, standard convention.
    uint32_t requiredSystemVersion;
    uint32_t requiredApplicationVersion;
};

struct PackagedContentInfo {
    uint8_t hash[32];
    uint8_t contentId[16];
    uint8_t size[6];                 // 48-bit little-endian size.
    uint8_t contentType;
    uint8_t idOffset;
};
#pragma pack(pop)

void WriteSize48(uint8_t out[6], uint64_t size) {
    for (int i = 0; i < 6; i++)
        out[i] = (uint8_t)((size >> (8 * i)) & 0xFF);
}

}  // namespace

std::vector<uint8_t> BuildApplicationCnmt(uint64_t titleId, uint32_t version,
                                          const std::vector<CnmtContentEntry> &entries) {
    std::vector<uint8_t> out;

    PackagedContentMetaHeader header{};
    header.titleId = titleId;
    header.version = version;
    header.type = 0x80;  // NcmContentMetaType_Application
    header.extendedHeaderSize = sizeof(PackagedContentMetaExtendedHeaderApplication);
    header.contentCount = (uint16_t)entries.size();
    header.contentMetaCount = 0;  // No patch/addon content metas for a bare forwarder.
    header.attributes = 0;
    header.requiredDownloadSystemVersion = 0;

    out.resize(sizeof(header));
    memcpy(out.data(), &header, sizeof(header));

    PackagedContentMetaExtendedHeaderApplication ext{};
    // Standard patch-ID derivation (base title ID with the 0x800 bit set) -
    // matches a known-good reference forwarder's CNMT byte-for-byte.
    ext.patchId = titleId | 0x800ULL;
    ext.requiredSystemVersion = 0;
    ext.requiredApplicationVersion = 0;
    out.insert(out.end(), (uint8_t *)&ext, (uint8_t *)&ext + sizeof(ext));

    int idx = 0;
    for (const auto &entry : entries) {
        PackagedContentInfo info{};
        memcpy(info.hash, entry.hash.data(), 32);
        memcpy(info.contentId, &entry.contentId, 16);
        WriteSize48(info.size, entry.size);
        info.contentType = (uint8_t)entry.type;
        info.idOffset = 0;
        out.insert(out.end(), (uint8_t *)&info, (uint8_t *)&info + sizeof(info));
        idx++;
    }

    // Trailing 32-byte digest field - present (as all-zero) in a known-good
    // reference forwarder's CNMT. Not verified by ncm for an unsigned/
    // ticketless forwarder install, but its absence changes the file's total
    // size and layout, so it's included for structural correctness.
    out.resize(out.size() + 32, 0);

    return out;
}

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
