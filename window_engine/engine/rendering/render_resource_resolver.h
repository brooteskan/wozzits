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

#include <optional>
#include <span>
#include <vector>

namespace wz::engine::rendering
{
    struct TerrainFarSplatChunk
    {
        wz::gpu::GPUHandle gpu_resource{};
        uint32_t splat_count = 0;
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
        std::span<const wz::engine::assets::TerrainVisualChunk> terrain_chunks{};
        std::span<const TerrainFarSplatChunk> terrain_far_splat_chunks{};
    };

    struct TerrainRenderStats
    {
        uint64_t total_chunks = 0;
        uint64_t submitted_chunks = 0;
        uint64_t total_triangles = 0;
        uint64_t submitted_triangles = 0;
        uint64_t lod_candidate_chunks = 0;
        uint64_t lod_candidate_triangles = 0;
        uint64_t lod_replacement_available_chunks = 0;
        uint64_t lod_replacement_available_triangles = 0;
        uint64_t lod_replacement_drawn_chunks = 0;
        uint64_t lod_replacement_drawn_triangles = 0;
        uint64_t far_splat_chunks = 0;
        uint64_t far_splats = 0;
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
            std::span<const TerrainFarSplatChunk> terrain_far_splat_chunks = {});

        // Resolve a MeshHandle.
        // Returns nullopt if the handle is out-of-range or INVALID_MESH.
        std::optional<ResolvedRenderableResource>
        resolve_mesh(wz::scene::MeshHandle handle) const noexcept;

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
            uint64_t pixels_per_triangle_triangles_le_256 = 0) const noexcept;
        TerrainRenderStats terrain_render_stats() const noexcept;

    private:
        struct Entry
        {
            wz::gpu::GPUHandle                       gpu_resource{};
            wz::engine::assets::BuiltinRenderProgram program{};
            wz::asset::ResourceHandle                render_program{};
            wz::engine::assets::TerrainLightingData  terrain_lighting{};
            float                                   terrain_target_pixels_per_triangle = 0.0f;
            wz::engine::assets::MeshRenderStyleData  mesh_style{};
            std::vector<wz::engine::assets::TerrainVisualChunk> terrain_chunks{};
            std::vector<TerrainFarSplatChunk> terrain_far_splat_chunks{};
        };

        std::vector<Entry> splat_entries_;
        std::vector<Entry> mesh_entries_;
        mutable TerrainRenderStats terrain_stats_{};
    };
}
