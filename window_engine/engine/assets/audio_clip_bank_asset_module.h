#pragma once

// engine/assets/audio_clip_bank_asset_module.h

#include <asset/system.h>
#include <asset/types.h>

#include <logging/logger.h>

#include <engine/assets/audio/audio_clip_bank.h>
#include <engine/assets/audio_clip_asset_module.h>
#include <engine/assets/file_carrier_asset_module.h>

#include <string>
#include <vector>

namespace wz::engine::assets
{
    // ─── Audio clip bank asset descriptors ────────────────────────────────────────

    // Describes an audio clip bank built from an explicit, ordered list of clips.
    // Each item pairs a debug/editor name with an already-registered clip asset;
    // the bank's index space follows item order, and item.name is hashed
    // (FNV-1a/32) for name-keyed lookup.
    struct AudioClipBankFromClipsDesc
    {
        struct Item
        {
            std::string    name;
            AudioClipAsset clip;
        };

        std::vector<Item> items;
    };

    // Describes an audio clip bank built by enumerating *.wav files under a
    // directory. Files are imported as WAV clips, named by filename stem, and
    // ordered lexicographically by resolved path for determinism.
    struct AudioClipBankFromDirectoryDesc
    {
        std::string directory;
        bool        recursive = false;
    };

    // Returned by create_*(). Wraps the DAG output node key.
    struct AudioClipBankAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    // Returned by get_audio_clip_bank(). Wraps the ResourceHandle into
    // AudioClipBankTable.
    struct AudioClipBankHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept { return handle.valid(); }
    };


    // ─── AudioClipBankAssetModule ─────────────────────────────────────────────────

    class AudioClipBankAssetModule
    {
    public:
        // Constructed with the existing AudioClipAssetModule + FileCarrierAssetModule
        // so it can register clips from files when building a bank from a directory.
        AudioClipBankAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger&             logger,
            AudioClipBankTable&     table,
            AudioClipAssetModule&   clips,
            FileCarrierAssetModule& files
        );

        // Register an audio clip bank from an ordered list of clips. Deps are the
        // clip keys in item order; meta carries the parallel name hashes.
        AudioClipBankAsset create_audio_clip_bank_from_clips(
            const AudioClipBankFromClipsDesc& desc);

        // Enumerate *.wav under desc.directory (case-insensitive extension),
        // import each as a clip (name = filename stem), sort by resolved path for
        // determinism, and delegate to create_audio_clip_bank_from_clips. Returns
        // an invalid asset if the directory is missing or has no WAVs.
        AudioClipBankAsset create_audio_clip_bank_from_directory(
            const AudioClipBankFromDirectoryDesc& desc);

        // Retrieve the ResourceHandle for a resolved audio clip bank asset.
        AudioClipBankHandle get_audio_clip_bank(
            const AudioClipBankAsset& asset) const;

        // Retrieve the resolved data for a bank by handle. nullptr if invalid/stale.
        const AudioClipBankData* get_audio_clip_bank_data(
            AudioClipBankHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger&             logger_;
        AudioClipBankTable&     table_;
        AudioClipAssetModule&   clips_;
        FileCarrierAssetModule& files_;
    };

} // namespace wz::engine::assets
