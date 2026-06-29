#pragma once

// engine/assets/key_factories/audio_clip.h
//
// Key factories for audio clip recipe nodes.
//
//   make_audio_clip_from_wav_key  — file-backed: identity is the WAV file
//       dependency. The decoder reads rate/channels/format from the header, so
//       there are no recipe parameters; two different files produce distinct
//       keys via deps_hash, and re-importing the same file is idempotent.
//
//   make_procedural_tone_audio_clip_key — file-free: identity is the full set of
//       generation parameters plus the asset name (so two identically-parameterised
//       tones with different names are distinct assets and coexist in the DAG).

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

#include <cstring>
#include <string_view>

namespace wz::engine::assets {

    // Key for a WAV-backed audio clip recipe node.
    //
    // source_file_key — key of the kRawFileSchema carrier node holding WAV bytes.
    [[nodiscard]] inline wz::asset::AssetKey make_audio_clip_from_wav_key(
        const wz::asset::AssetKey& source_file_key) noexcept
    {
        return wz::asset::AssetKey{
            .content_hash = { detail::fnv1a_64("audio_clip/wav"), 0 },
            .schema_hash = detail::hash_u64(kAudioClipFromWavSchema.value),
            .compiler_hash = detail::hash_u64(kAudioClipCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_file_key),
        };
    }

    // Key for a procedural tone audio clip recipe node.
    [[nodiscard]] inline wz::asset::AssetKey make_procedural_tone_audio_clip_key(
        std::string_view name,
        uint32_t         sample_rate,
        uint32_t         channels,
        uint8_t          waveform_ordinal,
        float            frequency,
        float            duration_seconds,
        float            amplitude) noexcept
    {
        // Reinterpret floats as bits for stable mixing without precision concerns.
        uint32_t frequency_bits = 0;
        uint32_t duration_bits = 0;
        uint32_t amplitude_bits = 0;
        static_assert(sizeof(float) == sizeof(uint32_t));
        std::memcpy(&frequency_bits, &frequency, sizeof(float));
        std::memcpy(&duration_bits, &duration_seconds, sizeof(float));
        std::memcpy(&amplitude_bits, &amplitude, sizeof(float));

        const uint64_t rate_channels =
            (static_cast<uint64_t>(sample_rate) << 32) |
            static_cast<uint64_t>(channels);

        const uint64_t waveform_and_frequency =
            (static_cast<uint64_t>(waveform_ordinal) << 32) |
            static_cast<uint64_t>(frequency_bits);

        const uint64_t float_params =
            (static_cast<uint64_t>(duration_bits) << 32) |
            static_cast<uint64_t>(amplitude_bits);

        const wz::asset::Hash name_hash = detail::hash_str(name);

        const uint64_t lo = detail::mix64(
            detail::mix64(name_hash.lo, rate_channels),
            detail::mix64(waveform_and_frequency, float_params)
        );

        const uint64_t hi = detail::mix64(
            name_hash.hi,
            detail::mix64(rate_channels, float_params)
        );

        return wz::asset::AssetKey{
            .content_hash = { lo, hi },
            .schema_hash = detail::hash_u64(kAudioClipProceduralToneSchema.value),
            .compiler_hash = detail::hash_u64(kAudioClipCompilerVersion),
            .deps_hash = {},   // no file dependency
        };
    }

} // namespace wz::engine::assets
