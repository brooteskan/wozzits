#pragma once

// engine/assets/light/light.h
//
// CPU-side authored lighting assets. Scene components reference these asset
// definitions; renderer-specific light records are projections from the scene.

#include <asset/types.h>

#include <scene/compile/compiled_scene.h>

#include <cmath>
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

    enum class HDRIEnvironmentFormat : uint8_t
    {
        Auto,
        RadianceHDR,
        OpenEXR,
    };

    struct HDRIEnvironmentData
    {
        wz::asset::AssetKey source_file{};
        HDRIEnvironmentFormat format = HDRIEnvironmentFormat::Auto;

        // Scene environment controls. A sky sphere/environment component can
        // decide which of these channels it consumes.
        float exposure = 0.0f;
        float rotation_x_radians = 0.0f;
        float rotation_y_radians = 0.0f;
        float rotation_z_radians = 0.0f;
        float lighting_intensity = 1.0f;
        float reflection_intensity = 1.0f;
        float background_intensity = 1.0f;
        uint32_t lighting_sample_resolution = 1024;

        // Diffuse environment-light metadata derived from the HDR image or
        // authored explicitly. This is not a scene AmbientLighting component;
        // it is shader-facing environment radiance for consumers such as
        // terrain, sky, and later material/probe systems.
        float environment_light_color[3]{ 1.0f, 1.0f, 1.0f };
        float environment_light_intensity = 0.0f;

        // Optional dominant-light metadata for editor actions such as
        // "align directional light to HDRI sun". Confidence 0 means absent.
        float dominant_light_direction[3]{ 0.0f, -1.0f, 0.0f };
        float dominant_light_color[3]{ 1.0f, 1.0f, 1.0f };
        float dominant_light_intensity = 0.0f;
        float dominant_light_confidence = 0.0f;

        bool valid() const noexcept
        {
            const bool has_source =
                !(source_file == wz::asset::AssetKey{});
            const bool finite_controls =
                std::isfinite(exposure)
                && std::isfinite(rotation_x_radians)
                && std::isfinite(rotation_y_radians)
                && std::isfinite(rotation_z_radians)
                && std::isfinite(lighting_intensity)
                && std::isfinite(reflection_intensity)
                && std::isfinite(background_intensity)
                && std::isfinite(environment_light_color[0])
                && std::isfinite(environment_light_color[1])
                && std::isfinite(environment_light_color[2])
                && std::isfinite(environment_light_intensity);
            const bool nonnegative_controls =
                lighting_intensity >= 0.0f
                && reflection_intensity >= 0.0f
                && background_intensity >= 0.0f
                && environment_light_intensity >= 0.0f;
            const bool finite_dominant =
                std::isfinite(dominant_light_direction[0])
                && std::isfinite(dominant_light_direction[1])
                && std::isfinite(dominant_light_direction[2])
                && std::isfinite(dominant_light_color[0])
                && std::isfinite(dominant_light_color[1])
                && std::isfinite(dominant_light_color[2])
                && std::isfinite(dominant_light_intensity)
                && std::isfinite(dominant_light_confidence);

            return has_source
                && finite_controls
                && nonnegative_controls
                && lighting_sample_resolution > 0
                && finite_dominant
                && dominant_light_intensity >= 0.0f
                && dominant_light_confidence >= 0.0f
                && dominant_light_confidence <= 1.0f;
        }
    };

    using HDRIEnvironmentCompileDesc = HDRIEnvironmentData;

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

    class HDRIEnvironmentTable
    {
    public:
        HDRIEnvironmentTable();

        wz::asset::ResourceHandle add(HDRIEnvironmentData environment);
        const HDRIEnvironmentData* get(
            wz::asset::ResourceHandle handle) const;
        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            HDRIEnvironmentData environment;
        };

        std::vector<Slot> slots_;
    };

    wz::scene::LightType direct_light_kind_to_scene_light_type(
        DirectLightKind kind) noexcept;

} // namespace wz::engine::assets
