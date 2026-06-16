#include "wz_test.h"

#include <wozzits/rhi/draw_packet_validation.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

using namespace wz::rhi;

namespace
{
    struct PacketFixture
    {
        DescriptorSemanticRegistry descriptors;
        ConstantSemanticRegistry constants;
        DrawListTagRegistry passes;
        RenderProgramRegistry programs;

        Tag position_data{};
        Tag object_constants{};
        DrawListTag debug{};
        DrawListTag depth{};
        DrawListTag selected_filter{};
        DrawListTag unselected_filter{};
        Tag wire_program{};
        Tag depth_program{};
        ShaderResourceGroupLayout object_layout{};
    };

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
        geometry.index_buffer = GpuResourceHandle{ 20, 1 };
        geometry.index_count = 36;
        geometry.vertex_count = 24;
        return geometry;
    }

    RenderProgramDesc make_program_desc(
        const char* name,
        const ShaderResourceGroupLayout& layout,
        VertexSource source = VertexSource::InputAssembler)
    {
        RenderProgramDesc desc;
        desc.name = name;
        desc.vertex_shader = std::string(name) + "_vs.hlsl";
        desc.pixel_shader = std::string(name) + "_ps.hlsl";
        desc.vertex_source = source;
        if (source == VertexSource::InputAssembler) {
            desc.vertex_layout.attributes = {
                VertexAttribute{ 0, VertexFormat::Float32x3, 0, 0, VertexStepRate::PerVertex },
                VertexAttribute{ 1, VertexFormat::Float32x3, 0, 1, VertexStepRate::PerVertex },
            };
        }
        desc.shader_resource_groups.push_back(layout);
        return desc;
    }

    PacketFixture make_fixture()
    {
        PacketFixture f;
        f.position_data = f.descriptors.acquire("position_data");
        f.object_constants = f.constants.acquire("object_constants");
        f.debug = f.passes.acquire("debug");
        f.depth = f.passes.acquire("depth");
        f.selected_filter = f.passes.acquire("selected");
        f.unselected_filter = f.passes.acquire("unselected");

        f.object_layout.binding_slot = 2;
        f.object_layout.descriptors.push_back(DescriptorBinding{
            DescriptorKind::StructuredBufferSRV,
            ShaderStage::Vertex,
            f.position_data,
            /*shader_register*/ 0,
            /*register_space*/ 2,
            /*descriptor_count*/ 1 });
        WZ_CHECK(f.object_layout.constants.append(f.object_constants, 64));

        f.wire_program = f.programs.register_program(
            make_program_desc("wire_debug", f.object_layout));
        f.depth_program = f.programs.register_program(
            make_program_desc("wire_depth", f.object_layout));
        WZ_CHECK(f.wire_program.valid());
        WZ_CHECK(f.depth_program.valid());
        return f;
    }

    ShaderResourceGroup make_satisfied_object_srg(
        const PacketFixture& f,
        GpuResourceHandle handle = GpuResourceHandle{ 30, 1 })
    {
        ShaderResourceGroup srg(f.object_layout);
        WZ_CHECK(srg.set(f.position_data, handle).has_value());
        return srg;
    }

    StreamBufferIndices streams(uint32_t a)
    {
        StreamBufferIndices indices;
        WZ_CHECK(indices.add(a));
        return indices;
    }

    StreamBufferIndices streams(uint32_t a, uint32_t b)
    {
        StreamBufferIndices indices;
        WZ_CHECK(indices.add(a));
        WZ_CHECK(indices.add(b));
        return indices;
    }

    DrawPacket make_valid_packet(
        PacketFixture& f,
        const ShaderResourceGroup& object_srg)
    {
        const std::array<uint32_t, 16> root_constants = {
            1u, 0u, 0u, 0u,
            0u, 1u, 0u, 0u,
            0u, 0u, 1u, 0u,
            0u, 0u, 0u, 1u,
        };

        DrawPacketAllocator allocator;
        DrawPacketBuilder builder = DrawPacketBuilder::begin(allocator);
        builder
            .set_geometry(make_geometry())
            .set_root_constants(std::span<const uint8_t>{
                reinterpret_cast<const uint8_t*>(root_constants.data()),
                root_constants.size() * sizeof(uint32_t) })
            .add_shader_resource_group(object_srg);

        WZ_CHECK(builder.add_draw_item(DrawRequest{
            f.debug,
            f.wire_program,
            nullptr,
            streams(0, 1),
            20,
            DrawListMask::from(f.selected_filter),
            0,
            RenderDomain::Opaque }));
        return builder.end();
    }
}

