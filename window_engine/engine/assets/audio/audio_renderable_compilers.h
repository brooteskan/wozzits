#pragma once

// engine/assets/audio/audio_renderable_compilers.h

#include <asset/compiler.h>
#include <logging/logger.h>
#include <engine/assets/audio/audio_clip.h>
#include <engine/assets/audio/audio_renderable.h>

namespace wz::engine::assets::internal {

    // Registers the audio renderable compiler. Compiles one kAssetTypeAudioClip
    // dependency plus playback params into an executable AudioRenderableData
    // stored in audio_renderable_table. audio_clip_table is used to validate the
    // resolved source clip.
    void register_audio_renderable_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        AudioRenderableTable& audio_renderable_table,
        AudioClipTable& audio_clip_table
    );

}
