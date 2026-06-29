#pragma once

// engine/assets/audio/audio_renderable_compilers.h

#include <asset/compiler.h>
#include <logging/logger.h>
#include <engine/assets/audio/audio_clip.h>
#include <engine/assets/audio/audio_clip_bank.h>
#include <engine/assets/audio/audio_renderable.h>

namespace wz::engine::assets::internal {

    // Registers the audio renderable compilers. Two recipes share the
    // kAssetTypeAudioRenderable output:
    //   • single-clip (kAudioRenderableSchema): one kAssetTypeAudioClip dep +
    //     playback params → a one-clip terminal.
    //   • bank-backed (kAudioClipBankRenderableSchema): one kAssetTypeAudioBank
    //     dep + playback params + default_index → an N-clip terminal.
    // audio_clip_table validates resolved source clips; audio_clip_bank_table
    // resolves the bank's clip handles.
    void register_audio_renderable_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        AudioRenderableTable& audio_renderable_table,
        AudioClipTable& audio_clip_table,
        AudioClipBankTable& audio_clip_bank_table
    );

}