static void validate_program_rejects_malformed_programs()
{
    PacketFixture f = make_fixture();

    RenderProgramDesc valid = make_program_desc("valid", f.object_layout);
    WZ_CHECK(validate_render_program_desc(valid));

    RenderProgramDesc duplicate_slot = valid;
    duplicate_slot.shader_resource_groups.push_back(f.object_layout);
    WZ_CHECK_FALSE(validate_render_program_desc(duplicate_slot));

    RenderProgramDesc null_descriptor = valid;
    null_descriptor.shader_resource_groups[0].descriptors[0].semantic = {};
    WZ_CHECK_FALSE(validate_render_program_desc(null_descriptor));

    RenderProgramDesc empty_shader = valid;
    empty_shader.vertex_shader.clear();
    WZ_CHECK_FALSE(validate_render_program_desc(empty_shader));
}

static void duplicate_descriptor_semantic_is_rejected()
{
    PacketFixture f = make_fixture();
    ShaderResourceGroupLayout duplicate = f.object_layout;
    duplicate.descriptors.push_back(DescriptorBinding{
        DescriptorKind::StructuredBufferSRV,
        ShaderStage::Pixel,
        f.position_data,
        /*shader_register*/ 1,
        /*register_space*/ 2,
        /*descriptor_count*/ 1 });

    WZ_CHECK_FALSE(validate_shader_resource_group_layout(duplicate));
    WZ_CHECK_FALSE(validate_render_program_desc(
        make_program_desc("duplicate_descriptor", duplicate)));
}

static void packet_item_streams_must_exist_in_geometry()
{
    PacketFixture f = make_fixture();
    ShaderResourceGroup object_srg = make_satisfied_object_srg(f);

    DrawPacketAllocator allocator;
    DrawPacketBuilder builder = DrawPacketBuilder::begin(allocator);
    builder
        .set_geometry(make_geometry())
        .add_shader_resource_group(object_srg);
    WZ_CHECK(builder.add_draw_item(DrawRequest{
        f.debug,
        f.wire_program,
        nullptr,
        streams(0, 99),
        0,
        DrawListMask::from(f.debug) }));

    WZ_CHECK_FALSE(validate_draw_packet(builder.end(), f.programs));
}

static void packet_missing_required_slot_srg_is_invalid()
{
    PacketFixture f = make_fixture();

    DrawPacketAllocator allocator;
    DrawPacketBuilder builder = DrawPacketBuilder::begin(allocator);
    builder.set_geometry(make_geometry());
    WZ_CHECK(builder.add_draw_item(DrawRequest{
        f.debug,
        f.wire_program,
        nullptr,
        streams(0, 1),
        0,
        DrawListMask::from(f.debug) }));

    WZ_CHECK_FALSE(validate_draw_packet(builder.end(), f.programs));
}

static void packet_shared_srg_slot_collision_is_invalid()
{
    PacketFixture f = make_fixture();
    ShaderResourceGroup first = make_satisfied_object_srg(f, GpuResourceHandle{ 31, 1 });
    ShaderResourceGroup second = make_satisfied_object_srg(f, GpuResourceHandle{ 32, 1 });

    DrawPacket packet = make_valid_packet(f, first);
    packet.shared_srgs.push_back(&second);

    WZ_CHECK_FALSE(validate_draw_packet(packet, f.programs));
}

static void item_streams_cover_program_vertex_buffer_slots()
{
    PacketFixture f = make_fixture();
    ShaderResourceGroup object_srg = make_satisfied_object_srg(f);

    DrawPacket valid = make_valid_packet(f, object_srg);
    WZ_CHECK(validate_draw_packet(valid, f.programs));

    DrawPacket missing_normal = valid;
    missing_normal.draw_items[0].streams = streams(0);
    missing_normal.pass_tags[0] = missing_normal.draw_items[0].pass;
    WZ_CHECK_FALSE(validate_draw_packet(missing_normal, f.programs));
}

static void pull_program_requires_no_ia_streams()
{
    PacketFixture f = make_fixture();
    RenderProgramDesc pull_desc =
        make_program_desc("pull_debug", f.object_layout, VertexSource::Pull);
    const Tag pull_program = f.programs.register_program(pull_desc);
    ShaderResourceGroup object_srg = make_satisfied_object_srg(f);

    DrawPacketAllocator allocator;
    DrawPacketBuilder builder = DrawPacketBuilder::begin(allocator);
    builder
        .set_geometry(make_geometry())
        .add_shader_resource_group(object_srg);
    WZ_CHECK(builder.add_draw_item(DrawRequest{
        f.debug,
        pull_program,
        nullptr,
        StreamBufferIndices{},
        0,
        DrawListMask::from(f.debug) }));
    WZ_CHECK(validate_draw_packet(builder.end(), f.programs));

    DrawPacketBuilder bad_builder = DrawPacketBuilder::begin(allocator);
    bad_builder
        .set_geometry(make_geometry())
        .add_shader_resource_group(object_srg);
    WZ_CHECK(bad_builder.add_draw_item(DrawRequest{
        f.debug,
        pull_program,
        nullptr,
        streams(0),
        0,
        DrawListMask::from(f.debug) }));
    WZ_CHECK_FALSE(validate_draw_packet(bad_builder.end(), f.programs));
}

