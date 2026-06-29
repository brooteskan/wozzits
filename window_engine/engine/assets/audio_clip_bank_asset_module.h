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

    // Describes an audio clip bank built from a directory of WAV files. Registers
    // a single directory-import graph node (kAudioClipBankFromDirectorySchema); the
    // enumeration + WAV decoding happens in that node's compiler at compile time,
    // so the scan is captured in the asset graph rather than fanned out into clip
    // nodes at authoring time. The directory is resolved to an absolute path.
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
        // Constructed with the FileCarrierAssetModule so it can resolve an authored
        // directory against the resource root before registering the import node.
        AudioClipBankAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger&             logger,
            AudioClipBankTable&     table,
            FileCarrierAssetModule& files
        );

        // Register an audio clip bank from an ordered list of clips. Deps are the
        // clip keys in item order; meta carries the parallel name hashes.
        AudioClipBankAsset create_audio_clip_bank_from_clips(
            const AudioClipBankFromClipsDesc& desc);

        // Register a directory-import bank node. The authored directory is resolved
        // to an absolute path and stored on the node; the compiler enumerates and
        // decodes the WAVs at compile time. Returns an invalid asset only if the
        // directory string is empty (the directory's existence/contents are checked
        // by the compiler, not here).
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
        FileCarrierAssetModule& files_;
    };

} // namespace wz::engine::assets
