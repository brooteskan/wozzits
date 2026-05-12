// src/gpu/dx12/dx12_mesh.cpp

#include <gpu/dx12/dx12_internal.h>

#include <engine/assets/mesh/mesh.h>

namespace wz::gpu::dx12::internal
{
    GPUHandle upload_mesh_dx12(
        Device&,
        const wz::engine::assets::MeshData&)
    {
        return {};
    }
}