static void satisfies_assumes_shared_tag_registry()
{
    DescriptorSemanticRegistry registry_a;
    DescriptorSemanticRegistry registry_b;
    const Tag line_a = registry_a.acquire("line_data");
    const Tag different_first = registry_b.acquire("different_first_tag");
    const Tag line_b = registry_b.acquire("line_data");
    WZ_CHECK(different_first.valid());

    ShaderResourceGroupLayout layout_a;
    layout_a.binding_slot = 2;
    layout_a.descriptors.push_back(DescriptorBinding{
        DescriptorKind::StructuredBufferSRV,
        ShaderStage::Vertex,
        line_a,
        0,
        2,
        1 });

    ShaderResourceGroupLayout layout_b;
    layout_b.binding_slot = 2;
    layout_b.descriptors.push_back(DescriptorBinding{
        DescriptorKind::StructuredBufferSRV,
        ShaderStage::Vertex,
        line_b,
        0,
        2,
        1 });

    ShaderResourceGroup srg(layout_a);
    WZ_CHECK(srg.set(line_a, GpuResourceHandle{ 40, 1 }).has_value());
    WZ_CHECK(srg.satisfies(layout_a));
    WZ_CHECK_FALSE(srg.satisfies(layout_b));
}

static void duplicate_pass_tag_can_be_disambiguated_by_filter_mask()
{
    PacketFixture f = make_fixture();
    ShaderResourceGroup object_srg = make_satisfied_object_srg(f);

    DrawPacketAllocator allocator;
    DrawPacketBuilder builder = DrawPacketBuilder::begin(allocator);
    builder
        .set_geometry(make_geometry())
        .add_shader_resource_group(object_srg);
    WZ_CHECK(builder.add_draw_item(DrawRequest{
        f.debug,
        f.wire_program,
        nullptr,
        streams(0, 1),
        10,
        DrawListMask::from(f.selected_filter) }));
    WZ_CHECK(builder.add_draw_item(DrawRequest{
        f.debug,
        f.wire_program,
        nullptr,
        streams(0, 1),
        20,
        DrawListMask::from(f.unselected_filter) }));

    const DrawPacket packet = builder.end();
    const DrawItem* selected =
        packet.get_draw_item(f.debug, DrawListMask::from(f.selected_filter));
    const DrawItem* unselected =
        packet.get_draw_item(f.debug, DrawListMask::from(f.unselected_filter));

    WZ_CHECK(selected != nullptr);
    WZ_CHECK(unselected != nullptr);
    if (selected && unselected) {
        WZ_CHECK_EQ(selected->sort_key, 10u);
        WZ_CHECK_EQ(unselected->sort_key, 20u);
    }
}

static void draw_item_properties_sort_by_key_without_moving_items()
{
    PacketFixture f = make_fixture();
    ShaderResourceGroup object_srg = make_satisfied_object_srg(f);

    DrawPacketAllocator allocator;
    DrawPacketBuilder builder = DrawPacketBuilder::begin(allocator);
    builder
        .set_geometry(make_geometry())
        .add_shader_resource_group(object_srg);
    WZ_CHECK(builder.add_draw_item(DrawRequest{
        f.debug, f.wire_program, nullptr, streams(0, 1), 30,
        DrawListMask::from(f.debug) }));
    WZ_CHECK(builder.add_draw_item(DrawRequest{
        f.depth, f.depth_program, nullptr, streams(0, 1), 10,
        DrawListMask::from(f.depth) }));

    DrawPacket packet = builder.end();
    const DrawItem* first_before = &packet.draw_items[0];
    const DrawItem* second_before = &packet.draw_items[1];

    std::array<DrawItemProperties, 2> properties = {
        DrawItemProperties{ &packet.draw_items[0], packet.draw_items[0].sort_key, 0.0f },
        DrawItemProperties{ &packet.draw_items[1], packet.draw_items[1].sort_key, 0.0f },
    };
    std::sort(properties.begin(), properties.end());

    WZ_CHECK(properties[0].item == second_before);
    WZ_CHECK(properties[1].item == first_before);
    WZ_CHECK(&packet.draw_items[0] == first_before);
    WZ_CHECK(&packet.draw_items[1] == second_before);
}

int main()
{
    WZ_RUN(validate_program_rejects_malformed_programs);
    WZ_RUN(duplicate_descriptor_semantic_is_rejected);
    WZ_RUN(packet_item_streams_must_exist_in_geometry);
    WZ_RUN(packet_missing_required_slot_srg_is_invalid);
    WZ_RUN(packet_shared_srg_slot_collision_is_invalid);
    WZ_RUN(item_streams_cover_program_vertex_buffer_slots);
    WZ_RUN(pull_program_requires_no_ia_streams);
    WZ_RUN(satisfies_assumes_shared_tag_registry);
    WZ_RUN(duplicate_pass_tag_can_be_disambiguated_by_filter_mask);
    WZ_RUN(draw_item_properties_sort_by_key_without_moving_items);
    WZ_TEST_RETURN();
}
