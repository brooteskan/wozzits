#include "wz_test.h"

#include <wozzits/rhi/draw_packet.h>
#include <wozzits/rhi/render_program_registry.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>

using namespace wz::rhi;

namespace
{
    struct WireframeFixture
    {
        DescriptorSemanticRegistry descriptors;
        ConstantSemanticRegistry constants;
        DrawListTagRegistry passes;
        RenderProgramRegistry programs;

        Tag world{};
        Tag wire_style{};
        Tag line_data{};
        DrawListTag debug_pass{};
        Tag wireframe_program{};
        ShaderResourceGroupLayout object_layout{};
    };

    GeometryView make_cube_geometry()
    {
        GeometryView geometry;
        geometry.streams.push_back(StreamBufferView{
            GpuResourceHandle{ 100, 1 },
            /*offset*/ 0,
            /*stride*/ 12 });
        geometry.streams.push_back(StreamBufferView{
            GpuResourceHandle{ 101, 1 },
            /*offset*/ 0,
            /*stride*/ 12 });
        geometry.index_buffer = GpuResourceHandle{ 200, 1 };
        geometry.index_count = 36;
        geometry.vertex_count = 24;
        return geometry;
    }

    WireframeFixture make_wireframe_fixture()
    {
        WireframeFixture f;
        f.world = f.constants.acquire("world");
        f.wire_style = f.constants.acquire("wire_style");
        f.line_data = f.descriptors.acquire("line_data");
        f.debug_pass = f.passes.acquire("debug");

        const RootConstantBinding constant_bindings[2] = {
            RootConstantBinding{
                ShaderStage::All,
                f.world,
                /*shader_register*/ 0,
                /*register_space*/ 2,
                /*value_count*/ 16 },
            RootConstantBinding{
                ShaderStage::Pixel,
                f.wire_style,
                /*shader_register*/ 1,
                /*register_space*/ 2,
                /*value_count*/ 4 },
        };
        const auto constants_layout = make_constants_layout(constant_bindings);
        WZ_CHECK(constants_layout.has_value());

        f.object_layout.binding_slot = 2;
        if (constants_layout) {
            f.object_layout.constants = *constants_layout;
        }
        f.object_layout.descriptors.push_back(DescriptorBinding{
            DescriptorKind::StructuredBufferSRV,
            ShaderStage::Vertex,
            f.line_data,
            /*shader_register*/ 0,
            /*register_space*/ 2,
            /*descriptor_count*/ 1 });

        RenderProgramDesc desc;
        desc.name = "mesh_wireframe_debug";
        desc.vertex_shader = "shaders/debug/wireframe_vs.hlsl";
        desc.pixel_shader = "shaders/debug/wireframe_ps.hlsl";
        desc.vertex_source = VertexSource::InputAssembler;
        desc.vertex_layout.attributes = {
            VertexAttribute{ 0, VertexFormat::Float32x3, 0, 0, VertexStepRate::PerVertex },
            VertexAttribute{ 1, VertexFormat::Float32x3, 12, 0, VertexStepRate::PerVertex },
        };
        desc.topology = PrimitiveTopology::TriangleList;
        desc.blend_mode = BlendMode::Opaque;
        desc.depth_mode = DepthMode::TestWrite;
        desc.raster_mode = RasterMode::WireframeCullNone;
        desc.shader_resource_groups.push_back(f.object_layout);

        f.wireframe_program = f.programs.register_program(desc);
        WZ_CHECK(f.wireframe_program.valid());
        return f;
    }
}

static void program_declares_tag_named_srg_layouts_and_constants()
{
    WireframeFixture f = make_wireframe_fixture();

    const RenderProgramDesc* program =
        f.programs.get(f.programs.find("mesh_wireframe_debug"));
    WZ_CHECK(program != nullptr);
    if (!program) {
        return;
    }

    const ShaderResourceGroupLayout* object_layout =
        find_shader_resource_group_layout(program->shader_resource_groups, 2);
    WZ_CHECK(object_layout != nullptr);
    if (!object_layout) {
        return;
    }

    WZ_CHECK(object_layout->descriptors[0].semantic.valid());
    WZ_CHECK_EQ(f.descriptors.name_of(object_layout->descriptors[0].semantic),
                std::string_view{ "line_data" });

    WZ_CHECK(object_layout->constants.find(f.world).has_value());
    WZ_CHECK(object_layout->constants.find(f.wire_style).has_value());
    WZ_CHECK_EQ(f.constants.name_of(f.world), std::string_view{ "world" });
    WZ_CHECK_EQ(f.constants.name_of(f.wire_style),
                std::string_view{ "wire_style" });
}

