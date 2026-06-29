#pragma once

// engine/assets/audio_clip_asset_module.h

#include <asset/system.h>
#include <asset/types.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <engine/assets/file_carrier_asset_module.h>
#include <engine/assets/audio/audio_clip.h>

namespace wz::engine::assets
{
    // ─── Audio clip asset descriptors ─────────────────────────────────────────────

    // Describes a WAV-backed audio clip asset to register.
    // path is relative to the EngineAssetLibrary's resource_root. Sample rate,
    // channel count, and format are read from the WAV header at compile time, so
    // there are no authored parameters.
    struct AudioClipWavFileDesc
    {
        std::string name;

        wz::fs::Path path;
    };

    // Describes a procedural tone audio clip asset to register.
    // No file path is required — samples are generated from the parameters alone.
    // name contributes to the asset key so differently-named tones with identical
    // parameters are treated as distinct assets.
    struct ProceduralToneAudioClipDesc
    {
        std::string name;

        uint32_t sample_rate = 48000;
        uint32_t channels = 1;

        AudioToneWaveform waveform = AudioToneWaveform::Sine;

        float frequency = 440.0f;
        float duration_seconds = 1.0f;
        float amplitude = 0.5f;
    };

    // Returned by create_*(). Wraps the DAG output node key.
    struct AudioClipAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    // Returned by get_audio_clip(). Wraps the ResourceHandle into AudioClipTable.
    struct AudioClipHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };


    // ─── AudioClipAssetModule ─────────────────────────────────────────────────────

    class AudioClipAssetModule
    {
    public:
        AudioClipAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger&             logger,
            FileCarrierAssetModule& files,
            AudioClipTable&         table
        );

        // Register a WAV-backed audio clip asset in the DAG.
        // Call commit() and resolve_all() on EngineAssetLibrary before querying.
        AudioClipAsset create_audio_clip_from_wav(const AudioClipWavFileDesc& desc);

        // Register a procedural tone audio clip asset in the DAG.
        AudioClipAsset create_procedural_tone_audio_clip(
            const ProceduralToneAudioClipDesc& desc);

        // Retrieve the ResourceHandle for a resolved audio clip asset.
        // Returns an invalid handle if the asset has not been resolved.
        AudioClipHandle get_audio_clip(const AudioClipAsset& asset) const;

        // Retrieve the resolved data for an audio clip by handle.
        // Returns nullptr if the handle is invalid or stale.
        const AudioClipData* get_audio_clip_data(AudioClipHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger&             logger_;
        FileCarrierAssetModule& files_;
        AudioClipTable&         table_;
    };

} // namespace wz::engine::assets
