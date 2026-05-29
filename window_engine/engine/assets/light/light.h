#pragma once

// engine/assets/light/light.h
//
// CPU-side authored lighting assets. Scene components reference these asset
// definitions; renderer-specific light records are projections from the scene.

#include <asset/types.h>

#include <scene/compile/compiled_scene.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    enum class DirectLightKind : uint8_t
    {
        Directional,
        Point,
        Spot,
    };

    struct DirectLightData
    {
        DirectLightKind kind = DirectLightKind::Directional;
        float color[3]{ 1.0f, 1.0f, 1.0f };
        float intensity = 1.0f;
        float range = 10.0f;
        float inner_cone_radians = 0.4f;
        float outer_cone_radians = 0.8f;

        bool valid() const noexcept
        {
            return intensity >= 0.0f
                && range >= 0.0f
                && inner_cone_radians >= 0.0f
                && outer_cone_radians >= inner_cone_radians;
        }
    };

    using DirectLightCompileDesc = DirectLightData;

    enum class AmbientLightingMode : uint8_t
    {
        Constant,
        FieldModulated,
    };

    enum class AmbientLightingDomainMapping : uint8_t
    {
        TerrainUV,
        WorldXZ,
    };

    struct AmbientLightingData
    {
        AmbientLightingMode mode = AmbientLightingMode::Constant;
        float color[3]{ 1.0f, 1.0f, 1.0f };
        float intensity = 0.2f;
        wz::asset::AssetKey intensity_field{};
        wz::asset::AssetKey color_field{};
        AmbientLightingDomainMapping domain_mapping =
            AmbientLightingDomainMapping::TerrainUV;

        bool valid() const noexcept
        {
            return intensity >= 0.0f;
        }
    };

    using AmbientLightingCompileDesc = AmbientLightingData;

    class DirectLightTable
    {
    public:
        DirectLightTable();

        wz::asset::ResourceHandle add(DirectLightData light);
        const DirectLightData* get(wz::asset::ResourceHandle handle) const;
        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            DirectLightData light;
        };

        std::vector<Slot> slots_;
    };

    class AmbientLightingTable
    {
    public:
        AmbientLightingTable();

        wz::asset::ResourceHandle add(AmbientLightingData lighting);
        const AmbientLightingData* get(wz::asset::ResourceHandle handle) const;
        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            AmbientLightingData lighting;
        };

        std::vector<Slot> slots_;
    };

    wz::scene::LightType direct_light_kind_to_scene_light_type(
        DirectLightKind kind) noexcept;

} // namespace wz::engine::assets
