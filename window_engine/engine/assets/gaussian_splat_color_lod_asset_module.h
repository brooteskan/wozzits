#pragma once

// engine/assets/gaussian_splat_color_lod_asset_module.h
//
// Public surface for registering and resolving Gaussian splat color LOD
// derived assets.

#include <asset/system.h>
#include <engine/assets/gaussian_splat/gaussian_splat_color_lod.h>
#include <engine/assets/gaussian_splat_asset_module.h>
#include <logging/logger.h>

#include <string>

namespace wz::engine::assets
{
    struct GaussianSplatColorLODAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct GaussianSplatColorLODHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept { return handle.valid(); }
    };

    struct GaussianSplatColorLODFromCloudDesc
    {
        std::string                       name;
        GaussianSplatCloudAsset           source_cloud{};
        GaussianSplatColorLODCompileDesc  compile{};
    };

    class GaussianSplatColorLODAssetModule
    {
    public:
        GaussianSplatColorLODAssetModule(
            wz::asset::AssetSystem&        system,
            wz::Logger&                    logger,
            GaussianSplatColorLODTable&    table);

        // Register a derived-color-LOD recipe from a previously-registered
        // GaussianSplatCloudAsset.  The source cloud must already exist in
        // the asset system; this only sets up the recipe — actual compile
        // runs at resolve_all() time.
        GaussianSplatColorLODAsset create_from_cloud(
            const GaussianSplatColorLODFromCloudDesc& desc);

        GaussianSplatColorLODHandle get_lod(
            const GaussianSplatColorLODAsset& asset) const;

        const GaussianSplatColorLODData* get_lod_data(
            GaussianSplatColorLODHandle handle) const;

    private:
        wz::asset::AssetSystem&     system_;
        wz::Logger&                 logger_;
        GaussianSplatColorLODTable& table_;
    };
}
