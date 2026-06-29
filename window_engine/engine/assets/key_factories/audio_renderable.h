#pragma once

// engine/assets/key_factories/audio_renderable.h
//
// Key factory for audio renderable recipe nodes. Identity = the source clip
// dependency (deps_hash) + the playback params (content_hash). Two terminals
// over the same clip but different gain/pitch/looping are distinct assets.

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/audio/audio_renderable.h>

#include <cstdint>
#include <cstring>

namespace wz::engine::assets {

    namespace detail {
        [[nodiscard]] inline uint64_t mix_f32(uint64_t acc, float f) noexcept
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &f, sizeof(float));
            return mix64(acc, static_cast<uint64_t>(bits));
        }
    }

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

    // Key for a bank-backed audio renderable. Identity = the bank dependency
    // (deps_hash) + the playback params and default clip index (content_hash).
    // Two terminals over the same bank but a different default_index — or
    // different gain/pitch/looping — are distinct assets.
    [[nodiscard]] inline wz::asset::AssetKey make_audio_clip_bank_renderable_key(
        const wz::asset::AssetKey& bank_key,
        uint32_t default_index,
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

        const uint64_t lo = detail::mix64(float_params, flags);
        const uint64_t hi = detail::mix64(
            static_cast<uint64_t>(default_index), 0x9e3779b97f4a7c15ull);

        return wz::asset::AssetKey{
            .content_hash = { lo, hi },
            .schema_hash =
                detail::hash_u64(kAudioClipBankRenderableSchema.value),
            .compiler_hash =
                detail::hash_u64(kAudioClipBankRenderableCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(bank_key),
        };
    }

    // Key for a grain-cloud renderable. Identity = the bank dependency (deps_hash)
    // + every granular parameter (content_hash), folded field-by-field so padding
    // never leaks into identity.
    [[nodiscard]] inline wz::asset::AssetKey make_audio_grain_cloud_renderable_key(
        const wz::asset::AssetKey& bank_key,
        const GrainCloudParams&    g) noexcept
    {
        uint64_t lo = 1469598103934665603ull;
        lo = detail::mix64(lo, g.max_grains);
        lo = detail::mix64(lo, g.seed);
        lo = detail::mix_f32(lo, g.gain);
        lo = detail::mix_f32(lo, g.density);
        lo = detail::mix_f32(lo, g.position);
        lo = detail::mix_f32(lo, g.pitch);

        uint64_t hi = 1099511628211ull;
        hi = detail::mix_f32(hi, g.position_jitter);
        hi = detail::mix_f32(hi, g.pitch_jitter_semitones);
        hi = detail::mix_f32(hi, g.pan_center);
        hi = detail::mix_f32(hi, g.pan_spread);
        hi = detail::mix_f32(hi, g.grain_ms);
        hi = detail::mix64(hi, static_cast<uint64_t>(g.window));
        hi = detail::mix_f32(hi, g.window_param);

        return wz::asset::AssetKey{
            .content_hash = { lo, hi },
            .schema_hash =
                detail::hash_u64(kAudioGrainCloudRenderableSchema.value),
            .compiler_hash =
                detail::hash_u64(kAudioGrainCloudRenderableCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(bank_key),
        };
    }

} // namespace wz::engine::assets
