#include <gpu/mesh_field_visualization.h>

#include <gpu/dx12/dx12_internal.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace wz::gpu
{
    namespace
    {
        const wz::engine::assets::MeshDerivedFieldChannel*
        find_field_channel(
            const wz::engine::assets::MeshDerivedFieldData& field,
            uint32_t channel_id) noexcept
        {
            const auto found = std::find_if(
                field.channels.begin(),
                field.channels.end(),
                [&](const wz::engine::assets::MeshDerivedFieldChannel& channel)
                {
                    return channel.channel_id == channel_id;
                });

            if (found == field.channels.end()
                || (found->value_type
                        != wz::engine::assets::MeshDerivedFieldValueType::Float1
                    && found->value_type
                        != wz::engine::assets::MeshDerivedFieldValueType::UInt1))
            {
                return nullptr;
            }
            return &*found;
        }

        const wz::engine::assets::MeshDerivedFieldChannel*
        find_visualization_channel(
            const MeshFieldVisualizationUploadDesc& desc) noexcept
        {
            return desc.field
                ? find_field_channel(*desc.field, desc.channel_id)
                : nullptr;
        }

        float read_channel_value_as_float(
            const wz::engine::assets::MeshDerivedFieldData& field,
            const wz::engine::assets::MeshDerivedFieldChannel& channel,
            uint32_t element) noexcept
        {
            const std::byte* source =
                field.values.data()
                + channel.byte_offset
                + static_cast<size_t>(element)
                    * wz::engine::assets::mesh_derived_field_value_stride(
                        channel.value_type);

            if (channel.value_type
                == wz::engine::assets::MeshDerivedFieldValueType::UInt1)
            {
                uint32_t value = 0u;
                std::memcpy(&value, source, sizeof(value));
                return static_cast<float>(value);
            }

            float value = 0.0f;
            std::memcpy(&value, source, sizeof(value));
            return value;
        }

        bool channel_bytes_are_valid(
            const wz::engine::assets::MeshDerivedFieldData& field,
            const wz::engine::assets::MeshDerivedFieldChannel& channel)
        {
            const uint32_t expected_bytes =
                field.element_count
                * wz::engine::assets::mesh_derived_field_value_stride(
                    channel.value_type);
            return channel.byte_count == expected_bytes
                && channel.byte_offset <= field.values.size()
                && channel.byte_count <= field.values.size()
                    - channel.byte_offset;
        }

        bool channel_to_float_values(
            const wz::engine::assets::MeshDerivedFieldData& field,
            const wz::engine::assets::MeshDerivedFieldChannel& channel,
            std::vector<float>& out)
        {
            if (!channel_bytes_are_valid(field, channel)) {
                return false;
            }

            out.resize(field.element_count);
            for (uint32_t i = 0; i < field.element_count; ++i) {
                out[i] = read_channel_value_as_float(field, channel, i);
            }
            return true;
        }

        bool project_face_field_to_vertices(
            const MeshFieldVisualizationUploadDesc& desc,
            const wz::engine::assets::MeshDerivedFieldChannel& channel,
            std::vector<float>& out)
        {
            if (!desc.mesh
                || !desc.field
                || desc.field->domain
                    != wz::engine::assets::MeshDerivedFieldDomain::Face
                || desc.field->element_count
                    != desc.mesh->index_count() / 3u
                || !channel_bytes_are_valid(*desc.field, channel))
            {
                return false;
            }

            out.assign(desc.mesh->vertex_count(), 0.0f);
            std::vector<uint32_t> counts(desc.mesh->vertex_count(), 0u);
            for (uint32_t face = 0; face < desc.field->element_count; ++face) {
                const float value =
                    read_channel_value_as_float(*desc.field, channel, face);
                const size_t i = static_cast<size_t>(face) * 3u;
                for (uint32_t corner = 0; corner < 3u; ++corner) {
                    const uint32_t vertex = desc.mesh->indices[i + corner];
                    if (vertex < out.size()) {
                        out[vertex] += value;
                        ++counts[vertex];
                    }
                }
            }
            for (uint32_t vertex = 0; vertex < out.size(); ++vertex) {
                if (counts[vertex] > 0u) {
                    out[vertex] /= static_cast<float>(counts[vertex]);
                }
            }
            return true;
        }
    }

    bool MeshFieldVisualizationUploadDesc::valid() const noexcept
    {
        if (!mesh || !mesh->valid() || !field || !field->valid()) {
            return false;
        }
        if (channel_id == 0u)
        {
            return false;
        }

        const auto* channel = find_visualization_channel(*this);
        if (!channel) {
            return false;
        }

        if (field->domain == wz::engine::assets::MeshDerivedFieldDomain::Vertex
            && field->element_count == mesh->vertex_count())
        {
            return channel_bytes_are_valid(*field, *channel);
        }
        if (field->domain == wz::engine::assets::MeshDerivedFieldDomain::Face
            && field->element_count == mesh->index_count() / 3u)
        {
            return channel_bytes_are_valid(*field, *channel);
        }
        return false;
    }

    const std::byte* MeshFieldVisualizationUploadDesc::values_begin()
        const noexcept
    {
        if (!valid()) {
            return nullptr;
        }
        const auto* channel = find_visualization_channel(*this);
        return channel ? field->values.data() + channel->byte_offset : nullptr;
    }

    uint64_t MeshFieldVisualizationUploadDesc::value_byte_count()
        const noexcept
    {
        if (!valid()) {
            return 0u;
        }
        const auto* channel = find_visualization_channel(*this);
        return channel ? channel->byte_count : 0u;
    }

    uint32_t MeshFieldVisualizationUploadDesc::element_count() const noexcept
    {
        return valid() && field ? field->element_count : 0u;
    }

    uint32_t MeshFieldVisualizationUploadDesc::stride_bytes() const noexcept
    {
        return valid()
            ? wz::engine::assets::mesh_derived_field_value_stride(
                wz::engine::assets::MeshDerivedFieldValueType::Float1)
            : 0u;
    }

    bool MeshFieldRawFaceUploadDesc::valid() const noexcept
    {
        if (!mesh
            || !mesh->valid()
            || !field
            || !field->valid()
            || !channel_ids
            || channel_count == 0u
            || field->domain
                != wz::engine::assets::MeshDerivedFieldDomain::Face
            || field->element_count != mesh->index_count() / 3u)
        {
            return false;
        }

        for (uint32_t i = 0; i < channel_count; ++i) {
            const auto* channel = find_field_channel(*field, channel_ids[i]);
            if (!channel || !channel_bytes_are_valid(*field, *channel)) {
                return false;
            }
        }
        return true;
    }

    uint32_t MeshFieldRawFaceUploadDesc::face_count() const noexcept
    {
        return valid() && field ? field->element_count : 0u;
    }

    uint32_t MeshFieldRawFaceUploadDesc::element_count() const noexcept
    {
        return valid() && field
            ? field->element_count * channel_count
            : 0u;
    }

    uint32_t MeshFieldRawFaceUploadDesc::stride_bytes() const noexcept
    {
        return valid()
            ? wz::engine::assets::mesh_derived_field_value_stride(
                wz::engine::assets::MeshDerivedFieldValueType::Float1)
            : 0u;
    }

    bool MeshFieldRawVertexUploadDesc::valid() const noexcept
    {
        if (!mesh
            || !mesh->valid()
            || !field
            || !field->valid()
            || !channel_ids
            || channel_count == 0u
            || field->domain
                != wz::engine::assets::MeshDerivedFieldDomain::Vertex
            || field->element_count != mesh->vertex_count())
        {
            return false;
        }

        for (uint32_t i = 0; i < channel_count; ++i) {
            const auto* channel = find_field_channel(*field, channel_ids[i]);
            if (!channel || !channel_bytes_are_valid(*field, *channel)) {
                return false;
            }
        }
        return true;
    }

    uint32_t MeshFieldRawVertexUploadDesc::vertex_count() const noexcept
    {
        return valid() && field ? field->element_count : 0u;
    }

    uint32_t MeshFieldRawVertexUploadDesc::element_count() const noexcept
    {
        return valid() && field
            ? field->element_count * channel_count
            : 0u;
    }

    uint32_t MeshFieldRawVertexUploadDesc::stride_bytes() const noexcept
    {
        return valid()
            ? wz::engine::assets::mesh_derived_field_value_stride(
                wz::engine::assets::MeshDerivedFieldValueType::Float1)
            : 0u;
    }

    GPUHandle upload_mesh_field_visualization(
        Device& device,
        const MeshFieldVisualizationUploadDesc& desc)
    {
        if (!device_ok(device) || !desc.valid()) {
            return {};
        }

        const auto* channel = find_visualization_channel(desc);
        if (!channel) {
            return {};
        }

        if (desc.field
            && (desc.field->domain
                    == wz::engine::assets::MeshDerivedFieldDomain::Face
                || channel->value_type
                    == wz::engine::assets::MeshDerivedFieldValueType::UInt1))
        {
            std::vector<float> projected_values;
            if (desc.field->domain
                == wz::engine::assets::MeshDerivedFieldDomain::Face)
            {
                if (!project_face_field_to_vertices(
                        desc,
                        *channel,
                        projected_values))
                {
                    return {};
                }
            }
            else if (!channel_to_float_values(
                         *desc.field,
                         *channel,
                         projected_values))
            {
                return {};
            }

            std::vector<std::byte> projected_bytes(
                projected_values.size() * sizeof(float),
                std::byte{0});
            if (!projected_values.empty()) {
                std::memcpy(
                    projected_bytes.data(),
                    projected_values.data(),
                    projected_bytes.size());
            }

            const wz::engine::assets::MeshDerivedFieldData projected_field{
                .source_mesh_key = desc.field->source_mesh_key,
                .source_topology_hash = desc.field->source_topology_hash,
                .domain = wz::engine::assets::MeshDerivedFieldDomain::Vertex,
                .element_count = desc.mesh->vertex_count(),
                .channels = {
                    wz::engine::assets::MeshDerivedFieldChannel{
                        .channel_id = desc.channel_id,
                        .value_type =
                            wz::engine::assets::MeshDerivedFieldValueType::
                                Float1,
                        .byte_offset = 0u,
                        .byte_count =
                            static_cast<uint32_t>(
                                projected_bytes.size()),
                    },
                },
                .values = std::move(projected_bytes),
            };
            return dx12::internal::upload_mesh_field_visualization_dx12(
                device,
                MeshFieldVisualizationUploadDesc{
                    .mesh = desc.mesh,
                    .field = &projected_field,
                    .channel_id = desc.channel_id,
                });
        }

        return dx12::internal::upload_mesh_field_visualization_dx12(
            device,
            desc);
    }

    GPUHandle upload_mesh_field_visualization_values(
        Device& device,
        const std::byte* values,
        uint64_t value_byte_count,
        uint32_t element_count,
        uint32_t stride_bytes)
    {
        if (!device_ok(device)
            || !values
            || value_byte_count == 0u
            || element_count == 0u
            || stride_bytes == 0u)
        {
            return {};
        }

        return dx12::internal::upload_mesh_field_visualization_values_dx12(
            device,
            values,
            value_byte_count,
            element_count,
            stride_bytes);
    }

    bool update_mesh_field_visualization_values(
        Device& device,
        GPUHandle destination,
        const std::byte* values,
        uint64_t value_byte_count,
        uint32_t element_count,
        uint32_t stride_bytes)
    {
        if (!device_ok(device)
            || !destination.valid()
            || !values
            || value_byte_count == 0u
            || element_count == 0u
            || stride_bytes == 0u)
        {
            return false;
        }

        return dx12::internal::update_mesh_field_visualization_values_dx12(
            device,
            destination,
            values,
            value_byte_count,
            element_count,
            stride_bytes);
    }

    GPUHandle upload_mesh_field_raw_faces(
        Device& device,
        const MeshFieldRawFaceUploadDesc& desc)
    {
        if (!device_ok(device) || !desc.valid()) {
            return {};
        }

        std::vector<float> packed(
            static_cast<size_t>(desc.field->element_count)
            * static_cast<size_t>(desc.channel_count),
            0.0f);
        for (uint32_t channel_index = 0u;
             channel_index < desc.channel_count;
             ++channel_index)
        {
            const auto* channel =
                find_field_channel(*desc.field, desc.channel_ids[channel_index]);
            if (!channel || !channel_bytes_are_valid(*desc.field, *channel)) {
                return {};
            }

            const size_t base =
                static_cast<size_t>(channel_index)
                * static_cast<size_t>(desc.field->element_count);
            for (uint32_t face = 0u; face < desc.field->element_count; ++face)
            {
                packed[base + face] =
                    read_channel_value_as_float(*desc.field, *channel, face);
            }
        }

        return upload_mesh_field_visualization_values(
            device,
            reinterpret_cast<const std::byte*>(packed.data()),
            static_cast<uint64_t>(packed.size() * sizeof(float)),
            static_cast<uint32_t>(packed.size()),
            sizeof(float));
    }

    GPUHandle upload_mesh_field_raw_vertices(
        Device& device,
        const MeshFieldRawVertexUploadDesc& desc)
    {
        if (!device_ok(device) || !desc.valid()) {
            return {};
        }

        std::vector<float> packed(
            static_cast<size_t>(desc.field->element_count)
            * static_cast<size_t>(desc.channel_count),
            0.0f);
        for (uint32_t channel_index = 0u;
             channel_index < desc.channel_count;
             ++channel_index)
        {
            const auto* channel =
                find_field_channel(*desc.field, desc.channel_ids[channel_index]);
            if (!channel || !channel_bytes_are_valid(*desc.field, *channel)) {
                return {};
            }

            const size_t base =
                static_cast<size_t>(channel_index)
                * static_cast<size_t>(desc.field->element_count);
            for (uint32_t vertex = 0u;
                 vertex < desc.field->element_count;
                 ++vertex)
            {
                packed[base + vertex] =
                    read_channel_value_as_float(*desc.field, *channel, vertex);
            }
        }

        return upload_mesh_field_visualization_values(
            device,
            reinterpret_cast<const std::byte*>(packed.data()),
            static_cast<uint64_t>(packed.size() * sizeof(float)),
            static_cast<uint32_t>(packed.size()),
            sizeof(float));
    }

    GPUHandle create_mesh_field_visualization_from_gpu_source(
        Device& device,
        GPUHandle source_buffer,
        uint64_t byte_offset,
        uint32_t element_count,
        uint32_t stride_bytes)
    {
        if (!device_ok(device)
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

    bool update_mesh_field_visualization_from_gpu_source(
        Device& device,
        GPUHandle destination,
        GPUHandle source_buffer,
        uint64_t byte_offset,
        uint32_t element_count,
        uint32_t stride_bytes)
    {
        if (!device_ok(device)
            || !destination.valid()
            || !source_buffer.valid()
            || element_count == 0u
            || stride_bytes == 0u)
        {
            return false;
        }

        return dx12::internal
            ::update_mesh_field_visualization_from_gpu_source_dx12(
                device,
                destination,
                source_buffer,
                byte_offset,
                element_count,
                stride_bytes);
    }

    bool release_mesh_field_visualization(
        Device& device,
        GPUHandle handle)
    {
        if (!device_ok(device) || !handle.valid()) {
            return false;
        }

        return dx12::internal::release_mesh_field_visualization_dx12(
            device,
            handle);
    }
}
