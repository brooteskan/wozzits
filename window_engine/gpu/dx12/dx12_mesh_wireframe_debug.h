#pragma once
// gpu/dx12/dx12_mesh_wireframe_debug.h
//
// DX12-only diagnostic wireframe renderer for uploaded GPU mesh buffers.
//
// This is not the general render path. It is a debug/validation tool used to
// prove that CPU MeshData can become GPU mesh buffers and be drawn visibly.

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

namespace wz::gpu::dx12
{
    struct MeshWireframeDebugContextDesc
    {
        GPUHandle vertex_shader{};
        GPUHandle pixel_shader{};
        GPUHandle mesh{};

        bool valid() const noexcept
        {
            return vertex_shader.valid()
                && pixel_shader.valid()
                && mesh.valid();
        }
    };

    struct MeshWireframeDebugView
    {
        // Column-major 4x4 matrices, matching the existing debug transform path.
        float world[16]{};
        float view_proj[16]{};
    };

    void create_mesh_wireframe_debug_context(
        Device& device,
        const MeshWireframeDebugContextDesc& desc);

    void submit_mesh_wireframe_debug_frame(
        Device& device,
        const MeshWireframeDebugView& view);
}