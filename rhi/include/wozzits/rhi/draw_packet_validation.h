#pragma once

// wozzits/rhi/draw_packet_validation.h
//
// Null/fake validation for packet/program contracts. This is intentionally
// backend-free: it catches malformed packets before any API-specific bind path
// exists.

#include <wozzits/rhi/draw_packet.h>
#include <wozzits/rhi/render_program_registry.h>

#include <cstddef>

namespace wz::rhi
{
    [[nodiscard]] inline bool
    descriptor_semantics_are_unique(
        const ShaderResourceGroupLayout& layout) noexcept
    {
        for (size_t i = 0; i < layout.descriptors.size(); ++i) {
            const Tag semantic = layout.descriptors[i].semantic;
            if (!semantic.valid()) {
                return false;
            }
            for (size_t j = i + 1; j < layout.descriptors.size(); ++j) {
                if (layout.descriptors[j].semantic == semantic) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] inline bool
    validate_shader_resource_group_layout(
        const ShaderResourceGroupLayout& layout) noexcept
    {
        return descriptor_semantics_are_unique(layout);
    }

    [[nodiscard]] inline bool
    validate_render_program_desc(const RenderProgramDesc& desc) noexcept
    {
        if (desc.name.empty()
            || desc.vertex_shader.empty()
            || desc.pixel_shader.empty()
            || !shader_resource_group_slots_are_unique(
                desc.shader_resource_groups))
        {
            return false;
        }

        for (const ShaderResourceGroupLayout& layout :
             desc.shader_resource_groups)
        {
            if (!validate_shader_resource_group_layout(layout)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] inline bool
    item_streams_cover_program_vertex_layout(
        const DrawItem& item,
        const GeometryView& geometry,
        const RenderProgramDesc& program) noexcept
    {
        if (program.vertex_source != VertexSource::InputAssembler) {
            return item.streams.indices.empty();
        }

        if (!item.streams.valid_for(geometry)) {
            return false;
        }

        for (const VertexAttribute& attribute :
             program.vertex_layout.attributes)
        {
            if (!item.streams.contains(attribute.buffer_slot)
                || !geometry.valid_stream(attribute.buffer_slot))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] inline bool
    shared_srg_slots_are_unique(const DrawPacket& packet) noexcept
    {
        for (size_t i = 0; i < packet.shared_srgs.size(); ++i) {
            const ShaderResourceGroup* a = packet.shared_srgs[i];
            if (!a) {
                return false;
            }
            for (size_t j = i + 1; j < packet.shared_srgs.size(); ++j) {
                const ShaderResourceGroup* b = packet.shared_srgs[j];
                if (!b || a->binding_slot() == b->binding_slot()) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] inline bool
    packet_has_srg_for_layout(
        const DrawPacket& packet,
        const DrawItem& item,
        const ShaderResourceGroupLayout& layout) noexcept
    {
        if (item.unique_srg && item.unique_srg->satisfies(layout)) {
            return true;
        }
        for (const ShaderResourceGroup* srg : packet.shared_srgs) {
            if (srg && srg->satisfies(layout)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] inline bool
    packet_parallel_arrays_match_items(const DrawPacket& packet) noexcept
    {
        const size_t count = packet.draw_items.size();
        if (packet.pass_tags.size() != count
            || packet.sort_keys.size() != count
            || packet.filter_masks.size() != count)
        {
            return false;
        }
        for (size_t i = 0; i < count; ++i) {
            const DrawItem& item = packet.draw_items[i];
            if (!(packet.pass_tags[i] == item.pass)
                || packet.sort_keys[i] != item.sort_key
                || packet.filter_masks[i].bits != item.filter_mask.bits)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] inline bool
    validate_draw_packet(
        const DrawPacket& packet,
        const RenderProgramRegistry& programs) noexcept
    {
        if (!shared_srg_slots_are_unique(packet)
            || !packet_parallel_arrays_match_items(packet))
        {
            return false;
        }

        for (const DrawItem& item : packet.draw_items) {
            if (!item.pass.valid()
                || !item.program.valid()
                || !packet.draw_list_mask.contains(item.pass))
            {
                return false;
            }

            const RenderProgramDesc* program = programs.get(item.program);
            if (!program
                || !validate_render_program_desc(*program)
                || !item_streams_cover_program_vertex_layout(
                    item, packet.geometry, *program))
            {
                return false;
            }

            for (const ShaderResourceGroupLayout& layout :
                 program->shader_resource_groups)
            {
                if (!packet_has_srg_for_layout(packet, item, layout)) {
                    return false;
                }
            }
        }
        return true;
    }
}
