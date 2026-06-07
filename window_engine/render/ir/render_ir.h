#pragma once

// wz/render/render_ir.h
//
// Backend-agnostic intermediate representation.
// Consumes CompiledScene. Produces sorted, culled draw references.
// No GPU types. No backend dependencies.

#include <stats/scene_render_storage.h>
#include <scene/compile/compiled_scene.h>
#include <graph/static_dag.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>

namespace wz::render {

    using namespace wz::scene;

    // ─── DrawRef ──────────────────────────────────────────────────────────────────
    //
    // Lightweight reference into a CompiledScene pipeline span.
    // Sorting operates on DrawRefs — no data is moved.

    struct DrawRef {
        uint32_t index;    // into the relevant CompiledScene span
        uint64_t sort_key; // retained for debugging and secondary sorts
    };

    enum class TerrainDrawRefKind : uint8_t {
        ChunkLod = 0,
        LodTransition,
    };

    struct TerrainDrawRef {
        TerrainDrawRefKind kind{ TerrainDrawRefKind::ChunkLod };
        uint32_t terrain_instance_index = 0;
        wz::engine::assets::TerrainChunkId chunk_id{};
        wz::engine::assets::TerrainChunkId neighbor_chunk_id{
            wz::engine::assets::kInvalidTerrainChunkId
        };
        wz::engine::assets::TerrainVisualRepresentationKind representation_kind =
            wz::engine::assets::TerrainVisualRepresentationKind::MeshChunks;
        wz::engine::assets::TerrainLodId lod_id{};
        wz::engine::assets::TerrainLodId neighbor_lod_id{};
        wz::engine::assets::TerrainVisualProxyBoundaryEdge transition_edge =
            wz::engine::assets::TerrainVisualProxyBoundaryEdge::NegativeX;
        uint64_t batch_key = 0;
        uint64_t sort_key = 0;
    };

    // ─── CullingStats ────────────────────────────────────────────────────────────
    //
    // Per-pipeline visible and culled counts from the last build or update.

    struct CullingStats {
        uint32_t visible_opaque      = 0;
        uint32_t culled_opaque       = 0;
        uint32_t visible_transparent = 0;
        uint32_t culled_transparent  = 0;
        uint32_t visible_splats      = 0;
        uint32_t culled_splats       = 0;
        uint32_t visible_particles   = 0;
        uint32_t culled_particles    = 0;
        uint32_t visible_terrain     = 0;
        uint32_t culled_terrain      = 0;
    };

    // ─── RenderIRView ─────────────────────────────────────────────────────────────
    //
    // Non-owning view into RenderIRStorage.
    // Sorted, frustum-culled draw reference lists, one per pipeline.
    // source is stored by value — spans inside it still point into the originating
    // CompiledSceneStorage, which must outlive this view.
    //
    // Buffer layout (single allocation sized for maximum capacity):
    //   [ DrawRef * opaque_capacity      ]
    //   [ DrawRef * transparent_capacity ]
    //   [ DrawRef * splat_capacity       ]
    //   [ DrawRef * particle_capacity    ]
    //
    // Spans reflect only the visible (non-culled) subset of each section.

    struct RenderIRView {
        // Sorted, culled draw reference lists (subsets of buffer capacity)
        std::span<const DrawRef> opaque;       // front-to-back by material
        std::span<const DrawRef> transparent;  // back-to-front by depth
        std::span<const DrawRef> splats;       // back-to-front by depth
        std::span<const DrawRef> particles;    // back-to-front by depth
        std::span<const TerrainDrawRef> terrain;

        // Source view — by value; spans point into the originating CompiledSceneStorage
        CompiledSceneView source{};

        CullingStats culling{};
    };

    // ─── RenderIRStorage ──────────────────────────────────────────────────────────

    struct RenderIRStorage {
        std::unique_ptr<std::byte[]> buffer;
        size_t                       buffer_bytes = 0; // owned capacity of buffer

        // Per-section base pointers and capacities (full compiled scene count, not visible).
        // update_render_ir() writes into these ranges and narrows the ir spans.
        DrawRef* opaque_base      = nullptr;
        DrawRef* transparent_base = nullptr;
        DrawRef* splat_base       = nullptr;
        DrawRef* particle_base    = nullptr;
        TerrainDrawRef* terrain_base = nullptr;
        uint32_t opaque_capacity      = 0;
        uint32_t transparent_capacity = 0;
        uint32_t splat_capacity       = 0;
        uint32_t particle_capacity    = 0;
        uint32_t terrain_capacity     = 0;

        wz::scene_render::AllocationStats stats{};

        RenderIRView ir;
    };


    // ─── build_render_ir() ────────────────────────────────────────────────────────
    //
    // Builds frustum-culled, sorted DrawRef lists from a CompiledSceneView.
    // The originating CompiledSceneStorage must already have update_view() applied.
    // Fills storage; returns a view into it.
    // Buffer is sized for the full compiled scene; spans reflect only visible primitives.
    // O(N) cull pass + O(V log V) sort per pipeline, where V <= N.

    RenderIRView build_render_ir(RenderIRStorage& storage, CompiledSceneView scene);

    // ─── update_render_ir() ───────────────────────────────────────────────────────
    //
    // Reculls and re-sorts all pipelines from the full compiled scene.
    // Must be called after update_view() whenever the camera or frustum changes.
    // Requires the same scene structure (same primitive counts) as the last
    // build_render_ir() call — does not reallocate the buffer.

    void update_render_ir(RenderIRStorage& storage, CompiledSceneView scene);

} // namespace wz::render
