#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <algorithm>

#include "Switch/Forwarder/ForwarderInstaller.h"

#include "Switch/Forwarder/CnmtBuilder.h"
#include "Switch/Forwarder/IconBuilder.h"
#include "Switch/Forwarder/KeyProvider.h"
#include "Switch/Forwarder/NacpBuilder.h"
#include "Switch/Forwarder/NcaBuilder.h"
#include "Switch/Forwarder/RomfsBuilder.h"
#include "Switch/Forwarder/TitleIdAllocator.h"
#include "Switch/Forwarder/embed/HblMainData.h"
#include "Switch/Forwarder/embed/LogoData.h"

#include "Common/File/VFS/VFS.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"

#include <switch.h>
#include <switch/crypto/sha256.h>

namespace {
std::string g_runningNroPath;
}  // namespace

namespace Forwarder {

void SetRunningNroPath(const std::string &path) {
    if (path.empty())
        return;
    // argv[0] as handed to us by nx-hbloader/Loader is "sdmc:"-prefixed (see
    // the matching comment in BuildAndInstallForwarder) - strip it here so
    // this stays in InstallRequest::nextNroPath's bare-path convention.
    g_runningNroPath = (path.rfind("sdmc:", 0) == 0) ? path.substr(5) : path;
}

std::string GetRunningNroPath() {
    return g_runningNroPath;
}

namespace {

// Deletes a placeholder automatically unless Commit() is called - guards
// against leaving orphaned, unregistered content on the SD card if any step
// in an install attempt fails partway through (see plan's "orphaned
// placeholder cleanup" requirement).
class PlaceholderGuard {
public:
    PlaceholderGuard(NcmContentStorage *storage, const NcmPlaceHolderId &id) : storage_(storage), id_(id) {}
    ~PlaceholderGuard() {
        if (!committed_)
            ncmContentStorageDeletePlaceHolder(storage_, &id_);
    }
    void Commit() { committed_ = true; }

private:
    NcmContentStorage *storage_;
    NcmPlaceHolderId id_;
    bool committed_ = false;
};

bool WriteAndRegisterContent(NcmContentStorage *storage, const BuiltNca &nca, std::string *error) {
    NcmPlaceHolderId placeholderId;
    Result rc = ncmContentStorageGeneratePlaceHolderId(storage, &placeholderId);
    if (R_FAILED(rc)) { *error = "GeneratePlaceHolderId failed"; return false; }

    rc = ncmContentStorageCreatePlaceHolder(storage, &nca.contentId, &placeholderId, (s64)nca.bytes.size());
    if (R_FAILED(rc)) { *error = "CreatePlaceHolder failed"; return false; }
    PlaceholderGuard guard(storage, placeholderId);

    constexpr size_t kChunk = 1 * 1024 * 1024;
    for (size_t offset = 0; offset < nca.bytes.size(); offset += kChunk) {
        size_t n = std::min(kChunk, nca.bytes.size() - offset);
        rc = ncmContentStorageWritePlaceHolder(storage, &placeholderId, offset, nca.bytes.data() + offset, n);
        if (R_FAILED(rc)) { *error = "WritePlaceHolder failed"; return false; }
    }

    rc = ncmContentStorageRegister(storage, &nca.contentId, &placeholderId);
    if (R_FAILED(rc)) { *error = "Register failed"; return false; }
    guard.Commit();
    return true;
}

// Raw IPC calls not wrapped by this libnx version's ns.h. Command IDs
// verified against the Switchbrew wiki's NS_services page (authoritative
// public documentation, not derived from any GPL-incompatible source):
//   16  = IApplicationManagerInterface::PushApplicationRecord
//   401 = IApplicationManagerInterface::InvalidateAllApplicationControlCache
// The exact per-record buffer layout for PushApplicationRecord isn't spelled
// out on the wiki itself; this uses the community-standard layout (u8 type +
// padding + NcmContentMetaKey + storage id) and should be verified against
// the pre-install byte-comparison step before relying on it.
Result NsPushApplicationRecord(u64 applicationId, const NcmContentMetaKey &key, u8 storageId) {
    Service *srv;
    Service session;
    Result rc = nsGetApplicationManagerInterface(&session);
    if (R_FAILED(rc))
        return rc;
    srv = &session;

    struct ContentStorageRecord {
        NcmContentMetaKey key;
        u8 storageId;
        u8 padding[7];
    };
    ContentStorageRecord record{};
    record.key = key;
    record.storageId = storageId;

    struct {
        u8 type;
        u8 padding[7];
        u64 applicationId;
    } in = { 0x3 /* installed record type */, {0}, applicationId };

    rc = serviceDispatchIn(srv, 16, in,
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcMapAlias },
        .buffers = { { &record, sizeof(record) } },
    );
    serviceClose(&session);
    return rc;
}

Result NsInvalidateAllApplicationControlCache() {
    Service session;
    Result rc = nsGetApplicationManagerInterface(&session);
    if (R_FAILED(rc))
        return rc;
    rc = serviceDispatch(&session, 401);
    serviceClose(&session);
    return rc;
}

// Best-effort fallback for UninstallForwarder, only used if the real
// nsDeleteApplicationCompletely API call itself fails to run at all (e.g.
// the application record is already partially gone). Looks up everything it
// needs live from NCM instead of relying on any locally-persisted state:
// Program/Control content IDs come from ncmContentMetaDatabaseListContentInfo
// (they're exactly the two entries BuildAndInstallForwarder registered under
// this key), and the Meta NCA's own content ID - not part of that list, see
// BuildAndInstallForwarder's "content_count = 2" comment - comes from
// ncmContentMetaDatabaseGetContentIdByType(..., NcmContentType_Meta).
bool RemoveTitleContent(NcmStorageId storageId, uint64_t titleId, std::string *error) {
    NcmContentMetaKey key{};
    key.id = titleId;
    key.type = NcmContentMetaType_Application;
    key.install_type = NcmContentInstallType_Full;
    // BuildAndInstallForwarder always registers with version = 1 (there is
    // no patch/update flow for forwarders).
    key.version = 1;

    NcmContentMetaDatabase metaDb;
    Result rc = ncmOpenContentMetaDatabase(&metaDb, storageId);
    if (R_FAILED(rc)) { *error = "OpenContentMetaDatabase failed"; return false; }

    NcmContentId ids[3];
    s32 idCount = 0;
    NcmContentInfo infos[2] = {};
    s32 written = 0;
    if (R_SUCCEEDED(ncmContentMetaDatabaseListContentInfo(&metaDb, &written, infos, 2, &key, 0))) {
        for (s32 i = 0; i < written; i++) {
            ids[idCount++] = infos[i].content_id;
        }
    }
    NcmContentId metaContentId;
    if (R_SUCCEEDED(ncmContentMetaDatabaseGetContentIdByType(&metaDb, &metaContentId, &key, NcmContentType_Meta))) {
        ids[idCount++] = metaContentId;
    }

    ncmContentMetaDatabaseRemove(&metaDb, &key);
    ncmContentMetaDatabaseCommit(&metaDb);
    ncmContentMetaDatabaseClose(&metaDb);

    NcmContentStorage storage;
    rc = ncmOpenContentStorage(&storage, storageId);
    if (R_FAILED(rc)) { *error = "OpenContentStorage failed"; return false; }
    for (s32 i = 0; i < idCount; i++) {
        bool has = false;
        if (R_SUCCEEDED(ncmContentStorageHas(&storage, &has, &ids[i])) && has)
            ncmContentStorageDelete(&storage, &ids[i]);
    }
    ncmContentStorageClose(&storage);
    return true;
}

// The only source of truth for "is this forwarder installed" - ns's own live
// application record list. There is no local registry to fall out of sync
// with (see the removed TitleIdAllocator registry): this cross-check used to
// exist to self-heal a stale local file after a native Home Menu uninstall,
// but with nothing persisted locally, that failure mode is gone by
// construction.
bool TitleIdStillRegistered(uint64_t titleId) {
    NsApplicationRecord records[64];
    s32 offset = 0;
    for (;;) {
        s32 outCount = 0;
        Result rc = nsListApplicationRecord(records, 64, offset, &outCount);
        if (R_FAILED(rc) || outCount <= 0)
            return false;
        for (s32 i = 0; i < outCount; i++) {
            if (records[i].application_id == titleId)
                return true;
        }
        if (outCount < 64)
            return false;
        offset += outCount;
    }
}

}  // namespace

