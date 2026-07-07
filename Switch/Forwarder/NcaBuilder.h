#pragma once

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <switch.h>

namespace Forwarder {

void Sha256(const uint8_t *data, size_t size, uint8_t out[32]);

struct BuiltNca {
    std::vector<uint8_t> bytes;
    NcmContentId contentId;      // First 16 bytes of the SHA-256 of `bytes`.
    std::array<uint8_t, 32> hash;
};

// Patches the embedded hbloader main.npdm's title-ID fields (ACI0.program_id,
// ACID.program_id_min, ACID.program_id_max) to titleId. Offsets were verified
// directly against the vendored embed/hbl_main.npdm binary (see
// Switch/Forwarder/embed/README.md), not assumed from documentation alone:
//   - u32 aci0_offset at file offset 0x70, u32 acid_offset at file offset 0x78
//   - ACI0.program_id  (u64) at aci0_offset + 0x10
//   - ACID.program_id_min/max (u64 each) at acid_offset + 0x210 / + 0x218
// ACID's RSA signature is left untouched/invalid - Atmosphere/CFW environments
// (the only environments homebrew forwarders run in) don't verify it.
std::vector<uint8_t> PatchHbloaderNpdmTitleId(const uint8_t *npdmData, size_t npdmSize, uint64_t titleId);

// Builds a PFS0 (partition filesystem) image containing exactly the given
// named files, used both for a Program NCA's ExeFS section (main + main.npdm)
// and conceptually reusable for other PFS0 needs.
std::vector<uint8_t> BuildPfs0(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &files);

// Builds the Program NCA: ExeFS section = PFS0{main, patched main.npdm},
// RomFS section = the forwarder's nextArgv/nextNroPath RomFS image, Logo
// section = PFS0{NintendoLogo.png, StartupMovie.gif} (a third section every
// real Application Program NCA carries - confirmed present, in this exact
// PFS0/HierarchicalSha256 form, in a known-working reference forwarder NCA
// via hactool; omitting it produced a structurally-incomplete Program NCA).
// Returns an empty BuiltNca (bytes.empty()) on failure - most commonly
// because /switch/prod.keys could not be read (see KeyProvider.h/NcaCrypto
// notes below for why that file is needed at all).
BuiltNca BuildProgramNca(const std::vector<uint8_t> &exeFsPfs0, const std::vector<uint8_t> &romFsImage,
                          const std::vector<uint8_t> &logoPfs0, uint64_t titleId);

// Builds the Control NCA: a single "control.nacp" + "icon_AmericanEnglish.dat"
// pair inside a RomFS/IVFC section - confirmed via hactool against a
// known-good reference forwarder that real Control NCAs use RomFS here, not
// PFS0 (unlike the Program NCA's ExeFS/Logo sections, which are PFS0).
BuiltNca BuildControlNca(const NacpStruct &nacp, const std::vector<uint8_t> &iconJpeg, uint64_t titleId);

// Wraps a raw .cnmt file (from CnmtBuilder) in a minimal Meta NCA.
BuiltNca BuildMetaNca(const std::vector<uint8_t> &cnmtBytes, uint64_t titleId);

// Decrypts just enough of an already-built NCA's header (using /switch/prod.keys)
// to read back its content_type byte, without touching the rest of the file.
// Returns -1 on failure (e.g. keys missing or file too short). Used by the
// debug raw-.nsp installer to tell which of an NSP's NCAs is Program/Control/
// Meta without needing any out-of-band naming convention.
int PeekNcaContentType(const std::vector<uint8_t> &ncaBytes);

// Same idea as PeekNcaContentType, but reads back the NCA's program_id
// (title ID) field instead. Returns 0 on failure.
uint64_t PeekNcaProgramId(const std::vector<uint8_t> &ncaBytes);

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
