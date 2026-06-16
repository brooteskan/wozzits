#include "wz_test.h"

#include <wozzits/rhi/draw_packet.h>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

using namespace wz::rhi;

namespace
{
    GeometryView make_geometry()
    {
        GeometryView geometry;
        geometry.streams.push_back(StreamBufferView{
            GpuResourceHandle{ 10, 1 },
            /*offset*/ 0,
            /*stride*/ 12 });
        geometry.streams.push_back(StreamBufferView{
            GpuResourceHandle{ 11, 1 },
            /*offset*/ 0,
            /*stride*/ 12 });
        geometry.streams.push_back(StreamBufferView{
            GpuResourceHandle{ 12, 1 },
            /*offset*/ 0,
            /*stride*/ 8 });
        geometry.index_buffer = GpuResourceHandle{ 20, 1 };
        geometry.index_count = 36;
        return geometry;
    }

    ShaderResourceGroup make_empty_srg(uint32_t slot)
    {
        ShaderResourceGroupLayout layout;
        layout.binding_slot = slot;
        return ShaderResourceGroup(layout);
    }
}

static void builder_splits_packet_shared_state_from_pass_items()
{
    DrawListTagRegistry passes;
    const DrawListTag depth = passes.acquire("depth");
    const DrawListTag forward = passes.acquire("forward");

    TagRegistry<16> programs;
    const Tag depth_program = programs.acquire("mesh_depth");
    const Tag forward_program = programs.acquire("mesh_forward");

    ShaderResourceGroup material_srg = make_empty_srg(1);
    ShaderResourceGroup unique_forward_srg = make_empty_srg(2);

    const std::array<uint32_t, 16> world_constants = {
        1u, 0u, 0u, 0u,
        0u, 1u, 0u, 0u,
        0u, 0u, 1u, 0u,
        0u, 0u, 0u, 1u,
    };

    StreamBufferIndices depth_streams;
    WZ_CHECK(depth_streams.add(0));

    StreamBufferIndices forward_streams;
    WZ_CHECK(forward_streams.add(0));
    WZ_CHECK(forward_streams.add(1));
    WZ_CHECK(forward_streams.add(2));

    DrawPacketAllocator allocator;
    DrawPacketBuilder builder = DrawPacketBuilder::begin(allocator);
    builder
        .set_geometry(make_geometry())
        .set_root_constants(std::span<const uint8_t>{
            reinterpret_cast<const uint8_t*>(world_constants.data()),
            world_constants.size() * sizeof(uint32_t) })
        .add_shader_resource_group(material_srg);

    WZ_CHECK(builder.add_draw_item(DrawRequest{
        depth,
        depth_program,
        /*unique_srg*/ nullptr,
        depth_streams,
        /*sort_key*/ 10,
        DrawListMask::from(depth),
        /*stencil_ref*/ 1,
        RenderDomain::Opaque }));
    WZ_CHECK(builder.add_draw_item(DrawRequest{
        forward,
        forward_program,
        &unique_forward_srg,
        forward_streams,
        /*sort_key*/ 20,
        DrawListMask::from(forward),
        /*stencil_ref*/ 2,
        RenderDomain::Opaque }));

    const DrawPacket packet = builder.end();

    WZ_CHECK_EQ(packet.geometry.index_count, 36u);
    WZ_CHECK_EQ(packet.root_constants.size(), world_constants.size() * sizeof(uint32_t));
    WZ_CHECK_EQ(packet.shared_srgs.size(), static_cast<size_t>(1));
    WZ_CHECK(packet.shared_srgs[0] == &material_srg);
    WZ_CHECK_EQ(packet.draw_items.size(), static_cast<size_t>(2));
    WZ_CHECK(packet.draw_list_mask.contains(depth));
    WZ_CHECK(packet.draw_list_mask.contains(forward));

    const DrawItem* depth_item = packet.get_draw_item(depth);
    const DrawItem* forward_item = packet.get_draw_item(forward);
    WZ_CHECK(depth_item != nullptr);
    WZ_CHECK(forward_item != nullptr);
    if (!depth_item || !forward_item) {
        return;
    }

    WZ_CHECK(depth_item->program == depth_program);
    WZ_CHECK(depth_item->unique_srg == nullptr);
    WZ_CHECK_EQ(depth_item->streams.indices.size(), static_cast<size_t>(1));
    WZ_CHECK_EQ(depth_item->sort_key, 10u);
    WZ_CHECK_EQ(depth_item->stencil_ref, 1u);

    WZ_CHECK(forward_item->program == forward_program);
    WZ_CHECK(forward_item->unique_srg == &unique_forward_srg);
    WZ_CHECK_EQ(forward_item->streams.indices.size(), static_cast<size_t>(3));
    WZ_CHECK_EQ(forward_item->sort_key, 20u);
    WZ_CHECK_EQ(forward_item->stencil_ref, 2u);
}

static void packet_keeps_parallel_pass_sort_and_filter_arrays()
{
    DrawListTagRegistry passes;
    const DrawListTag depth = passes.acquire("depth");
    const DrawListTag forward = passes.acquire("forward");

    TagRegistry<16> programs;
    const Tag depth_program = programs.acquire("mesh_depth");
    const Tag forward_program = programs.acquire("mesh_forward");

    DrawPacketAllocator allocator;
    DrawPacketBuilder builder = DrawPacketBuilder::begin(allocator);
    WZ_CHECK(builder.add_draw_item(DrawRequest{
        depth, depth_program, nullptr, {}, 11, DrawListMask::from(depth) }));
    WZ_CHECK(builder.add_draw_item(DrawRequest{
        forward, forward_program, nullptr, {}, 22, DrawListMask::from(forward) }));

    const DrawPacket packet = builder.end();

    WZ_CHECK_EQ(packet.pass_tags.size(), static_cast<size_t>(2));
    WZ_CHECK_EQ(packet.sort_keys.size(), static_cast<size_t>(2));
    WZ_CHECK_EQ(packet.filter_masks.size(), static_cast<size_t>(2));
    WZ_CHECK(packet.pass_tags[0] == depth);
    WZ_CHECK(packet.pass_tags[1] == forward);
    WZ_CHECK_EQ(packet.sort_keys[0], 11u);
    WZ_CHECK_EQ(packet.sort_keys[1], 22u);
    WZ_CHECK(packet.filter_masks[0].contains(depth));
    WZ_CHECK(packet.filter_masks[1].contains(forward));
}

static void builder_rejects_missing_pass_or_program()
{
    DrawListTagRegistry passes;
    const DrawListTag depth = passes.acquire("depth");

    TagRegistry<16> programs;
    const Tag program = programs.acquire("mesh_depth");

    DrawPacketAllocator allocator;
    DrawPacketBuilder builder = DrawPacketBuilder::begin(allocator);
    WZ_CHECK_FALSE(builder.add_draw_item(DrawRequest{
        DrawListTag{}, program, nullptr, {}, 0, DrawListMask::from(depth) }));
    WZ_CHECK_FALSE(builder.add_draw_item(DrawRequest{
        depth, Tag{}, nullptr, {}, 0, DrawListMask::from(depth) }));
}

int main()
{
    WZ_RUN(builder_splits_packet_shared_state_from_pass_items);
    WZ_RUN(packet_keeps_parallel_pass_sort_and_filter_arrays);
    WZ_RUN(builder_rejects_missing_pass_or_program);
    WZ_TEST_RETURN();
}
