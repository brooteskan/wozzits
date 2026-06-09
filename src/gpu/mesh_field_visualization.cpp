#include <gpu/mesh_field_visualization.h>

#include <gpu/dx12/dx12_internal.h>

#include <algorithm>

namespace wz::gpu
{
    bool MeshFieldVisualizationUploadDesc::valid() const noexcept
    {
        if (!mesh || !mesh->valid() || !field || !field->valid()) {
            return false;
        }
        if (channel_id == 0u
            || field->domain
                != wz::engine::assets::MeshDerivedFieldDomain::Vertex
            || field->element_count != mesh->vertex_count())
        {
            return false;
        }

        const auto found = std::find_if(
            field->channels.begin(),
            field->channels.end(),
            [&](const wz::engine::assets::MeshDerivedFieldChannel& channel)
            {
                return channel.channel_id == channel_id;
            });

        if (found == field->channels.end()
            || found->value_type
                != wz::engine::assets::MeshDerivedFieldValueType::Float1)
        {
            return false;
        }

        const uint32_t expected_bytes =
            field->element_count
            * wz::engine::assets::mesh_derived_field_value_stride(
                found->value_type);
        return found->byte_count == expected_bytes
            && found->byte_offset <= field->values.size()
            && found->byte_count <= field->values.size() - found->byte_offset;
    }

    GPUHandle upload_mesh_field_visualization(
        Device& device,
        const MeshFieldVisualizationUploadDesc& desc)
    {
        if (!device.valid() || !desc.valid()) {
            return {};
        }

        return dx12::internal::upload_mesh_field_visualization_dx12(
            device,
            desc);
    }

    GPUHandle create_mesh_field_visualization_from_gpu_source(
        Device& device,
        GPUHandle source_buffer,
        uint64_t byte_offset,
        uint32_t element_count,
        uint32_t stride_bytes)
    {
        if (!device.valid()
            || !source_buffer.valid()
            || element_count == 0u
            || stride_bytes == 0u)
        {
            return {};
        }

        return dx12::internal
            ::create_mesh_field_visualization_from_gpu_source_dx12(
                device,
                source_buffer,
                byte_offset,
                element_count,
                stride_bytes);
    }

    bool release_mesh_field_visualization(
        Device& device,
        GPUHandle handle)
    {
        if (!device.valid() || !handle.valid()) {
            return false;
        }

        return dx12::internal::release_mesh_field_visualization_dx12(
            device,
            handle);
    }
}
