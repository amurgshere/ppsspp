#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <cstring>

#include "Switch/Forwarder/NacpBuilder.h"

namespace Forwarder {

namespace {

void CopyTruncated(char *dst, size_t dstSize, const std::string &src) {
    size_t n = src.size() < dstSize - 1 ? src.size() : dstSize - 1;
    memcpy(dst, src.data(), n);
    dst[n] = 0;
}

}  // namespace

void BuildNacp(NacpStruct *out, const NacpBuildParams &params) {
    memset(out, 0, sizeof(NacpStruct));

    // Fill only the first 12 language slots identically (the original NACP
    // language set, before Traditional/Simplified Chinese and Brazilian
    // Portuguese were added as slots 12-15) - byte-for-byte comparison
    // against a known-good reference forwarder's NACP showed slots 12-15
    // left entirely zeroed, not filled like the rest. Filling all 16
    // (an earlier version of this code) still worked but didn't match the
    // reference exactly.
    constexpr size_t kNumLegacyLanguageSlots = 12;
    for (size_t i = 0; i < kNumLegacyLanguageSlots; i++) {
        CopyTruncated(out->lang[i].name, sizeof(out->lang[i].name), params.title);
        CopyTruncated(out->lang[i].author, sizeof(out->lang[i].author), params.author);
    }

    CopyTruncated(out->display_version, sizeof(out->display_version), params.version);

    // 0xBFF matches the exact value found (via hactool byte inspection) in a
    // known-working reference forwarder NSP, which - like us - ships only a
    // single icon_AmericanEnglish.dat file yet works correctly across
    // different console system languages (confirmed on an English
    // Australia/NZ console, which maps to Nintendo's BritishEnglish slot, not
    // AmericanEnglish). The system evidently falls back to whatever icon is
    // actually present rather than requiring an exact per-language file, so
    // matching the reference's broad flag (rather than guessing 0 or 0xFFFF
    // or a single bit, both tried and reverted here) is the safest choice.
    out->supported_language_flag = 0xBFF;

    // No save data, no DLC, ticketless/unsigned homebrew forwarder: leave
    // save-data-size and account fields at zero.
    out->startup_user_account = 0;
    out->user_account_switch_lock = 0;
    out->add_on_content_registration_type = 0;

    // These identity/rating fields were left entirely zeroed in earlier
    // versions of this code. Byte-comparison against a known-working
    // reference forwarder NSP (built by an independent, proven tool) showed
    // it fills all of them in - presence_group_id, save_data_owner_id, and
    // every local_communication_id[] slot are set to the title ID itself,
    // add_on_content_base_id to titleId+0x1000 (the standard AOC-base
    // convention), and rating_age has real per-region byte values rather
    // than all zero. Since the Home Menu's per-tile "+" options menu (Manage
    // Save Data, Software Info, etc.) reads this same metadata, leaving it
    // zeroed is a plausible cause of that menu coming up empty/corrupted -
    // matching the reference's exact values here to be safe.
    out->presence_group_id = params.titleId;
    out->save_data_owner_id = params.titleId;
    out->add_on_content_base_id = params.titleId + 0x1000;
    for (auto &id : out->local_communication_id) {
        id = params.titleId;
    }
    out->data_loss_confirmation = 1;

    // These three fields are left at their reference tool's own hardcoded
    // defaults (not zero) - found by reading its source (@tootallnate/nacp),
    // not by guessing from byte-diffs, since a from-scratch NACP with save
    // data size 0 is a plausible content-metadata inconsistency the Home
    // Menu's "+" info panel could be reacting to.
    out->user_account_save_data_size = 0x3e00000;
    out->user_account_save_data_journal_size = 0x180000;
    out->logo_type = 2;    // Nintendo logo
    out->logo_handling = 0;
    // Per-region age rating bytes copied verbatim from the reference NSP's
    // control.nacp (offset 0x3040, 0x20 bytes) - CERO/ESRB/USK/PEGI/etc slot
    // order isn't documented publicly in detail, so these are reused as-is
    // rather than re-derived.
    static const uint8_t kRatingAge[0x20] = {
        0x0c, 0xff, 0xff, 0x0a, 0xff, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0d, 0x0d,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    };
    memcpy(out->rating_age, kRatingAge, sizeof(kRatingAge));
}

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
