#pragma once

// engine/assets/light_asset_module.h

#include <asset/system.h>
#include <asset/types.h>

#include <logging/logger.h>

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

    class LightAssetModule
    {
    public:
        LightAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            DirectLightTable& direct_light_table,
            AmbientLightingTable& ambient_lighting_table);

        DirectLightAsset create_direct_light(const DirectLightDesc& desc);
        AmbientLightingAsset create_ambient_lighting(
            const AmbientLightingDesc& desc);

        DirectLightHandle get_direct_light(const DirectLightAsset& asset) const;
        AmbientLightingHandle get_ambient_lighting(
            const AmbientLightingAsset& asset) const;

        const DirectLightData* get_direct_light_data(
            DirectLightHandle handle) const;
        const AmbientLightingData* get_ambient_lighting_data(
            AmbientLightingHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        DirectLightTable& direct_light_table_;
        AmbientLightingTable& ambient_lighting_table_;
    };

} // namespace wz::engine::assets