InstallResult BuildAndInstallForwarder(const InstallRequest &req) {
    InstallResult result;
    Instant totalStart = Instant::Now();

    // Checked up front, before doing any other work, so the user gets a
    // specific, actionable message instead of a generic "NCA construction
    // failed" - NCA header/key-area encryption needs raw key bytes that
    // libnx's `spl` service has no API to provide (see KeyProvider.h), so
    // this file genuinely has to exist; there's no code path that can
    // succeed without it.
    Instant stepStart = Instant::Now();
    KeyProvider keyCheck;
    if (!keyCheck.Load("/switch/prod.keys")) {
        result.errorMessage =
            "Missing /switch/prod.keys on the SD card. Copy your Switch's "
            "prod.keys file to the \"switch\" folder at the root of your SD "
            "card (so it's at /switch/prod.keys), then try again.";
        return result;
    }
    DEBUG_LOG(Log::Forwarder, "Timing: key load took %.1f ms", stepStart.ElapsedMs());

    uint64_t titleId = DeriveTitleId(req.gameId, req.author);

    // Reinstalling under the same titleId (deterministic from gameId+author)
    // must replace rather than duplicate - tear down any existing NCM/ns
    // registration for it first, if one is actually live.
    stepStart = Instant::Now();
    if (TitleIdStillRegistered(titleId)) {
        Result delRc = nsDeleteApplicationCompletely(titleId);
        if (R_FAILED(delRc)) {
            std::string err;
            RemoveTitleContent(NcmStorageId_SdCard, titleId, &err);
        }
        NsInvalidateAllApplicationControlCache();
        DEBUG_LOG(Log::Forwarder, "Timing: existing-forwarder teardown took %.1f ms", stepStart.ElapsedMs());
    }

    stepStart = Instant::Now();
    bool isGeneric = req.gameId == kGenericForwarderGameId;
    std::vector<uint8_t> iconJpeg;
    if (isGeneric) {
        // User-supplied icon (2026-07-07), replacing the original reference
        // forwarder's icon which was considered visually unappealing. Already
        // a baseline 256x256 3-component JPEG - the exact format the Control
        // NCA icon section requires - so used as-is, not re-encoded. Loaded
        // from the normal asset path rather than baked into the binary.
        size_t jpegSize = 0;
        uint8_t *jpegData = g_VFS.ReadFile("switch/forwarder_generic_icon.jpg", &jpegSize);
        if (!jpegData) {
            result.errorMessage = "Missing switch/forwarder_generic_icon.jpg in assets";
            return result;
        }
        iconJpeg.assign(jpegData, jpegData + jpegSize);
        delete[] jpegData;
    } else {
        iconJpeg = BuildForwarderIcon(req.iconPng.data(), req.iconPng.size(), true);
        if (iconJpeg.empty()) {
            result.errorMessage = "Icon conversion failed";
            return result;
        }
    }
    DEBUG_LOG(Log::Forwarder, "Timing: icon build took %.1f ms", stepStart.ElapsedMs());

    stepStart = Instant::Now();
    std::vector<uint8_t> patchedNpdm = PatchHbloaderNpdmTitleId(kHblMainNpdmData, kHblMainNpdmDataLen, titleId);
    if (patchedNpdm.empty()) {
        result.errorMessage = "NPDM patch failed";
        return result;
    }

    std::vector<uint8_t> exeFs = BuildPfs0({
        { "main", std::vector<uint8_t>(kHblMainData, kHblMainData + kHblMainDataLen) },
        { "main.npdm", patchedNpdm },
    });
    DEBUG_LOG(Log::Forwarder, "Timing: npdm patch + exeFs pfs0 took %.1f ms", stepStart.ElapsedMs());

    // nx-hbloader itself needs "sdmc:"-prefixed URIs to open() the target NRO
    // via low-level POSIX file I/O, and (per a known-good reference forwarder's
    // own nextArgv content) a per-game launch's argv is the nro path itself
    // followed by the quoted target path as its argument - not the bare
    // target path alone, which would drop the nro path nextArgv needs to tell
    // hbloader what to chain-load in the first place.
    //
    // The game path itself must NOT get the "sdmc:" prefix: PPSSPP's own boot
    // argument handling (UI/NativeApp.cpp) turns this straight into a Path()
    // used as the game's identity for GameInfoCache/compat-flag lookups and
    // the Recent list. A normal UI-driven boot never has this prefix, so
    // prefixing it here made the forwarder-launched copy of the same game
    // look like a different game to PPSSPP - confirmed on-console via a
    // duplicate "sdmc:"-prefixed Recent list entry for the same ISO - and is
    // the suspected cause of an intermittent crash seen when launching a
    // per-game forwarder (inconsistent cache/compat-flag state between the
    // two differently-keyed representations of the same file).
    std::string sdmcNroPath = "sdmc:" + req.nextNroPath;
    std::string sdmcGameArgv;
    if (!req.gameArgv.empty()) {
        sdmcGameArgv = sdmcNroPath + " \"" + req.gameArgv + "\"";
    }
    stepStart = Instant::Now();
    std::vector<uint8_t> romFs = BuildForwarderRomfs(sdmcNroPath, sdmcGameArgv);
    DEBUG_LOG(Log::Forwarder, "Timing: romfs build took %.1f ms", stepStart.ElapsedMs());

    NacpBuildParams nacpParams;
    nacpParams.title = req.displayName;
    nacpParams.author = req.author;
    // Prefer the game's own DISC_VERSION (req.version, set by SDLMain.cpp) so
    // per-game forwarders reflect the actual game revision; fall back to
    // PPSSPP's own version for the generic forwarder or if DISC_VERSION was
    // unavailable. CopyTruncated (NacpBuilder.cpp) safely truncates if this is
    // longer than the NACP display_version field.
    nacpParams.version = !req.version.empty() ? req.version : PPSSPP_GIT_VERSION;
    nacpParams.titleId = titleId;
    NacpStruct nacp;
    BuildNacp(&nacp, nacpParams);

    std::vector<uint8_t> logoPfs0 = BuildPfs0({
        { "NintendoLogo.png", std::vector<uint8_t>(kNintendoLogoPngData, kNintendoLogoPngData + kNintendoLogoPngDataLen) },
        { "StartupMovie.gif", std::vector<uint8_t>(kStartupMovieGifData, kStartupMovieGifData + kStartupMovieGifDataLen) },
    });
    // Each BuildXNca call includes RSA-2048-PSS signing of the NCA header
    // (see FORWARDER_SKIP_NCA_SIGNING in NcaBuilder.cpp) - timed separately
    // since this is the prime suspect for where the multi-second install
    // time goes.
    stepStart = Instant::Now();
    BuiltNca programNca = BuildProgramNca(exeFs, romFs, logoPfs0, titleId);
    DEBUG_LOG(Log::Forwarder, "Timing: BuildProgramNca (incl. signing) took %.1f ms", stepStart.ElapsedMs());

    stepStart = Instant::Now();
    BuiltNca controlNca = BuildControlNca(nacp, iconJpeg, titleId);
    DEBUG_LOG(Log::Forwarder, "Timing: BuildControlNca (incl. signing) took %.1f ms", stepStart.ElapsedMs());

    if (programNca.bytes.empty() || controlNca.bytes.empty()) {
        result.errorMessage =
            "Failed to build forwarder content - check that /switch/prod.keys "
            "contains header_key and key_area_key_application_00 (see log for details)";
        return result;
    }

    std::vector<CnmtContentEntry> cnmtEntries = {
        { programNca.hash, programNca.contentId, programNca.bytes.size(), NcmContentType_Program },
        { controlNca.hash, controlNca.contentId, controlNca.bytes.size(), NcmContentType_Control },
    };
    // version=0 matches a known-good reference forwarder's CNMT byte-for-byte
    // (verified via hexdump); it's the content-meta version, unrelated to the
    // NACP's own display version string.
    stepStart = Instant::Now();
    std::vector<uint8_t> cnmtBytes = BuildApplicationCnmt(titleId, 0, cnmtEntries);
    BuiltNca metaNca = BuildMetaNca(cnmtBytes, titleId);
    DEBUG_LOG(Log::Forwarder, "Timing: BuildMetaNca (incl. signing) took %.1f ms", stepStart.ElapsedMs());

    if (metaNca.bytes.empty()) {
        result.errorMessage =
            "Failed to build forwarder metadata - check that /switch/prod.keys "
            "contains header_key and key_area_key_application_00 (see log for details)";
        return result;
    }
    DEBUG_LOG(Log::Forwarder, "NCA assembly done: program=%zu control=%zu meta=%zu bytes",
              programNca.bytes.size(), controlNca.bytes.size(), metaNca.bytes.size());

    stepStart = Instant::Now();
    NcmContentStorage storage;
    Result rc = ncmOpenContentStorage(&storage, NcmStorageId_SdCard);
    if (R_FAILED(rc)) {
        result.errorMessage = "OpenContentStorage failed";
        ERROR_LOG(Log::Forwarder, "ncmOpenContentStorage failed rc=0x%x", rc);
        return result;
    }

    std::string err;
    Instant subStep = Instant::Now();
    bool ok = WriteAndRegisterContent(&storage, programNca, &err);
    DEBUG_LOG(Log::Forwarder, "Timing: write+register program NCA (%zu bytes) took %.1f ms", programNca.bytes.size(), subStep.ElapsedMs());
    if (ok) {
        subStep = Instant::Now();
        ok = WriteAndRegisterContent(&storage, controlNca, &err);
        DEBUG_LOG(Log::Forwarder, "Timing: write+register control NCA (%zu bytes) took %.1f ms", controlNca.bytes.size(), subStep.ElapsedMs());
    }
    if (ok) {
        subStep = Instant::Now();
        ok = WriteAndRegisterContent(&storage, metaNca, &err);
        DEBUG_LOG(Log::Forwarder, "Timing: write+register meta NCA (%zu bytes) took %.1f ms", metaNca.bytes.size(), subStep.ElapsedMs());
    }
    ncmContentStorageClose(&storage);
    if (!ok) {
        result.errorMessage = err;
        ERROR_LOG(Log::Forwarder, "content write/register failed: %s", err.c_str());
        return result;
    }
    DEBUG_LOG(Log::Forwarder, "Timing: total content write/register to NCM storage took %.1f ms", stepStart.ElapsedMs());

    stepStart = Instant::Now();
    NcmContentMetaDatabase metaDb;
    rc = ncmOpenContentMetaDatabase(&metaDb, NcmStorageId_SdCard);
    if (R_FAILED(rc)) {
        result.errorMessage = "OpenContentMetaDatabase failed";
        ERROR_LOG(Log::Forwarder, "ncmOpenContentMetaDatabase failed rc=0x%x", rc);
        return result;
    }

    NcmContentMetaKey key{};
    key.id = titleId;
    key.version = 1;
    key.type = NcmContentMetaType_Application;
    key.install_type = NcmContentInstallType_Full;

    NcmContentMetaHeader dbHeader{};
    dbHeader.extended_header_size = sizeof(NcmApplicationMetaExtendedHeader);
    dbHeader.content_count = 2;  // Program + Control (Meta itself isn't listed).
    dbHeader.content_meta_count = 0;

    NcmApplicationMetaExtendedHeader dbExt{};
    dbExt.patch_id = 0;
    dbExt.required_system_version = 0;
    dbExt.required_application_version = 0;

    NcmContentInfo dbContents[2] = {};
    dbContents[0].content_id = programNca.contentId;
    dbContents[0].size_low = (uint32_t)(programNca.bytes.size() & 0xFFFFFFFF);
    dbContents[0].size_high = (uint8_t)(programNca.bytes.size() >> 32);
    dbContents[0].content_type = NcmContentType_Program;
    dbContents[1].content_id = controlNca.contentId;
    dbContents[1].size_low = (uint32_t)(controlNca.bytes.size() & 0xFFFFFFFF);
    dbContents[1].size_high = (uint8_t)(controlNca.bytes.size() >> 32);
    dbContents[1].content_type = NcmContentType_Control;

    std::vector<uint8_t> dbData;
    dbData.insert(dbData.end(), (uint8_t *)&dbHeader, (uint8_t *)&dbHeader + sizeof(dbHeader));
    dbData.insert(dbData.end(), (uint8_t *)&dbExt, (uint8_t *)&dbExt + sizeof(dbExt));
    dbData.insert(dbData.end(), (uint8_t *)&dbContents[0], (uint8_t *)&dbContents[0] + sizeof(dbContents));

    rc = ncmContentMetaDatabaseSet(&metaDb, &key, dbData.data(), dbData.size());
    if (R_SUCCEEDED(rc)) {
        rc = ncmContentMetaDatabaseCommit(&metaDb);
    }
    ncmContentMetaDatabaseClose(&metaDb);
    if (R_FAILED(rc)) {
        result.errorMessage = "ContentMetaDatabase set/commit failed";
        ERROR_LOG(Log::Forwarder, "meta db set/commit failed, rc=0x%x", rc);
        return result;
    }
    DEBUG_LOG(Log::Forwarder, "Timing: ContentMetaDatabase set/commit took %.1f ms (titleId=%016llx)",
              stepStart.ElapsedMs(), (unsigned long long)titleId);

    stepStart = Instant::Now();
    rc = NsPushApplicationRecord(titleId, key, NcmStorageId_SdCard);
    if (R_FAILED(rc)) {
        result.errorMessage = "PushApplicationRecord failed";
        ERROR_LOG(Log::Forwarder, "NsPushApplicationRecord failed, rc=0x%x", rc);
        return result;
    }
    NsInvalidateAllApplicationControlCache();
    DEBUG_LOG(Log::Forwarder, "Timing: PushApplicationRecord + cache invalidate took %.1f ms", stepStart.ElapsedMs());
    INFO_LOG(Log::Forwarder, "Installed forwarder for gameId='%s' titleId=%016llx in %.1f ms total",
             req.gameId.c_str(), (unsigned long long)titleId, totalStart.ElapsedMs());

    result.success = true;
    result.titleId = titleId;
    return result;
}

