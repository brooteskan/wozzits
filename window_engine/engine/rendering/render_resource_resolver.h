#pragma once
// window_engine/engine/rendering/render_resource_resolver.h
//
// Maps logical draw-command handles (SplatHandle, MeshHandle, …) to
// GPU-resident resources + the pipeline program needed to draw them.
//
// Lives in window-engine so that wozzits-scene-render stays GPU-agnostic:
// the scene graph emits DrawCommands with logical handles; this resolver
// bridges them to the GPU resource tables owned by the device.

#include <scene/compile/compiled_scene.h>
#include <gpu/gpu_types.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/terrain/terrain.h>
#include <render/ir/render_ir.h>
#include <render/diagnostics/terrain_diagnostics.h>

#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace wz::engine::rendering
{
    inline constexpr uint64_t kTerrainSubmittedDrawCallsUseSubmittedChunks =
        (std::numeric_limits<uint64_t>::max)();

    struct TerrainFarSplatChunk
    {
        wz::engine::assets::TerrainChunkId chunk_id{};
        wz::engine::assets::TerrainLodId density_id{};
        wz::gpu::GPUHandle gpu_resource{};
        uint32_t first_splat = 0;
        uint32_t splat_count = 0;
    };

    struct TerrainTransitionDrawRange
    {
        wz::engine::assets::TerrainChunkId chunk_id{};
        wz::engine::assets::TerrainChunkId neighbor_chunk_id{};
        wz::engine::assets::TerrainLodId lod_id{};
        wz::engine::assets::TerrainLodId neighbor_lod_id{};
        wz::engine::assets::TerrainVisualProxyBoundaryEdge edge =
            wz::engine::assets::TerrainVisualProxyBoundaryEdge::NegativeX;
        uint32_t first_index = 0;
        uint32_t index_count = 0;

        [[nodiscard]] uint32_t triangle_count() const noexcept
        {
            return index_count / 3u;
        }
    };

    // Resolved result: the GPU resource handle plus pipeline identification.
    // submit_render_frame uses render_program (when valid) to look up the PSO
    // in RenderProgramPipelineCache; falls back to the legacy BuiltinRenderProgram
    // → RenderablePipelineCache path when render_program is invalid.
    struct ResolvedRenderableResource
    {
        wz::gpu::GPUHandle                       gpu_resource{};
        wz::engine::assets::BuiltinRenderProgram program{};
        wz::asset::ResourceHandle                render_program{};  // preferred when valid
        wz::engine::assets::TerrainLightingData  terrain_lighting{};
        float                                   terrain_target_pixels_per_triangle = 0.0f;
        wz::engine::assets::MeshRenderStyleData  mesh_style{};
        wz::gpu::GPUHandle                       mesh_field_visualization_resource{};
        std::span<const wz::engine::assets::TerrainVisualChunk> terrain_chunks{};
        std::span<const TerrainFarSplatChunk> terrain_far_splat_chunks{};
    };

    struct ResolvedTerrainDrawResource
    {
        wz::gpu::GPUHandle                       gpu_resource{};
        wz::engine::assets::BuiltinRenderProgram program{};
        wz::asset::ResourceHandle                render_program{};
        wz::engine::assets::TerrainLightingData  terrain_lighting{};
        float                                   terrain_target_pixels_per_triangle = 0.0f;
        uint32_t                                first_index = 0;
        uint32_t                                index_count = 0;
        uint32_t                                source_triangle_count = 0;
        uint32_t                                first_splat = 0;
        uint32_t                                far_splat_count = 0;
        bool                                    lod_replacement_available = false;
        bool                                    lod_replacement_selected = false;
        bool                                    transition_selected = false;
        bool                                    far_splat_selected = false;
    };

    struct TerrainRenderStats
    {
        uint64_t total_chunks = 0;
        uint64_t submitted_chunks = 0;
        uint64_t total_triangles = 0;
        uint64_t visible_chunks = 0;
        uint64_t submitted_triangles = 0;
        uint64_t submitted_draw_calls = 0;
        uint64_t lod_candidate_chunks = 0;
        uint64_t lod_candidate_triangles = 0;
        uint64_t lod_replacement_available_chunks = 0;
        uint64_t lod_replacement_available_triangles = 0;
        uint64_t lod_replacement_drawn_chunks = 0;
        uint64_t lod_replacement_drawn_triangles = 0;
        uint64_t far_splat_chunks = 0;
        uint64_t far_splats = 0;
        double terrain_submit_cpu_us = 0.0;
        double terrain_submit_resolve_cpu_us = 0.0;
        double terrain_submit_resource_cpu_us = 0.0;
        double terrain_submit_constants_cpu_us = 0.0;
        double terrain_submit_bind_cpu_us = 0.0;
        double terrain_submit_draw_cpu_us = 0.0;
        double terrain_submit_stats_cpu_us = 0.0;
        uint64_t terrain_submit_surfel_draw_calls = 0;
        uint64_t terrain_submit_mesh_draw_calls = 0;
        uint64_t terrain_submit_fallback_mesh_draw_calls = 0;
        uint64_t terrain_submit_surfel_fallbacks = 0;
        float lod_target_pixels_per_triangle = 0.0f;
        float pixels_per_triangle_min = 0.0f;
        float pixels_per_triangle_max = 0.0f;
        float pixels_per_triangle_weighted_mean = 0.0f;

        uint64_t pixels_per_triangle_triangles_le_0_5 = 0;
        uint64_t pixels_per_triangle_triangles_le_1 = 0;
        uint64_t pixels_per_triangle_triangles_le_2 = 0;
        uint64_t pixels_per_triangle_triangles_le_4 = 0;
        uint64_t pixels_per_triangle_triangles_le_8 = 0;
        uint64_t pixels_per_triangle_triangles_le_16 = 0;
        uint64_t pixels_per_triangle_triangles_le_32 = 0;
        uint64_t pixels_per_triangle_triangles_le_64 = 0;
        uint64_t pixels_per_triangle_triangles_le_128 = 0;
        uint64_t pixels_per_triangle_triangles_le_256 = 0;

        std::array<
            uint64_t,
            wz::render::kTerrainDiagnosticLodHistogramSize> lod_histogram{};
        double selector_cpu_us = 0.0;
        double terrain_gpu_us = 0.0;
        bool terrain_gpu_us_valid = false;
        uint64_t budget_target_triangles = 0;
        uint64_t budget_misses = 0;
        wz::render::TerrainRepresentationCounts representation_counts{};
        uint64_t projected_error_sample_count = 0;
        float max_projected_error_px = 0.0f;
        float median_projected_error_px = 0.0f;
        float p95_projected_error_px = 0.0f;
    };

    class RenderResourceResolver
    {
    public:
        // Register a GPU-resident splat cloud together with its render program.
        // render_program is optional; when valid the submit path prefers it
        // over the legacy BuiltinRenderProgram → RenderablePipelineCache path.
        // Returns the SplatHandle to store in DrawCommand::splats_buffer.
        wz::scene::SplatHandle register_splat_cloud(
            wz::gpu::GPUHandle                       gpu_resource,
            wz::engine::assets::BuiltinRenderProgram program,
            wz::asset::ResourceHandle                render_program = {});

        // Resolve a SplatHandle.
        // Returns nullopt if the handle is out-of-range or INVALID_SPLAT.
        std::optional<ResolvedRenderableResource>
        resolve_splats(wz::scene::SplatHandle handle) const noexcept;

        // Update the render program bound to an existing splat handle.
        // Returns false on out-of-range or invalid handle.  Useful for the
        // toolhost to swap between PullDebug and NeighborhoodColorBlend
        // without rebuilding the scene graph or the GPU resource.
        bool set_splat_render_program(
            wz::scene::SplatHandle    handle,
            wz::asset::ResourceHandle render_program) noexcept;

        // Update the GPU resource bound to an existing splat handle.
        // Returns false on out-of-range or invalid handle.  Useful for the
        // toolhost to swap in a re-compiled / re-uploaded cloud (e.g. live
        // tuning of terrain compile parameters) without rebuilding the
        // scene graph.  The previous resource is NOT released — its
        // lifetime is owned by the GPU resource table.
        bool set_splat_gpu_resource(
            wz::scene::SplatHandle handle,
            wz::gpu::GPUHandle     gpu_resource) noexcept;

        // Register a GPU-resident mesh together with its render program.
        // render_program is optional; when valid the submit path prefers it.
        // Returns the MeshHandle to store in DrawCommand::mesh.
        wz::scene::MeshHandle register_mesh(
            wz::gpu::GPUHandle                       gpu_resource,
            wz::engine::assets::BuiltinRenderProgram program,
            wz::asset::ResourceHandle                render_program = {},
            wz::engine::assets::TerrainLightingData  terrain_lighting = {},
            float                                   terrain_target_pixels_per_triangle = 0.0f,
            wz::engine::assets::MeshRenderStyleData  mesh_style = {},
            std::span<const wz::engine::assets::TerrainVisualChunk> terrain_chunks = {},
            std::span<const TerrainFarSplatChunk> terrain_far_splat_chunks = {},
            std::span<const TerrainTransitionDrawRange> terrain_transition_ranges = {},
            wz::gpu::GPUHandle                       mesh_field_visualization_resource = {});

        bool register_terrain_proxy(
            wz::engine::assets::TerrainProxyId terrain_proxy_id,
            wz::gpu::GPUHandle                       gpu_resource,
            wz::engine::assets::BuiltinRenderProgram program,
            wz::asset::ResourceHandle                render_program = {},
            wz::engine::assets::TerrainLightingData  terrain_lighting = {},
            float                                   terrain_target_pixels_per_triangle = 0.0f,
            wz::engine::assets::MeshRenderStyleData  mesh_style = {},
            std::span<const wz::engine::assets::TerrainVisualChunk> terrain_chunks = {},
            std::span<const TerrainFarSplatChunk> terrain_far_splat_chunks = {},
            std::span<const TerrainTransitionDrawRange> terrain_transition_ranges = {},
            wz::gpu::GPUHandle                       mesh_field_visualization_resource = {});

        // Resolve a MeshHandle.
        // Returns nullopt if the handle is out-of-range or INVALID_MESH.
        std::optional<ResolvedRenderableResource>
        resolve_mesh(wz::scene::MeshHandle handle) const noexcept;

        std::optional<ResolvedRenderableResource>
        resolve_terrain_proxy(
            wz::engine::assets::TerrainProxyId terrain_proxy_id) const noexcept;

        std::optional<ResolvedTerrainDrawResource>
        resolve_terrain_draw(
            wz::engine::assets::TerrainProxyId terrain_proxy_id,
            const wz::render::TerrainDrawRef& ref) const noexcept;

        std::optional<ResolvedTerrainDrawResource>
        resolve_terrain_draw(
            wz::scene::TerrainProxyId terrain_proxy_id,
            const wz::render::TerrainDrawRef& ref) const noexcept;

        std::optional<ResolvedTerrainDrawResource>
        resolve_terrain_draw_mesh_fallback(
            wz::engine::assets::TerrainProxyId terrain_proxy_id,
            const wz::render::TerrainDrawRef& ref) const noexcept;

        std::optional<ResolvedTerrainDrawResource>
        resolve_terrain_draw_mesh_fallback(
            wz::scene::TerrainProxyId terrain_proxy_id,
            const wz::render::TerrainDrawRef& ref) const noexcept;

        std::optional<wz::render::TerrainFrameDiagnostics>
        resolve_terrain_proxy_diagnostics(
            wz::engine::assets::TerrainProxyId terrain_proxy_id) const noexcept;

        std::optional<wz::render::TerrainFrameDiagnostics>
        resolve_terrain_proxy_diagnostics(
            wz::scene::TerrainProxyId terrain_proxy_id) const noexcept;

        void reset_terrain_render_stats() const noexcept;
        void record_terrain_render_stats(
            uint64_t total_chunks,
            uint64_t submitted_chunks,
            uint64_t total_triangles,
            uint64_t submitted_triangles,
            uint64_t lod_candidate_chunks = 0,
            uint64_t lod_candidate_triangles = 0,
            uint64_t lod_replacement_available_chunks = 0,
            uint64_t lod_replacement_available_triangles = 0,
            uint64_t lod_replacement_drawn_chunks = 0,
            uint64_t lod_replacement_drawn_triangles = 0,
            uint64_t far_splat_chunks = 0,
            uint64_t far_splats = 0,
            float lod_target_pixels_per_triangle = 0.0f,
            float pixels_per_triangle_min = 0.0f,
            float pixels_per_triangle_max = 0.0f,
            double pixels_per_triangle_weighted_sum = 0.0,
            uint64_t pixels_per_triangle_triangles_le_0_5 = 0,
            uint64_t pixels_per_triangle_triangles_le_1 = 0,
            uint64_t pixels_per_triangle_triangles_le_2 = 0,
            uint64_t pixels_per_triangle_triangles_le_4 = 0,
            uint64_t pixels_per_triangle_triangles_le_8 = 0,
            uint64_t pixels_per_triangle_triangles_le_16 = 0,
            uint64_t pixels_per_triangle_triangles_le_32 = 0,
            uint64_t pixels_per_triangle_triangles_le_64 = 0,
            uint64_t pixels_per_triangle_triangles_le_128 = 0,
            uint64_t pixels_per_triangle_triangles_le_256 = 0,
            uint32_t lod_level = 0,
            wz::engine::assets::TerrainVisualRepresentationKind representation_kind =
                wz::engine::assets::TerrainVisualRepresentationKind::MeshChunks,
            uint64_t submitted_draw_calls =
                kTerrainSubmittedDrawCallsUseSubmittedChunks) const noexcept;
        void record_terrain_source_totals(
            uint64_t proxy_chunks,
            uint64_t source_triangles)
            const noexcept;
        void record_terrain_visible_chunks(uint64_t visible_chunks)
            const noexcept;
        void record_terrain_projected_error_samples(
            std::span<const float> projected_error_px) const;
        void record_terrain_selector_cpu_us(double selector_cpu_us)
            const noexcept;
        void record_terrain_gpu_us(double terrain_gpu_us) const noexcept;
        void record_terrain_budget_diagnostics(
            uint64_t budget_target_triangles,
            bool budget_missed) const noexcept;
        void record_terrain_submit_cpu_profile(
            double total_us,
            double resolve_us,
            double resource_us,
            double constants_us,
            double bind_us,
            double draw_us,
            double stats_us,
            uint64_t surfel_draw_calls,
            uint64_t mesh_draw_calls,
            uint64_t fallback_mesh_draw_calls,
            uint64_t surfel_fallbacks) const noexcept;
        TerrainRenderStats terrain_render_stats() const noexcept;
        wz::render::TerrainFrameDiagnostics terrain_frame_diagnostics()
            const noexcept;

    private:
        struct Entry
        {
            struct TerrainFarSplatLookup
            {
                uint64_t key = 0;
                size_t index = 0;
            };

            wz::gpu::GPUHandle                       gpu_resource{};
            wz::engine::assets::BuiltinRenderProgram program{};
            wz::asset::ResourceHandle                render_program{};
            wz::engine::assets::TerrainLightingData  terrain_lighting{};
            float                                   terrain_target_pixels_per_triangle = 0.0f;
            wz::engine::assets::MeshRenderStyleData  mesh_style{};
            wz::gpu::GPUHandle                       mesh_field_visualization_resource{};
            std::vector<wz::engine::assets::TerrainVisualChunk> terrain_chunks{};
            std::vector<TerrainFarSplatChunk> terrain_far_splat_chunks{};
            std::vector<TerrainFarSplatLookup> terrain_far_splat_lookup{};
            std::vector<TerrainTransitionDrawRange> terrain_transition_ranges{};
        };

        static uint64_t terrain_far_splat_lookup_key(
            wz::engine::assets::TerrainChunkId chunk_id,
            wz::engine::assets::TerrainLodId density_id) noexcept;
        static void rebuild_terrain_far_splat_lookup(Entry& entry);
        static const TerrainFarSplatChunk* find_terrain_far_splat_chunk(
            const Entry& entry,
            wz::engine::assets::TerrainChunkId chunk_id,
            wz::engine::assets::TerrainLodId density_id);

        std::vector<Entry> splat_entries_;
        std::vector<Entry> mesh_entries_;
        std::vector<std::pair<wz::engine::assets::TerrainProxyId, Entry>>
            terrain_proxy_entries_;
        mutable TerrainRenderStats terrain_stats_{};
    };
}
