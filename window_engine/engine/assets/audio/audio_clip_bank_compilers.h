#pragma once

// engine/assets/audio/audio_clip_bank_compilers.h

#include <asset/compiler.h>
#include <logging/logger.h>
#include <engine/assets/audio/audio_clip.h>
#include <engine/assets/audio/audio_clip_bank.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets {

    // ─── AudioClipBankCompileDesc ─────────────────────────────────────────────────
    //
    // Stored in AssetNode::meta for an audio-clip-bank-from-clips recipe. The clips
    // themselves are ordered dependencies (not params); this carries only the
    // parallel per-entry name hashes (FNV-1a/32). If shorter than the dep list, the
    // missing entries get name_hash 0.

    struct AudioClipBankCompileDesc
    {
        std::vector<uint32_t> name_hashes;
    };

} // namespace wz::engine::assets

namespace wz::engine::assets::internal {

    // Registers the audio clip bank compiler. Compiles N kAssetTypeAudioClip
    // dependencies plus a parallel name-hash list into an AudioClipBankData stored
    // in audio_clip_bank_table. audio_clip_table is used to validate each resolved
    // source clip.
    void register_audio_clip_bank_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        AudioClipBankTable& audio_clip_bank_table,
        AudioClipTable& audio_clip_table
    );

}
