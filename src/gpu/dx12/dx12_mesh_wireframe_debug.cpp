// src/gpu/dx12/dx12_mesh_wireframe_debug.cpp

#include <gpu/dx12/dx12_mesh_wireframe_debug.h>

#include "dx12_device_internal.h"

#include <cassert>

namespace wz::gpu::dx12
{
    void create_mesh_wireframe_debug_context(
        Device& device,
        const MeshWireframeDebugContextDesc& desc)
    {
        assert(desc.valid());

        auto* impl = static_cast<DX12Device*>(device.impl);
        assert(impl);
        assert(!impl->mesh_wire_debug_ctx);

        impl->mesh_wire_debug_ctx = new MeshWireframeDebugContext{};
        impl->mesh_wire_debug_ctx->mesh = desc.mesh;

        // PSO/root signature creation comes next checkpoint.
    }

    void submit_mesh_wireframe_debug_frame(
        Device& device,
        const MeshWireframeDebugView&)
    {
        auto* impl = static_cast<DX12Device*>(device.impl);
        assert(impl);
        assert(impl->mesh_wire_debug_ctx);

        // Draw submission comes after PSO/root signature creation.
    }
}