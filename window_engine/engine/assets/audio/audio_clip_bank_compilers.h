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

    // Registers the audio clip bank compilers. Two recipes, both producing
    // kAssetTypeAudioBank stored in audio_clip_bank_table:
    //   • from-clips (kAudioClipBankFromClipsSchema): folds N kAssetTypeAudioClip
    //     dependencies + a parallel name-hash list (audio_clip_table validates each
    //     resolved source clip).
    //   • from-directory (kAudioClipBankFromDirectorySchema): a no-dependency source
    //     recipe that enumerates *.wav under an authored "directory" param, reads +
    //     decodes each WAV inline (the I/O boundary), and adds the decoded clips to
    //     audio_clip_table before building the bank.
    void register_audio_clip_bank_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        AudioClipBankTable& audio_clip_bank_table,
        AudioClipTable& audio_clip_table
    );

}
