#pragma once

// engine/assets/light_asset_module.h

#include <asset/system.h>
#include <asset/types.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <engine/assets/file_carrier_asset_module.h>
#include <engine/assets/light/light.h>

#include <string>

namespace wz::engine::assets
{
    struct DirectLightDesc
    {
        std::string name;
        DirectLightKind kind = DirectLightKind::Directional;
        float color[3]{ 1.0f, 1.0f, 1.0f };
        float intensity = 1.0f;
        float range = 10.0f;
        float inner_cone_radians = 0.4f;
        float outer_cone_radians = 0.8f;
    };

    struct AmbientLightingDesc
    {
        std::string name;
        AmbientLightingMode mode = AmbientLightingMode::Constant;
        float color[3]{ 1.0f, 1.0f, 1.0f };
        float intensity = 0.2f;
        wz::asset::AssetKey intensity_field{};
        wz::asset::AssetKey color_field{};
        AmbientLightingDomainMapping domain_mapping =
            AmbientLightingDomainMapping::TerrainUV;
    };

    struct HDRIEnvironmentDesc
    {
        std::string name;
        wz::fs::Path path;
        HDRIEnvironmentFormat format = HDRIEnvironmentFormat::Auto;
        float exposure = 0.0f;
        float rotation_x_radians = 0.0f;
        float rotation_y_radians = 0.0f;
        float rotation_z_radians = 0.0f;
        float lighting_intensity = 1.0f;
        float reflection_intensity = 1.0f;
        float background_intensity = 1.0f;
        uint32_t lighting_sample_resolution = 1024;
        float environment_light_color[3]{ 1.0f, 1.0f, 1.0f };
        float environment_light_intensity = 0.0f;
        float dominant_light_direction[3]{ 0.0f, -1.0f, 0.0f };
        float dominant_light_color[3]{ 1.0f, 1.0f, 1.0f };
        float dominant_light_intensity = 0.0f;
        float dominant_light_confidence = 0.0f;
    };

    struct DirectLightAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct AmbientLightingAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct HDRIEnvironmentAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct DirectLightHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept { return handle.valid(); }
    };

    struct AmbientLightingHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept { return handle.valid(); }
    };

    struct HDRIEnvironmentHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept { return handle.valid(); }
    };

    class LightAssetModule
    {
    public:
        LightAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            FileCarrierAssetModule& files,
            DirectLightTable& direct_light_table,
            AmbientLightingTable& ambient_lighting_table,
            HDRIEnvironmentTable& hdri_environment_table);

        DirectLightAsset create_direct_light(const DirectLightDesc& desc);
        AmbientLightingAsset create_ambient_lighting(
            const AmbientLightingDesc& desc);
        HDRIEnvironmentAsset create_hdri_environment(
            const HDRIEnvironmentDesc& desc);

        DirectLightHandle get_direct_light(const DirectLightAsset& asset) const;
        AmbientLightingHandle get_ambient_lighting(
            const AmbientLightingAsset& asset) const;
        HDRIEnvironmentHandle get_hdri_environment(
            const HDRIEnvironmentAsset& asset) const;

        const DirectLightData* get_direct_light_data(
            DirectLightHandle handle) const;
        const AmbientLightingData* get_ambient_lighting_data(
            AmbientLightingHandle handle) const;
        const HDRIEnvironmentData* get_hdri_environment_data(
            HDRIEnvironmentHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        FileCarrierAssetModule& files_;
        DirectLightTable& direct_light_table_;
        AmbientLightingTable& ambient_lighting_table_;
        HDRIEnvironmentTable& hdri_environment_table_;
    };

} // namespace wz::engine::assets
