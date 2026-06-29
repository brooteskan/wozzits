// src/engine/assets/audio_clip_bank_asset_module.cpp

#include <engine/assets/audio_clip_bank_asset_module.h>
#include <engine/assets/audio/audio_clip_bank_compilers.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/key_factories/audio_clip_bank.h>

#include <vector>

namespace wz::engine::assets
{
    namespace
    {
        // FNV-1a/32 over a name, matching the 32-bit hash carried per bank entry.
        uint32_t fnv1a_32(std::string_view s) noexcept
        {
            uint32_t h = 2166136261u;
            for (const unsigned char c : s) {
                h ^= static_cast<uint32_t>(c);
                h *= 16777619u;
            }
            return h;
        }
    }

    AudioClipBankAssetModule::AudioClipBankAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger&             logger,
        AudioClipBankTable&     table,
        FileCarrierAssetModule& files)
        : system_(system)
        , logger_(logger)
        , table_(table)
        , files_(files)
    {
    }

    AudioClipBankAsset AudioClipBankAssetModule::create_audio_clip_bank_from_clips(
        const AudioClipBankFromClipsDesc& desc)
    {
        AudioClipBankAsset out{};

        if (desc.items.empty()) {
            logger_.error("audio clip bank requires at least one clip");
            return out;
        }

        std::vector<wz::asset::AssetKey> clip_keys;
        std::vector<uint32_t>            name_hashes;
        clip_keys.reserve(desc.items.size());
        name_hashes.reserve(desc.items.size());

        for (const AudioClipBankFromClipsDesc::Item& item : desc.items) {
            if (!item.clip.valid()) {
                logger_.error(
                    "audio clip bank item '" + item.name
                    + "' has an invalid clip");
                return out;
            }
            clip_keys.push_back(item.clip.output);
            name_hashes.push_back(fnv1a_32(item.name));
        }

        const wz::asset::AssetKey key =
            make_audio_clip_bank_key(clip_keys, name_hashes);

        AudioClipBankCompileDesc compile_desc{};
        compile_desc.name_hashes = name_hashes;

        wz::asset::AssetNode node;
        node.key     = key;
        node.type    = kAssetTypeAudioBank;
        node.schema  = kAudioClipBankFromClipsSchema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta    = compile_desc;

        if (!system_.register_asset(std::move(node), clip_keys))
            return AudioClipBankAsset{ .output = key };

        out.output = key;
        return out;
    }

    AudioClipBankAsset AudioClipBankAssetModule::create_audio_clip_bank_from_directory(
        const AudioClipBankFromDirectoryDesc& desc)
    {
        AudioClipBankAsset out{};

        if (desc.directory.empty()) {
            logger_.error("audio clip bank from directory requires a directory");
            return out;
        }

        // Resolve to an absolute path (absolute paths pass through) and store it on
        // the node; the compiler enumerates + decodes the WAVs at compile time.
        const wz::fs::Path resolved = files_.resolve_path(desc.directory);
        const std::string canonical = detail::canonical_asset_path(resolved);

        const wz::asset::AssetKey key =
            make_audio_clip_bank_from_directory_key(canonical, desc.recursive);

        wz::asset::ParamBlock params{};
        params.values["directory"] = std::string(resolved);
        params.values["recursive"] = desc.recursive;

        wz::asset::AssetNode node;
        node.key     = key;
        node.type    = kAssetTypeAudioBank;
        node.schema  = kAudioClipBankFromDirectorySchema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta    = std::move(params);

        if (!system_.register_asset(std::move(node)))
            return AudioClipBankAsset{ .output = key };

        out.output = key;
        return out;
    }

    AudioClipBankHandle AudioClipBankAssetModule::get_audio_clip_bank(
        const AudioClipBankAsset& asset) const
    {
        AudioClipBankHandle out{};

        if (!asset.valid())
            return out;

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("audio clip bank handle not found");

        return out;
    }

    const AudioClipBankData* AudioClipBankAssetModule::get_audio_clip_bank_data(
        AudioClipBankHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }

} // namespace wz::engine::assets
