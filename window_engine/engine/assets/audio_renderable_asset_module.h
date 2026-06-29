#pragma once

// engine/assets/audio_renderable_asset_module.h

#include <asset/system.h>
#include <asset/types.h>

#include <logging/logger.h>

#include <engine/assets/audio/audio_renderable.h>
#include <engine/assets/audio_clip_asset_module.h>
#include <engine/assets/audio_clip_bank_asset_module.h>

namespace wz::engine::assets
{
    // Describes an audio renderable to register: a source clip plus playback
    // params. The clip is referenced by its resolved AudioClipAsset (the terminal
    // depends on the clip node).
    struct AudioRenderableDesc
    {
        AudioClipAsset clip;

        float gain = 1.0f;
        float pitch = 1.0f;
        bool looping = false;
    };

    // Describes a bank-backed audio renderable: a source clip bank plus playback
    // params and the default clip index. The bank is referenced by its resolved
    // AudioClipBankAsset (the terminal depends on the bank node). The behavior
    // PLAY command selects a clip by index at trigger time.
    struct AudioClipBankRenderableDesc
    {
        AudioClipBankAsset bank;

        uint32_t default_index = 0;
        float gain = 1.0f;
        float pitch = 1.0f;
        bool looping = false;
    };

    // Describes a grain-cloud renderable: a source clip bank run through the
    // granular generator with the given parameters (an evolving environmental bed).
    struct AudioGrainCloudRenderableDesc
    {
        AudioClipBankAsset bank;
        GrainCloudParams   params;
    };

    struct AudioRenderableAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct AudioRenderableHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept { return handle.valid(); }
    };


    class AudioRenderableAssetModule
    {
    public:
        AudioRenderableAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger&             logger,
            AudioRenderableTable&   table
        );

        // Register an audio renderable in the DAG, depending on desc.clip.
        AudioRenderableAsset create_audio_renderable(
            const AudioRenderableDesc& desc);

        // Register a bank-backed audio renderable in the DAG, depending on
        // desc.bank. The terminal holds the bank's whole clip table.
        AudioRenderableAsset create_audio_clip_bank_renderable(
            const AudioClipBankRenderableDesc& desc);

        // Register a grain-cloud renderable in the DAG, depending on desc.bank.
        // The terminal runs the bank's clips through the granular generator.
        AudioRenderableAsset create_audio_grain_cloud_renderable(
            const AudioGrainCloudRenderableDesc& desc);

        AudioRenderableHandle get_audio_renderable(
            const AudioRenderableAsset& asset) const;

        const AudioRenderableData* get_audio_renderable_data(
            AudioRenderableHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger&             logger_;
        AudioRenderableTable&   table_;
    };

} // namespace wz::engine::assets
