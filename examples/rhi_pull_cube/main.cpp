#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <engine/assets/render_program/render_program.h>
#include <engine/rendering/rhi_context.h>
#include <engine/rendering/rhi_dx12_command_recorder.h>
#include <engine/rendering/rhi_dx12_pipeline.h>
#include <engine/rendering/rhi_gpu_backend.h>
#include <engine/rendering/rhi_mesh_bridge.h>
#include <engine/rendering/rhi_render_program_bridge.h>
#include <engine/rendering/rhi_shader_bridge.h>
#include <gpu/gpu.h>
#include <window/window2.h>

#include <wozzits/rhi/draw_encode.h>
#include <wozzits/rhi/draw_packet.h>
#include <wozzits/rhi/frame_graph.h>
#include <wozzits/rhi/shader_resource_group.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <vector>

namespace ea = wz::engine::assets;

namespace
{
    struct Mat4
    {
        std::array<float, 16> m{};
    };

    Mat4 identity()
    {
        Mat4 out;
        out.m[0] = out.m[5] = out.m[10] = out.m[15] = 1.0f;
        return out;
    }

    Mat4 multiply(const Mat4& a, const Mat4& b)
    {
        Mat4 out;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float v = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    v += a.m[k * 4 + row] * b.m[col * 4 + k];
                }
                out.m[col * 4 + row] = v;
            }
        }
        return out;
    }

    Mat4 rotation_y(float radians)
    {
        Mat4 out = identity();
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        out.m[0] = c;
        out.m[2] = -s;
        out.m[8] = s;
        out.m[10] = c;
        return out;
    }

    Mat4 translation(float x, float y, float z)
    {
        Mat4 out = identity();
        out.m[12] = x;
        out.m[13] = y;
        out.m[14] = z;
        return out;
    }

    Mat4 perspective_lh(float fovy_radians, float aspect, float zn, float zf)
    {
        Mat4 out{};
        const float y_scale = 1.0f / std::tan(fovy_radians * 0.5f);
        const float x_scale = y_scale / aspect;
        out.m[0] = x_scale;
        out.m[5] = y_scale;
        out.m[10] = zf / (zf - zn);
        out.m[11] = 1.0f;
        out.m[14] = (-zn * zf) / (zf - zn);
        return out;
    }

    std::vector<uint8_t> read_binary(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        if (size <= 0) {
            return {};
        }
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(bytes.data()), size);
        return file ? bytes : std::vector<uint8_t>{};
    }

    std::filesystem::path executable_dir(const char* argv0)
    {
        std::filesystem::path path = argv0 ? argv0 : ".";
        if (path.has_parent_path()) {
            return std::filesystem::absolute(path).parent_path();
        }
        return std::filesystem::current_path();
    }

    wz::asset::AssetKey key(uint64_t lo)
    {
        return wz::asset::AssetKey{
            .content_hash = { lo, 0xC0BE },
            .schema_hash = { 0x700, 0 },
            .compiler_hash = { 1, 0 },
            .deps_hash = { 0, 0 },
        };
    }

    ea::CustomRenderProgramDesc make_pull_cube_program()
    {
        ea::CustomRenderProgramDesc desc;
        desc.name = "pull_cube";
        desc.vertex_shader = key(0x1001);
        desc.pixel_shader = key(0x1002);
        desc.binding_model = ea::RenderBindingModel::MeshVertexPull;
        desc.input_layout = ea::InputLayoutKind::None;
        desc.topology = ea::RenderPrimitiveTopology::TriangleList;
        desc.blend_mode = ea::BlendMode::Opaque;
        desc.depth_mode = ea::DepthMode::TestWrite;
        desc.raster_mode = ea::RasterMode::SolidCullNone;
        desc.descriptor_bindings.push_back(ea::DescriptorBinding{
            ea::DescriptorKind::StructuredBufferSRV,
            ea::ShaderVisibility::Vertex,
            ea::DescriptorSemantic::PulledMeshPositions,
            0,
            2,
            1 });
        desc.descriptor_bindings.push_back(ea::DescriptorBinding{
            ea::DescriptorKind::StructuredBufferSRV,
            ea::ShaderVisibility::Vertex,
            ea::DescriptorSemantic::PulledMeshIndices,
            1,
            2,
            1 });
        desc.root_constants.push_back(ea::RootConstantBinding{
            ea::ShaderVisibility::Vertex,
            0,
            2,
            16,
            "mvp" });
        return desc;
    }

    template <typename T>
    std::span<const uint8_t> bytes_of(const T& value)
    {
        return std::span<const uint8_t>{
            reinterpret_cast<const uint8_t*>(&value),
            sizeof(T) };
    }
}

