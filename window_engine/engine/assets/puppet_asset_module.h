#pragma once

// engine/assets/puppet_asset_module.h
//
// Authoring facade for the Inochi2D puppet asset (kAssetTypePuppet). Mirrors
// GaussianSplatAssetModule: registers a source AssetNode whose compiler loads
// the .inp/.inx bytes, publishes GPU residency, and stores the resident puppet
// in the PuppetTable. See puppet_compilers.h / the inochi-runtime-track.

#include <asset/system.h>
#include <engine/assets/inochi/puppet_table.h>
#include <logging/logger.h>
#include <asset/types.h>

#include <string>

namespace wz::engine::assets
{
    struct PuppetAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    // An Inochi2D puppet imported from a .inp / .inx (TRNSRTS) file. source_file
    // is a raw-file carrier holding the container bytes (register it via
    // FileAssetModule / the raw-file path).
    struct PuppetFromFileDesc
    {
        std::string name;
        wz::asset::AssetKey source_file{};
    };

    class PuppetAssetModule
    {
    public:
        PuppetAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            PuppetTable& table);

        PuppetAsset create_puppet_from_file(const PuppetFromFileDesc& desc);

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        PuppetTable& table_;
    };
}
