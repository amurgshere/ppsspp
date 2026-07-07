#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <algorithm>
#include <cstring>

#include "Switch/Forwarder/RomfsBuilder.h"

namespace Forwarder {

namespace {

// RomFS binary format per the Switchbrew/libnx spec, verified field-for-field
// against the reference `@tootallnate/romfs` TypeScript implementation
// (packages/romfs/src/index.ts) - notably its layout is FILE DATA FIRST (at a
// fixed offset 0x200), with the hash/metadata tables stored AFTER the data
// section, not before. An earlier version of this builder had that backwards
// (tables first, then data with a computed offset), which produced a
// structurally different (and, for the Control NCA specifically, wrong-sized)
// RomFS image compared to any real reference tool's output despite being
// internally self-consistent.
//
// This builder only ever needs to represent a single, fixed shape - one root
// directory containing a flat list of files - so the general "arbitrary tree"
// logic is simplified down to that flat case rather than implemented
// generically.

constexpr uint32_t kInvalidOffset = 0xFFFFFFFFu;
constexpr uint32_t kDirEntrySize = 0x18;
constexpr uint32_t kFileEntrySize = 0x20;
constexpr uint32_t kHeaderSize = 0x50;
constexpr uint64_t kFilePartitionOfs = 0x200;

uint32_t Align4(uint32_t v) { return (v + 3) & ~3u; }
uint64_t Align16(uint64_t v) { return (v + 0xF) & ~(uint64_t)0xF; }
uint64_t Align4_64(uint64_t v) { return (v + 3) & ~(uint64_t)3; }

// libnx's romfs_get_hash_table_count: number of buckets for a hash table
// given a number of entries.
uint32_t HashTableBucketCount(uint32_t numEntries) {
    if (numEntries < 3)
        return 3;
    if (numEntries < 19)
        return numEntries | 1;
    uint32_t count = numEntries;
    while (count % 2 == 0 || count % 3 == 0 || count % 5 == 0 ||
           count % 7 == 0 || count % 11 == 0 || count % 13 == 0 || count % 17 == 0) {
        count++;
    }
    return count;
}

// libnx's romfs path hash (a variant of a rotating-XOR hash), used by both
// the directory and file hash tables for O(1) lookup by (parent, name).
uint32_t CalcPathHash(uint32_t parent, const char *path, size_t pathLen) {
    uint32_t hash = parent ^ 123456789u;
    for (size_t i = 0; i < pathLen; i++) {
        hash = (hash >> 5) | (hash << 27);
        hash ^= (uint8_t)path[i];
    }
    return hash;
}

void AppendU32(std::vector<uint8_t> &out, uint32_t v) {
    out.insert(out.end(), (uint8_t *)&v, (uint8_t *)&v + 4);
}
void AppendU64(std::vector<uint8_t> &out, uint64_t v) {
    out.insert(out.end(), (uint8_t *)&v, (uint8_t *)&v + 8);
}

}  // namespace

std::vector<uint8_t> BuildRomfsFromFiles(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &filesIn) {
    // The real tool walks a JS object's keys via Object.keys(fs).sort() -
    // match that ordering for byte-identical output.
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files = filesIn;
    std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    const size_t n = files.size();

    // --- File data offsets within the data partition (each file's start is
    // aligned to 16 bytes; unlike the earlier reversed-layout version, the
    // final file is not itself padded - matches the reference algorithm's
    // pre-increment alignment, which only pads *before* adding each size). ---
    std::vector<uint64_t> fileDataOffsets(n);
    uint64_t dataCursor = 0;
    for (size_t i = 0; i < n; i++) {
        dataCursor = Align16(dataCursor);
        fileDataOffsets[i] = dataCursor;
        dataCursor += files[i].second.size();
    }
    uint64_t filePartitionSize = dataCursor;

    // --- File metadata table ---
    // Entry layout: u32 parentDirOffset, u32 nextSiblingFileOffset,
    //               u64 fileDataOffset, u64 fileDataSize,
    //               u32 nextFileHashOffset, u32 nameSize, char name[align4(nameSize)]
    std::vector<uint8_t> fileMetaTable;
    std::vector<uint32_t> fileMetaOffsets(n);
    uint32_t fileHashBuckets = HashTableBucketCount((uint32_t)n);
    std::vector<uint32_t> fileHashTable(fileHashBuckets, kInvalidOffset);

    for (size_t i = 0; i < n; i++) {
        fileMetaOffsets[i] = (uint32_t)fileMetaTable.size();

        AppendU32(fileMetaTable, 0);              // parentDirOffset (root is at offset 0)
        AppendU32(fileMetaTable, kInvalidOffset);  // nextSiblingFileOffset (patched below)
        AppendU64(fileMetaTable, fileDataOffsets[i]);
        AppendU64(fileMetaTable, files[i].second.size());

        uint32_t hash = CalcPathHash(0, files[i].first.data(), files[i].first.size());
        uint32_t bucket = hash % fileHashBuckets;
        AppendU32(fileMetaTable, fileHashTable[bucket]);  // nextFileHashOffset (chain)
        fileHashTable[bucket] = fileMetaOffsets[i];

        AppendU32(fileMetaTable, (uint32_t)files[i].first.size());
        size_t nameStart = fileMetaTable.size();
        fileMetaTable.insert(fileMetaTable.end(), files[i].first.begin(), files[i].first.end());
        fileMetaTable.resize(nameStart + Align4((uint32_t)files[i].first.size()), 0);
    }
    // Chain each file's nextSiblingFileOffset to the next one in sorted order
    // (field starts at byte 4 within each entry, after parentDirOffset).
    for (size_t i = 0; i + 1 < n; i++) {
        uint32_t sibling = fileMetaOffsets[i + 1];
        memcpy(fileMetaTable.data() + fileMetaOffsets[i] + 4, &sibling, 4);
    }

    // --- Directory metadata table --- (just the root directory)
    // Entry layout: u32 parentDirOffset, u32 nextSiblingDirOffset, u32 firstChildDirOffset,
    //               u32 firstFileOffset, u32 nextDirHashOffset, u32 nameSize, char name[align4]
    std::vector<uint8_t> dirMetaTable;
    uint32_t dirHashBuckets = HashTableBucketCount(1);
    std::vector<uint32_t> dirHashTable(dirHashBuckets, kInvalidOffset);

    AppendU32(dirMetaTable, 0);                                        // parentDirOffset: root is its own parent (offset 0)
    AppendU32(dirMetaTable, kInvalidOffset);                           // nextSiblingDirOffset
    AppendU32(dirMetaTable, kInvalidOffset);                           // firstChildDirOffset: no subdirectories
    AppendU32(dirMetaTable, n > 0 ? fileMetaOffsets[0] : kInvalidOffset);  // firstFileOffset
    AppendU32(dirMetaTable, kInvalidOffset);                           // nextDirHashOffset
    AppendU32(dirMetaTable, 0);                                        // nameSize: root has no name
    // Root's hash: parent = root's own offset (0), name = "" (matches the
    // reference tool's self-referential parent lookup for the root entry).
    {
        uint32_t hash = CalcPathHash(0, "", 0);
        uint32_t bucket = hash % dirHashBuckets;
        dirHashTable[bucket] = 0;  // root's own metadata offset is 0
    }

    // --- Assemble: header, then data-partition-fixed-offset layout ---
    uint64_t dirHashTableOffset = Align4_64(filePartitionSize + kFilePartitionOfs);
    uint64_t dirHashTableSize = dirHashTable.size() * 4ull;
    uint64_t dirMetaTableOffset = dirHashTableOffset + dirHashTableSize;
    uint64_t dirMetaTableSize = dirMetaTable.size();
    uint64_t fileHashTableOffset = dirMetaTableOffset + dirMetaTableSize;
    uint64_t fileHashTableSize = fileHashTable.size() * 4ull;
    uint64_t fileMetaTableOffset = fileHashTableOffset + fileHashTableSize;
    uint64_t fileMetaTableSize = fileMetaTable.size();

    std::vector<uint8_t> out;
    out.reserve((size_t)(fileMetaTableOffset + fileMetaTableSize));

    AppendU64(out, kHeaderSize);
    AppendU64(out, dirHashTableOffset);
    AppendU64(out, dirHashTableSize);
    AppendU64(out, dirMetaTableOffset);
    AppendU64(out, dirMetaTableSize);
    AppendU64(out, fileHashTableOffset);
    AppendU64(out, fileHashTableSize);
    AppendU64(out, fileMetaTableOffset);
    AppendU64(out, fileMetaTableSize);
    AppendU64(out, kFilePartitionOfs);

    // Padding from end of header to the fixed data partition offset.
    out.resize((size_t)kFilePartitionOfs, 0);

    // File data, each aligned to 16 bytes from the partition start (matches
    // fileDataOffsets computed above).
    for (size_t i = 0; i < n; i++) {
        out.resize((size_t)(kFilePartitionOfs + fileDataOffsets[i]), 0);
        out.insert(out.end(), files[i].second.begin(), files[i].second.end());
    }

    // Padding up to dirHashTableOffset, then the tables in order.
    out.resize((size_t)dirHashTableOffset, 0);
    for (uint32_t v : dirHashTable) AppendU32(out, v);
    out.insert(out.end(), dirMetaTable.begin(), dirMetaTable.end());
    for (uint32_t v : fileHashTable) AppendU32(out, v);
    out.insert(out.end(), fileMetaTable.begin(), fileMetaTable.end());

    return out;
}

std::vector<uint8_t> BuildForwarderRomfs(const std::string &nextNroPath, const std::string &gameArgv) {
    const std::string argvContent = gameArgv.empty() ? nextNroPath : gameArgv;
    const std::string pathContent = nextNroPath;

    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    files.emplace_back("nextArgv", std::vector<uint8_t>(argvContent.begin(), argvContent.end()));
    files.emplace_back("nextNroPath", std::vector<uint8_t>(pathContent.begin(), pathContent.end()));
    return BuildRomfsFromFiles(files);
}

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
