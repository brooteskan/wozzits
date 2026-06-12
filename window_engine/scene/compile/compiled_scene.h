#pragma once

// wz/scene/compiled_scene.h

#include <asset/types.h>
#include <stats/scene_render_storage.h>
#include <graph/static_polytree.h>
#include <scene/geometry.h>
#include <scene/compile/scene_node_class.h>
#include <cstdint>
#include <limits>
#include <span>
#include <memory>

namespace wz::engine::assets
{
    struct TerrainVisualProxyData;
}

namespace wz::scene {

    using wz::core::graph::NodeHandle;

    // ─── Asset handles ────────────────────────────────────────────────────────────

    using MeshHandle     = uint32_t;
    using MaterialHandle = uint32_t;
    using SplatHandle    = uint32_t;

    static constexpr MeshHandle     INVALID_MESH     = 0xFFFF'FFFFu;
    static constexpr MaterialHandle INVALID_MATERIAL = 0xFFFF'FFFFu;
    static constexpr SplatHandle    INVALID_SPLAT    = 0xFFFF'FFFFu;

    struct TerrainProxyId
    {
        wz::asset::AssetKey key{};

        [[nodiscard]] bool valid() const noexcept
        {
            return key != wz::asset::AssetKey{};
        }

        bool operator==(const TerrainProxyId&) const = default;
    };

    struct TerrainChunkId
    {
        uint32_t value = 0;

        bool operator==(const TerrainChunkId&) const = default;
    };

    inline constexpr TerrainChunkId kInvalidTerrainChunkId{
        (std::numeric_limits<uint32_t>::max)()
    };

    struct TerrainLodId
    {
        uint32_t value = 0;

        bool operator==(const TerrainLodId&) const = default;
    };

    enum class TerrainVisualRepresentationKind : uint8_t
    {
        MeshChunks = 0,
        GridTiles,
        SurfelCloud,
    };

    enum class TerrainVisualProxyBoundaryEdge : uint8_t
    {
        NegativeX,
        PositiveX,
        NegativeZ,
        PositiveZ,
    };

    struct TerrainVisualChunkBoundaryMetadata
    {
        uint32_t boundary_flags = 0;
        TerrainChunkId negative_x_neighbor = kInvalidTerrainChunkId;
        TerrainChunkId positive_x_neighbor = kInvalidTerrainChunkId;
        TerrainChunkId negative_z_neighbor = kInvalidTerrainChunkId;
        TerrainChunkId positive_z_neighbor = kInvalidTerrainChunkId;
    };


    // ─── Pipeline classification (legacy) ─────────────────────────────────────────
    //
    // RenderPipeline is retained as a parameter type for classify_legacy_renderable()
    // in legacy_classification.h. It is no longer a field of RenderableDescriptor.
    // New code should populate RenderableDescriptor::node_class directly.

    enum class RenderPipeline : uint8_t {
        None,
        OpaqueGeometry,
        TransparentGeometry,
        Splat,
        Particle,
    };


    // ─── Compiled output metadata types ──────────────────────────────────────────
    //
    // CompiledOutputKind names the compiled output array a source node maps into.
    // It is intentionally narrower than SceneNodeClass — it describes where
    // compiled output lives, not the full identity of the source node.
    //
    // CompiledOutputRef is a (kind, index) pair stored in the per-node metadata
    // table so the compiler can answer "which compiled record came from node N?"
    //
    // These types are declared here; the metadata table and buffer live in
    // CompiledSceneStorage once that step is implemented.

    enum class CompiledOutputKind : uint8_t
    {
        None = 0,

        SurfacePrimitive,   // opaque or transparent surface record (current or future)
        Splat,
        Particle,
        Light,
        TerrainVisualInstance,
        Camera,
        Volume,
        Decal,
    };

    static constexpr uint32_t INVALID_COMPILED_OUTPUT = 0xFFFF'FFFFu;

    struct CompiledOutputRef
    {
        CompiledOutputKind kind  = CompiledOutputKind::None;
        uint32_t           index = INVALID_COMPILED_OUTPUT;
    };


    // ─── Pipeline primitive types ─────────────────────────────────────────────────
    //
    // Stable primitives carry no view-dependent data.
    // View-dependent primitives (transparent, particle) carry depth
    // because their counts are small and they are rebuilt entirely by update_view().
    // Splat depth is separate — splat counts can be large, depth is the only
    // thing that changes per frame.

    struct OpaqueGeometryPrimitive {
        wz::math::Mat4 world{};
        AABB           bounds{};
        MeshHandle     mesh{ INVALID_MESH };
        MaterialHandle material{ INVALID_MATERIAL };
    };

    struct TransparentGeometryPrimitive {
        wz::math::Mat4 world{};
        AABB           bounds{};
        MeshHandle     mesh{ INVALID_MESH };
        MaterialHandle material{ INVALID_MATERIAL };
        float          depth{ 0.f };
    };

