#pragma once
// gpu/mesh_field_visualization.h

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

#include <engine/assets/mesh/mesh.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>

#include <cstddef>
#include <cstdint>

namespace wz::gpu
{
    struct MeshFieldVisualizationUploadDesc
    {
        const wz::engine::assets::MeshData* mesh = nullptr;
        const wz::engine::assets::MeshDerivedFieldData* field = nullptr;
        uint32_t channel_id = 0;

        bool valid() const noexcept;
        const std::byte* values_begin() const noexcept;
        uint64_t value_byte_count() const noexcept;
        uint32_t element_count() const noexcept;
        uint32_t stride_bytes() const noexcept;
    };

    struct MeshFieldRawFaceUploadDesc
    {
        const wz::engine::assets::MeshData* mesh = nullptr;
        const wz::engine::assets::MeshDerivedFieldData* field = nullptr;
        const uint32_t* channel_ids = nullptr;
        uint32_t channel_count = 0;

        bool valid() const noexcept;
        uint32_t face_count() const noexcept;
        uint32_t element_count() const noexcept;
        uint32_t stride_bytes() const noexcept;
    };

    [[nodiscard]] GPUHandle upload_mesh_field_visualization(
        Device& device,
        const MeshFieldVisualizationUploadDesc& desc);

    [[nodiscard]] GPUHandle upload_mesh_field_visualization_values(
        Device& device,
        const std::byte* values,
        uint64_t value_byte_count,
        uint32_t element_count,
        uint32_t stride_bytes);

    [[nodiscard]] GPUHandle upload_mesh_field_raw_faces(
        Device& device,
        const MeshFieldRawFaceUploadDesc& desc);

    [[nodiscard]] GPUHandle create_mesh_field_visualization_from_gpu_source(
        Device& device,
        GPUHandle source_buffer,
        uint64_t byte_offset,
        uint32_t element_count,
        uint32_t stride_bytes);

    // Copy a GPU source buffer into an existing mesh field visualization
    // resource, keeping the destination handle (and any SRVs captured from
    // it) stable. Only resources created via
    // create_mesh_field_visualization_from_gpu_source can be updated, and the
    // element count and stride must match the destination exactly.
    bool update_mesh_field_visualization_from_gpu_source(
        Device& device,
        GPUHandle destination,
        GPUHandle source_buffer,
        uint64_t byte_offset,
        uint32_t element_count,
        uint32_t stride_bytes);

    bool release_mesh_field_visualization(
        Device& device,
        GPUHandle handle);
}
