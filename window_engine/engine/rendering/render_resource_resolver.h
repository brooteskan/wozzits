#pragma once
// window_engine/engine/rendering/render_resource_resolver.h
//
// Maps logical draw-command handles (SplatHandle, …) to GPU-resident
// resource handles (GPUHandle) at submit time.
//
// Lives in window-engine so that wozzits-scene-render stays GPU-agnostic:
// the scene graph emits DrawCommands with logical handles; this resolver
// bridges them to the GPU resource tables owned by the device.

#include <scene/compile/compiled_scene.h>
#include <gpu/gpu_types.h>

#include <vector>

namespace wz::engine::rendering
{
    class RenderResourceResolver
    {
    public:
        // Register a GPU-resident splat cloud.
        // Returns the SplatHandle to store in DrawCommand::splats_buffer.
        wz::scene::SplatHandle register_splat_cloud(wz::gpu::GPUHandle gpu_handle);

        // Resolve a SplatHandle to its GPU resource handle.
        // Returns an invalid GPUHandle if the handle is out-of-range or INVALID_SPLAT.
        wz::gpu::GPUHandle resolve_splats(wz::scene::SplatHandle handle) const noexcept;

    private:
        std::vector<wz::gpu::GPUHandle> splat_entries_;
    };
}
