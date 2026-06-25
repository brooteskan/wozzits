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
        MeshMaskStyle,
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

    // World-space settings for a geometry-clipmap landscape renderable. The
    // lattice mesh is authored in unitless grid space centered at the origin;
    // these fields place its finest cell in world meters and describe how the
    // height ScalarField maps onto the world XZ plane. Slice 3b packs these
    // (together with the per-frame camera-snapped offset) into shader
    // constants; the renderer-agnostic view transform is computed by
    // engine/rendering/clipmap_view.h.
    struct ClipmapLandscapeRenderSettings
    {
        // World XZ footprint covered by the height texture, in meters. The
        // texture's [0,1] UV space maps linearly onto
        // [world_origin, world_origin + world_size].
        float world_origin[2]{ 0.0f, 0.0f };
        float world_size[2]{ 1.0f, 1.0f };

        // Heightmap value (R32, typically [0,1] or raw elevation) is scaled by
        // vertical_scale and offset by base_height to produce world Y.
        float vertical_scale = 1.0f;
        float base_height = 0.0f;

        // World meters spanned by one finest lattice cell. The lattice's
        // unitless cell size is scaled by this to reach world units, and it is
        // the natural quantum for view-snapping (see clipmap_view.h).
        float lattice_world_cell_size = 1.0f;

        // When true (the procedural-lattice case) the geometry is camera-snapped
        // and scaled into world space — the lattice follows the camera. When
        // false, the supplied mesh is treated as already in world space (no
        // camera translation, unit scale), so an arbitrary static mesh (e.g. a
        // gpu_sparse_mesh) is displaced in place by the height field (#205).
        bool view_snapped = true;
    };

    // Per-splat-cloud render settings for a GaussianSplatCloud RHI renderable
    // (issue #208). The cloud is uploaded as a resident StructuredBuffer of
    // decoded splats and rendered as camera-facing gaussian quads; splat_size
    // scales the per-splat (already decoded) world-space axis sizes when the VS
    // builds each quad's screen-space footprint.
    struct GaussianSplatCloudRenderSettings
    {
        float splat_size = 1.0f;
    };

    struct RhiRenderableRecipe
    {
        // Exactly one geometry source is set:
        //   mesh_key                 — CPU pull-mesh upload source (rhi pull-mesh)
        //   gpu_sparse_mesh_key      — GPU-resident pull source (#190 gpu_sparse_mesh)
        //   gaussian_splat_cloud_key — resident splat StructuredBuffer (#208)
        wz::asset::AssetKey mesh_key{};
        wz::asset::AssetKey gpu_sparse_mesh_key{};
        wz::asset::AssetKey program_key{};

        // Optional clipmap-landscape binding. Empty for mesh / gpu_sparse
        // recipes — their valid() behavior is unchanged. When set,
        // height_texture_key names the resident R32 height ScalarField the
        // clipmap vertex shader samples (slice 3b), and clipmap carries the
        // world-space placement / mapping. mesh_key holds the lattice mesh.
        wz::asset::AssetKey height_texture_key{};
        ClipmapLandscapeRenderSettings clipmap{};

        // Optional gaussian-splat-cloud source (issue #208). When set, the
        // renderable has no pull mesh: the renderer binds the resident decoded
        // splat StructuredBuffer (rhi_asset_identity(key, "splat_cloud")) into
        // the object SRG at the SplatCloud semantic and records a non-indexed
        // DrawInstanced of 4 * splat_count vertices (camera-facing quads).
        wz::asset::AssetKey gaussian_splat_cloud_key{};
        GaussianSplatCloudRenderSettings splat{};

        bool valid() const noexcept
        {
            const bool has_geometry =
                !(mesh_key == wz::asset::AssetKey{})
                || !(gpu_sparse_mesh_key == wz::asset::AssetKey{})
                || !(gaussian_splat_cloud_key == wz::asset::AssetKey{});
            return has_geometry
                && !(program_key == wz::asset::AssetKey{});
        }
    };

    class RhiRenderableTable
    {
    public:
        RhiRenderableTable();

        wz::asset::ResourceHandle add(RhiRenderableRecipe recipe);
        const RhiRenderableRecipe* get(
            wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        std::vector<RhiRenderableRecipe> recipes_;
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

    struct RhiPullMeshRenderableCompileDesc
    {
        wz::asset::AssetKey mesh_asset{};
        wz::asset::AssetKey render_program_asset{};
    };

    struct GpuSparseMeshRenderableCompileDesc
    {
        wz::asset::AssetKey sparse_mesh_asset{};
        wz::asset::AssetKey render_program_asset{};
    };

    struct ClipmapLandscapeRenderableCompileDesc
    {
        // Lattice geometry (kAssetTypeMesh, the clipmap lattice recipe).
        wz::asset::AssetKey lattice_mesh_asset{};
        // Height source (kAssetTypeScalarField, resident as an R32 texture).
        wz::asset::AssetKey height_field_asset{};
        // Render program (kAssetTypeRenderProgram).
        wz::asset::AssetKey render_program_asset{};
        // World-space placement / mapping for the landscape.
        ClipmapLandscapeRenderSettings settings{};
    };

    struct GaussianSplatCloudRhiRenderableCompileDesc
    {
        // Splat cloud (kAssetTypeGaussianSplatCloud), resident as a decoded
        // splat StructuredBuffer (#208).
        wz::asset::AssetKey splat_cloud_asset{};
        // Render program (kAssetTypeRenderProgram, SplatPull binding model).
        wz::asset::AssetKey render_program_asset{};
        // Per-cloud render settings (splat size).
        GaussianSplatCloudRenderSettings settings{};
    };
}
