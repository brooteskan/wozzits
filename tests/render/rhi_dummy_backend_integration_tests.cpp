#include <gtest/gtest.h>

#include "fake_gpu_backend.h"

#include <engine/rendering/rhi_context.h>
#include <engine/rendering/rhi_mesh_bridge.h>
#include <engine/rendering/rhi_render_program_bridge.h>

#include <wozzits/rhi/constants_layout.h>
#include <wozzits/rhi/draw_encode.h>
#include <wozzits/rhi/draw_packet.h>
#include <wozzits/rhi/frame_graph.h>
#include <wozzits/rhi/render_program_registry.h>
#include <wozzits/rhi/shader_resource_group.h>
#include <wozzits/rhi/shader_resource_group_layout.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ea = wz::engine::assets;

namespace
{
    struct RecordedBarrier
    {
        wz::rhi::GpuResourceHandle resource;
        wz::rhi::ResourceState from = wz::rhi::ResourceState::Undefined;
        wz::rhi::ResourceState to = wz::rhi::ResourceState::Undefined;
    };

    struct RecordingCommandRecorder final : wz::rhi::CommandRecorder
    {
        std::vector<RecordedBarrier> barriers;
        wz::rhi::Tag pipeline{};
        std::vector<uint32_t> bound_slots;
        std::optional<wz::rhi::DrawArgs> draw_args;

        void barrier(wz::rhi::GpuResourceHandle resource,
                     wz::rhi::ResourceState from,
                     wz::rhi::ResourceState to) override
        {
            barriers.push_back(RecordedBarrier{ resource, from, to });
        }

        void set_pipeline(wz::rhi::Tag program) override
        {
            pipeline = program;
        }

        void set_root_constants(std::span<const uint8_t>) override
        {
        }

        void bind_resource_group(
            uint32_t slot,
            const wz::rhi::ShaderResourceGroup&) override
        {
            bound_slots.push_back(slot);
        }

        void set_geometry(const wz::rhi::GeometryView&,
                          const wz::rhi::StreamBufferIndices&) override
        {
        }

        void draw(const wz::rhi::DrawArgs& args) override
        {
            draw_args = args;
        }
    };

    wz::asset::AssetKey key(uint64_t lo)
    {
        return wz::asset::AssetKey{
            .content_hash = { lo, 0x1234 },
            .schema_hash = { 0x700, 0 },
            .compiler_hash = { 1, 0 },
            .deps_hash = { 0, 0 },
        };
    }

    ea::CustomRenderProgramDesc make_pull_wireframe_program()
    {
        ea::CustomRenderProgramDesc desc;
        desc.name = "pull_wireframe";
        desc.vertex_shader = key(0xAA01);
        desc.pixel_shader = key(0xAA02);
        desc.binding_model = ea::RenderBindingModel::MeshVertexPull;
        desc.input_layout = ea::InputLayoutKind::None;
        desc.topology = ea::RenderPrimitiveTopology::TriangleList;
        desc.blend_mode = ea::BlendMode::Opaque;
        desc.depth_mode = ea::DepthMode::TestWrite;
        desc.raster_mode = ea::RasterMode::WireframeCullNone;
        desc.root_constants.push_back(ea::RootConstantBinding{
            ea::ShaderVisibility::All,
            /*shader_register*/ 0,
            /*register_space*/ 2,
            /*value_count*/ 16,
            "world" });
        desc.descriptor_bindings.push_back(ea::DescriptorBinding{
            ea::DescriptorKind::StructuredBufferSRV,
            ea::ShaderVisibility::Vertex,
            ea::DescriptorSemantic::PulledMeshPositions,
            /*shader_register*/ 0,
            /*register_space*/ 2,
            /*descriptor_count*/ 1 });
        desc.descriptor_bindings.push_back(ea::DescriptorBinding{
            ea::DescriptorKind::StructuredBufferSRV,
            ea::ShaderVisibility::Vertex,
            ea::DescriptorSemantic::PulledMeshIndices,
            /*shader_register*/ 1,
            /*register_space*/ 2,
            /*descriptor_count*/ 1 });
        return desc;
    }

    bool has_barrier(const std::vector<RecordedBarrier>& barriers,
                     wz::rhi::GpuResourceHandle resource,
                     wz::rhi::ResourceState from,
                     wz::rhi::ResourceState to)
    {
        for (const RecordedBarrier& barrier : barriers) {
            if (barrier.resource == resource
                && barrier.from == from
                && barrier.to == to)
            {
                return true;
            }
        }
        return false;
    }
}

