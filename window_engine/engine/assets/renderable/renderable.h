#pragma once

// engine/assets/renderable/renderable.h

#include <asset/types.h>
#include <engine/assets/gaussian_splat/gaussian_splat_cloud.h>
#include <engine/assets/mesh_render_style/mesh_render_style.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    enum class RenderableKind : uint8_t
    {
        Mesh,
        GaussianSplatCloud,
        ScalarField,
        VectorField,
    };

    enum class RenderDomain : uint8_t
    {
        Debug,
        Sky,
        Opaque,
        Transparent,
        Splat,
    };

    enum class BuiltinRenderProgram : uint8_t
    {
        MeshWireframeDebug,
        MeshWireframeDepthDebug,
        MeshDepthPrepassDebug,
        MeshWireframeAlpha,
        MeshSurface,
        MeshSurfaceAlpha,
        MeshFieldHeatmap,
        TerrainMeshSurface,
        GaussianSplatDebug,
        TerrainSurfelSurface,    // IA-based splat with depth R/W for terrain
        ScalarFieldDebug,
        GaussianSplatPullDebug,  // pull-based splat: no IA, SRV at t0
        GaussianSplatNeighborhoodColorBlend,  // SplatPull + LOD color blend modes
        GaussianSplatTerrainCoverageDebug,    // SplatPull + coverage modes (depth-writing)
        SkySurface,

        Count  // sentinel — keep last
    };

    static constexpr size_t kBuiltinRenderProgramCount =
        static_cast<size_t>(BuiltinRenderProgram::Count);

    enum RenderPolicyFlags : uint32_t
    {
        RenderPolicy_None = 0,
        RenderPolicy_Wireframe = 1u << 0,
        RenderPolicy_AlphaBlend = 1u << 1,
        RenderPolicy_DepthTest = 1u << 2,
        RenderPolicy_DepthWrite = 1u << 3,
    };

    enum class TerrainLightingMode : uint8_t
    {
        SceneLights = 0,
        HDRIEnvironment,
    };

    struct TerrainLightingData
    {
        TerrainLightingMode mode = TerrainLightingMode::SceneLights;

        // HDRI lighting starts as authored metadata so terrain can move onto
        // a real environment-lighting path before generated irradiance maps,
        // prefiltered cubemaps, or SH probes exist. Those GPU products can be
        // added beside these constants without routing HDRI through scene
        // ambient/directional light nodes.
        float environment_color[3]{ 1.0f, 1.0f, 1.0f };
        float environment_intensity = 0.25f;
        float dominant_light_direction[3]{ 0.0f, -1.0f, 0.0f };
        float dominant_light_color[3]{ 1.0f, 1.0f, 1.0f };
        float dominant_light_intensity = 0.0f;
        float sky_visibility_strength = 1.0f;
        float normal_lighting_strength = 1.0f;
        float terrain_bounce_strength = 0.0f;
    };

    struct RenderableAssetData
    {
        RenderableKind kind{};
        wz::asset::AssetKey source_asset{};

        // Optional companion asset key (e.g. GaussianSplatColorLOD).
        // Empty when not used.  The GPU realize step consults this to find
        // and pass through derived data to the upload pipeline.
        wz::asset::AssetKey companion_asset{};

        // Optional mesh-derived field used by mesh field visualization styles.
        // Empty when the mesh style uses only constant wire/surface colors.
        wz::asset::AssetKey mesh_field_visualization_asset{};

        BuiltinRenderProgram program{};
        RenderDomain domain{};
        uint32_t policy_flags = RenderPolicy_None;

        // Optional: handle into RenderProgramTable.  Invalid until set by
        // call-site code that has resolved a RenderProgramAsset.
        // When valid, the submit path prefers this over the BuiltinRenderProgram.
        wz::asset::ResourceHandle render_program{};

        float bounds_min[3]{};
        float bounds_max[3]{};

        TerrainLightingData terrain_lighting{};
        float terrain_target_pixels_per_triangle = 0.0f;
        std::vector<GaussianSplatCloudData> terrain_far_splat_chunks;
        MeshRenderStyleData mesh_style{};

        bool valid() const noexcept;
    };

    class RenderableAssetTable
    {
    public:
        RenderableAssetTable();

        wz::asset::ResourceHandle add(RenderableAssetData data);
        const RenderableAssetData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        std::vector<RenderableAssetData> renderables_;
        std::vector<uint32_t> epochs_;
    };

    struct MeshWireframeRenderableCompileDesc
    {
        wz::asset::AssetKey mesh_asset{};
        BuiltinRenderProgram program = BuiltinRenderProgram::MeshWireframeDebug;
        RenderDomain domain = RenderDomain::Debug;
        uint32_t policy_flags = RenderPolicy_Wireframe;
    };

    struct MeshStyledRenderableCompileDesc
    {
        wz::asset::AssetKey mesh_asset{};
        wz::asset::AssetKey style_asset{};
        wz::asset::AssetKey mesh_field_visualization_asset{};
        wz::asset::AssetKey render_program_asset{};
    };

    struct GaussianSplatDebugRenderableCompileDesc
    {
        wz::asset::AssetKey splat_cloud_asset{};

        // Optional derived color-LOD asset key.  Empty if not used.
        wz::asset::AssetKey color_lod_asset{};
    };

    struct ScalarFieldDebugRenderableCompileDesc
    {
        wz::asset::AssetKey scalar_field_asset{};
    };

    struct TerrainDebugRenderableCompileDesc
    {
        wz::asset::AssetKey terrain_asset{};
        BuiltinRenderProgram mesh_program =
            BuiltinRenderProgram::MeshWireframeDepthDebug;
        RenderDomain domain = RenderDomain::Debug;
        uint32_t mesh_policy_flags =
            RenderPolicy_Wireframe
            | RenderPolicy_DepthTest
            | RenderPolicy_DepthWrite;
    };

    struct TerrainSurfaceRenderableCompileDesc
    {
        wz::asset::AssetKey terrain_asset{};
        wz::asset::AssetKey visual_proxy_asset{};
        BuiltinRenderProgram mesh_program =
            BuiltinRenderProgram::TerrainMeshSurface;
        RenderDomain domain = RenderDomain::Opaque;
        uint32_t mesh_policy_flags =
            RenderPolicy_DepthTest
            | RenderPolicy_DepthWrite;
        TerrainLightingData lighting{};
        float target_pixels_per_triangle = 0.0f;
    };
}
