#pragma once

// engine/assets/audio_renderable_asset_module.h

#include <asset/system.h>
#include <asset/types.h>

#include <logging/logger.h>

#include <engine/assets/audio/audio_renderable.h>
#include <engine/assets/audio_clip_asset_module.h>

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