int main(int argc, char** argv)
{
    const std::filesystem::path exe_dir =
        executable_dir(argc > 0 ? argv[0] : nullptr);
    const std::vector<uint8_t> vs =
        read_binary(exe_dir / "pull_cube_vs.cso");
    const std::vector<uint8_t> ps =
        read_binary(exe_dir / "pull_cube_ps.cso");
    if (vs.empty() || ps.empty()) {
        std::cerr
            << "Missing pull_cube_vs.cso / pull_cube_ps.cso next to "
            << exe_dir.string()
            << ". Build the rhi_pull_cube target with fxc available.\n";
        return 1;
    }

    wz::window::WindowHandle window = wz::window::create_window({
        "RHI Pull Cube",
        1280,
        720,
        true,
        false });
    if (!window.valid()) {
        std::cerr << "Failed to create window.\n";
        return 1;
    }

    wz::gpu::Device device = wz::gpu::create_device(window);
    if (!device.valid()) {
        wz::window::destroy_window(window);
        std::cerr << "Failed to create GPU device.\n";
        return 1;
    }

    bool ok = true;
    {
        wz::engine::rendering::EngineGpuBackend backend(device);
        wz::engine::rendering::RhiContext ctx(backend);

        const ea::CustomRenderProgramDesc authored_program =
            make_pull_cube_program();
        if (!wz::engine::rendering::register_program_shaders(
                ctx,
                authored_program,
                vs,
                ps))
        {
            std::cerr << "Failed to register pull cube shaders.\n";
            ok = false;
        }

        const auto converted =
            wz::engine::rendering::to_rhi_render_program_desc(
                authored_program,
                ctx.descriptor_semantics,
                ctx.constant_semantics);
        if (!converted) {
            std::cerr << "Failed to convert pull cube render program.\n";
            ok = false;
        }

        wz::rhi::Tag program{};
        const wz::rhi::ShaderResourceGroupLayout* slot2_layout = nullptr;
        if (ok) {
            program = ctx.programs.register_program(*converted);
            const wz::rhi::RenderProgramDesc* registered =
                ctx.programs.get(program);
            slot2_layout = registered
                ? wz::rhi::find_shader_resource_group_layout(
                    registered->shader_resource_groups,
                    2)
                : nullptr;
            if (!program.valid() || !slot2_layout) {
                std::cerr << "Failed to register pull cube program.\n";
                ok = false;
            }
        }

        constexpr std::array<float, 24> positions = {
            -0.6f, -0.6f, -0.6f,
             0.6f, -0.6f, -0.6f,
             0.6f,  0.6f, -0.6f,
            -0.6f,  0.6f, -0.6f,
            -0.6f, -0.6f,  0.6f,
             0.6f, -0.6f,  0.6f,
             0.6f,  0.6f,  0.6f,
            -0.6f,  0.6f,  0.6f,
        };
        constexpr std::array<uint32_t, 36> indices = {
            0, 2, 1, 0, 3, 2,
            4, 5, 6, 4, 6, 7,
            0, 1, 5, 0, 5, 4,
            3, 6, 2, 3, 7, 6,
            1, 2, 6, 1, 6, 5,
            0, 4, 7, 0, 7, 3,
        };

        const wz::rhi::Tag position_variant =
            ctx.resource_variants.acquire("mesh.pull_positions");
        const wz::rhi::Tag index_variant =
            ctx.resource_variants.acquire("mesh.pull_indices");
        const wz::rhi::GpuResourceHandle positions_h =
            wz::engine::rendering::acquire_pull_buffer(
                ctx,
                0xC0BE,
                position_variant,
                positions.data(),
                sizeof(positions),
                3u * sizeof(float));
        const wz::rhi::GpuResourceHandle indices_h =
            wz::engine::rendering::acquire_pull_buffer(
                ctx,
                0xC0BE,
                index_variant,
                indices.data(),
                sizeof(indices),
                sizeof(uint32_t));
        if (!positions_h.valid() || !indices_h.valid()) {
            std::cerr << "Failed to upload pull cube buffers.\n";
            ok = false;
        }

        wz::rhi::ShaderResourceGroup object_srg;
        if (ok) {
            object_srg.reset(*slot2_layout);
            const wz::rhi::Tag pulled_positions =
                ctx.descriptor_semantics.find("pulled_mesh_positions");
            const wz::rhi::Tag pulled_indices =
                ctx.descriptor_semantics.find("pulled_mesh_indices");
            if (!object_srg.set(pulled_positions, positions_h)
                || !object_srg.set(pulled_indices, indices_h)
                || !object_srg.satisfies(*slot2_layout))
            {
                std::cerr << "Failed to build pull cube SRG.\n";
                ok = false;
            }
        }

        Mat4 initial_mvp = identity();
        wz::rhi::DrawPacket packet;
        wz::rhi::DrawListTag forward{};
        if (ok) {
            forward = ctx.passes.acquire("forward");
            wz::rhi::GeometryView geometry;
            geometry.index_buffer = indices_h;
            geometry.index_count = static_cast<uint32_t>(indices.size());
            geometry.vertex_count = static_cast<uint32_t>(positions.size() / 3u);

            wz::rhi::DrawPacketAllocator allocator;
            wz::rhi::DrawPacketBuilder builder =
                wz::rhi::DrawPacketBuilder::begin(allocator);
            builder
                .set_geometry(geometry)
                .set_root_constants(bytes_of(initial_mvp.m))
                .add_shader_resource_group(object_srg);
            if (!builder.add_draw_item(wz::rhi::DrawRequest{
                    forward,
                    program,
                    nullptr,
                    wz::rhi::StreamBufferIndices{},
                    0,
                    wz::rhi::DrawListMask::from(forward) }))
            {
                std::cerr << "Failed to build pull cube draw packet.\n";
                ok = false;
            }
            packet = builder.end();
        }

        wz::rhi::FrameGraph frame_graph;
        wz::rhi::CompiledFrameGraph compiled;
        if (ok) {
            const wz::rhi::FrameGraphResource positions_r =
                frame_graph.import(
                    "positions",
                    positions_h,
                    wz::rhi::ResourceState::ShaderRead);
            const wz::rhi::FrameGraphResource indices_r =
                frame_graph.import(
                    "indices",
                    indices_h,
                    wz::rhi::ResourceState::ShaderRead);
            const uint32_t forward_pass = frame_graph.add_pass("forward");
            frame_graph.read(
                forward_pass,
                positions_r,
                wz::rhi::ResourceState::ShaderRead);
            frame_graph.read(
                forward_pass,
                indices_r,
                wz::rhi::ResourceState::ShaderRead);
            frame_graph.mark_output(positions_r);
            frame_graph.set_execute(
                forward_pass,
                [&](const wz::rhi::PassContext& pass) {
                    wz::rhi::record_packet(packet, forward, pass.commands());
                });
            compiled = frame_graph.compile();
            if (!compiled.acyclic || compiled.pass_count() != 1u) {
                std::cerr << "Failed to compile pull cube frame graph.\n";
                ok = false;
            }
        }

        wz::engine::rendering::RhiDx12PipelineCache pipeline_cache(
            device,
            ctx.programs,
            ctx.compute_programs,
            ctx.shaders);
        wz::engine::rendering::RhiDx12CommandRecorder recorder(
            device,
            pipeline_cache,
            ctx.resources,
            backend);
        if (ok && !pipeline_cache.realize(program)) {
            std::cerr << "Failed to realize pull cube pipeline.\n";
            ok = false;
        }

        const auto start = std::chrono::steady_clock::now();
        bool running = true;
        while (ok && running && !wz::window::window_should_close(window)) {
            wz::window::pump_messages();
            PlatformEvent event{};
            while (wz::window::poll_event(window, event)) {
                if (event.type == PlatformEvent::Type::Close) {
                    running = false;
                    break;
                }
                if (event.type == PlatformEvent::Type::Resize) {
                    wz::gpu::resize(
                        device,
                        event.resize.width,
                        event.resize.height);
                }
            }
            if (!ok) {
                break;
            }

            const auto now = std::chrono::steady_clock::now();
            const float seconds =
                std::chrono::duration<float>(now - start).count();
            const float aspect = 1280.0f / 720.0f;
            const Mat4 model = multiply(
                translation(0.0f, 0.0f, 3.0f),
                rotation_y(seconds));
            const Mat4 projection =
                perspective_lh(60.0f * 3.1415926535f / 180.0f,
                               aspect,
                               0.1f,
                               100.0f);
            const Mat4 mvp = multiply(projection, model);
            packet.root_constants.assign(
                reinterpret_cast<const uint8_t*>(mvp.m.data()),
                reinterpret_cast<const uint8_t*>(mvp.m.data())
                    + sizeof(mvp.m));

            if (!wz::gpu::begin_frame(device)) {
                break;
            }
            wz::gpu::clear(device, 0.10f, 0.10f, 0.12f, 1.0f);
            frame_graph.execute(compiled, ctx.resources, recorder);
            if (!recorder.ready()) {
                std::cerr << "DX12 RHI recorder rejected the draw.\n";
                break;
            }
            if (!wz::gpu::end_frame(device)) {
                break;
            }
            if (!wz::gpu::present(device)) {
                break;
            }
        }
    }

    if (device.valid()) {
        wz::gpu::wait_idle(device);
        wz::gpu::destroy_device(device);
    }
    wz::window::destroy_window(window);
    return ok ? 0 : 1;
}
