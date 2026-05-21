#pragma once

// engine/assets/gaussian_splat_asset_module.h

#include <asset/system.h>
#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <logging/logger.h>
#include <asset/types.h>

namespace wz::engine::assets
{
    struct GaussianSplatCloudAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct GaussianSplatCloudHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    struct GaussianSplatFromPLYDesc
    {
        std::string name;
        wz::asset::AssetKey source_file{};
    };

    struct GaussianSplatFromScalarFieldDesc
    {
        std::string name;
        wz::asset::AssetKey scalar_field_key{};  // from ScalarFieldAsset::output

        float height_scale = 1.0f;
        float step_x = 1.0f;
        float step_z = 1.0f;
        float splat_scale = 0.05f;
        float opacity = 0.9f;
        bool normalize_values = true;
        bool use_threshold = false;
        float emit_threshold = 0.0f;
    };

    // Terrain-surface splat cloud from a 2D height field.
    //
    // Distinct from GaussianSplatFromScalarFieldDesc — see the compile-desc
    // comment in gaussian_splat/gaussian_splat.h for the semantic differences.
    struct GaussianSplatTerrainSurfaceFromHeightFieldDesc
    {
        std::string name;
        wz::asset::AssetKey scalar_field_key{};

        float    height_scale    = 1.0f;
        float    step_x          = 1.0f;
        float    step_z          = 1.0f;
        float    overlap_factor  = 1.25f;
        float    thickness       = 0.0f;        // 0 = auto from step
        uint32_t subsample_step  = 1;
        float    opacity         = 0.95f;
        float    flat_luminance  = 0.55f;
        float    steep_luminance = 0.30f;
    };

    class GaussianSplatAssetModule
    {
    public:
        GaussianSplatAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            GaussianSplatCloudTable& table);

        GaussianSplatCloudAsset create_procedural_cloud(
            const ProceduralGaussianSplatCloudDesc& desc);

        GaussianSplatCloudAsset create_from_ply(
            const GaussianSplatFromPLYDesc& desc);

        GaussianSplatCloudAsset create_from_scalar_field(
            const GaussianSplatFromScalarFieldDesc& desc);

        GaussianSplatCloudAsset create_terrain_surface_from_height_field(
            const GaussianSplatTerrainSurfaceFromHeightFieldDesc& desc);

        GaussianSplatCloudHandle get_cloud(
            const GaussianSplatCloudAsset& asset) const;

        const GaussianSplatCloudData* get_cloud_data(
            GaussianSplatCloudHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        GaussianSplatCloudTable& table_;
    };
}