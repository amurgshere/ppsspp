#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "Switch/Forwarder/NcaBuilder.h"
#include "Switch/Forwarder/AesPrimitives.h"
#include "Switch/Forwarder/KeyProvider.h"
#include "Switch/Forwarder/RomfsBuilder.h"
#include "Switch/Forwarder/RsaSign.h"

#include "Common/Log.h"

#include <switch/crypto/sha256.h>

// Debug toggle to test whether the Program NCA's RSA-2048-PSS header
// signature is actually required for a forwarder to work under CFW (which
// normally disables signature verification). MUST default to 0 (signing
// enabled) - this exists purely for one-off diagnostic builds, never for a
// release build, so an accidental release with signing skipped can't ship.
#define FORWARDER_SKIP_NCA_SIGNING 0

namespace Forwarder {

void Sha256(const uint8_t *data, size_t size, uint8_t out[32]) {
    sha256CalculateHash(out, data, size);
}

namespace {

void WriteU64LE(std::vector<uint8_t> &buf, size_t offset, uint64_t value) {
    memcpy(buf.data() + offset, &value, 8);
}

uint32_t ReadU32LE(const uint8_t *data, size_t offset) {
    uint32_t v;
    memcpy(&v, data + offset, 4);
    return v;
}

}  // namespace

std::vector<uint8_t> PatchHbloaderNpdmTitleId(const uint8_t *npdmData, size_t npdmSize, uint64_t titleId) {
    std::vector<uint8_t> out(npdmData, npdmData + npdmSize);

    if (npdmSize < 0x7C || memcmp(out.data(), "META", 4) != 0) {
        ERROR_LOG(Log::Forwarder, "embedded main.npdm has unexpected header, refusing to patch");
        return {};
    }

    // Offsets verified directly against the vendored embed/hbl_main.npdm
    // binary (see NcaBuilder.h comment / embed/README.md), not assumed.
    uint32_t aci0Offset = ReadU32LE(out.data(), 0x70);
    uint32_t acidOffset = ReadU32LE(out.data(), 0x78);

    if ((uint64_t)aci0Offset + 0x18 > npdmSize || (uint64_t)acidOffset + 0x220 > npdmSize ||
        memcmp(out.data() + aci0Offset, "ACI0", 4) != 0) {
        ERROR_LOG(Log::Forwarder, "main.npdm ACI0/ACID offsets look invalid, refusing to patch");
        return {};
    }

    WriteU64LE(out, aci0Offset + 0x10, titleId);          // ACI0.program_id
    // ACID.program_id_min/max (acidOffset+0x210/+0x218) are deliberately left
    // untouched, matching a known-good reference forwarder's main.npdm
    // exactly (confirmed via byte-diff: the reference never patches these,
    // only ACI0.program_id, which is the field NCM/ns actually reads) - an
    // earlier version of this code patched the ACID range to titleId too,
    // which is functionally harmless (ACID signature verification is
    // disabled under CFW) but was an unnecessary, unverified departure from
    // the reference's exact bytes.
    //
    // The ACID blob's own RSA-2048 signature (acidOffset+0x00..0x100, i.e.
    // absolute file offset 384-639 in the vendored template) is a genuine,
    // valid signature over the *original* unmodified ACID content baked into
    // the real nx-hbloader release build - since we no longer alter any
    // ACID-covered bytes (only ACI0, a separate unsigned blob), this
    // signature is a fixed constant, not per-title data, and our own vendored
    // template's copy of it was zeroed out at some point. Restoring the real
    // bytes (extracted from a known-good reference forwarder's main.npdm)
    // makes this section byte-identical instead of a zeroed placeholder.
    static const uint8_t kAcidFixedSignature[0x100] = {
        0xbd, 0x54, 0x73, 0xb7, 0xef, 0x26, 0x13, 0xba, 0x04, 0xe1, 0x19, 0x26,
        0x4a, 0x1d, 0xf0, 0xb3, 0x80, 0x86, 0x94, 0x18, 0xfb, 0xba, 0x11, 0xe4,
        0x7f, 0x00, 0xa9, 0x3c, 0x5b, 0x27, 0xe1, 0x33, 0x55, 0x74, 0xb4, 0x68,
        0x61, 0x86, 0x35, 0xee, 0x34, 0x18, 0x59, 0x3b, 0x5c, 0x39, 0x83, 0xcc,
        0x70, 0x7c, 0x70, 0x52, 0x98, 0x09, 0xca, 0xca, 0x46, 0x37, 0xc4, 0x06,
        0x5c, 0x49, 0x09, 0xa9, 0x8f, 0x23, 0x20, 0xbb, 0xf6, 0x78, 0xed, 0x23,
        0x04, 0x6b, 0x60, 0xec, 0x1a, 0xf6, 0x69, 0xf5, 0x01, 0xa7, 0xaf, 0xf1,
        0x04, 0xe3, 0x13, 0xd9, 0x19, 0x58, 0x55, 0x7e, 0x87, 0xe1, 0xad, 0x54,
        0x03, 0x5f, 0x47, 0xce, 0x67, 0x27, 0xf9, 0x3d, 0x61, 0x74, 0x3c, 0x12,
        0xea, 0x80, 0x58, 0xa6, 0x2f, 0x2b, 0x25, 0x29, 0xb4, 0xfa, 0xaf, 0xb2,
        0x07, 0x7e, 0x1d, 0xb9, 0xe3, 0x64, 0x56, 0xc9, 0x38, 0x78, 0xa6, 0xe3,
        0x08, 0xd3, 0x4a, 0x16, 0x2f, 0x97, 0x83, 0x23, 0x41, 0x8b, 0x8d, 0x5d,
        0xe7, 0xb4, 0x8f, 0x0a, 0xb9, 0x1c, 0x9b, 0xff, 0x6d, 0x91, 0xa8, 0x11,
        0xa2, 0xb1, 0x3c, 0xbc, 0xb7, 0x05, 0x3d, 0xc5, 0xdc, 0x60, 0xe8, 0xdd,
        0x5c, 0x7d, 0xcb, 0xe1, 0x74, 0xd7, 0xab, 0xbb, 0x31, 0xc7, 0x2b, 0x40,
        0x23, 0xae, 0x9e, 0xad, 0xf4, 0x9c, 0xe1, 0x7b, 0xa7, 0x92, 0x82, 0xe2,
        0x7d, 0xc6, 0xdf, 0x30, 0xe0, 0x77, 0x82, 0x31, 0x82, 0xa1, 0x0b, 0x3a,
        0x19, 0xf6, 0xa2, 0x01, 0x4d, 0xd3, 0xc3, 0x17, 0xb0, 0x43, 0xb8, 0x5d,
        0xa2, 0xab, 0xe3, 0xe4, 0x69, 0xbd, 0x57, 0x21, 0xea, 0xcd, 0xe0, 0xc5,
        0xe6, 0x65, 0x13, 0x96, 0x67, 0x9d, 0xd8, 0x8e, 0xa6, 0xe0, 0x42, 0xf1,
        0x6d, 0x5d, 0x5b, 0xd2, 0x55, 0x20, 0xb9, 0x1e, 0x59, 0x13, 0x3c, 0x17,
        0xc2, 0x25, 0x56, 0xc7,
    };
    // acidOffset points to the ACID public-key modulus (0x100 bytes); the
    // signature itself follows immediately at acidOffset+0x100 (confirmed by
    // locating exactly where the reference's real signature bytes actually
    // live via byte-diff, which was 0x100 further in than initially assumed).
    if (acidOffset + 0x200 <= npdmSize) {
        memcpy(out.data() + acidOffset + 0x100, kAcidFixedSignature, sizeof(kAcidFixedSignature));
    }
    return out;
}

std::vector<uint8_t> BuildPfs0(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &files) {
    struct Entry { uint64_t dataOffset; uint64_t size; uint32_t nameTableOffset; };
    std::vector<Entry> entries(files.size());

    std::string nameTable;
    std::vector<uint32_t> nameOffsets(files.size());
    for (size_t i = 0; i < files.size(); i++) {
        nameOffsets[i] = (uint32_t)nameTable.size();
        nameTable += files[i].first;
        nameTable.push_back('\0');
    }

    uint64_t dataCursor = 0;
    std::vector<uint64_t> dataOffsets(files.size());
    for (size_t i = 0; i < files.size(); i++) {
        dataOffsets[i] = dataCursor;
        dataCursor += files[i].second.size();
    }

    constexpr size_t kHeaderSize = 0x10;
    constexpr size_t kEntrySize = 0x18;
    constexpr size_t kStringTableAlignment = 0x20;
    uint64_t nameTableOffset = kHeaderSize + kEntrySize * files.size();
    // The string table is padded to a 0x20-byte boundary (matching real
    // hacbrewpack/nca-writer output, confirmed via hexdump against a
    // known-good reference forwarder) - the header's own nameTableSize field
    // reflects this padded size, and file data offsets are derived from that
    // same padded end, so header and layout stay mutually consistent (an
    // earlier version of this code padded only the on-disk region while
    // leaving the header field at the unpadded size, which disagreed with
    // itself and corrupted every extracted file's leading bytes; the fix
    // here is to pad the SIZE FIELD itself, not just the bytes on disk).
    nameTable.resize((nameTable.size() + kStringTableAlignment - 1) & ~(kStringTableAlignment - 1), '\0');
    uint64_t dataStart = nameTableOffset + nameTable.size();

    std::vector<uint8_t> out;
    out.resize((size_t)dataStart);

    memcpy(out.data(), "PFS0", 4);
    uint32_t numFiles = (uint32_t)files.size();
    uint32_t nameTableSize = (uint32_t)nameTable.size();
    memcpy(out.data() + 4, &numFiles, 4);
    memcpy(out.data() + 8, &nameTableSize, 4);
    // out.data()+12: reserved, already zero.

    for (size_t i = 0; i < files.size(); i++) {
        size_t entryOff = kHeaderSize + kEntrySize * i;
        uint64_t off = dataOffsets[i];
        uint64_t size = files[i].second.size();
        uint32_t nameOff = nameOffsets[i];
        memcpy(out.data() + entryOff, &off, 8);
        memcpy(out.data() + entryOff + 8, &size, 8);
        memcpy(out.data() + entryOff + 16, &nameOff, 4);
        // +20: reserved, already zero.
    }
    memcpy(out.data() + nameTableOffset, nameTable.data(), nameTable.size());

    for (size_t i = 0; i < files.size(); i++) {
        out.resize((size_t)(dataStart + dataOffsets[i]));
        out.insert(out.end(), files[i].second.begin(), files[i].second.end());
    }

    return out;
}

// --- NCA assembly -----------------------------------------------------
//
// Layout (all offsets confirmed against the user's locally-decrypted
// reference NSP, see project memory):
//   0x000-0x100  header RSA-PSS signature #1 (zero - unsigned content)
//   0x100-0x200  header RSA-PSS signature #2 (zero - unsigned content)
//   0x200-0x400  main header fields, magic "NCA3" at relative +0x00
//   0x400-0xC00  4x 0x200-byte FS headers (one per content section, unused
//                slots left zeroed)
//   0xC00-...    section data (ExeFS/RomFS/etc), each individually aligned
//
// Encryption: the main header (bytes 0x200-0x400) and each in-use FS header
// (0x400-0x600, 0x600-0x800, ...) are AES-XTS-128 encrypted in-place with
// Nintendo's "header_key" (a fixed, PUBLIC system key - not tied to any
// specific title), using Nintendo's non-standard big-endian tweak (see
// AesPrimitives.h). The 4 key-area slots at header offset 0x300 are each
// separately AES-128-ECB encrypted with "key_area_key_application_00".
//
// Neither key is title-specific or secret in the sense of DRM (they gate
// nothing on their own - real per-title protection comes from rights_id/
// tickets, which forwarder content deliberately has none of), but deriving
// them still requires raw key bytes that libnx's `spl` service has no API
// to hand back (spl only exposes sealed hardware-keyslot CTR operations,
// not a general AES-ECB/XTS primitive) - see KeyProvider.h for why this
// falls back to reading the user's own /switch/prod.keys at runtime.
//
// Section crypto: content is registered ticketless/unsigned, so section
// bodies use NcaEncryptionType_None (crypto_type=0) - only the header and
// key area above are ever encrypted. This avoids needing AES-CTR body
// encryption entirely for section data.
//
// Verified: HierarchicalSha256 (PFS0-style) section layout - hash table
// region is a fixed, mostly-unused 0x200 bytes, data starts at relative
// +0x200, section size = alignUp(0x200 + dataSize, 0x200), masterHash =
// SHA256(data).
//
// NOT yet hardware/hex-verified: the IVFC (HierarchicalIntegrity) wrapper
// used for the Program NCA's RomFS section below is a best-effort
// reconstruction from the public Switchbrew NCA format docs (6 levels,
// 0x4000 hash block size, cascading SHA-256 tables) - flagged in project
// memory as the single highest-uncertainty piece of this feature pending
// real-hardware / byte-level testing.

#pragma pack(push, 1)
struct NcaFsEntry {
    uint32_t startOffsetBlocks;  // in 0x200-byte blocks, relative to file start
    uint32_t endOffsetBlocks;
    uint8_t reserved[8];
};

struct NcaHeader {
    uint8_t headerSig1[0x100];
    uint8_t headerSig2[0x100];
    char magic[4];
    uint8_t distributionType;
    uint8_t contentType;
    uint8_t keyGenerationOld;
    uint8_t keyAreaEncryptionKeyIndex;
    uint64_t contentSize;
    uint64_t programId;
    uint32_t contentIndex;
    uint32_t sdkAddonVersion;
    uint8_t keyGeneration;
    uint8_t signatureKeyGeneration;
    uint8_t reserved1[0xE];
    uint8_t rightsId[0x10];
    NcaFsEntry fsEntries[4];
    uint8_t fsHeaderHashes[4][0x20];
    uint8_t encryptedKeyArea[0x40];
};

struct NcaFsHeader {
    uint16_t version;
    uint8_t fsType;         // 0 = RomFs, 1 = PartitionFs
    uint8_t hashType;       // 2 = HierarchicalSha256, 3 = HierarchicalIntegrity
    uint8_t encryptionType; // 1 = None, 3 = AesCtr
    uint8_t reserved0[3];
    uint8_t hashData[0xF8];
    uint8_t patchInfo[0x40];
    uint32_t generation;
    uint32_t secureValue;
    uint8_t sparseInfo[0x30];
    uint8_t compressionInfo[0x28];
    uint8_t padding[0x60];
};
#pragma pack(pop)

static_assert(sizeof(NcaHeader) == 0x340, "NcaHeader layout mismatch");
static_assert(sizeof(NcaFsHeader) == 0x200, "NcaFsHeader layout mismatch");

constexpr size_t kNcaHeaderTotalSize = 0xC00;  // 0x400 main + 4*0x200 FS headers

// Cascading SHA-256 hash table over `data`, one 32-byte hash per blockSize
// chunk (final partial chunk is hashed as-is, not zero-padded - matches
// standard IVFC construction). Forward-declared here since it's also used
// below by BuildIvfcSection.
std::vector<uint8_t> HashBlocks(const std::vector<uint8_t> &data, size_t blockSize);

// PFS0 / HierarchicalSha256-hashed section, used for ExeFS, Control, and Meta.
// Confirmed byte-for-byte against a known-good reference forwarder NCA (built
// by nsp-forwarder.n8.io, extracted with hactool): the hash-block size for
// this section type is a fixed 0x10000 (64KB, not 0x200), the PFS0 data
// always starts at a fixed relative offset of 0x200 regardless of hash table
// size, and the FS header's master_hash is SHA-256 of the raw hash-table
// bytes stored in the section body (not SHA-256 of the PFS0 data directly -
// a "hash of the hash"). Since our PFS0 payloads are always well under 64KB,
// this always collapses to a single 32-byte hash-table entry in practice,
// but the block size must still be reported as 0x10000 for the section to
// parse correctly.
std::vector<uint8_t> BuildHierarchicalSha256Section(const std::vector<uint8_t> &pfs0Data, uint8_t masterHash[32],
                                                    uint64_t *hashTableSizeOut, size_t hashBlockSize) {
    constexpr size_t kDataOffset = 0x200;
    std::vector<uint8_t> hashTable = HashBlocks(pfs0Data, hashBlockSize);
    Sha256(hashTable.data(), hashTable.size(), masterHash);

    std::vector<uint8_t> section(kDataOffset, 0);
    memcpy(section.data(), hashTable.data(), hashTable.size());
    section.insert(section.end(), pfs0Data.begin(), pfs0Data.end());
    *hashTableSizeOut = hashTable.size();

    size_t alignedSize = (section.size() + 0x1FF) & ~(size_t)0x1FF;
    section.resize(alignedSize, 0);
    return section;
}

void FillHierarchicalSha256HashData(NcaFsHeader *fsHeader, const uint8_t masterHash[32],
                                    uint64_t hashTableSize, uint64_t dataSize, uint32_t hashBlockSize) {
    fsHeader->hashType = 2;  // HierarchicalSha256
    fsHeader->fsType = 1;    // PartitionFs
    uint8_t *h = fsHeader->hashData;
    memcpy(h, masterHash, 0x20);
    uint32_t blockSize = hashBlockSize;
    uint32_t layerCount = 2;
    memcpy(h + 0x20, &blockSize, 4);
    memcpy(h + 0x24, &layerCount, 4);
    uint64_t layer0Offset = 0, layer0Size = hashTableSize;
    uint64_t layer1Offset = 0x200, layer1Size = dataSize;
    memcpy(h + 0x28, &layer0Offset, 8);
    memcpy(h + 0x30, &layer0Size, 8);
    memcpy(h + 0x38, &layer1Offset, 8);
    memcpy(h + 0x40, &layer1Size, 8);
    // Remainder of hashData left zeroed (reserved).
}

// Cascading SHA-256 hash table over `data`, one 32-byte hash per blockSize
// chunk (final partial chunk is hashed as-is, not zero-padded - matches
// standard IVFC construction).
std::vector<uint8_t> HashBlocks(const std::vector<uint8_t> &data, size_t blockSize) {
    std::vector<uint8_t> table;
    size_t offset = 0;
    if (data.empty()) {
        uint8_t hash[32];
        Sha256(nullptr, 0, hash);
        table.insert(table.end(), hash, hash + 32);
        return table;
    }
    while (offset < data.size()) {
        size_t chunk = std::min(blockSize, data.size() - offset);
        uint8_t hash[32];
        Sha256(data.data() + offset, chunk, hash);
        table.insert(table.end(), hash, hash + 32);
        offset += chunk;
    }
    return table;
}

// Builds a best-effort IVFC (HierarchicalIntegrity) section for the tiny
// forwarder RomFS image: 5 cascading hash-table levels plus the raw data,
// all stored back-to-back in the section body (no extra padding between
// levels other than aligning the final data start to blockSize, a common
// real-world convention). See the NOT-yet-verified note above this block.
std::vector<uint8_t> BuildIvfcSection(const std::vector<uint8_t> &romFsData, uint8_t masterHash[32],
                                      std::array<std::pair<uint64_t, uint64_t>, 6> &levelOffsetsSizes) {
    constexpr size_t kBlockSize = 0x4000;

    // Each intermediate hash-table level is hashed AFTER zero-padding to a
    // full kBlockSize multiple (matching how it's actually stored on disk),
    // not over its tightly-packed raw hash bytes - confirmed against a
    // known-good reference NCA (padding-then-appending without padding
    // before hashing produced a correctly-laid-out but hash-mismatched
    // section; the parent level's hash must cover the same zero-padded bytes
    // that end up on disk).
    auto padToBlock = [&](std::vector<uint8_t> v) -> std::vector<uint8_t> {
        size_t padded = ((v.size() + kBlockSize - 1) / kBlockSize) * kBlockSize;
        if (padded == 0) padded = kBlockSize;
        v.resize(padded, 0);
        return v;
    };

    // The leaf data itself is hashed as if zero-padded to a full kBlockSize
    // multiple too (even though the actual on-disk data region keeps its
    // real, unpadded size) - same convention as every level above it.
    std::vector<uint8_t> paddedRomFsData = padToBlock(romFsData);
    std::vector<uint8_t> level4 = padToBlock(HashBlocks(paddedRomFsData, kBlockSize));
    std::vector<uint8_t> level3 = padToBlock(HashBlocks(level4, kBlockSize));
    std::vector<uint8_t> level2 = padToBlock(HashBlocks(level3, kBlockSize));
    std::vector<uint8_t> level1 = padToBlock(HashBlocks(level2, kBlockSize));
    std::vector<uint8_t> level0 = padToBlock(HashBlocks(level1, kBlockSize));
    Sha256(level0.data(), level0.size(), masterHash);

    // Each intermediate level (0-4) reserves a full kBlockSize region in the
    // file regardless of how few actual hash bytes it contains (the rest is
    // zero padding) - confirmed against a known-good reference NCA, where
    // every non-data IVFC level reports Data Size == Hash Block Size
    // verbatim, not the tightly-packed actual hash-table byte count an
    // earlier version of this function used (which made every produced
    // RomFS section fail hash verification, since level offsets/sizes
    // didn't match what a real parser expects).
    std::vector<uint8_t> section;
    auto appendLevel = [&](const std::vector<uint8_t> &lvl) {
        uint64_t off = section.size();
        std::vector<uint8_t> padded = lvl;
        padded.resize(kBlockSize, 0);
        section.insert(section.end(), padded.begin(), padded.end());
        return off;
    };

    levelOffsetsSizes[0] = {appendLevel(level0), kBlockSize};
    levelOffsetsSizes[1] = {appendLevel(level1), kBlockSize};
    levelOffsetsSizes[2] = {appendLevel(level2), kBlockSize};
    levelOffsetsSizes[3] = {appendLevel(level3), kBlockSize};
    levelOffsetsSizes[4] = {appendLevel(level4), kBlockSize};

    // Data (level 5) immediately follows - already block-aligned since every
    // preceding level is now a full kBlockSize.
    uint64_t dataStart = section.size();
    levelOffsetsSizes[5] = {dataStart, romFsData.size()};
    section.insert(section.end(), romFsData.begin(), romFsData.end());

    // Trailing alignment is to kBlockSize (0x4000), not the 0x200 media-unit
    // size an earlier version of this used - confirmed via hactool section
    // size comparison against a known-good reference forwarder (its RomFS
    // section size is always the data end rounded up to the next 0x4000
    // boundary, for both the Program NCA's tiny nextArgv romfs and the
    // Control NCA's romfs).
    size_t alignedSize = (section.size() + kBlockSize - 1) & ~(kBlockSize - 1);
    section.resize(alignedSize, 0);
    return section;
}

void FillIvfcHashData(NcaFsHeader *fsHeader, const uint8_t masterHash[32],
                       const std::array<std::pair<uint64_t, uint64_t>, 6> &levelOffsetsSizes) {
    fsHeader->hashType = 3;  // HierarchicalIntegrity (IVFC)
    fsHeader->fsType = 0;    // RomFs
    uint8_t *h = fsHeader->hashData;
    memcpy(h, "IVFC", 4);
    uint32_t magicNumber = 0x20000;
    memcpy(h + 4, &magicNumber, 4);
    uint32_t masterHashSize = 0x20;
    memcpy(h + 8, &masterHashSize, 4);
    // 7, not 6: this is "number of levels" (6 hash levels + 1 data level),
    // confirmed via the reference tool's own source
    // (packages/ivfc/src/index.ts: "num_levels = 7 (6 hash levels + 1 data
    // level)") and by directly byte-diffing a real reference NCA's decrypted
    // FS header, which showed 7 here where ours previously had 6.
    uint32_t levelCount = 7;
    memcpy(h + 12, &levelCount, 4);
    size_t cursor = 16;
    constexpr uint32_t kBlockSizeLog2 = 14;  // log2(0x4000)
    for (int i = 0; i < 6; i++) {
        uint64_t offset = levelOffsetsSizes[i].first;
        uint64_t size = levelOffsetsSizes[i].second;
        memcpy(h + cursor, &offset, 8);
        memcpy(h + cursor + 8, &size, 8);
        memcpy(h + cursor + 16, &kBlockSizeLog2, 4);
        // +20: reserved, zeroed.
        cursor += 24;
    }
    cursor += 0x20;  // signature_salt, left zeroed - unused under CFW.
    memcpy(h + cursor, masterHash, 0x20);
    // Remainder of hashData left zeroed (reserved).
}

// Assembles a complete, encrypted NCA from up to 4 already-built section
// bodies + FS headers. sections[i].first = already-hashed section bytes,
// sections[i].second = the FS header describing it (fsType/hashType/
// hashData already filled in by the caller).
BuiltNca AssembleNca(const std::vector<std::pair<std::vector<uint8_t>, NcaFsHeader>> &sections,
                     uint64_t titleId, uint8_t contentType) {
    KeyProvider keys;
    if (!keys.Load()) {
        ERROR_LOG(Log::Forwarder, "could not read /switch/prod.keys - required for NCA header/key-area "
                                "encryption (see KeyProvider.h). Forwarder install cannot proceed without it.");
        return {};
    }

    uint8_t headerKey[32];
    uint8_t kaek[16];
    if (!keys.GetKey("header_key", headerKey, 32) ||
        !keys.GetKey("key_area_key_application_00", kaek, 16)) {
        ERROR_LOG(Log::Forwarder, "/switch/prod.keys is missing header_key or "
                                "key_area_key_application_00");
        return {};
    }

    // These two 0x100-byte header signature blocks are NOT random per-file
    // RSA signatures for unsigned (Control/Meta) content - byte-diffing 3
    // real reference NCAs of different types/content (Program/Control/Meta)
    // showed headerSig1 is an IDENTICAL fixed constant across all three, and
    // headerSig2 is identical between the two *unsigned* NCAs (Control/Meta)
    // while Program's (the only *signed* type) differs - confirming these are
    // fixed placeholder blobs the real packer embeds, not zero as previously
    // assumed. Leaving them zero (the prior behavior) was a real structural
    // difference from any working reference NSP, not a functionally-inert
    // detail.
    static const uint8_t kFixedHeaderSig1[0x100] = {
        0x44, 0x99, 0x2f, 0xc0, 0x05, 0x09, 0x0c, 0x46, 0x8c, 0x47, 0xb9, 0xaa,
        0xd4, 0xe4, 0x95, 0x86, 0x48, 0x4f, 0x32, 0x7c, 0xb6, 0x68, 0xa9, 0x63,
        0x2c, 0x00, 0xc8, 0xd8, 0xdc, 0xf6, 0x3b, 0x89, 0x6d, 0x36, 0x23, 0x6a,
        0xd5, 0x5c, 0x41, 0x8e, 0x49, 0x90, 0x90, 0xed, 0xab, 0xc9, 0x80, 0x42,
        0x3d, 0x0c, 0xd1, 0x98, 0x49, 0x1a, 0x27, 0xf0, 0x83, 0x4b, 0x6e, 0x96,
        0xc4, 0xca, 0x4c, 0x4a, 0x4a, 0x08, 0x81, 0xa4, 0x99, 0xc7, 0x37, 0x92,
        0x47, 0xbf, 0x74, 0x86, 0xa8, 0x42, 0xfc, 0xc5, 0x06, 0xa0, 0xac, 0xf4,
        0xe1, 0x39, 0x4e, 0xec, 0x34, 0xd5, 0xa4, 0x1f, 0x7b, 0x20, 0x13, 0x0f,
        0x14, 0x9f, 0xc7, 0xda, 0xfc, 0x9d, 0xde, 0x00, 0xa7, 0x6b, 0x37, 0xf8,
        0x4d, 0xc9, 0x9a, 0x3d, 0x68, 0x2e, 0xea, 0x76, 0xd4, 0xaa, 0xc0, 0x7a,
        0xa6, 0xb0, 0x68, 0xaf, 0x2d, 0x71, 0xf5, 0x72, 0x2a, 0xe1, 0xdf, 0x42,
        0x91, 0x26, 0x65, 0x35, 0x53, 0xd8, 0x8a, 0x31, 0xdb, 0x9c, 0x0f, 0xff,
        0xbb, 0xde, 0x81, 0x4a, 0x79, 0xbf, 0xec, 0xf2, 0x2c, 0xcb, 0xea, 0x6c,
        0x6c, 0x26, 0x88, 0xe4, 0xc5, 0x2f, 0x0c, 0x25, 0x13, 0x7d, 0x04, 0x7e,
        0xc7, 0xc0, 0xe3, 0x0e, 0x87, 0xae, 0x75, 0x7c, 0xc3, 0x46, 0xc9, 0xd6,
        0xb8, 0x4c, 0x55, 0xa2, 0x23, 0x63, 0xf0, 0xb3, 0x2c, 0x17, 0xe9, 0x67,
        0x2a, 0xb5, 0x21, 0x3b, 0x7c, 0xb0, 0x68, 0x1f, 0x19, 0x68, 0x13, 0xbd,
        0x48, 0xc2, 0xae, 0x99, 0x00, 0xea, 0x12, 0x7a, 0x03, 0x74, 0x7e, 0x3a,
        0xe3, 0x84, 0x13, 0x09, 0xde, 0x91, 0x87, 0x11, 0x12, 0x06, 0x1a, 0x00,
        0xf7, 0x6b, 0x7b, 0xfe, 0xd1, 0x61, 0x82, 0x2d, 0x07, 0x86, 0x67, 0x22,
        0xa1, 0x04, 0x65, 0x9a, 0x22, 0x9c, 0xe5, 0x92, 0x0f, 0x98, 0x53, 0x54,
        0xa6, 0xf5, 0x53, 0xeb,
    };
    static const uint8_t kFixedHeaderSig2Unsigned[0x100] = {
        0x99, 0x6b, 0x1d, 0xf8, 0x29, 0x5a, 0xf5, 0xa8, 0xfc, 0xf1, 0x46, 0xfe,
        0x0b, 0x16, 0x45, 0x2d, 0x4b, 0xaf, 0xdf, 0x8f, 0x78, 0x21, 0x27, 0x69,
        0xc7, 0xe4, 0xa3, 0xb2, 0x5b, 0xbe, 0xec, 0x4c, 0x99, 0x12, 0x7e, 0x83,
        0xef, 0xbb, 0x45, 0xae, 0xa5, 0x48, 0xa9, 0xb3, 0xc1, 0xcf, 0xfb, 0x3c,
        0x40, 0x16, 0xbb, 0x33, 0xfc, 0x1f, 0x93, 0x57, 0x9d, 0x2b, 0x74, 0x1e,
        0x24, 0xc2, 0x53, 0xde, 0xfe, 0x1e, 0x18, 0x6b, 0x91, 0x23, 0xfd, 0xbf,
        0x22, 0x57, 0x25, 0xdf, 0x23, 0x7c, 0xc9, 0x24, 0x39, 0xb9, 0xc7, 0x05,
        0x4d, 0x1e, 0x96, 0x94, 0x53, 0x5d, 0x17, 0x8b, 0x2e, 0x16, 0x74, 0xf2,
        0x27, 0xfe, 0x26, 0x55, 0x9c, 0xb4, 0x05, 0x91, 0x5a, 0x60, 0x92, 0x0a,
        0xaa, 0xd2, 0x87, 0xd2, 0x13, 0x78, 0xff, 0x62, 0xaf, 0x56, 0xf4, 0x31,
        0x8a, 0xe5, 0x9f, 0x42, 0x8c, 0x19, 0x38, 0x31, 0x72, 0x22, 0xca, 0xc7,
        0x04, 0x3f, 0x9a, 0x48, 0x1b, 0xa6, 0x45, 0xaf, 0xd1, 0x96, 0xe2, 0x8c,
        0xdf, 0xf6, 0xf3, 0xcb, 0x34, 0xa1, 0x99, 0x9f, 0xe7, 0x30, 0x6d, 0x9d,
        0x3d, 0x0b, 0xde, 0x53, 0xc4, 0x36, 0xbd, 0xe0, 0x40, 0x1b, 0x1b, 0x55,
        0x27, 0xd3, 0xfb, 0x24, 0x10, 0xfd, 0x54, 0x2f, 0xbc, 0x83, 0xb2, 0x34,
        0x9f, 0x2e, 0xf3, 0x8a, 0xd9, 0x3e, 0xba, 0x1f, 0x6a, 0x3b, 0x78, 0xe3,
        0x0f, 0x95, 0x4b, 0x25, 0x7f, 0xfb, 0xf6, 0xbc, 0xf7, 0xca, 0x89, 0xf2,
        0xcc, 0xd4, 0x1f, 0x6e, 0x29, 0x2d, 0x27, 0x6c, 0xf3, 0x5f, 0x61, 0xff,
        0x91, 0xbd, 0xde, 0x8f, 0xd4, 0x43, 0xad, 0xe7, 0x97, 0xf8, 0xf6, 0x52,
        0x6c, 0xa2, 0x4d, 0xc9, 0x1e, 0x1a, 0xa3, 0x1c, 0x34, 0xa5, 0x5d, 0xee,
        0x41, 0xe7, 0xaa, 0xf8, 0xab, 0xaa, 0x26, 0xcd, 0x79, 0x54, 0x93, 0x8a,
        0xea, 0xb1, 0xef, 0x00,
    };

    NcaHeader header;
    memset(&header, 0, sizeof(header));
    memcpy(header.headerSig1, kFixedHeaderSig1, sizeof(kFixedHeaderSig1));
    // headerSig2: real per-content RSA-PSS signature for Program NCA (which
    // we cannot reproduce byte-for-byte without the signing private key, and
    // which is inherently randomly-salted anyway - the sole confirmed
    // unavoidable exception in this pass); the same fixed placeholder as
    // Control/Meta otherwise.
    if (contentType != 0 /* Program */) {
        memcpy(header.headerSig2, kFixedHeaderSig2Unsigned, sizeof(kFixedHeaderSig2Unsigned));
    }
    memcpy(header.magic, "NCA3", 4);
    header.distributionType = 0;  // Download
    header.contentType = contentType;
    header.keyGenerationOld = 0;
    header.keyAreaEncryptionKeyIndex = 0;  // Application
    header.programId = titleId;
    header.contentIndex = 0;
    // Matches the reference tool's hardcoded default (packages/nca/src/index.ts,
    // sdkVersion = 0x000c1100) - a fixed SDK-build-version constant, not tied
    // to titleId/content, so reusing it exactly is correct rather than 0.
    header.sdkAddonVersion = 0x000c1100;
    header.keyGeneration = 0;
    header.signatureKeyGeneration = 0;
    // rightsId left zeroed: ticketless/unsigned content.

    // Key area: real Nintendo NCAs of this generation decrypt to an
    // all-zero key in slots 0/1/3 and a repeating-0x04 key in slot 2 (see
    // project memory) - not real per-title secrets. We reproduce those same
    // plaintext values and encrypt them with the (public) application key
    // area key, so a correctly-keyed decrypt reproduces the same pattern.
    uint8_t plainKeys[4][16];
    memset(plainKeys[0], 0x00, 16);
    memset(plainKeys[1], 0x00, 16);
    memset(plainKeys[2], 0x04, 16);
    memset(plainKeys[3], 0x00, 16);
    for (int i = 0; i < 4; i++) {
        Aes128EcbEncryptBlock(kaek, plainKeys[i], header.encryptedKeyArea + i * 16);
    }

    std::vector<uint8_t> body;  // section data, appended after the 0xC00 header
    for (size_t i = 0; i < sections.size(); i++) {
        const auto &sectionData = sections[i].first;
        uint32_t startBlock = (uint32_t)((kNcaHeaderTotalSize + body.size()) / 0x200);
        body.insert(body.end(), sectionData.begin(), sectionData.end());
        uint32_t endBlock = (uint32_t)((kNcaHeaderTotalSize + body.size()) / 0x200);
        header.fsEntries[i].startOffsetBlocks = startBlock;
        header.fsEntries[i].endOffsetBlocks = endBlock;
        // Reference tool always sets this byte to 1 for in-use entries
        // (packages/nca/src/index.ts: "nca[entryOffset + 0x08] = 1; // Always
        // 1") - confirmed missing via byte-diff (this was previously left at
        // the memset-zero default).
        header.fsEntries[i].reserved[0] = 1;
    }
    header.contentSize = kNcaHeaderTotalSize + body.size();

    // FS header hashes (SHA-256 of each 0x200-byte FS header, pre-encryption).
    std::array<NcaFsHeader, 4> fsHeaders;
    for (auto &fh : fsHeaders) {
        memset(&fh, 0, sizeof(fh));
    }
    for (size_t i = 0; i < sections.size(); i++) {
        fsHeaders[i] = sections[i].second;
        fsHeaders[i].version = 2;
        fsHeaders[i].encryptionType = 1;  // None - ticketless/unsigned content.
        // The high 4 bytes of the section CTR ("generation") are the section
        // index (confirmed via the reference tool's own source comment,
        // packages/nca/src/index.ts:427, and by byte-diffing a reference
        // Program NCA whose RomFS/Logo FS headers had generation=1/2
        // respectively where ours left this field 0 for every section).
        fsHeaders[i].generation = (uint32_t)i;
        Sha256(reinterpret_cast<const uint8_t *>(&fsHeaders[i]), sizeof(NcaFsHeader), header.fsHeaderHashes[i]);
    }

    // Assemble the plaintext file, then encrypt the main header + each
    // in-use FS header in place with the XTS "header_key".
    std::vector<uint8_t> out(kNcaHeaderTotalSize + body.size());
    memcpy(out.data(), &header, sizeof(NcaHeader));
    // out.data()+sizeof(NcaHeader) .. +0x400: reserved, already zeroed.
    for (size_t i = 0; i < 4; i++) {
        memcpy(out.data() + 0x400 + i * 0x200, &fsHeaders[i], sizeof(NcaFsHeader));
    }
    memcpy(out.data() + kNcaHeaderTotalSize, body.data(), body.size());

    // Program NCA header is RSA-2048-PSS signed (matches the reference
    // tool's own behavior exactly: rsaSign(nca.subarray(0x200, 0x400)),
    // written to offset 0x100) using the same self-generated keypair
    // hacbrewpack/the reference tooling embeds (see embed/RsaKeyData.h).
    // PSS uses a fresh random salt per signature by design, so this can
    // never be made byte-identical to any specific past reference file, but
    // it is now a real, valid signature rather than left as zero.
#if FORWARDER_SKIP_NCA_SIGNING
    WARN_LOG(Log::Forwarder, "FORWARDER_SKIP_NCA_SIGNING is set, leaving Program NCA signature zeroed");
#else
    if (contentType == 0 /* Program */) {
        std::vector<uint8_t> signature = RsaPssSignSha256(out.data() + 0x200, 0x200);
        memcpy(out.data() + 0x100, signature.data(), signature.size());
    }
#endif

    const uint8_t *key1 = headerKey;
    const uint8_t *key2 = headerKey + 16;
    // Main header occupies sector 0 (the 0x200 zero signature blocks aren't
    // encrypted - only relative +0x200..+0x400, i.e. sector 1) plus the FS
    // headers at sectors 2..5.
    NcaHeaderXtsCrypt(key1, key2, 1, out.data() + 0x200, 1, true);
    // All 4 FS header slots are always XTS-encrypted, even unused ones (real
    // NCA readers, including hactool, always decrypt all 4 slots
    // unconditionally) - encrypting only in-use slots left unused slots as
    // plaintext zero, which decrypted back to non-zero garbage instead of
    // zero. Confirmed via direct byte-diff of hactool's --plaintext= dump
    // against a reference NCA (whose unused slot decrypts to all-zero).
    for (size_t i = 0; i < 4; i++) {
        NcaHeaderXtsCrypt(key1, key2, 2 + i, out.data() + 0x400 + i * 0x200, 1, true);
    }

    BuiltNca result;
    result.bytes = std::move(out);
    Sha256(result.bytes.data(), result.bytes.size(), result.hash.data());
    memcpy(result.contentId.c, result.hash.data(), 16);
    return result;
}

BuiltNca BuildProgramNca(const std::vector<uint8_t> &exeFsPfs0, const std::vector<uint8_t> &romFsImage,
                          const std::vector<uint8_t> &logoPfs0, uint64_t titleId) {
    uint8_t exeFsHash[32];
    uint64_t exeFsHashTableSize = 0;
    // Block sizes confirmed via the reference tool's own source
    // (packages/nca/src/index.ts: PFS0_EXEFS_HASH_BLOCK_SIZE=0x10000,
    // PFS0_LOGO_HASH_BLOCK_SIZE=0x1000, PFS0_META_HASH_BLOCK_SIZE=0x1000) and
    // by directly byte-diffing a reference Program NCA's decrypted FS
    // headers, which showed the Logo section using 0x1000 (13 hash entries)
    // where ours previously used 0x10000 (1 hash entry) for all PFS0
    // sections uniformly.
    constexpr uint32_t kExeFsHashBlockSize = 0x10000;
    constexpr uint32_t kLogoHashBlockSize = 0x1000;
    std::vector<uint8_t> exeFsSection = BuildHierarchicalSha256Section(exeFsPfs0, exeFsHash, &exeFsHashTableSize, kExeFsHashBlockSize);
    NcaFsHeader exeFsHeader;
    memset(&exeFsHeader, 0, sizeof(exeFsHeader));
    FillHierarchicalSha256HashData(&exeFsHeader, exeFsHash, exeFsHashTableSize, exeFsPfs0.size(), kExeFsHashBlockSize);

    uint8_t romFsHash[32];
    std::array<std::pair<uint64_t, uint64_t>, 6> ivfcLevels;
    std::vector<uint8_t> romFsSection = BuildIvfcSection(romFsImage, romFsHash, ivfcLevels);
    NcaFsHeader romFsHeader;
    memset(&romFsHeader, 0, sizeof(romFsHeader));
    FillIvfcHashData(&romFsHeader, romFsHash, ivfcLevels);

    uint8_t logoHash[32];
    uint64_t logoHashTableSize = 0;
    std::vector<uint8_t> logoSection = BuildHierarchicalSha256Section(logoPfs0, logoHash, &logoHashTableSize, kLogoHashBlockSize);
    NcaFsHeader logoHeader;
    memset(&logoHeader, 0, sizeof(logoHeader));
    FillHierarchicalSha256HashData(&logoHeader, logoHash, logoHashTableSize, logoPfs0.size(), kLogoHashBlockSize);

    std::vector<std::pair<std::vector<uint8_t>, NcaFsHeader>> sections;
    sections.emplace_back(std::move(exeFsSection), exeFsHeader);
    sections.emplace_back(std::move(romFsSection), romFsHeader);
    sections.emplace_back(std::move(logoSection), logoHeader);

    BuiltNca res = AssembleNca(sections, titleId, /*contentType=*/0 /* Program */);
    return res;
}

BuiltNca BuildControlNca(const NacpStruct &nacp, const std::vector<uint8_t> &iconJpeg, uint64_t titleId) {
    std::vector<uint8_t> nacpBytes(sizeof(NacpStruct));
    memcpy(nacpBytes.data(), &nacp, sizeof(NacpStruct));

    // A real Control NCA's content section is RomFS/IVFC, not PFS0 - confirmed
    // via hactool against a known-good reference forwarder (Section 0:
    // Partition Type RomFS). An earlier PFS0-based version of this function
    // produced a structurally-invalid Control NCA that the Switch's Home Menu
    // silently rejected (generic "?" icon, error 2123-0011 on Software
    // Information), despite every individual embedded file (NACP, icon) being
    // byte-correct.
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    files.emplace_back("control.nacp", std::move(nacpBytes));
    files.emplace_back("icon_AmericanEnglish.dat", iconJpeg);
    std::vector<uint8_t> romFsImage = BuildRomfsFromFiles(files);

    uint8_t romFsHash[32];
    std::array<std::pair<uint64_t, uint64_t>, 6> ivfcLevels;
    std::vector<uint8_t> section = BuildIvfcSection(romFsImage, romFsHash, ivfcLevels);
    NcaFsHeader fsHeader;
    memset(&fsHeader, 0, sizeof(fsHeader));
    FillIvfcHashData(&fsHeader, romFsHash, ivfcLevels);

    std::vector<std::pair<std::vector<uint8_t>, NcaFsHeader>> sections;
    sections.emplace_back(std::move(section), fsHeader);

    BuiltNca res = AssembleNca(sections, titleId, /*contentType=*/2 /* Control */);
    return res;
}

BuiltNca BuildMetaNca(const std::vector<uint8_t> &cnmtBytes, uint64_t titleId) {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    // Real Nintendo Meta NCAs name this file "Application_<titleId>.cnmt" -
    // confirmed via the reference tool's own source
    // (packages/nca/src/index.ts: "CNMT filename (e.g.,
    // 'Application_0100000000000001.cnmt')") and by directly comparing our
    // extracted CNMT's inner PFS0 filename against a known-good reference
    // NSP's, which was missing the "Application_" prefix entirely - the exact
    // basename doesn't matter for parsing (ncm reads the meta database
    // record it's given at registration time, not this filename), but we
    // match the convention for consistency with real forwarder tooling.
    char name[40];
    snprintf(name, sizeof(name), "Application_%016llx.cnmt", (unsigned long long)titleId);
    files.emplace_back(std::string(name), cnmtBytes);
    std::vector<uint8_t> pfs0 = BuildPfs0(files);

    uint8_t hash[32];
    uint64_t hashTableSize = 0;
    // Meta NCA's CNMT PFS0 also uses the 0x1000 block size (matches
    // PFS0_META_HASH_BLOCK_SIZE in the reference tool's source), not the
    // ExeFS-only 0x10000.
    constexpr uint32_t kMetaHashBlockSize = 0x1000;
    std::vector<uint8_t> section = BuildHierarchicalSha256Section(pfs0, hash, &hashTableSize, kMetaHashBlockSize);
    NcaFsHeader fsHeader;
    memset(&fsHeader, 0, sizeof(fsHeader));
    FillHierarchicalSha256HashData(&fsHeader, hash, hashTableSize, pfs0.size(), kMetaHashBlockSize);

    std::vector<std::pair<std::vector<uint8_t>, NcaFsHeader>> sections;
    sections.emplace_back(std::move(section), fsHeader);

    BuiltNca res = AssembleNca(sections, titleId, /*contentType=*/1 /* Meta */);
    return res;
}

int PeekNcaContentType(const std::vector<uint8_t> &ncaBytes) {
    if (ncaBytes.size() < 0x400)
        return -1;
    KeyProvider keys;
    if (!keys.Load())
        return -1;
    uint8_t headerKey[32];
    if (!keys.GetKey("header_key", headerKey, 32))
        return -1;
    uint8_t sector1[0x200];
    memcpy(sector1, ncaBytes.data() + 0x200, 0x200);
    NcaHeaderXtsCrypt(headerKey, headerKey + 16, 1, sector1, 1, /*encrypt=*/false);
    if (memcmp(sector1, "NCA3", 4) != 0)
        return -1;
    return sector1[5];  // relative offset 0x205 - 0x200 = 5: contentType byte.
}

uint64_t PeekNcaProgramId(const std::vector<uint8_t> &ncaBytes) {
    if (ncaBytes.size() < 0x400)
        return 0;
    KeyProvider keys;
    if (!keys.Load())
        return 0;
    uint8_t headerKey[32];
    if (!keys.GetKey("header_key", headerKey, 32))
        return 0;
    uint8_t sector1[0x200];
    memcpy(sector1, ncaBytes.data() + 0x200, 0x200);
    NcaHeaderXtsCrypt(headerKey, headerKey + 16, 1, sector1, 1, /*encrypt=*/false);
    if (memcmp(sector1, "NCA3", 4) != 0)
        return 0;
    uint64_t programId;
    memcpy(&programId, sector1 + 0x10, 8);  // relative offset 0x210 - 0x200 = 0x10.
    return programId;
}

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
