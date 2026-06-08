#include <gtest/gtest.h>

#include <engine/assets/compute_pipeline_asset_module.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/assets/key_factories/compute_pipeline.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>
#include <window/window2.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
    namespace fs = std::filesystem;

    wz::asset::AssetKey dummy_shader_key(uint64_t seed = 1)
    {
        return wz::asset::AssetKey{
            .content_hash = { seed, 0 },
            .schema_hash = { 2, 0 },
            .compiler_hash = { 3, 0 },
            .deps_hash = { 4, 0 },
        };
    }

    struct TempComputeShaderDir
    {
        fs::path root;

        TempComputeShaderDir()
        {
            root = fs::temp_directory_path() /
                ("wozzits_compute_pipeline_tests_" +
                 std::to_string(::GetCurrentProcessId()));

            fs::remove_all(root);
            fs::create_directories(root / "shaders" / "compute");
        }

        ~TempComputeShaderDir()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        wz::fs::Path wz_root() const { return root.string(); }

        void write_compute_shader() const
        {
            std::ofstream out(
                root / "shaders" / "compute" / "fill_uint_cs.hlsl",
                std::ios::binary);
            out
                << "RWStructuredBuffer<uint> Output : register(u0);\n"
                << "[numthreads(8, 1, 1)]\n"
                << "void main(uint3 id : SV_DispatchThreadID) {\n"
                << "    Output[id.x] = id.x;\n"
                << "}\n";
        }
    };

    struct ComputePipelineGpuFixture : public ::testing::Test
    {
        wz::Logger logger;
        wz::window::WindowHandle window{};
        wz::gpu::Device device{};
        TempComputeShaderDir resources;

        void SetUp() override
        {
            resources.write_compute_shader();

            wz::window::WindowDesc desc{};
            desc.title = "compute_pipeline_test";
            desc.width = 64;
            desc.height = 64;
            desc.resizable = false;

            window = wz::window::create_window(desc);
            ASSERT_TRUE(window.native);

            device = wz::gpu::create_device(window);
            ASSERT_TRUE(device.impl);
        }

        void TearDown() override
        {
            if (device.impl)
                wz::gpu::destroy_device(device);
            if (window.native)
                wz::window::destroy_window(window);
        }
    };
}

TEST(ComputePipelineTable, DefaultHandleIsInvalid)
{
    wz::engine::assets::ComputePipelineTable table;

    EXPECT_EQ(table.get({}), nullptr);
}

TEST(ComputePipelineTable, RejectsDuplicateBindingSlots)
{
    using namespace wz::engine::assets;

    ComputePipelineTable table;
    ComputePipelineData data{};
    data.name = "compute/duplicate";
    data.compute_shader = wz::asset::ResourceHandle{
        .id = 1,
        .epoch = 1,
        .type = wz::asset::AssetType::Shader,
    };
    data.bindings = {
        {
            .kind = ComputeBindingKind::StructuredBufferSRV,
            .semantic = ComputeBindingSemantic::MeshVertices,
            .shader_register = 0,
            .register_space = 0,
            .descriptor_count = 1,
            .stride_bytes = 32,
        },
        {
            .kind = ComputeBindingKind::ByteAddressBufferSRV,
            .semantic = ComputeBindingSemantic::MeshIndices,
            .shader_register = 0,
            .register_space = 0,
            .descriptor_count = 1,
            .stride_bytes = 0,
        },
    };

    EXPECT_FALSE(table.add(std::move(data)).valid());
}

TEST(ComputePipelineKeyFactory, BindingLayoutChangesIdentity)
{
    using namespace wz::engine::assets;

    ComputePipelineDesc a{};
    a.name = "compute/key";
    a.compute_shader = dummy_shader_key();
    a.bindings = {{
        .kind = ComputeBindingKind::StructuredBufferSRV,
        .semantic = ComputeBindingSemantic::MeshVertices,
        .shader_register = 0,
        .register_space = 0,
        .descriptor_count = 1,
        .stride_bytes = 32,
    }};

    ComputePipelineDesc b = a;
    b.bindings[0].semantic = ComputeBindingSemantic::MeshIndices;

    EXPECT_EQ(make_compute_pipeline_key(a), make_compute_pipeline_key(a));
    EXPECT_NE(make_compute_pipeline_key(a), make_compute_pipeline_key(b));
}

