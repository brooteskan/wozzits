// src/engine/assets/audio/audio_clip_compilers.cpp

#include <engine/assets/audio/audio_clip_compilers.h>
#include <engine/assets/audio/wav_decoder.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr std::array<std::string_view, 4> kAudioToneWaveformOptions = {
            "Sine",
            "Square",
            "Saw",
            "Triangle",
        };

        template<class Enum, std::size_t Count>
        Enum enum_param(
            const wz::asset::ParamBlock& params,
            std::string_view name,
            Enum fallback,
            const std::array<std::string_view, Count>&)
        {
            const int64_t value =
                params.get<int64_t>(name, static_cast<int64_t>(fallback));
            if (value >= 0 && value < static_cast<int64_t>(Count)) {
                return static_cast<Enum>(value);
            }
            return fallback;
        }

        ProceduralToneAudioClipCompileDesc tone_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            ProceduralToneAudioClipCompileDesc desc{};
            desc.sample_rate =
                params.get<uint32_t>("sample_rate", desc.sample_rate);
            desc.channels = params.get<uint32_t>("channels", desc.channels);
            desc.waveform =
                enum_param(params, "waveform", desc.waveform,
                           kAudioToneWaveformOptions);
            desc.frequency = params.get<float>("frequency", desc.frequency);
            desc.duration_seconds =
                params.get<float>("duration_seconds", desc.duration_seconds);
            desc.amplitude = params.get<float>("amplitude", desc.amplitude);
            return desc;
        }

        // Rejects any non-finite sample (NaN / infinity). Returns false and logs on
        // the first bad value. Mirrors the scalar-field value validation.
        bool validate_finite(
            const std::vector<float>& samples,
            wz::Logger& logger,
            std::string_view label)
        {
            for (size_t i = 0; i < samples.size(); ++i) {
                const float v = samples[i];
                if (std::isnan(v)) {
                    logger.error(std::string(label)
                        + " contains NaN at sample index " + std::to_string(i));
                    return false;
                }
                if (std::isinf(v)) {
                    logger.error(std::string(label)
                        + " contains infinity at sample index "
                        + std::to_string(i));
                    return false;
                }
            }
            return true;
        }

        wz::asset::AssetNode finalize_audio_clip(
            const wz::asset::AssetNode& input,
            AudioClipData clip,
            AudioClipTable& audio_clip_table)
        {
            const wz::asset::ResourceHandle handle =
                audio_clip_table.add(std::move(clip));

            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }
    }


    void register_audio_clip_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        AudioClipTable& audio_clip_table)
    {
        // ── Audio clip compiler (WAV file-backed) ─────────────────────────────
        //
        // Dispatches on kAudioClipFromWavSchema. Expects exactly one dependency: a
        // kRawFileSchema node whose compiled payload is the raw WAV bytes. Sample
        // rate, channel count, and format come from the WAV header, so the recipe
        // has no authored parameters.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kAudioClipFromWavSchema,
            .output_type = kAssetTypeAudioClip,
            .input_ports = {
                { "source_file", kAssetTypeRawFile },
            },
            .compile = [&logger, &audio_clip_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode* const> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                if (dep_nodes.empty()) {
                    logger.error("audio clip node has no WAV file dependency");
                    return compile_failed_node(input);
                }

                const auto* bytes =
                    std::get_if<std::vector<uint8_t>>(&dep_nodes[0]->payload);
                if (!bytes) {
                    logger.error("audio clip dep node has no byte payload");
                    return compile_failed_node(input);
                }

                AudioClipData clip;
                std::string error;
                if (!decode_wav(*bytes, clip, error)) {
                    logger.error("audio clip WAV decode failed: " + error);
                    return compile_failed_node(input);
                }

                if (!clip.valid()) {
                    logger.error("audio clip WAV decode produced invalid data");
                    return compile_failed_node(input);
                }

                if (!validate_finite(clip.samples, logger, "audio clip")) {
                    return compile_failed_node(input);
                }

                logger.info(
                    "asset compile: audio clip WAV "
                    + std::to_string(clip.sample_rate) + "Hz x"
                    + std::to_string(clip.channels) + "ch frames="
                    + std::to_string(clip.frame_count));

                return finalize_audio_clip(
                    input, std::move(clip), audio_clip_table);
            }
            });


        // ── Audio clip compiler (procedural tone) ─────────────────────────────
        //
        // Dispatches on kAudioClipProceduralToneSchema. Generates samples from
        // ProceduralToneAudioClipCompileDesc metadata alone; expects no
        // dependencies. V1 oscillators are naive (not band-limited) — anti-aliased
        // wavetables are the separate wavetable asset (audio-track item 2).

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kAudioClipProceduralToneSchema,
            .output_type = kAssetTypeAudioClip,
            .parameters = {
                {
                    .name = "sample_rate",
                    .type = wz::asset::ParamType::Int,
                    .label = "Sample rate",
                    .default_num = 48000,
                    .min = 1,
                    .max = 384000,
                },
                {
                    .name = "channels",
                    .type = wz::asset::ParamType::Int,
                    .label = "Channels",
                    .default_num = 1,
                    .min = 1,
                    .max = 8,
                },
                {
                    .name = "waveform",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Waveform",
                    .default_num =
                        static_cast<double>(AudioToneWaveform::Sine),
                    .options = kAudioToneWaveformOptions,
                },
                {
                    .name = "frequency",
                    .type = wz::asset::ParamType::Float,
                    .label = "Frequency (Hz)",
                    .default_num = 440.0,
                    .min = 0.0,
                    .max = 192000.0,
                },
                {
                    .name = "duration_seconds",
                    .type = wz::asset::ParamType::Float,
                    .label = "Duration (s)",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 3600.0,
                },
                {
                    .name = "amplitude",
                    .type = wz::asset::ParamType::Float,
                    .label = "Amplitude",
                    .default_num = 0.5,
                    .min = 0.0,
                    .max = 1.0,
                },
            },
            .compile = [&logger, &audio_clip_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode* const> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                // ── 1. Resolve desc (typed or ParamBlock) ─────────────────────
                ProceduralToneAudioClipCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<ProceduralToneAudioClipCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(&input.meta))
                    {
                        param_desc = tone_desc_from_params(*params);
                        desc = &param_desc;
                    }
                }
                if (!desc) {
                    logger.error("procedural tone audio clip node missing "
                        "ProceduralToneAudioClipCompileDesc");
                    return compile_failed_node(input);
                }

                // ── 2. Validate parameters ────────────────────────────────────
                if (!dep_nodes.empty()) {
                    logger.error(
                        "procedural tone audio clip should not have dependencies");
                    return compile_failed_node(input);
                }
                if (desc->sample_rate == 0 || desc->channels == 0) {
                    logger.error(
                        "procedural tone audio clip has zero sample rate or "
                        "channel count");
                    return compile_failed_node(input);
                }
                if (!(desc->duration_seconds > 0.0f)) {
                    logger.error(
                        "procedural tone audio clip duration must be positive");
                    return compile_failed_node(input);
                }

                const double frames_d =
                    static_cast<double>(desc->duration_seconds) *
                    static_cast<double>(desc->sample_rate);
                const uint64_t frame_count =
                    static_cast<uint64_t>(frames_d + 0.5);
                if (frame_count == 0) {
                    logger.error(
                        "procedural tone audio clip rounds to zero frames");
                    return compile_failed_node(input);
                }

                // ── 3. Generate samples ───────────────────────────────────────
                constexpr float two_pi = 6.28318530717958647692f;

                AudioClipData clip;
                clip.sample_rate = desc->sample_rate;
                clip.channels = desc->channels;
                clip.frame_count = frame_count;
                clip.source = AudioClipSource::ProceduralTone;
                clip.samples.resize(
                    static_cast<size_t>(frame_count) * desc->channels);

                const float phase_step =
                    desc->frequency / static_cast<float>(desc->sample_rate);

                for (uint64_t f = 0; f < frame_count; ++f) {
                    // Fractional phase in [0, 1) for sample f.
                    const float phase =
                        std::fmod(phase_step * static_cast<float>(f), 1.0f);

                    float wave = 0.0f;
                    switch (desc->waveform) {
                    case AudioToneWaveform::Sine:
                        wave = std::sin(two_pi * phase);
                        break;
                    case AudioToneWaveform::Square:
                        wave = (phase < 0.5f) ? 1.0f : -1.0f;
                        break;
                    case AudioToneWaveform::Saw:
                        // Rising ramp from -1 at phase 0 to +1 approaching 1.
                        wave = 2.0f * phase - 1.0f;
                        break;
                    case AudioToneWaveform::Triangle:
                        wave = 4.0f * std::fabs(phase - 0.5f) - 1.0f;
                        break;
                    }

                    const float value = wave * desc->amplitude;
                    for (uint32_t ch = 0; ch < desc->channels; ++ch) {
                        clip.samples[
                            static_cast<size_t>(f) * desc->channels + ch] = value;
                    }
                }

                // ── 4. Validate + store ───────────────────────────────────────
                if (!clip.valid()) {
                    logger.error(
                        "procedural tone audio clip produced invalid data");
                    return compile_failed_node(input);
                }
                if (!validate_finite(
                        clip.samples, logger, "procedural tone audio clip")) {
                    return compile_failed_node(input);
                }

                logger.info(
                    "asset compile: procedural tone audio clip "
                    + std::to_string(clip.sample_rate) + "Hz x"
                    + std::to_string(clip.channels) + "ch frames="
                    + std::to_string(clip.frame_count));

                return finalize_audio_clip(
                    input, std::move(clip), audio_clip_table);
            }
            });
    }

} // namespace wz::engine::assets::internal
