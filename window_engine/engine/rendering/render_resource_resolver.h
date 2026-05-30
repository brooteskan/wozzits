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

#include <optional>
#include <vector>

namespace wz::engine::rendering
{
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
            wz::engine::assets::TerrainLightingData  terrain_lighting = {});

        // Resolve a MeshHandle.
        // Returns nullopt if the handle is out-of-range or INVALID_MESH.
        std::optional<ResolvedRenderableResource>
        resolve_mesh(wz::scene::MeshHandle handle) const noexcept;

    private:
        struct Entry
        {
            wz::gpu::GPUHandle                       gpu_resource{};
            wz::engine::assets::BuiltinRenderProgram program{};
            wz::asset::ResourceHandle                render_program{};
            wz::engine::assets::TerrainLightingData  terrain_lighting{};
        };

        std::vector<Entry> splat_entries_;
        std::vector<Entry> mesh_entries_;
    };
}
