// src/gpu/dx12/dx12_mesh.cpp

#include <gpu/dx12/dx12_internal.h>

#include <engine/assets/mesh/mesh.h>

#include "dx12_device_internal.h"

namespace wz::gpu::dx12::internal
{
    GPUHandle upload_mesh_dx12(
        Device& device,
        const wz::engine::assets::MeshData& mesh)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        assert(impl);

        if (!mesh.valid())
            return {};

        DX12MeshResource resource{};
        resource.vertex_count = mesh.vertex_count();
        resource.index_count = mesh.index_count();

        return impl->meshes.add(resource);
    }

    const DX12MeshResource* get_mesh(
        Device& device,
        GPUHandle handle)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        assert(impl);

        return impl->meshes.get(handle);
    }
}