#pragma once
// gpu/mesh.h
//
// Public GPU mesh upload API.
//
// This is the boundary between CPU-side engine mesh assets and backend-owned
// GPU mesh buffers. The caller provides CPU MeshData; the GPU backend owns the
// resulting vertex/index buffers and returns an opaque GPUHandle.
//
// This header intentionally does not expose DX12/Vulkan/etc. resource details.

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

#include <engine/assets/mesh/mesh.h>

namespace wz::gpu
{
    struct MeshUploadDesc
    {
        const wz::engine::assets::MeshData* mesh = nullptr;

        bool valid() const noexcept
        {
            return mesh != nullptr && mesh->valid();
        }
    };

    [[nodiscard]] GPUHandle upload_mesh(
        Device& device,
        const MeshUploadDesc& desc);

    [[nodiscard]] inline GPUHandle upload_mesh(
        Device& device,
        const wz::engine::assets::MeshData& mesh)
    {
        return upload_mesh(device, MeshUploadDesc{
            .mesh = &mesh,
            });
    }
}