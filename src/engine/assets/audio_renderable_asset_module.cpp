// src/engine/assets/audio_renderable_asset_module.cpp

#include <engine/assets/audio_renderable_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/key_factories/audio_renderable.h>

#include <vector>

namespace wz::engine::assets
{

    AudioRenderableAssetModule::AudioRenderableAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger&             logger,
        AudioRenderableTable&   table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    AudioRenderableAsset AudioRenderableAssetModule::create_audio_renderable(
        const AudioRenderableDesc& desc)
    {
        AudioRenderableAsset out{};

        if (!desc.clip.valid()) {
            logger_.error("audio renderable requires a valid source clip");
            return out;
        }

        const wz::asset::AssetKey clip_key = desc.clip.output;

        const AudioRenderableCompileDesc compile_desc{
            .gain    = desc.gain,
            .pitch   = desc.pitch,
            .looping = desc.looping,
        };

        const wz::asset::AssetKey key = make_audio_renderable_key(
            clip_key, compile_desc.gain, compile_desc.pitch,
            compile_desc.looping);

        wz::asset::AssetNode node;
        node.key     = key;
        node.type    = kAssetTypeAudioRenderable;
        node.schema  = kAudioRenderableSchema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta    = compile_desc;

        if (!system_.register_asset(std::move(node), { clip_key }))
            return AudioRenderableAsset{ .output = key };

        out.output = key;
        return out;
    }

    AudioRenderableAsset AudioRenderableAssetModule::create_audio_clip_bank_renderable(
        const AudioClipBankRenderableDesc& desc)
    {
        AudioRenderableAsset out{};

        if (!desc.bank.valid()) {
            logger_.error("bank audio renderable requires a valid source bank");
            return out;
        }

        const wz::asset::AssetKey bank_key = desc.bank.output;

        const AudioRenderableCompileDesc compile_desc{
            .gain          = desc.gain,
            .pitch         = desc.pitch,
            .looping       = desc.looping,
            .default_index = desc.default_index,
        };

        const wz::asset::AssetKey key = make_audio_clip_bank_renderable_key(
            bank_key, compile_desc.default_index, compile_desc.gain,
            compile_desc.pitch, compile_desc.looping);

        wz::asset::AssetNode node;
        node.key     = key;
        node.type    = kAssetTypeAudioRenderable;
        node.schema  = kAudioClipBankRenderableSchema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta    = compile_desc;

        if (!system_.register_asset(std::move(node), { bank_key }))
            return AudioRenderableAsset{ .output = key };

        out.output = key;
        return out;
    }

    AudioRenderableAsset AudioRenderableAssetModule::create_audio_grain_cloud_renderable(
        const AudioGrainCloudRenderableDesc& desc)
    {
        AudioRenderableAsset out{};

        if (!desc.bank.valid()) {
            logger_.error("grain cloud renderable requires a valid source bank");
            return out;
        }

        const wz::asset::AssetKey bank_key = desc.bank.output;

        const wz::asset::AssetKey key =
            make_audio_grain_cloud_renderable_key(bank_key, desc.params);

        wz::asset::AssetNode node;
        node.key     = key;
        node.type    = kAssetTypeAudioRenderable;
        node.schema  = kAudioGrainCloudRenderableSchema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta    = desc.params;

        if (!system_.register_asset(std::move(node), { bank_key }))
            return AudioRenderableAsset{ .output = key };

        out.output = key;
        return out;
    }

    AudioRenderableHandle AudioRenderableAssetModule::get_audio_renderable(
        const AudioRenderableAsset& asset) const
    {
        AudioRenderableHandle out{};

        if (!asset.valid())
            return out;

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("audio renderable handle not found");

        return out;
    }

    const AudioRenderableData*
    AudioRenderableAssetModule::get_audio_renderable_data(
        AudioRenderableHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }

} // namespace wz::engine::assets