    struct SplatPrimitive {
        wz::math::Vec3 position{};
        wz::math::Vec3 scale{ 1.f, 1.f, 1.f };
        wz::math::Vec4 rotation{ 0.f, 0.f, 0.f, 1.f };
        wz::math::Vec3 color{ 1.f, 1.f, 1.f };
        float       opacity{ 1.f };
        SplatHandle cloud_handle{ INVALID_SPLAT };  // GPU cloud registry handle; INVALID_SPLAT = legacy path
        uint32_t    cloud_local_index{ 0 };         // index into t0 StructuredBuffer<Splat> for this cloud
        // World-space AABB of the cloud-instance footprint.  Populated by
        // scene_compiler from RenderableDescriptor::local_bounds transformed
        // by the node's world matrix.  Used for frustum culling instead of
        // the per-splat center (which causes large clouds to disappear when
        // their origin leaves the frustum even though their bounds still
        // intersect it).  When local_bounds is degenerate (min == max) the
        // culling code treats the primitive as always visible.
        AABB        bounds{};
        // no depth — lives in a parallel splat_depths span
    };

    struct ParticlePrimitive {
        wz::math::Mat4 world{};
        wz::math::Vec4 color{ 1.f, 1.f, 1.f, 1.f };
        MeshHandle     mesh{ INVALID_MESH };
        MaterialHandle material{ INVALID_MATERIAL };
        float          depth{ 0.f };
    };

    struct TerrainVisualInstance {
        wz::math::Mat4 world{};
        AABB           bounds{};
        TerrainProxyId terrain_proxy_id{};
        wz::asset::AssetKey visual_proxy_asset{};
        // Non-owning CPU metadata owned by TerrainVisualProxyTable. The asset
        // library/table must outlive compiled scene views that select terrain LODs.
        const wz::engine::assets::TerrainVisualProxyData* visual_proxy_data = nullptr;
        uint32_t       visual_proxy_chunk_count{ 0 };
        MaterialHandle material{ INVALID_MATERIAL };
        bool           visible{ true };
    };

    struct TerrainLodSelectionParams {
        float viewport_width = 1920.0f;
        float viewport_height = 1080.0f;
        float fov_y_radians = 1.0471975512f;
        float pixel_error_threshold = 2.0f;
        uint32_t triangle_budget = (std::numeric_limits<uint32_t>::max)();
        float hysteresis_fraction = 0.2f;
        bool enforce_neighbor_lod_delta = false;
        uint32_t max_neighbor_lod_delta = 1;
        bool enable_lod_transitions = false;
        bool enable_surfel_lods = false;
        float surfel_target_coverage_px = 1.0f;
        // 0 disables density rejection. Asset density is source triangles per
        // proxy XZ unit squared; screen density is selected triangles per
        // projected pixel.
        float max_asset_triangle_density = 0.0f;
        float max_screen_triangle_density = 0.0f;
    };

    struct TerrainLodChoice {
        uint32_t terrain_instance_index = 0;
        TerrainChunkId chunk_id{};
        TerrainVisualRepresentationKind representation_kind =
            TerrainVisualRepresentationKind::MeshChunks;
        TerrainLodId lod_id{};
        float projected_error_px = 0.0f;
        float projected_area_px = 0.0f;
        float asset_triangle_density = 0.0f;
        float screen_triangle_density = 0.0f;
        float priority = 0.0f;
    };


    // ─── Light record ─────────────────────────────────────────────────────────────

    enum class LightType : uint8_t { Directional, Point, Spot, Ambient };

    struct LightRecord {
        wz::math::Vec3 position{};
        wz::math::Vec3 direction{};
        wz::math::Vec3 color{ 1.f, 1.f, 1.f };
        float     intensity{ 1.f };
        float     range{ 10.f };
        LightType type{ LightType::Point };
    };


    // ─── ViewData ─────────────────────────────────────────────────────────────────

    struct ViewData {
        wz::math::Mat4 view{};
        wz::math::Mat4 projection{};
        wz::math::Mat4 view_projection{};
        wz::math::Vec3 camera_position{};
        TerrainLodSelectionParams terrain_lod{};
    };


    // ─── CompiledSceneView ────────────────────────────────────────────────────────
    //
    // Non-owning view into CompiledSceneStorage.
    // Two categories of data with different update frequencies:
    //
    // Stable (compile() only):
    //   opaque, splats, lights — valid until scene structure changes
    //
    // View-dependent (update_view() every frame camera moves):
    //   transparent, particles — small counts, rebuilt entirely
    //   splat_depths           — parallel to splats, large count, in-place update

