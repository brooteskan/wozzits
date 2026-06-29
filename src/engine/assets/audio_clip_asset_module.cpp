// src/engine/assets/audio_clip_asset_module.cpp

#include <engine/assets/audio_clip_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/key_factories/audio_clip.h>

#include <vector>

namespace wz::engine::assets
{

    AudioClipAssetModule::AudioClipAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger&             logger,
        FileCarrierAssetModule& files,
        AudioClipTable&         table)
        : system_(system)
        , logger_(logger)
        , files_(files)
        , table_(table)
    {
    }

    AudioClipAsset AudioClipAssetModule::create_audio_clip_from_wav(
        const AudioClipWavFileDesc& desc)
    {
        AudioClipAsset out{};

        const wz::asset::AssetKey file_key = files_.register_file_node(
            desc.path, kRawFileSchema, kAssetTypeRawFile);

        if (file_key == wz::asset::AssetKey{}) {
            logger_.error(
                "failed to register audio clip source file: " + desc.path);
            return out;
        }

        const wz::asset::AssetKey clip_key =
            make_audio_clip_from_wav_key(file_key);

        wz::asset::AssetNode node;
        node.key     = clip_key;
        node.type    = kAssetTypeAudioClip;
        node.schema  = kAudioClipFromWavSchema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta    = AudioClipWavCompileDesc{};

        if (!system_.register_asset(std::move(node), { file_key }))
            return AudioClipAsset{ .output = clip_key };

        out.output = clip_key;
        return out;
    }

    AudioClipAsset AudioClipAssetModule::create_procedural_tone_audio_clip(
        const ProceduralToneAudioClipDesc& desc)
    {
        AudioClipAsset out{};

        const ProceduralToneAudioClipCompileDesc compile_desc{
            .sample_rate      = desc.sample_rate,
            .channels         = desc.channels,
            .waveform         = desc.waveform,
            .frequency        = desc.frequency,
            .duration_seconds = desc.duration_seconds,
            .amplitude        = desc.amplitude,
        };

        const wz::asset::AssetKey key = make_procedural_tone_audio_clip_key(
            desc.name,
            compile_desc.sample_rate,
            compile_desc.channels,
            static_cast<uint8_t>(compile_desc.waveform),
            compile_desc.frequency,
            compile_desc.duration_seconds,
            compile_desc.amplitude
        );

        wz::asset::AssetNode node;
        node.key     = key;
        node.type    = kAssetTypeAudioClip;
        node.schema  = kAudioClipProceduralToneSchema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta    = compile_desc;

        if (!system_.register_asset(std::move(node)))
            return AudioClipAsset{ .output = key };

        out.output = key;
        return out;
    }

    AudioClipHandle AudioClipAssetModule::get_audio_clip(
        const AudioClipAsset& asset) const
    {
        AudioClipHandle out{};

        if (!asset.valid())
            return out;

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("audio clip handle not found");

        return out;
    }

    const AudioClipData* AudioClipAssetModule::get_audio_clip_data(
        AudioClipHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }

} // namespace wz::engine::assets