static void unknown_program_or_srg_tag_is_checkable_not_silent()
{
    WireframeFixture f = make_wireframe_fixture();
    ShaderResourceGroup object_srg(f.object_layout);

    const auto line_index =
        object_srg.set(f.line_data, GpuResourceHandle{ 300, 1 });
    WZ_CHECK(line_index.has_value());
    if (line_index) {
        WZ_CHECK_EQ(*line_index, 0u);
    }

    const Tag unknown_resource = f.descriptors.acquire("not_in_wireframe_layout");
    WZ_CHECK(unknown_resource.valid());
    WZ_CHECK_FALSE(object_srg.resolve_resource_index(unknown_resource).has_value());
    WZ_CHECK_FALSE(
        object_srg.set(unknown_resource, GpuResourceHandle{ 301, 1 }).has_value());

    const Tag missing_program = f.programs.find("not_registered_program");
    WZ_CHECK_FALSE(missing_program.valid());

    DrawPacketAllocator allocator;
    DrawPacketBuilder builder = DrawPacketBuilder::begin(allocator);
    WZ_CHECK_FALSE(builder.add_draw_item(DrawRequest{
        f.debug_pass,
        missing_program,
        nullptr,
        {},
        0,
        DrawListMask::from(f.debug_pass) }));
}

static void srg_satisfies_only_when_slot_hash_and_bindings_match()
{
    WireframeFixture f = make_wireframe_fixture();
    ShaderResourceGroup object_srg(f.object_layout);

    WZ_CHECK_FALSE(object_srg.satisfies(f.object_layout));
    WZ_CHECK(object_srg.set(f.line_data, GpuResourceHandle{ 400, 1 }).has_value());
    WZ_CHECK(object_srg.satisfies(f.object_layout));

    ShaderResourceGroupLayout wrong_slot = f.object_layout;
    wrong_slot.binding_slot = 1;
    WZ_CHECK_FALSE(object_srg.satisfies(wrong_slot));

    ShaderResourceGroupLayout changed_hash = f.object_layout;
    changed_hash.descriptors.push_back(DescriptorBinding{
        DescriptorKind::Sampler,
        ShaderStage::Pixel,
        f.descriptors.acquire("debug_sampler"),
        /*shader_register*/ 1,
        /*register_space*/ 2,
        /*descriptor_count*/ 1 });
    WZ_CHECK_FALSE(object_srg.satisfies(changed_hash));

    ShaderResourceGroup missing_required(changed_hash);
    WZ_CHECK(missing_required.set(f.line_data, GpuResourceHandle{ 401, 1 }).has_value());
    WZ_CHECK_FALSE(missing_required.satisfies(changed_hash));
}

static void packet_builder_routes_pass_items_without_enum_fallback()
{
    WireframeFixture f = make_wireframe_fixture();
    const DrawListTag depth = f.passes.acquire("depth");
    RenderProgramDesc depth_desc;
    depth_desc.name = "mesh_depth_debug";
    depth_desc.vertex_shader = "shaders/debug/depth_vs.hlsl";
    depth_desc.pixel_shader = "shaders/debug/depth_ps.hlsl";
    const Tag depth_program = f.programs.register_program(depth_desc);

    StreamBufferIndices debug_streams;
    WZ_CHECK(debug_streams.add(0));
    WZ_CHECK(debug_streams.add(1));
    StreamBufferIndices depth_streams;
    WZ_CHECK(depth_streams.add(0));

    const std::array<uint32_t, 16> world = {
        1u, 0u, 0u, 0u,
        0u, 1u, 0u, 0u,
        0u, 0u, 1u, 0u,
        0u, 0u, 0u, 1u,
    };
    ShaderResourceGroup object_srg(f.object_layout);

    DrawPacketAllocator allocator;
    DrawPacketBuilder builder = DrawPacketBuilder::begin(allocator);
    builder
        .set_geometry(make_cube_geometry())
        .set_root_constants(std::span<const uint8_t>{
            reinterpret_cast<const uint8_t*>(world.data()),
            world.size() * sizeof(uint32_t) })
        .add_shader_resource_group(object_srg);

    WZ_CHECK(builder.add_draw_item(DrawRequest{
        f.debug_pass,
        f.wireframe_program,
        nullptr,
        debug_streams,
        50,
        DrawListMask::from(f.debug_pass),
        7,
        RenderDomain::Opaque }));
    WZ_CHECK(builder.add_draw_item(DrawRequest{
        depth,
        depth_program,
        nullptr,
        depth_streams,
        10,
        DrawListMask::from(depth),
        0,
        RenderDomain::Opaque }));

    const DrawPacket packet = builder.end();

    WZ_CHECK(packet.draw_list_mask.contains(f.debug_pass));
    WZ_CHECK(packet.draw_list_mask.contains(depth));
    WZ_CHECK(packet.get_draw_item(DrawListTag{}) == nullptr);

    const DrawItem* debug_item = packet.get_draw_item(f.debug_pass);
    const DrawItem* depth_item = packet.get_draw_item(depth);
    WZ_CHECK(debug_item != nullptr);
    WZ_CHECK(depth_item != nullptr);
    if (!debug_item || !depth_item) {
        return;
    }

    WZ_CHECK(debug_item->program == f.wireframe_program);
    WZ_CHECK(depth_item->program == depth_program);
    WZ_CHECK_EQ(debug_item->streams.indices.size(), static_cast<size_t>(2));
    WZ_CHECK_EQ(depth_item->streams.indices.size(), static_cast<size_t>(1));
    WZ_CHECK_EQ(packet.shared_srgs.size(), static_cast<size_t>(1));
    WZ_CHECK(packet.shared_srgs[0] == &object_srg);
    WZ_CHECK_EQ(packet.root_constants.size(), world.size() * sizeof(uint32_t));
    WZ_CHECK_EQ(packet.geometry.index_count, 36u);
}

