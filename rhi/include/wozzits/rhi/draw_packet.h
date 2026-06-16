#pragma once

// wozzits/rhi/draw_packet.h
//
// O3DE-style packet split: per-object state is stored once on DrawPacket, and
// each DrawItem carries only the per-pass leaf state.

#include <wozzits/rhi/draw_item.h>
#include <wozzits/rhi/geometry_view.h>
#include <wozzits/rhi/shader_resource_group.h>

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace wz::rhi
{
    struct DrawPacketAllocator
    {
    };

    struct DrawRequest
    {
        DrawListTag pass{};
        Tag program{};
        const ShaderResourceGroup* unique_srg = nullptr;
        StreamBufferIndices streams;
        DrawItemSortKey sort_key = 0;
        DrawListMask filter_mask{};
        uint32_t stencil_ref = 0;
        RenderDomain render_domain = RenderDomain::Opaque;
    };

    struct DrawPacket
    {
        GeometryView geometry;
        std::vector<uint8_t> root_constants;
        std::vector<const ShaderResourceGroup*> shared_srgs;

        std::vector<DrawItem> draw_items;
        std::vector<DrawListTag> pass_tags;
        std::vector<DrawItemSortKey> sort_keys;
        std::vector<DrawListMask> filter_masks;
        DrawListMask draw_list_mask;

        [[nodiscard]] const DrawItem* get_draw_item(
            DrawListTag pass) const noexcept
        {
            if (!pass.valid()) {
                return nullptr;
            }
            for (const DrawItem& item : draw_items) {
                if (item.pass == pass) {
                    return &item;
                }
            }
            return nullptr;
        }

        [[nodiscard]] const DrawItem* get_draw_item(
            DrawListTag pass,
            DrawListMask filter_mask) const noexcept
        {
            if (!pass.valid() || filter_mask.empty()) {
                return nullptr;
            }
            for (const DrawItem& item : draw_items) {
                if (item.pass == pass
                    && item.filter_mask.intersects(filter_mask))
                {
                    return &item;
                }
            }
            return nullptr;
        }

        [[nodiscard]] DrawItem* get_draw_item(DrawListTag pass) noexcept
        {
            return const_cast<DrawItem*>(
                std::as_const(*this).get_draw_item(pass));
        }

        [[nodiscard]] DrawItem* get_draw_item(
            DrawListTag pass,
            DrawListMask filter_mask) noexcept
        {
            return const_cast<DrawItem*>(
                std::as_const(*this).get_draw_item(pass, filter_mask));
        }
    };

    class DrawPacketBuilder
    {
    public:
        [[nodiscard]] static DrawPacketBuilder begin(
            DrawPacketAllocator& allocator)
        {
            DrawPacketBuilder builder;
            builder.allocator_ = &allocator;
            return builder;
        }

        DrawPacketBuilder& set_geometry(GeometryView geometry)
        {
            packet_.geometry = std::move(geometry);
            return *this;
        }

        DrawPacketBuilder& set_root_constants(std::span<const uint8_t> bytes)
        {
            packet_.root_constants.assign(bytes.begin(), bytes.end());
            return *this;
        }

        DrawPacketBuilder& add_shader_resource_group(
            const ShaderResourceGroup& srg)
        {
            packet_.shared_srgs.push_back(&srg);
            return *this;
        }

        bool add_draw_item(const DrawRequest& request)
        {
            if (!request.pass.valid() || !request.program.valid()) {
                return false;
            }

            DrawItem item;
            item.program = request.program;
            item.render_domain = request.render_domain;
            item.pass = request.pass;
            item.streams = request.streams;
            item.unique_srg = request.unique_srg;
            item.sort_key = request.sort_key;
            item.filter_mask = request.filter_mask;
            item.stencil_ref = request.stencil_ref;

            packet_.draw_items.push_back(item);
            packet_.pass_tags.push_back(request.pass);
            packet_.sort_keys.push_back(request.sort_key);
            packet_.filter_masks.push_back(request.filter_mask);
            packet_.draw_list_mask.add(request.pass);
            return true;
        }

        [[nodiscard]] DrawPacket end()
        {
            return std::move(packet_);
        }

    private:
        DrawPacketAllocator* allocator_ = nullptr;
        DrawPacket packet_;
    };
}
