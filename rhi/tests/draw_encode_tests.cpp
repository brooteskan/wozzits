#include "wz_test.h"

#include <wozzits/rhi/draw_encode.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace wz::rhi;

namespace
{
    struct Recorder : CommandRecorder
    {
        std::vector<std::string> log;
        Tag pipeline{};
        std::vector<uint32_t> bound_slots;
        bool root_set = false;
        std::optional<DrawArgs> args;
        StreamBufferIndices streams;

        void barrier(GpuResourceHandle,
                     ResourceState,
                     ResourceState) override
        {
        }

        void set_pipeline(Tag program) override
        {
            pipeline = program;
            log.push_back("pipeline");
        }

        void set_root_constants(std::span<const uint8_t>) override
        {
            root_set = true;
            log.push_back("root");
        }

        void bind_resource_group(uint32_t slot,
                                 const ShaderResourceGroup&) override
        {
            bound_slots.push_back(slot);
            log.push_back("bind");
        }

        void set_geometry(const GeometryView&,
                          const StreamBufferIndices& stream_indices) override
        {
            streams = stream_indices;
            log.push_back("geometry");
        }

        void draw(const DrawArgs& draw_args) override
        {
            args = draw_args;
            log.push_back("draw");
        }
    };

    ShaderResourceGroup make_srg(uint32_t slot)
    {
        ShaderResourceGroupLayout layout;
        layout.binding_slot = slot;
        return ShaderResourceGroup(layout);
    }

    GeometryView make_indexed_geometry()
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
        geometry.first_index = 3;
        geometry.vertex_offset = -1;
        return geometry;
    }

    DrawPacket make_single_pass_packet(DrawListTag pass,
                                       Tag program,
                                       const ShaderResourceGroup* shared,
                                       const ShaderResourceGroup* unique,
                                       std::span<const uint8_t> root_constants,
                                       GeometryView geometry)
    {
        StreamBufferIndices streams;
        WZ_CHECK(streams.add(0));

        DrawPacketAllocator allocator;
        DrawPacketBuilder builder = DrawPacketBuilder::begin(allocator);
        builder
            .set_geometry(std::move(geometry))
            .set_root_constants(root_constants);
        if (shared) {
            builder.add_shader_resource_group(*shared);
        }
        WZ_CHECK(builder.add_draw_item(DrawRequest{
            pass,
            program,
            unique,
            streams,
            /*sort_key*/ 0,
            DrawListMask::from(pass) }));
        return builder.end();
    }
}

static void scene_packet_records_verbs_in_order()
{
    DrawListTagRegistry passes;
    const DrawListTag forward = passes.acquire("forward");

    TagRegistry<16> programs;
    const Tag program = programs.acquire("mesh_forward");

    ShaderResourceGroup view_srg = make_srg(0);
    ShaderResourceGroup material_srg = make_srg(1);
    ShaderResourceGroup object_srg = make_srg(2);

    const std::array<uint8_t, 4> root_constants = { 1, 2, 3, 4 };

    StreamBufferIndices streams;
    WZ_CHECK(streams.add(0));

    DrawPacketAllocator allocator;
    DrawPacketBuilder builder = DrawPacketBuilder::begin(allocator);
    builder
        .set_geometry(make_indexed_geometry())
        .set_root_constants(root_constants)
        .add_shader_resource_group(view_srg)
        .add_shader_resource_group(material_srg);
    WZ_CHECK(builder.add_draw_item(DrawRequest{
        forward,
        program,
        &object_srg,
        streams,
        /*sort_key*/ 0,
        DrawListMask::from(forward) }));
    const DrawPacket packet = builder.end();

    Recorder recorder;
    record_packet(packet, forward, recorder);

    const std::vector<std::string> expected_log = {
        "pipeline", "root", "bind", "bind", "bind", "geometry", "draw"
    };
    const std::vector<uint32_t> expected_slots = { 0, 1, 2 };
    WZ_CHECK(recorder.log == expected_log);
    WZ_CHECK(recorder.bound_slots == expected_slots);
    WZ_CHECK(recorder.pipeline == program);
    WZ_CHECK(recorder.args.has_value());
    if (recorder.args) {
        WZ_CHECK(recorder.args->indexed);
        WZ_CHECK_EQ(recorder.args->index_count, packet.geometry.index_count);
    }
}