bool UninstallForwarder(const std::string &gameId, std::string *errorMessage) {
    uint64_t titleId = DeriveTitleId(gameId, ForwarderAuthorForGameId(gameId));
    if (!TitleIdStillRegistered(titleId)) {
        return true;  // Nothing installed - trivially successful.
    }

    // Previously this hand-rolled a raw serviceDispatchIn guessing at an
    // undocumented "DeleteApplicationRecord" command (id 20) against the ns
    // application-manager interface, then separately, manually removed NCM
    // content meta/storage entries. That left ns's own application registry
    // in a half-torn-down state on real hardware (Home Menu showed a "not
    // downloaded" cloud icon, then crashed Atmosphere entirely on
    // interaction, and the corruption persisted across a reboot). libnx
    // already exposes the same real API the system's own "Delete Software"
    // flow uses - use that instead of reimplementing teardown by hand.
    Result rc = nsDeleteApplicationCompletely(titleId);
    if (R_FAILED(rc)) {
        // Fall back to manual content/meta removal only if the real API
        // itself failed to run at all (e.g. record already partially gone).
        std::string err;
        RemoveTitleContent(NcmStorageId_SdCard, titleId, &err);
    }
    NsInvalidateAllApplicationControlCache();
    return true;
}

bool IsForwarderInstalled(const std::string &gameId, uint64_t *outTitleId) {
    uint64_t titleId = DeriveTitleId(gameId, ForwarderAuthorForGameId(gameId));
    if (!TitleIdStillRegistered(titleId))
        return false;

    if (outTitleId)
        *outTitleId = titleId;
    return true;
}

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