    struct CompiledSceneView {
        // Stable
        std::span<const OpaqueGeometryPrimitive> opaque;
        std::span<const SplatPrimitive>          splats;
        std::span<const LightRecord>             lights;
        std::span<const TerrainVisualInstance>   terrain_instances;

        // View-dependent
        std::span<const TransparentGeometryPrimitive> transparent;
        std::span<const ParticlePrimitive>            particles;
        std::span<float>                              splat_depths; // mutable, parallel to splats
        std::span<const TerrainLodChoice>             terrain_lod_choices;

        ViewData view{};
    };


    // ─── CompiledSceneMetadataView ───────────────────────────────────────────────
    //
    // Sidecar provenance data parallel to the compiled output spans.
    // Answers two questions:
    //   Forward:  which compiled record did source node N produce?  (node_to_output)
    //   Reverse:  which source node produced compiled output[i]?   (source_nodes spans)
    //
    // node_to_output is indexed by NodeHandle. Nodes that produce no output
    // have kind == None and index == INVALID_COMPILED_OUTPUT.
    //
    // Short-term naming: opaque_source_nodes and transparent_source_nodes mirror
    // the current packed-array split. These are implementation partitions, not
    // scene identity categories. The long-term direction collapses both into
    // surface_source_nodes with per-primitive SurfaceClass.
    //
    // node_to_output uses SurfacePrimitive for both opaque and transparent records.
    // The index is into the respective array (opaque or transparent). To resolve
    // which array, check the source descriptor's RenderPipeline or use the reverse
    // spans.

    struct CompiledSceneMetadataView
    {
        // Forward: NodeHandle → compiled output
        std::span<const CompiledOutputRef> node_to_output;

        // Reverse: compiled output index → source NodeHandle
        std::span<const NodeHandle> opaque_source_nodes;       // parallel to CompiledSceneView::opaque
        std::span<const NodeHandle> transparent_source_nodes;  // parallel to CompiledSceneView::transparent
        std::span<const NodeHandle> splat_source_nodes;        // parallel to CompiledSceneView::splats
        std::span<const NodeHandle> particle_source_nodes;     // parallel to CompiledSceneView::particles
        std::span<const NodeHandle> light_source_nodes;        // parallel to CompiledSceneView::lights
        std::span<const NodeHandle> terrain_source_nodes;      // parallel to CompiledSceneView::terrain_instances
    };


    // ─── Storage ──────────────────────────────────────────────────────────────────
    //
    // Three separate allocations — stable, view-dependent, and metadata.
    // update_view() only rewrites view_buffer, never stable_buffer or metadata_buffer.
    // compile() rewrites all three.

    struct CompiledSceneStorage {
        std::unique_ptr<std::byte[]> stable_buffer;
        size_t                       stable_bytes = 0; // owned capacity of stable_buffer

        std::unique_ptr<std::byte[]> view_buffer;
        size_t                       view_bytes = 0; // owned capacity of view_buffer

        std::unique_ptr<std::byte[]> metadata_buffer;
        size_t                       metadata_bytes = 0; // owned capacity of metadata_buffer

        wz::scene_render::AllocationStats stable_stats{};
        wz::scene_render::AllocationStats view_stats{};
        wz::scene_render::AllocationStats metadata_stats{};

        CompiledSceneView         scene;
        CompiledSceneMetadataView metadata;
        std::span<TerrainLodChoice> terrain_lod_choice_capacity;
    };


    // ─── RenderableDescriptor ─────────────────────────────────────────────────────

    struct SplatDescriptor {
        wz::math::Vec3 scale{ 1.f, 1.f, 1.f };
        wz::math::Vec4 rotation{ 0.f, 0.f, 0.f, 1.f };
        wz::math::Vec3 color{ 1.f, 1.f, 1.f };
        float       opacity{ 1.f };
        SplatHandle cloud_handle{ INVALID_SPLAT };  // GPU cloud registry handle; INVALID_SPLAT = legacy path
        uint32_t    cloud_local_index{ 0 };         // index into the cloud's t0 buffer (must match upload order)
    };

    struct RenderableDescriptor {
        SceneNodeClass  node_class{};           // orthogonal classification tuple
        MeshHandle      mesh{ INVALID_MESH };
        MaterialHandle  material{ INVALID_MATERIAL };
        AABB            local_bounds{};
        SplatDescriptor splat_data{};
        wz::asset::AssetKey terrain_visual_proxy_asset{};
        TerrainProxyId terrain_proxy_id{};
        // Non-owning CPU metadata owned by TerrainVisualProxyTable. Resource
        // resolvers populate this for terrain LOD selection; they do not transfer
        // ownership to scene-render.
        const wz::engine::assets::TerrainVisualProxyData* terrain_visual_proxy_data = nullptr;
        uint32_t        terrain_visual_chunk_count{ 0 };
        bool            visible{ true };
    };

} // namespace wz::scene