static void end_to_end_wireframe_packet_is_self_describing()
{
    WireframeFixture f = make_wireframe_fixture();
    ShaderResourceGroup object_srg(f.object_layout);
    WZ_CHECK(object_srg.set(f.line_data, GpuResourceHandle{ 500, 1 }).has_value());
    WZ_CHECK(object_srg.satisfies(f.object_layout));

    StreamBufferIndices streams;
    WZ_CHECK(streams.add(0));
    WZ_CHECK(streams.add(1));

    const std::array<uint32_t, 20> root_constants = {
        1u, 0u, 0u, 0u,
        0u, 1u, 0u, 0u,
        0u, 0u, 1u, 0u,
        0u, 0u, 0u, 1u,
        0xff00ffu, 1u, 0u, 0u,
    };

    DrawPacketAllocator allocator;
    DrawPacketBuilder builder = DrawPacketBuilder::begin(allocator);
    builder
        .set_geometry(make_cube_geometry())
        .set_root_constants(std::span<const uint8_t>{
            reinterpret_cast<const uint8_t*>(root_constants.data()),
            root_constants.size() * sizeof(uint32_t) })
        .add_shader_resource_group(object_srg);
    WZ_CHECK(builder.add_draw_item(DrawRequest{
        f.debug_pass,
        f.wireframe_program,
        nullptr,
        streams,
        1,
        DrawListMask::from(f.debug_pass),
        0,
        RenderDomain::Opaque }));

    const DrawPacket packet = builder.end();
    const DrawItem* item = packet.get_draw_item(f.debug_pass);

    WZ_CHECK(item != nullptr);
    WZ_CHECK(packet.draw_list_mask.contains(f.debug_pass));
    WZ_CHECK_EQ(packet.draw_items.size(), static_cast<size_t>(1));
    WZ_CHECK_EQ(packet.shared_srgs.size(), static_cast<size_t>(1));
    WZ_CHECK_EQ(packet.geometry.streams.size(), static_cast<size_t>(2));
    WZ_CHECK(packet.geometry.indexed());
    WZ_CHECK_EQ(packet.root_constants.size(),
                root_constants.size() * sizeof(uint32_t));
    if (item) {
        WZ_CHECK(item->program == f.wireframe_program);
        WZ_CHECK(item->pass == f.debug_pass);
        WZ_CHECK(item->streams.valid_for(packet.geometry));
        WZ_CHECK_EQ(item->sort_key, 1u);
        WZ_CHECK(item->filter_mask.contains(f.debug_pass));
    }
}

int main()
{
    WZ_RUN(program_declares_tag_named_srg_layouts_and_constants);
    WZ_RUN(unknown_program_or_srg_tag_is_checkable_not_silent);
    WZ_RUN(srg_satisfies_only_when_slot_hash_and_bindings_match);
    WZ_RUN(packet_builder_routes_pass_items_without_enum_fallback);
    WZ_RUN(end_to_end_wireframe_packet_is_self_describing);
    WZ_TEST_RETURN();
}