TEST(RhiDummyBackendIntegration, DrivesResourceProgramSrgPacketAndFrameGraph)
{
    wz::engine::rendering::test::FakeGpuBackend backend;
    wz::rhi::GpuResourceRegistry resources(backend);
    // passes + resource_variants stay renderer-local in RhiContext; the
    // program/semantic registries moved to EngineGpuContext (#192) — here they
    // stand in as locals since this fake-backend test has no real device.
    wz::engine::rendering::RhiContext ctx;
    wz::rhi::RenderProgramRegistry      programs;
    wz::rhi::DescriptorSemanticRegistry descriptor_semantics;
    wz::rhi::ConstantSemanticRegistry   constant_semantics;

    const ea::CustomRenderProgramDesc authored_program =
        make_pull_wireframe_program();
    const auto converted =
        wz::engine::rendering::to_rhi_render_program_desc(
            authored_program,
            descriptor_semantics,
            constant_semantics);
    ASSERT_TRUE(converted.has_value());

    const wz::rhi::Tag prog_tag =
        programs.register_program(*converted);
    ASSERT_TRUE(prog_tag.valid());

    const wz::rhi::RenderProgramDesc* registered =
        programs.get(prog_tag);
    ASSERT_NE(registered, nullptr);
    const wz::rhi::ShaderResourceGroupLayout* layout =
        wz::rhi::find_shader_resource_group_layout(
            registered->shader_resource_groups,
            2);
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->descriptors.size(), 2u);
    EXPECT_EQ(layout->constants.dword_count(), 16u);

    const std::array<float, 9> positions = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
    };
    const std::array<uint32_t, 3> indices = { 0u, 1u, 2u };

    const wz::rhi::Tag position_variant =
        ctx.resource_variants.acquire("mesh.pull_positions");
    const wz::rhi::Tag index_variant =
        ctx.resource_variants.acquire("mesh.pull_indices");

    const wz::rhi::GpuResourceHandle pos_h =
        wz::engine::rendering::acquire_pull_buffer(
            resources,
            0xCAFE,
            position_variant,
            positions.data(),
            sizeof(positions),
            3 * sizeof(float));
    const wz::rhi::GpuResourceHandle idx_h =
        wz::engine::rendering::acquire_pull_buffer(
            resources,
            0xCAFE,
            index_variant,
            indices.data(),
            sizeof(indices),
            sizeof(uint32_t));

    ASSERT_TRUE(pos_h.valid());
    ASSERT_TRUE(idx_h.valid());
    EXPECT_EQ(backend.live_count(), 2u);
    ASSERT_EQ(backend.writes().size(), 2u);
    EXPECT_EQ(backend.writes()[0].bytes.size(), sizeof(positions));
    EXPECT_EQ(backend.writes()[1].bytes.size(), sizeof(indices));

    const wz::rhi::GpuResourceHandle pos_h_again =
        wz::engine::rendering::acquire_pull_buffer(
            resources,
            0xCAFE,
            position_variant,
            positions.data(),
            sizeof(positions),
            3 * sizeof(float));
    EXPECT_TRUE(pos_h_again == pos_h);
    EXPECT_EQ(backend.live_count(), 2u);

    wz::rhi::ShaderResourceGroup srg(*layout);
    EXPECT_FALSE(srg.satisfies(*layout));

    const wz::rhi::Tag pulled_positions =
        descriptor_semantics.find("pulled_mesh_positions");
    const wz::rhi::Tag pulled_indices =
        descriptor_semantics.find("pulled_mesh_indices");
    const wz::rhi::Tag world =
        constant_semantics.find("world");
    ASSERT_TRUE(pulled_positions.valid());
    ASSERT_TRUE(pulled_indices.valid());
    ASSERT_TRUE(world.valid());

    ASSERT_TRUE(srg.set(pulled_positions, pos_h).has_value());
    EXPECT_FALSE(srg.satisfies(*layout));

    const std::array<uint32_t, 16> world_matrix = {
        1u, 0u, 0u, 0u,
        0u, 1u, 0u, 0u,
        0u, 0u, 1u, 0u,
        0u, 0u, 0u, 1u,
    };
    ASSERT_TRUE(srg.set_constant(
        world,
        std::span<const uint8_t>{
            reinterpret_cast<const uint8_t*>(world_matrix.data()),
            sizeof(world_matrix) }).has_value());
    EXPECT_FALSE(srg.satisfies(*layout));

    ASSERT_TRUE(srg.set(pulled_indices, idx_h).has_value());
    EXPECT_TRUE(srg.satisfies(*layout));

    const wz::rhi::DrawListTag forward =
        ctx.passes.acquire("forward");
    ASSERT_TRUE(forward.valid());

    wz::rhi::DrawPacketAllocator allocator;
    wz::rhi::DrawPacketBuilder builder =
        wz::rhi::DrawPacketBuilder::begin(allocator);
    wz::rhi::GeometryView geometry;
    geometry.index_buffer = idx_h;
    geometry.index_count = static_cast<uint32_t>(indices.size());
    geometry.vertex_count = static_cast<uint32_t>(positions.size() / 3u);
    builder
        .set_geometry(geometry)
        .set_root_constants(std::span<const uint8_t>{
            reinterpret_cast<const uint8_t*>(world_matrix.data()),
            sizeof(world_matrix) })
        .add_shader_resource_group(srg);
    ASSERT_TRUE(builder.add_draw_item(wz::rhi::DrawRequest{
        forward,
        prog_tag,
        nullptr,
        wz::rhi::StreamBufferIndices{},
        0,
        wz::rhi::DrawListMask::from(forward) }));
    const wz::rhi::DrawPacket packet = builder.end();

    EXPECT_TRUE(packet.draw_list_mask.contains(forward));
    const wz::rhi::DrawItem* item = packet.get_draw_item(forward);
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->program == prog_tag);

    wz::rhi::FrameGraph frame_graph;
    const wz::rhi::FrameGraphResource pos_r =
        frame_graph.import("positions", pos_h, wz::rhi::ResourceState::Undefined);
    const wz::rhi::FrameGraphResource idx_r =
        frame_graph.import("indices", idx_h, wz::rhi::ResourceState::Undefined);
    const uint32_t forward_pass = frame_graph.add_pass("forward");
    frame_graph.read(forward_pass, pos_r, wz::rhi::ResourceState::ShaderRead);
    frame_graph.read(forward_pass, idx_r, wz::rhi::ResourceState::ShaderRead);
    frame_graph.mark_output(pos_r);
    frame_graph.set_execute(forward_pass, [&](const wz::rhi::PassContext& pass) {
        wz::rhi::record_packet(packet, forward, pass.commands());
    });

    const wz::rhi::CompiledFrameGraph compiled = frame_graph.compile();
    ASSERT_TRUE(compiled.acyclic);
    ASSERT_EQ(compiled.pass_count(), 1u);
    EXPECT_EQ(compiled.order[0].pass_index, forward_pass);
    ASSERT_EQ(compiled.order[0].barriers.size(), 2u);
    EXPECT_EQ(compiled.order[0].barriers[0].from,
        wz::rhi::ResourceState::Undefined);
    EXPECT_EQ(compiled.order[0].barriers[0].to,
        wz::rhi::ResourceState::ShaderRead);
    EXPECT_EQ(compiled.order[0].barriers[1].from,
        wz::rhi::ResourceState::Undefined);
    EXPECT_EQ(compiled.order[0].barriers[1].to,
        wz::rhi::ResourceState::ShaderRead);

    RecordingCommandRecorder recorder;
    // Fake-backend/offline loop: no engine device, so pass an explicit monotonic
    // frame value (the execute() timeline parameter is now required).
    frame_graph.execute(compiled, resources, recorder, /*frame_timeline*/ 1);
    ASSERT_EQ(recorder.barriers.size(), 2u);
    EXPECT_TRUE(has_barrier(
        recorder.barriers,
        pos_h,
        wz::rhi::ResourceState::Undefined,
        wz::rhi::ResourceState::ShaderRead));
    EXPECT_TRUE(has_barrier(
        recorder.barriers,
        idx_h,
        wz::rhi::ResourceState::Undefined,
        wz::rhi::ResourceState::ShaderRead));

    const std::vector<uint32_t> expected_slots = { 2 };
    EXPECT_TRUE(recorder.pipeline == prog_tag);
    EXPECT_EQ(recorder.bound_slots, expected_slots);
    ASSERT_TRUE(recorder.draw_args.has_value());
    EXPECT_TRUE(recorder.draw_args->indexed);
    EXPECT_EQ(recorder.draw_args->index_count, indices.size());
}

