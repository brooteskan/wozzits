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