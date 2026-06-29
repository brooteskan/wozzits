#pragma once

// engine/assets/key_factories/audio_renderable.h
//
// Key factory for audio renderable recipe nodes. Identity = the source clip
// dependency (deps_hash) + the playback params (content_hash). Two terminals
// over the same clip but different gain/pitch/looping are distinct assets.

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

#include <cstring>

namespace wz::engine::assets {

    [[nodiscard]] inline wz::asset::AssetKey make_audio_renderable_key(
        const wz::asset::AssetKey& source_clip_key,
        float gain,
        float pitch,
        bool  looping) noexcept
    {
        uint32_t gain_bits = 0;
        uint32_t pitch_bits = 0;
        static_assert(sizeof(float) == sizeof(uint32_t));
        std::memcpy(&gain_bits, &gain, sizeof(float));
        std::memcpy(&pitch_bits, &pitch, sizeof(float));

        const uint64_t float_params =
            (static_cast<uint64_t>(gain_bits) << 32) |
            static_cast<uint64_t>(pitch_bits);

        const uint64_t flags = looping ? 1ull : 0ull;

        return wz::asset::AssetKey{
            .content_hash = { detail::mix64(float_params, flags), 0 },
            .schema_hash = detail::hash_u64(kAudioRenderableSchema.value),
            .compiler_hash = detail::hash_u64(kAudioRenderableCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_clip_key),
        };
    }

} // namespace wz::engine::assets