static void pass_not_in_packet_records_nothing()
{
    DrawListTagRegistry passes;
    const DrawListTag forward = passes.acquire("forward");
    const DrawListTag shadow = passes.acquire("shadow");

    TagRegistry<16> programs;
    const Tag program = programs.acquire("mesh_forward");

    const std::array<uint8_t, 4> root_constants = { 1, 2, 3, 4 };
    const DrawPacket packet = make_single_pass_packet(
        forward,
        program,
        nullptr,
        nullptr,
        root_constants,
        make_indexed_geometry());

    Recorder recorder;
    record_packet(packet, shadow, recorder);

    WZ_CHECK(recorder.log.empty());
}

static void multi_pass_selects_per_pass_item()
{
    DrawListTagRegistry passes;
    const DrawListTag depth = passes.acquire("depth");
    const DrawListTag forward = passes.acquire("forward");

    TagRegistry<16> programs;
    const Tag depth_program = programs.acquire("mesh_depth");
    const Tag forward_program = programs.acquire("mesh_forward");

    StreamBufferIndices depth_streams;
    WZ_CHECK(depth_streams.add(0));

    StreamBufferIndices forward_streams;
    WZ_CHECK(forward_streams.add(1));

    DrawPacketAllocator allocator;
    DrawPacketBuilder builder = DrawPacketBuilder::begin(allocator);
    builder.set_geometry(make_indexed_geometry());
    WZ_CHECK(builder.add_draw_item(DrawRequest{
        depth,
        depth_program,
        nullptr,
        depth_streams,
        /*sort_key*/ 0,
        DrawListMask::from(depth) }));
    WZ_CHECK(builder.add_draw_item(DrawRequest{
        forward,
        forward_program,
        nullptr,
        forward_streams,
        /*sort_key*/ 0,
        DrawListMask::from(forward) }));
    const DrawPacket packet = builder.end();

    Recorder depth_recorder;
    Recorder forward_recorder;
    record_packet(packet, depth, depth_recorder);
    record_packet(packet, forward, forward_recorder);

    WZ_CHECK(depth_recorder.pipeline == depth_program);
    WZ_CHECK(forward_recorder.pipeline == forward_program);
    WZ_CHECK_EQ(depth_recorder.streams.indices.size(), static_cast<size_t>(1));
    WZ_CHECK_EQ(forward_recorder.streams.indices.size(), static_cast<size_t>(1));
    if (!depth_recorder.streams.indices.empty()
        && !forward_recorder.streams.indices.empty())
    {
        WZ_CHECK_EQ(depth_recorder.streams.indices[0], 0u);
        WZ_CHECK_EQ(forward_recorder.streams.indices[0], 1u);
    }
}

static void empty_root_constants_skips_set()
{
    DrawListTagRegistry passes;
    const DrawListTag forward = passes.acquire("forward");

    TagRegistry<16> programs;
    const Tag program = programs.acquire("mesh_forward");

    const DrawPacket packet = make_single_pass_packet(
        forward,
        program,
        nullptr,
        nullptr,
        std::span<const uint8_t>{},
        make_indexed_geometry());

    Recorder recorder;
    record_packet(packet, forward, recorder);

    WZ_CHECK_FALSE(recorder.root_set);
    WZ_CHECK(std::find(recorder.log.begin(), recorder.log.end(), "root")
        == recorder.log.end());
}

static void non_indexed_geometry_draw_args()
{
    DrawListTagRegistry passes;
    const DrawListTag forward = passes.acquire("forward");

    TagRegistry<16> programs;
    const Tag program = programs.acquire("mesh_forward");

    GeometryView geometry;
    geometry.streams.push_back(StreamBufferView{
        GpuResourceHandle{ 10, 1 },
        /*offset*/ 0,
        /*stride*/ 12 });
    geometry.vertex_count = 42;

    const DrawPacket packet = make_single_pass_packet(
        forward,
        program,
        nullptr,
        nullptr,
        std::span<const uint8_t>{},
        geometry);

    Recorder recorder;
    record_packet(packet, forward, recorder);

    WZ_CHECK(recorder.args.has_value());
    if (recorder.args) {
        WZ_CHECK_FALSE(recorder.args->indexed);
        WZ_CHECK_EQ(recorder.args->vertex_count, 42u);
    }
}

int main()
{
    WZ_RUN(scene_packet_records_verbs_in_order);
    WZ_RUN(pass_not_in_packet_records_nothing);
    WZ_RUN(multi_pass_selects_per_pass_item);
    WZ_RUN(empty_root_constants_skips_set);
    WZ_RUN(non_indexed_geometry_draw_args);
    WZ_TEST_RETURN();
}
