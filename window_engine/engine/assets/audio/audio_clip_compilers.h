#pragma once

// engine/assets/audio/audio_clip_compilers.h

#include <asset/compiler.h>
#include <logging/logger.h>
#include <engine/assets/audio/audio_clip.h>

namespace wz::engine::assets::internal {

    // Registers the WAV-import and procedural-tone audio clip compilers. Both
    // produce kAssetTypeAudioClip and store their decoded/generated PCM in
    // audio_clip_table. Audio is CPU-only — there is no GPU residency hook.
    void register_audio_clip_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        AudioClipTable& audio_clip_table
    );

}