TEST(ComputePipelineAssetModule, RejectsEmptyName)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(), "wozzits_compute_pipeline_reject_tests");
    wz::fs::create_directories(root);

    wz::engine::assets::EngineAssetLibrary assets(device, logger, root);

    const auto pipeline = assets.compute_pipelines().create_compute_pipeline({
        .name = "",
        .compute_shader = dummy_shader_key(),
        .thread_group_size_x = 8,
        .thread_group_size_y = 1,
        .thread_group_size_z = 1,
    });

    EXPECT_FALSE(pipeline.valid());
}

TEST_F(ComputePipelineGpuFixture, ResolvesComputePipelineBoundary)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets(device, logger, resources.wz_root());

    const auto shader = assets.shaders().create_compute_shader({
        .name = "compute/fill_uint",
        .path = "shaders/compute/fill_uint_cs.hlsl",
        .entry = "main",
        .target = "cs_5_0",
    });
    ASSERT_TRUE(shader.valid());

    const auto pipeline = assets.compute_pipelines().create_compute_pipeline({
        .name = "compute/fill_uint_pipeline",
        .compute_shader = shader.shader,
        .bindings = {{
            .kind = ComputeBindingKind::StructuredBufferUAV,
            .semantic = ComputeBindingSemantic::Scratch,
            .shader_register = 0,
            .register_space = 0,
            .descriptor_count = 1,
            .stride_bytes = 4,
        }},
        .root_constant_dwords = 4,
        .thread_group_size_x = 8,
        .thread_group_size_y = 1,
        .thread_group_size_z = 1,
    });
    ASSERT_TRUE(pipeline.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    if (!report.ok()) {
        ADD_FAILURE() << "resolve_all failed with "
                      << report.failures.size() << " failure(s)";
        for (const auto& f : report.failures)
            ADD_FAILURE() << "  error=" << static_cast<int>(f.error);
    }
    ASSERT_TRUE(report.ok());

    const auto shader_handle = assets.shaders().get_compute_shader(shader);
    ASSERT_TRUE(shader_handle.valid());

    const auto handle = assets.compute_pipelines().get_compute_pipeline(
        pipeline);
    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.type, kAssetTypeComputePipeline);

    const auto* data = assets.compute_pipelines().get_compute_pipeline_data(
        handle);
    ASSERT_NE(data, nullptr);

    EXPECT_EQ(data->name, "compute/fill_uint_pipeline");
    EXPECT_EQ(data->compute_shader.id, shader_handle.shader.id);
    EXPECT_EQ(data->compute_shader.epoch, shader_handle.shader.epoch);
    EXPECT_EQ(data->compute_shader.type, shader_handle.shader.type);
    EXPECT_EQ(data->root_constant_dwords, 4u);
    EXPECT_EQ(data->thread_group_size_x, 8u);
    EXPECT_EQ(data->thread_group_size_y, 1u);
    EXPECT_EQ(data->thread_group_size_z, 1u);

    ASSERT_EQ(data->bindings.size(), 1u);
    EXPECT_EQ(data->bindings[0].kind, ComputeBindingKind::StructuredBufferUAV);
    EXPECT_EQ(data->bindings[0].semantic, ComputeBindingSemantic::Scratch);
    EXPECT_EQ(data->bindings[0].shader_register, 0u);
    EXPECT_EQ(data->bindings[0].register_space, 0u);
    EXPECT_EQ(data->bindings[0].descriptor_count, 1u);
    EXPECT_EQ(data->bindings[0].stride_bytes, 4u);
}
