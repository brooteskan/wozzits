// src/engine/assets/audio_clip_bank_asset_module.cpp

#include <engine/assets/audio_clip_bank_asset_module.h>
#include <engine/assets/audio/audio_clip_bank_compilers.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/key_factories/audio_clip_bank.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
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

        bool has_wav_extension(const std::filesystem::path& p)
        {
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ext == ".wav";
        }
    }

    AudioClipBankAssetModule::AudioClipBankAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger&             logger,
        AudioClipBankTable&     table,
        AudioClipAssetModule&   clips,
        FileCarrierAssetModule& files)
        : system_(system)
        , logger_(logger)
        , table_(table)
        , clips_(clips)
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

        // Resolve the authored directory against the resource root (absolute
        // paths pass through), then enumerate WAVs through std::filesystem.
        const wz::fs::Path resolved = files_.resolve_path(desc.directory);
        const std::filesystem::path dir{ std::string(resolved) };

        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) {
            logger_.error(
                "audio clip bank directory not found: " + std::string(resolved));
            return out;
        }

        // Collect candidate WAV paths, then sort lexicographically for a
        // deterministic, order-stable bank index space.
        std::vector<std::filesystem::path> wavs;
        if (desc.recursive) {
            for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
                 it != std::filesystem::recursive_directory_iterator(); ++it)
            {
                if (it->is_regular_file(ec) && has_wav_extension(it->path()))
                    wavs.push_back(it->path());
            }
        }
        else {
            for (auto it = std::filesystem::directory_iterator(dir, ec);
                 it != std::filesystem::directory_iterator(); ++it)
            {
                if (it->is_regular_file(ec) && has_wav_extension(it->path()))
                    wavs.push_back(it->path());
            }
        }

        std::sort(wavs.begin(), wavs.end(),
            [](const std::filesystem::path& a, const std::filesystem::path& b) {
                return a.generic_string() < b.generic_string();
            });

        if (wavs.empty()) {
            logger_.error(
                "audio clip bank directory has no WAV files: "
                + std::string(resolved));
            return out;
        }

        // Import each WAV as a clip (name = filename stem) and build the
        // from-clips desc in the same deterministic order.
        AudioClipBankFromClipsDesc clips_desc{};
        clips_desc.items.reserve(wavs.size());
        for (const std::filesystem::path& wav : wavs) {
            const std::string name = wav.stem().string();
            const AudioClipAsset clip = clips_.create_audio_clip_from_wav({
                .name = name,
                .path = wz::fs::Path{ wav.string() },
                });
            if (!clip.valid()) {
                logger_.error(
                    "audio clip bank failed to import WAV: " + wav.string());
                return out;
            }
            clips_desc.items.push_back(
                AudioClipBankFromClipsDesc::Item{ .name = name, .clip = clip });
        }

        return create_audio_clip_bank_from_clips(clips_desc);
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
