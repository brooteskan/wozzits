#pragma once

// engine/assets/renderable/renderable.h

#include <asset/types.h>
#include <engine/assets/gaussian_splat/gaussian_splat_cloud.h>
#include <engine/assets/mesh_render_style/mesh_render_style.h>
#include <engine/assets/render_binding_layout/render_binding_constants.h>

#include <cstdint>
#include <string>
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

        // When true, the footprint fields above (world_origin, world_size,
        // vertical_scale, base_height) were baked from a connected Placement
        // asset (issue #218 Phase 2) and the renderer MUST use them verbatim
        // instead of re-deriving the footprint from the scene-node transform.
        // lattice_world_cell_size (c0) remains mesh-derived at render time even
        // when this is true — the placement governs only the texture->world
        // footprint, never the lattice geometry snap. Default false reproduces
        // the original node-transform-derived behaviour (fully back-compatible).
        bool placement_authoritative = false;
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

    // Per-recipe render dials for a star field (issue #266). The star VS
    // billboards a small camera-facing quad per resident star; star_size scales
    // its base angular footprint (packed into camera_and_diameter.w, the same
    // slot the splat cloud uses for sphere diameter).
    struct StarFieldRenderSettings
    {
        float star_size = 1.0f;
    };

    // The SHADING constants baked from an (optional) MeshRenderStyle dependency
    // into an rhi mesh renderable recipe (issue #195 slice A). This is the
    // absorbed subset of MeshRenderStyleData that the RHI pull-mesh path flows to
    // the shader as space2 root constants — the wireframe/surface layer colours,
    // their emissive strengths, and the overall alpha. It carries NEITHER the
    // style's depth/blend/raster properties (those are now PROGRAM properties:
    // DepthMode/BlendMode/RasterMode on the render_program asset) NOR its
    // geometry-generating parts (field_visualization, mask — deliberately out of
    // scope; the style asset keeps those fields, the recipe ignores them).
    //
    // Default-constructed = "no style": a fully-zero POD (has_style false). The
    // renderer treats a no-style recipe exactly as before slice A (plain 16-float
    // MVP root constants), so a program that never declares the "mesh_style"
    // root constant keeps working untouched.
    struct MeshRenderStyleShading
    {
        bool has_style = false;
        float wireframe_color[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
        float surface_color[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
        float wireframe_emissive = 0.0f;
        float surface_emissive = 0.0f;
        float alpha = 1.0f;
        bool wireframe_enabled = false;
        bool surface_enabled = false;
    };

    // One semantic resource binding baked into a custom renderable recipe
    // (issue #228). The compiler resolved the authored source against the
    // program's layout: `semantic` is the canonical descriptor-semantic name
    // the row declares, `key` the source asset, and `variant` the published
    // rhi identity discriminator (render_binding_sources.h) — so the renderer
    // binds with rhi_asset_identity(key, variant) and no type knowledge.
    struct RhiRenderableBinding
    {
        std::string semantic;
        wz::asset::AssetKey key{};
        std::string variant;
    };

    // One declared constant tail field baked into a custom renderable recipe.
    // The compiler resolved `name` against the layout's declared tail fields
    // and baked the field's absolute dword offset + width, so the renderer
    // writes value[0..dwords) at offset_dwords without re-reading the layout.
    // Since #229 the recipe carries EVERY declared field (value = the
    // graph-authored default, or zero when unauthored) — the full table is
    // the name→offset map per-instance scene-node overrides merge against at
    // pack time.
    struct RhiRenderableConstant
    {
        std::string name;
        uint32_t offset_dwords = 0;
        uint32_t dwords = 0;
        float value[4]{};
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

        // Optional star-field source (issue #266). Like the splat cloud, the
        // renderable has no pull mesh: the renderer binds the resident star point
        // StructuredBuffer (rhi_asset_identity(key, "star_catalog")) into the
        // object SRG at the StarCatalog semantic and records a non-indexed draw of
        // 6 * star_count vertices (camera-facing billboards).
        wz::asset::AssetKey star_catalog_key{};
        StarFieldRenderSettings star{};

        // Optional baked mesh-render-style shading (issue #195 slice A). Only the
        // CPU pull-mesh path (mesh_key) consumes it: when style.has_style is set
        // AND the recipe's program declares the "mesh_style" root constant
        // (binding_layout preset 4), the renderer packs these colours/alpha after
        // the MVP so the pixel shader can read them. Default (has_style false)
        // leaves the recipe's rendering identical to the pre-slice-A pull mesh.
        MeshRenderStyleShading style{};

        // Custom renderable recipe (issue #228, schema 0x70A). When `custom`
        // is set the renderer takes the GENERIC bind path: mesh_key supplies
        // the pull geometry (pulled_mesh_positions/indices, bound only when
        // the program's layout declares them), every extra resource comes
        // from walking `bindings`, and the root-constant block is packed as
        // the head packer named by constants_head followed by the authored
        // tail values — no per-recipe semantic table in the renderer. The
        // other geometry keys / clipmap / splat / style fields stay at their
        // empty defaults on a custom recipe.
        bool custom = false;
        std::vector<RhiRenderableBinding> bindings;
        RenderBindingConstantsHead constants_head =
            RenderBindingConstantsHead::None;
        // Total root-constant block size in dwords (the program block's
        // value_count); 0 = the program declares no root constants.
        uint32_t constants_dwords = 0;
        std::vector<RhiRenderableConstant> constants;

        bool valid() const noexcept
        {
            const bool has_geometry =
                !(mesh_key == wz::asset::AssetKey{})
                || !(gpu_sparse_mesh_key == wz::asset::AssetKey{})
                || !(gaussian_splat_cloud_key == wz::asset::AssetKey{})
                || !(star_catalog_key == wz::asset::AssetKey{});
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
        // Optional MeshRenderStyle dependency (issue #195 slice A). When valid it
        // is connected as a 3rd graph dep; the compiler bakes the style's SHADING
        // constants into the recipe (RhiRenderableRecipe::style). Empty leaves the
        // recipe style at its zero "no style" default.
        wz::asset::AssetKey style_asset{};
    };

    struct GpuSparseMeshRenderableCompileDesc
    {
        wz::asset::AssetKey sparse_mesh_asset{};
        wz::asset::AssetKey render_program_asset{};
    };

    // ClipmapLandscapeRenderableCompileDesc was retired with the 0x708 schema
    // (issue #234). ClipmapLandscapeRenderSettings (above) stays — it is the
    // recipe's terrain footprint, now produced by the 0x70A custom renderable
    // compiler from a Placement dep and consumed by the shared terrain packer.

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

    struct StarFieldRhiRenderableCompileDesc
    {
        // Star catalog (kAssetTypeStarCatalog), resident as a decoded point
        // StructuredBuffer under "star_catalog" (#266).
        wz::asset::AssetKey star_catalog_asset{};
        // Render program (kAssetTypeRenderProgram, SplatPull binding model).
        wz::asset::AssetKey render_program_asset{};
        // Per-field render settings (star size).
        StarFieldRenderSettings settings{};
    };

    // Typed compile desc for the custom renderable recipe (issue #228, schema
    // 0x70A). The graph/editor path recovers the same data from the node's
    // ParamBlock + its port-ordered registration dep keys; this desc is the
    // programmatic form (and the shape #229's per-scene-node synthesis fills).
    // Bindings/constants here are AUTHORED (semantic + source, name + value);
    // the compiler validates them against the wired program's layout and
    // bakes the resolved variant / offsets into the recipe.
    struct CustomRenderableCompileDesc
    {
        struct Binding
        {
            // Canonical descriptor-semantic name (kDescriptorSemanticNames).
            std::string semantic;
            wz::asset::AssetKey asset{};
        };

        struct Constant
        {
            // A tail field name declared by the program's binding layout.
            std::string name;
            float value[4]{};
        };

        // Geometry (kAssetTypeMesh) — the CPU pull-mesh source.
        wz::asset::AssetKey mesh_asset{};
        // Render program (kAssetTypeRenderProgram) with an authored binding
        // layout (#227) and the MeshVertexPull binding model.
        wz::asset::AssetKey render_program_asset{};
        std::vector<Binding> bindings;
        std::vector<Constant> constants;
        // Optional Placement (kAssetTypePlacement) — the world footprint for a
        // CameraSnappedTerrain-head program (issue #233): origin/extent/base
        // define the texture→world mapping the terrain packer uses, shared with
        // a collision reading the same Placement. Empty for non-terrain looks.
        wz::asset::AssetKey placement_asset{};
        // Optional PlacedField (kAssetTypePlacedField, issue #223) — a single
        // upstream bundling the height field + its Placement. When set it
        // SUPERSEDES the separate scalar_field_texture binding + placement port:
        // the combiner's field becomes the height binding and its placement the
        // footprint, so a terrain look references one node instead of wiring the
        // field and frame separately (and they cannot drift apart). Empty = use
        // the separate binding + placement_asset above.
        wz::asset::AssetKey placed_field_asset{};
    };
}
