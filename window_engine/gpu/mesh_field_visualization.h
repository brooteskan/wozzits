#pragma once
// gpu/mesh_field_visualization.h

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

#include <engine/assets/mesh/mesh.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>

#include <cstdint>

namespace wz::gpu
{
    struct MeshFieldVisualizationUploadDesc
    {
        const wz::engine::assets::MeshData* mesh = nullptr;
        const wz::engine::assets::MeshDerivedFieldData* field = nullptr;
        uint32_t channel_id = 0;

        bool valid() const noexcept;
    };

    [[nodiscard]] GPUHandle upload_mesh_field_visualization(
        Device& device,
        const MeshFieldVisualizationUploadDesc& desc);

    bool release_mesh_field_visualization(
        Device& device,
        GPUHandle handle);
}