TEST(RhiSharedRegistry, SrgDoesNotSatisfyLayoutFromDifferentRegistry)
{
    using namespace wz::rhi;

    DescriptorSemanticRegistry reg_a;
    DescriptorSemanticRegistry reg_b;

    const Tag a_x = reg_a.acquire("mesh.pull_positions");
    const Tag decoy = reg_b.acquire("decoy");
    const Tag b_x = reg_b.acquire("mesh.pull_positions");
    ASSERT_TRUE(a_x.valid());
    ASSERT_TRUE(decoy.valid());
    ASSERT_TRUE(b_x.valid());
    EXPECT_FALSE(a_x == b_x);

    auto make_layout = [](Tag semantic) {
        ShaderResourceGroupLayout layout;
        layout.binding_slot = 2;
        layout.descriptors.push_back(DescriptorBinding{
            DescriptorKind::StructuredBufferSRV,
            ShaderStage::Vertex,
            semantic,
            /*shader_register*/ 0,
            /*register_space*/ 2,
            /*descriptor_count*/ 1 });
        return layout;
    };

    const ShaderResourceGroupLayout layout_a = make_layout(a_x);
    const ShaderResourceGroupLayout layout_b = make_layout(b_x);

    ShaderResourceGroup srg(layout_a);
    ASSERT_TRUE(srg.set(a_x, GpuResourceHandle{ 100, 1 }).has_value());

    EXPECT_TRUE(srg.satisfies(layout_a));
    EXPECT_FALSE(srg.satisfies(layout_b));
}
