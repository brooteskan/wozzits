#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/key_factories/mesh_derived_field.h>
#include <gpu/gpu.h>
#include <gpu/gpu_resource_types.h>
#include <window/window2.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
    wz::engine::assets::EngineAssetLibrary make_assets(
        wz::gpu::Device& device,
        wz::Logger& logger,
        const char* suffix)
    {
        const wz::fs::Path root =
            wz::fs::join(
                wz::fs::temp_directory_path(),
                std::string("wozzits_mesh_derived_field_tests_") + suffix);

        EXPECT_EQ(
            wz::fs::create_directories(root),
            wz::fs::FileError::None);

        return wz::engine::assets::EngineAssetLibrary(
            device,
            logger,
            root);
    }

    wz::engine::assets::EngineAssetLibrary make_assets_no_disk_cache(
        wz::gpu::Device& device,
        wz::Logger& logger,
        const char* suffix)
    {
        const wz::fs::Path root =
            wz::fs::join(
                wz::fs::temp_directory_path(),
                std::string("wozzits_mesh_derived_field_tests_") + suffix);

        EXPECT_EQ(
            wz::fs::create_directories(root),
            wz::fs::FileError::None);

        return wz::engine::assets::EngineAssetLibrary(
            device,
            logger,
            root,
            wz::engine::assets::EngineAssetCacheSettings{
                .root = root,
                .enabled = false,
            });
    }

    wz::engine::assets::EngineAssetLibrary make_assets_with_disk_cache(
        wz::gpu::Device& device,
        wz::Logger& logger,
        const char* suffix)
    {
        const wz::fs::Path root =
            wz::fs::join(
                wz::fs::temp_directory_path(),
                std::string("wozzits_mesh_derived_field_tests_") + suffix);

        EXPECT_EQ(
            wz::fs::create_directories(root),
            wz::fs::FileError::None);

        return wz::engine::assets::EngineAssetLibrary(
            device,
            logger,
            root,
            wz::engine::assets::EngineAssetCacheSettings{
                .root = root,
                .enabled = true,
            });
    }

    std::vector<std::byte> float_values(std::initializer_list<float> values)
    {
        std::vector<std::byte> bytes(values.size() * sizeof(float));
        std::memcpy(bytes.data(), values.begin(), bytes.size());
        return bytes;
    }

    std::vector<std::byte> uint_values(std::initializer_list<uint32_t> values)
    {
        std::vector<std::byte> bytes(values.size() * sizeof(uint32_t));
        std::memcpy(bytes.data(), values.begin(), bytes.size());
        return bytes;
    }

    float read_float(
        const std::vector<std::byte>& bytes,
        uint32_t byte_offset,
        uint32_t element)
    {
        float out = 0.0f;
        std::memcpy(
            &out,
            bytes.data() + byte_offset + element * sizeof(float),
            sizeof(float));
        return out;
    }

    const wz::engine::assets::MeshDerivedFieldChannel* find_channel(
        const wz::engine::assets::MeshDerivedFieldData& data,
        uint32_t channel_id)
    {
        for (const auto& channel : data.channels) {
            if (channel.channel_id == channel_id) {
                return &channel;
            }
        }
        return nullptr;
    }

    std::vector<float> read_float_channel(
        const wz::engine::assets::MeshDerivedFieldData& data,
        uint32_t channel_id)
    {
        const auto* channel = find_channel(data, channel_id);
        EXPECT_NE(channel, nullptr);
        if (!channel) {
            return {};
        }

        std::vector<float> out(data.element_count, 0.0f);
        for (uint32_t i = 0; i < data.element_count; ++i) {
            out[i] = read_float(data.values, channel->byte_offset, i);
        }
        return out;
    }

    const wz::engine::assets::MeshDerivedFieldData* resolve_wavelet_field(
        wz::engine::assets::EngineAssetLibrary& assets,
        const wz::engine::assets::MeshAsset& mesh,
        const wz::engine::assets::MeshWaveletAnalysisDesc& desc)
    {
        const auto field =
            assets.mesh_derived_fields().create_wavelet_analysis(desc);
        EXPECT_TRUE(field.valid());
        if (!field.valid()) {
            return nullptr;
        }

        EXPECT_TRUE(assets.commit());
        EXPECT_TRUE(assets.resolve_all().ok());

        const auto handle =
            assets.mesh_derived_fields().get_mesh_derived_field(field);
        EXPECT_TRUE(handle.valid());
        if (!handle.valid()) {
            return nullptr;
        }

        (void)mesh;
        return assets.mesh_derived_fields().get_mesh_derived_field_data(handle);
    }

    struct MeshWaveletGpuFixture : public ::testing::Test
    {
        wz::Logger logger;
        wz::window::WindowHandle window{};
        wz::gpu::Device device{};
        std::filesystem::path root;

        void SetUp() override
        {
            root = std::filesystem::temp_directory_path()
                / ("wozzits_mesh_wavelet_gpu_tests_"
                    + std::to_string(::GetCurrentProcessId()));
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
            std::filesystem::create_directories(
                root / "shaders" / "mesh_wavelet");
            const std::filesystem::path source_root =
                std::filesystem::current_path().parent_path().parent_path();
            std::filesystem::copy_file(
                source_root
                    / "window_engine"
                    / "resources"
                    / "shaders"
                    / "mesh_wavelet"
                    / "detail_heat_cs.hlsl",
                root / "shaders" / "mesh_wavelet" / "detail_heat_cs.hlsl",
                std::filesystem::copy_options::overwrite_existing,
                ec);
            ASSERT_FALSE(ec);

            wz::window::WindowDesc desc{};
            desc.title = "mesh_wavelet_gpu_test";
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
            if (device.impl) {
                wz::gpu::destroy_device(device);
            }
            if (window.native) {
                wz::window::destroy_window(window);
            }
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }

        wz::fs::Path resource_root() const { return root.string(); }
    };

    uint32_t f32_bits(float value)
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    struct MeshComputeFieldGpuFixture : public ::testing::Test
    {
        wz::Logger logger;
        wz::window::WindowHandle window{};
        wz::gpu::Device device{};
        std::filesystem::path root;

        void SetUp() override
        {
            root = std::filesystem::temp_directory_path()
                / ("wozzits_mesh_compute_field_gpu_tests_"
                    + std::to_string(::GetCurrentProcessId()));
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
            std::filesystem::create_directories(root / "shaders" / "compute");

            // Writes channel 0 = position.y * Scale and channel 1 =
            // position.x into the packed output buffer; the first three
            // root constants are engine-filled.
            std::ofstream scaled_height(
                root / "shaders" / "compute" / "scaled_height_cs.hlsl");
            scaled_height <<
                "StructuredBuffer<float3> Positions : register(t0);\n"
                "RWStructuredBuffer<float> Output : register(u0);\n"
                "cbuffer Constants : register(b0) {\n"
                "    uint VertexCount;\n"
                "    uint IndexCount;\n"
                "    uint TriangleCount;\n"
                "    float Scale;\n"
                "};\n"
                "[numthreads(64, 1, 1)]\n"
                "void main(uint3 id : SV_DispatchThreadID) {\n"
                "    if (id.x < VertexCount) {\n"
                "        Output[id.x] = Positions[id.x].y * Scale;\n"
                "        Output[VertexCount + id.x] = Positions[id.x].x;\n"
                "    }\n"
                "}\n";
            scaled_height.close();

            std::ofstream normal_y(
                root / "shaders" / "compute" / "normal_y_cs.hlsl");
            normal_y <<
                "StructuredBuffer<float3> Normals : register(t0);\n"
                "RWStructuredBuffer<float> Output : register(u0);\n"
                "cbuffer Constants : register(b0) {\n"
                "    uint VertexCount;\n"
                "    uint IndexCount;\n"
                "    uint TriangleCount;\n"
                "};\n"
                "[numthreads(64, 1, 1)]\n"
                "void main(uint3 id : SV_DispatchThreadID) {\n"
                "    if (id.x < VertexCount) {\n"
                "        Output[id.x] = Normals[id.x].y;\n"
                "    }\n"
                "}\n";
            normal_y.close();

            wz::window::WindowDesc desc{};
            desc.title = "mesh_compute_field_gpu_test";
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
            if (device.impl) {
                wz::gpu::destroy_device(device);
            }
            if (window.native) {
                wz::window::destroy_window(window);
            }
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }

        wz::fs::Path resource_root() const { return root.string(); }
    };

    wz::engine::assets::ComputePipelineAsset
    create_scaled_height_compute_pipeline(
        wz::engine::assets::EngineAssetLibrary& assets)
    {
        using namespace wz::engine::assets;

        const ComputeShaderAsset shader =
            assets.shaders().create_compute_shader({
                .name = "project/scaled_height",
                .path = "shaders/compute/scaled_height_cs.hlsl",
                .entry = "main",
                .target = "cs_5_0",
            });
        EXPECT_TRUE(shader.valid());
        if (!shader.valid()) {
            return {};
        }

        return assets.compute_pipelines().create_compute_pipeline({
            .name = "project/scaled_height_pipeline",
            .compute_shader = shader.shader,
            .bindings = {
                ComputeBindingDesc{
                    .kind = ComputeBindingKind::StructuredBufferSRV,
                    .semantic = ComputeBindingSemantic::Unknown,
                    .shader_register = 0,
                    .register_space = 0,
                    .descriptor_count = 1,
                    .stride_bytes = sizeof(float) * 3u,
                },
                ComputeBindingDesc{
                    .kind = ComputeBindingKind::StructuredBufferUAV,
                    .semantic =
                        ComputeBindingSemantic::MeshDerivedFieldValues,
                    .shader_register = 0,
                    .register_space = 0,
                    .descriptor_count = 1,
                    .stride_bytes = sizeof(float),
                },
            },
            .root_constant_dwords = 4,
            .thread_group_size_x = 64,
            .thread_group_size_y = 1,
            .thread_group_size_z = 1,
        });
    }

    wz::engine::assets::ComputePipelineAsset
    create_normal_y_compute_pipeline(
        wz::engine::assets::EngineAssetLibrary& assets)
    {
        using namespace wz::engine::assets;

        const ComputeShaderAsset shader =
            assets.shaders().create_compute_shader({
                .name = "project/normal_y",
                .path = "shaders/compute/normal_y_cs.hlsl",
                .entry = "main",
                .target = "cs_5_0",
            });
        EXPECT_TRUE(shader.valid());
        if (!shader.valid()) {
            return {};
        }

        return assets.compute_pipelines().create_compute_pipeline({
            .name = "project/normal_y_pipeline",
            .compute_shader = shader.shader,
            .bindings = {
                ComputeBindingDesc{
                    .kind = ComputeBindingKind::StructuredBufferSRV,
                    .semantic = ComputeBindingSemantic::Unknown,
                    .shader_register = 0,
                    .register_space = 0,
                    .descriptor_count = 1,
                    .stride_bytes = sizeof(float) * 3u,
                },
                ComputeBindingDesc{
                    .kind = ComputeBindingKind::StructuredBufferUAV,
                    .semantic =
                        ComputeBindingSemantic::MeshDerivedFieldValues,
                    .shader_register = 0,
                    .register_space = 0,
                    .descriptor_count = 1,
                    .stride_bytes = sizeof(float),
                },
            },
            .root_constant_dwords = 3,
            .thread_group_size_x = 64,
            .thread_group_size_y = 1,
            .thread_group_size_z = 1,
        });
    }

    wz::engine::assets::ComputePipelineAsset create_test_wavelet_pipeline(
        wz::engine::assets::EngineAssetLibrary& assets)
    {
        using namespace wz::engine::assets;

        const ComputeShaderAsset shader =
            assets.shaders().create_compute_shader({
                .name = "mesh_wavelet/detail_heat",
                .path = "shaders/mesh_wavelet/detail_heat_cs.hlsl",
                .entry = "main",
                .target = "cs_5_0",
            });
        EXPECT_TRUE(shader.valid());
        if (!shader.valid()) {
            return {};
        }

        ComputePipelineAsset pipeline =
            assets.compute_pipelines().create_compute_pipeline({
                .name = "mesh_wavelet/detail_heat_pipeline",
                .compute_shader = shader.shader,
                .bindings = {
                    ComputeBindingDesc{
                        .kind = ComputeBindingKind::StructuredBufferSRV,
                        .semantic = ComputeBindingSemantic::MeshVertices,
                        .shader_register = 0,
                        .register_space = 0,
                        .descriptor_count = 1,
                        .stride_bytes = sizeof(float) * 6u,
                    },
                    ComputeBindingDesc{
                        .kind = ComputeBindingKind::StructuredBufferUAV,
                        .semantic =
                            ComputeBindingSemantic::MeshDerivedFieldValues,
                        .shader_register = 0,
                        .register_space = 0,
                        .descriptor_count = 1,
                        .stride_bytes = sizeof(float),
                    },
                },
                .root_constant_dwords = 12,
                .thread_group_size_x = 128,
                .thread_group_size_y = 1,
                .thread_group_size_z = 1,
            });
        EXPECT_TRUE(pipeline.valid());
        return pipeline;
    }
}

TEST(MeshDerivedFieldAssetModule, DefaultAssetAndHandleAreInvalid)
{
    wz::engine::assets::MeshDerivedFieldAsset asset{};
    wz::engine::assets::MeshDerivedFieldHandle handle{};

    EXPECT_FALSE(asset.valid());
    EXPECT_FALSE(handle.valid());
}

TEST(MeshDerivedFieldAssetModule, ResolvesExplicitVertexField)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets(device, logger, "vertex_field");

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());

    const auto field = assets.mesh_derived_fields().create_explicit_field({
        .name = "quad_vertex_mask",
        .source_mesh = mesh,
        .domain = MeshDerivedFieldDomain::Vertex,
        .element_count = 4u,
        .channels = {
            MeshDerivedFieldChannelDesc{
                .channel_id = 0x100u,
                .value_type = MeshDerivedFieldValueType::Float1,
                .values = float_values({ 0.0f, 0.25f, 0.75f, 1.0f }),
            },
        },
    });
    ASSERT_TRUE(field.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle =
        assets.mesh_derived_fields().get_mesh_derived_field(field);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->source_mesh_key, mesh.output);
    EXPECT_EQ(data->domain, MeshDerivedFieldDomain::Vertex);
    EXPECT_EQ(data->element_count, 4u);
    ASSERT_EQ(data->channels.size(), 1u);
    EXPECT_EQ(data->channels[0].channel_id, 0x100u);
    EXPECT_EQ(data->channels[0].byte_offset, 0u);
    EXPECT_EQ(data->channels[0].byte_count, 4u * sizeof(float));
    EXPECT_FLOAT_EQ(read_float(data->values, 0u, 2u), 0.75f);

    const auto mesh_handle = assets.meshes().get_mesh(mesh);
    ASSERT_TRUE(mesh_handle.valid());
    const auto* mesh_data = assets.meshes().get_mesh_data(mesh_handle);
    ASSERT_NE(mesh_data, nullptr);
    EXPECT_EQ(data->source_topology_hash, compute_mesh_topology_hash(*mesh_data));
}

TEST(MeshDerivedFieldAssetModule, PreservesFaceDomainAndSOAChannels)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets(device, logger, "face_field");

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());

    const auto field = assets.mesh_derived_fields().create_explicit_field({
        .name = "quad_face_masks",
        .source_mesh = mesh,
        .domain = MeshDerivedFieldDomain::Face,
        .element_count = 2u,
        .channels = {
            MeshDerivedFieldChannelDesc{
                .channel_id = 0x100u,
                .value_type = MeshDerivedFieldValueType::Float1,
                .values = float_values({ 0.4f, 0.9f }),
            },
            MeshDerivedFieldChannelDesc{
                .channel_id = 0x101u,
                .value_type = MeshDerivedFieldValueType::UInt1,
                .values = uint_values({ 7u, 8u }),
            },
        },
    });
    ASSERT_TRUE(field.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto handle =
        assets.mesh_derived_fields().get_mesh_derived_field(field);
    ASSERT_TRUE(handle.valid());
    const auto* data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(handle);
    ASSERT_NE(data, nullptr);

    EXPECT_EQ(data->domain, MeshDerivedFieldDomain::Face);
    EXPECT_EQ(data->element_count, 2u);
    ASSERT_EQ(data->channels.size(), 2u);
    EXPECT_EQ(data->channels[0].byte_offset, 0u);
    EXPECT_EQ(data->channels[0].byte_count, 2u * sizeof(float));
    EXPECT_EQ(data->channels[1].byte_offset, 2u * sizeof(float));
    EXPECT_EQ(data->channels[1].byte_count, 2u * sizeof(uint32_t));
    EXPECT_EQ(
        data->values.size(),
        2u * sizeof(float) + 2u * sizeof(uint32_t));
}

TEST(MeshDerivedFieldAssetModule, RejectsElementCountMismatch)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets(device, logger, "mismatch");

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "triangle",
        .kind = ProceduralMeshKind::Triangle,
    });
    ASSERT_TRUE(mesh.valid());

    const auto field = assets.mesh_derived_fields().create_explicit_field({
        .name = "bad_vertex_mask",
        .source_mesh = mesh,
        .domain = MeshDerivedFieldDomain::Vertex,
        .element_count = 4u,
        .channels = {
            MeshDerivedFieldChannelDesc{
                .channel_id = 0x100u,
                .value_type = MeshDerivedFieldValueType::Float1,
                .values = float_values({ 0.0f, 0.25f, 0.75f, 1.0f }),
            },
        },
    });
    ASSERT_TRUE(field.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    EXPECT_FALSE(report.ok());
    ASSERT_EQ(report.failures.size(), 1u);
    EXPECT_EQ(report.failures[0].key, field.output);
    EXPECT_FALSE(
        assets.mesh_derived_fields().get_mesh_derived_field(field).valid());
}

TEST(MeshDerivedFieldAssetModule, DeterministicIdentityIncludesChannels)
{
    using namespace wz::engine::assets;

    const MeshAsset mesh{
        .output = wz::asset::AssetKey{
            .content_hash = { 1u, 2u },
            .schema_hash = { 3u, 4u },
            .compiler_hash = { 5u, 6u },
            .deps_hash = { 7u, 8u },
        },
    };
    const ExplicitMeshDerivedFieldDesc first{
        .name = "a",
        .source_mesh = mesh,
        .domain = MeshDerivedFieldDomain::Face,
        .element_count = 2u,
        .channels = {
            MeshDerivedFieldChannelDesc{
                .channel_id = 0x100u,
                .value_type = MeshDerivedFieldValueType::Float1,
                .values = float_values({ 0.2f, 0.8f }),
            },
        },
    };
    ExplicitMeshDerivedFieldDesc second = first;
    ExplicitMeshDerivedFieldDesc changed = first;
    changed.channels[0].channel_id = 0x101u;

    EXPECT_EQ(
        make_explicit_mesh_derived_field_key(mesh.output, first),
        make_explicit_mesh_derived_field_key(mesh.output, second));
    EXPECT_NE(
        make_explicit_mesh_derived_field_key(mesh.output, first),
        make_explicit_mesh_derived_field_key(mesh.output, changed));
}

TEST(MeshDerivedFieldAssetModule, ResolvesWaveletAnalysisVertexField)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets(device, logger, "wavelet_vertex");

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());

    const auto field = assets.mesh_derived_fields().create_wavelet_analysis({
        .name = "quad_wavelet",
        .source_mesh = mesh,
        .scale_count = 2u,
        .lambda_max_estimate = 2.0f,
        .gamma = 1.0f,
    });
    ASSERT_TRUE(field.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle =
        assets.mesh_derived_fields().get_mesh_derived_field(field);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->source_mesh_key, mesh.output);
    EXPECT_EQ(data->domain, MeshDerivedFieldDomain::Vertex);
    EXPECT_EQ(data->element_count, 4u);
    ASSERT_EQ(data->channels.size(), 5u);

    const uint32_t expected_byte_count = 4u * sizeof(float);
    constexpr size_t kExpectedChannelCount = 5u;
    const uint32_t ids[kExpectedChannelCount] = {
        MeshWaveletChannelID::kPositionEnergyBase + 0u,
        MeshWaveletChannelID::kNormalEnergyBase + 0u,
        MeshWaveletChannelID::kPositionEnergyBase + 1u,
        MeshWaveletChannelID::kNormalEnergyBase + 1u,
        MeshWaveletChannelID::kDetailCost,
    };
    for (const uint32_t id : ids) {
        const auto* channel = find_channel(*data, id);
        ASSERT_NE(channel, nullptr);
        EXPECT_EQ(channel->value_type, MeshDerivedFieldValueType::Float1);
        EXPECT_EQ(channel->byte_count, expected_byte_count);
    }

    EXPECT_EQ(
        data->values.size(),
        kExpectedChannelCount * expected_byte_count);
}

TEST_F(MeshWaveletGpuFixture, ResolvesWaveletAnalysisThroughGpuPath)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets{
        device,
        logger,
        resource_root(),
        EngineAssetCacheSettings{
            .root = resource_root(),
            .enabled = false,
        }};

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const ComputePipelineAsset pipeline = create_test_wavelet_pipeline(assets);
    ASSERT_TRUE(pipeline.valid());

    const auto field = assets.mesh_derived_fields().create_wavelet_analysis({
        .name = "cube_wavelet_gpu",
        .source_mesh = mesh,
        .compute_pipeline = pipeline,
        .scale_count = 3u,
        .lambda_max_estimate = 2.0f,
        .gamma = 1.0f,
    });
    ASSERT_TRUE(field.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto handle =
        assets.mesh_derived_fields().get_mesh_derived_field(field);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(handle);
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->valid());
    EXPECT_EQ(data->source_mesh_key, mesh.output);
    EXPECT_EQ(data->domain, MeshDerivedFieldDomain::Vertex);
    EXPECT_EQ(data->element_count, 8u);
    ASSERT_EQ(data->channels.size(), 7u);

    const auto* detail =
        find_channel(*data, MeshWaveletChannelID::kDetailCost);
    ASSERT_NE(detail, nullptr);
    EXPECT_EQ(detail->value_type, MeshDerivedFieldValueType::Float1);
    EXPECT_EQ(detail->byte_count, data->element_count * sizeof(float));

    const std::vector<float> detail_values =
        read_float_channel(*data, MeshWaveletChannelID::kDetailCost);
    EXPECT_NE(
        std::max_element(detail_values.begin(), detail_values.end()),
        detail_values.end());
    EXPECT_GT(
        *std::max_element(detail_values.begin(), detail_values.end()),
        0.0f);
}

TEST_F(MeshWaveletGpuFixture, WaveletGpuDetailRespondsToParameters)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets{
        device,
        logger,
        resource_root(),
        EngineAssetCacheSettings{
            .root = resource_root(),
            .enabled = false,
        }};

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const ComputePipelineAsset pipeline = create_test_wavelet_pipeline(assets);
    ASSERT_TRUE(pipeline.valid());

    const auto base = assets.mesh_derived_fields().create_wavelet_analysis({
        .name = "cube_wavelet_base",
        .source_mesh = mesh,
        .compute_pipeline = pipeline,
        .scale_count = 2u,
        .lambda_max_estimate = 0.75f,
        .gamma = 1.0f,
    });
    const auto changed = assets.mesh_derived_fields().create_wavelet_analysis({
        .name = "cube_wavelet_changed",
        .source_mesh = mesh,
        .compute_pipeline = pipeline,
        .scale_count = 4u,
        .lambda_max_estimate = 3.0f,
        .gamma = 0.45f,
    });
    ASSERT_TRUE(base.valid());
    ASSERT_TRUE(changed.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto read_detail =
        [&assets](MeshDerivedFieldAsset field) -> std::vector<float> {
            const auto handle =
                assets.mesh_derived_fields().get_mesh_derived_field(field);
            EXPECT_TRUE(handle.valid());
            const auto* data =
                assets.mesh_derived_fields().get_mesh_derived_field_data(
                    handle);
            EXPECT_NE(data, nullptr);
            return data
                ? read_float_channel(*data, MeshWaveletChannelID::kDetailCost)
                : std::vector<float>{};
        };

    const std::vector<float> base_detail = read_detail(base);
    const std::vector<float> changed_detail = read_detail(changed);
    ASSERT_EQ(base_detail.size(), changed_detail.size());
    ASSERT_FALSE(base_detail.empty());

    float max_delta = 0.0f;
    for (size_t i = 0; i < base_detail.size(); ++i) {
        max_delta = (std::max)(
            max_delta,
            std::abs(base_detail[i] - changed_detail[i]));
    }
    EXPECT_GT(max_delta, 0.001f);
}

TEST(MeshDerivedFieldAssetModule, WaveletAnalysisTriangleReferenceIsZero)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets(device, logger, "wavelet_triangle_zero");

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "triangle",
        .kind = ProceduralMeshKind::Triangle,
    });
    ASSERT_TRUE(mesh.valid());

    const auto field = assets.mesh_derived_fields().create_wavelet_analysis({
        .name = "triangle_wavelet",
        .source_mesh = mesh,
        .scale_count = 2u,
    });
    ASSERT_TRUE(field.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto handle =
        assets.mesh_derived_fields().get_mesh_derived_field(field);
    ASSERT_TRUE(handle.valid());
    const auto* data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(handle);
    ASSERT_NE(data, nullptr);

    for (const auto& channel : data->channels) {
        for (uint32_t i = 0; i < data->element_count; ++i) {
            EXPECT_FLOAT_EQ(read_float(data->values, channel.byte_offset, i), 0.0f);
        }
    }
}

TEST(MeshDerivedFieldAssetModule, WaveletAnalysisIsStableAcrossCacheRoundTrip)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    const char* suffix = "wavelet_cache_round_trip";

    std::vector<MeshDerivedFieldChannel> first_channels;
    std::vector<std::byte> first_values;
    {
        auto assets = make_assets_with_disk_cache(device, logger, suffix);
        const auto mesh = assets.meshes().create_procedural_mesh({
            .name = "quad",
            .kind = ProceduralMeshKind::Quad,
        });
        ASSERT_TRUE(mesh.valid());
        const auto field =
            assets.mesh_derived_fields().create_wavelet_analysis({
                .name = "quad_wavelet",
                .source_mesh = mesh,
                .scale_count = 2u,
            });
        ASSERT_TRUE(field.valid());

        ASSERT_TRUE(assets.commit());
        ASSERT_TRUE(assets.resolve_all().ok());

        const auto handle =
            assets.mesh_derived_fields().get_mesh_derived_field(field);
        ASSERT_TRUE(handle.valid());
        const auto* data =
            assets.mesh_derived_fields().get_mesh_derived_field_data(handle);
        ASSERT_NE(data, nullptr);
        first_channels = data->channels;
        first_values = data->values;
    }

    auto cached_assets = make_assets_with_disk_cache(device, logger, suffix);
    const auto cached_mesh = cached_assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(cached_mesh.valid());
    const auto cached_field =
        cached_assets.mesh_derived_fields().create_wavelet_analysis({
            .name = "quad_wavelet",
            .source_mesh = cached_mesh,
            .scale_count = 2u,
        });
    ASSERT_TRUE(cached_field.valid());

    ASSERT_TRUE(cached_assets.commit());
    ASSERT_TRUE(cached_assets.resolve_all().ok());

    const auto handle =
        cached_assets.mesh_derived_fields().get_mesh_derived_field(cached_field);
    ASSERT_TRUE(handle.valid());
    const auto* data =
        cached_assets.mesh_derived_fields().get_mesh_derived_field_data(handle);
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->channels.size(), first_channels.size());
    for (size_t i = 0; i < data->channels.size(); ++i) {
        EXPECT_EQ(data->channels[i].channel_id, first_channels[i].channel_id);
        EXPECT_EQ(data->channels[i].value_type, first_channels[i].value_type);
        EXPECT_EQ(data->channels[i].byte_offset, first_channels[i].byte_offset);
        EXPECT_EQ(data->channels[i].byte_count, first_channels[i].byte_count);
    }
    EXPECT_EQ(data->values, first_values);
}

TEST(MeshDerivedFieldAssetModule, WaveletAnalysisDemandResolveUsesDiskCache)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    const char* suffix = "wavelet_demand_disk_cache";

    {
        auto assets = make_assets_with_disk_cache(device, logger, suffix);
        const auto mesh = assets.meshes().create_procedural_mesh({
            .name = "quad",
            .kind = ProceduralMeshKind::Quad,
        });
        ASSERT_TRUE(mesh.valid());
        const auto field =
            assets.mesh_derived_fields().create_wavelet_analysis({
                .name = "quad_wavelet",
                .source_mesh = mesh,
                .scale_count = 2u,
            });
        ASSERT_TRUE(field.valid());

        ASSERT_TRUE(assets.commit());
        ASSERT_TRUE(assets.resolve_all().ok());
    }

    auto cached_assets = make_assets_with_disk_cache(device, logger, suffix);
    const auto cached_mesh = cached_assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(cached_mesh.valid());
    const auto cached_field =
        cached_assets.mesh_derived_fields().create_wavelet_analysis({
            .name = "quad_wavelet",
            .source_mesh = cached_mesh,
            .scale_count = 2u,
        });
    ASSERT_TRUE(cached_field.valid());
    ASSERT_TRUE(cached_assets.system().register_demand_root(
        wz::asset::DemandRoot::GPURuntime,
        { cached_field.output }));

    ASSERT_TRUE(cached_assets.commit());
    const auto report = cached_assets.resolve_demanded(
        wz::asset::ResolvePolicy::CachePreferred);
    ASSERT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 1u);

    const auto field_handle =
        cached_assets.mesh_derived_fields().get_mesh_derived_field(
            cached_field);
    EXPECT_TRUE(field_handle.valid());
    EXPECT_FALSE(cached_assets.meshes().get_mesh(cached_mesh).valid());
}

TEST(MeshDerivedFieldAssetModule, WaveletAnalysisCubeProducesDetailHeat)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets_no_disk_cache(device, logger, "wavelet_cube");

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data = resolve_wavelet_field(
        assets,
        mesh,
        MeshWaveletAnalysisDesc{
            .name = "cube_wavelet",
            .source_mesh = mesh,
            .scale_count = 2u,
        });
    ASSERT_NE(data, nullptr);

    const std::vector<float> detail =
        read_float_channel(*data, MeshWaveletChannelID::kDetailCost);
    ASSERT_EQ(detail.size(), 8u);
    const float total =
        std::accumulate(detail.begin(), detail.end(), 0.0f);
    EXPECT_GT(total, 0.0f);
    EXPECT_GT(
        *std::max_element(detail.begin(), detail.end()),
        *std::min_element(detail.begin(), detail.end()));
}

TEST(MeshDerivedFieldAssetModule, WaveletAnalysisDisplacedQuadConcentratesEnergy)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets =
        make_assets_no_disk_cache(device, logger, "wavelet_displaced_quad");

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto mesh_handle = assets.meshes().get_mesh(mesh);
    ASSERT_TRUE(mesh_handle.valid());
    const auto* const_mesh_data = assets.meshes().get_mesh_data(mesh_handle);
    ASSERT_NE(const_mesh_data, nullptr);
    auto* mesh_data = const_cast<MeshData*>(const_mesh_data);
    mesh_data->vertices[2].position[2] = 2.0f;

    const auto* data = resolve_wavelet_field(
        assets,
        mesh,
        MeshWaveletAnalysisDesc{
            .name = "displaced_quad_wavelet",
            .source_mesh = mesh,
            .scale_count = 2u,
        });
    ASSERT_NE(data, nullptr);

    const std::vector<float> detail =
        read_float_channel(*data, MeshWaveletChannelID::kDetailCost);
    ASSERT_EQ(detail.size(), 4u);
    const auto max_it = std::max_element(detail.begin(), detail.end());
    EXPECT_EQ(
        static_cast<size_t>(std::distance(detail.begin(), max_it)),
        2u);
    EXPECT_GT(detail[2], 0.0f);
}

TEST(MeshDerivedFieldAssetModule, WaveletAnalysisPreservesQuadSymmetry)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets_no_disk_cache(device, logger, "wavelet_symmetry");

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());

    const auto* data = resolve_wavelet_field(
        assets,
        mesh,
        MeshWaveletAnalysisDesc{
            .name = "quad_wavelet",
            .source_mesh = mesh,
            .scale_count = 2u,
        });
    ASSERT_NE(data, nullptr);

    const std::vector<float> detail =
        read_float_channel(*data, MeshWaveletChannelID::kDetailCost);
    ASSERT_EQ(detail.size(), 4u);
    EXPECT_NEAR(detail[1], detail[3], 1.0e-5f);
}

TEST(MeshDerivedFieldAssetModule, WaveletAnalysisScaleChannelsRespondDifferently)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets =
        make_assets_no_disk_cache(device, logger, "wavelet_scale_response");

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto mesh_handle = assets.meshes().get_mesh(mesh);
    ASSERT_TRUE(mesh_handle.valid());
    const auto* const_mesh_data = assets.meshes().get_mesh_data(mesh_handle);
    ASSERT_NE(const_mesh_data, nullptr);
    auto* mesh_data = const_cast<MeshData*>(const_mesh_data);
    mesh_data->vertices[2].position[2] = 2.0f;

    const auto* data = resolve_wavelet_field(
        assets,
        mesh,
        MeshWaveletAnalysisDesc{
            .name = "scale_quad_wavelet",
            .source_mesh = mesh,
            .scale_count = 3u,
        });
    ASSERT_NE(data, nullptr);

    const std::vector<float> coarse = read_float_channel(
        *data,
        MeshWaveletChannelID::kPositionEnergyBase + 0u);
    const std::vector<float> fine = read_float_channel(
        *data,
        MeshWaveletChannelID::kPositionEnergyBase + 2u);
    ASSERT_EQ(coarse.size(), 4u);
    ASSERT_EQ(fine.size(), 4u);
    EXPECT_NE(coarse, fine);
    EXPECT_GE(fine[2], coarse[2]);
}

TEST(MeshDerivedFieldAssetModule, WaveletAnalysisIdentityIncludesParameters)
{
    using namespace wz::engine::assets;

    const MeshAsset mesh{
        .output = wz::asset::AssetKey{
            .content_hash = { 1u, 2u },
            .schema_hash = { 3u, 4u },
            .compiler_hash = { 5u, 6u },
            .deps_hash = { 7u, 8u },
        },
    };
    const MeshWaveletAnalysisDesc first{
        .name = "wavelet",
        .source_mesh = mesh,
        .scale_count = 2u,
        .lambda_max_estimate = 2.0f,
        .gamma = 1.0f,
    };
    MeshWaveletAnalysisDesc second = first;
    MeshWaveletAnalysisDesc changed_scale = first;
    changed_scale.scale_count = 3u;
    MeshWaveletAnalysisDesc changed_gamma = first;
    changed_gamma.gamma = 0.75f;

    EXPECT_EQ(
        make_mesh_wavelet_analysis_field_key(mesh.output, first),
        make_mesh_wavelet_analysis_field_key(mesh.output, second));
    EXPECT_NE(
        make_mesh_wavelet_analysis_field_key(mesh.output, first),
        make_mesh_wavelet_analysis_field_key(mesh.output, changed_scale));
    EXPECT_NE(
        make_mesh_wavelet_analysis_field_key(mesh.output, first),
        make_mesh_wavelet_analysis_field_key(mesh.output, changed_gamma));
}

TEST(MeshDerivedFieldAssetModule, RejectsInvalidWaveletAnalysisDesc)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets(device, logger, "wavelet_invalid_desc");

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());

    EXPECT_FALSE(assets.mesh_derived_fields().create_wavelet_analysis({
        .name = "bad_scale",
        .source_mesh = mesh,
        .scale_count = 0u,
    }).valid());
    EXPECT_FALSE(assets.mesh_derived_fields().create_wavelet_analysis({
        .name = "bad_lambda",
        .source_mesh = mesh,
        .lambda_max_estimate = 0.0f,
    }).valid());
    EXPECT_FALSE(assets.mesh_derived_fields().create_wavelet_analysis({
        .name = "bad_gamma",
        .source_mesh = mesh,
        .gamma = 0.0f,
    }).valid());
}

TEST(MeshDerivedFieldAssetModule, TopologyHashIgnoresVertexPositions)
{
    using namespace wz::engine::assets;

    MeshData a{};
    a.vertices.resize(3);
    a.indices = { 0u, 1u, 2u };

    MeshData b = a;
    b.vertices[0].position[0] = 10.0f;
    b.vertices[1].position[1] = 20.0f;

    MeshData c = a;
    c.indices = { 0u, 2u, 1u };

    EXPECT_EQ(compute_mesh_topology_hash(a), compute_mesh_topology_hash(b));
    EXPECT_NE(compute_mesh_topology_hash(a), compute_mesh_topology_hash(c));
}

TEST(MeshDerivedFieldAssetModule, MeshDomainElementCountsMatchQuad)
{
    using namespace wz::engine::assets;

    MeshData quad{};
    quad.vertices.resize(4);
    quad.indices = { 0u, 1u, 2u, 0u, 2u, 3u };

    EXPECT_EQ(
        mesh_domain_element_count(quad, MeshDerivedFieldDomain::Vertex),
        4u);
    EXPECT_EQ(
        mesh_domain_element_count(quad, MeshDerivedFieldDomain::Face),
        2u);
    EXPECT_EQ(
        mesh_domain_element_count(quad, MeshDerivedFieldDomain::Corner),
        6u);
    EXPECT_EQ(
        mesh_domain_element_count(quad, MeshDerivedFieldDomain::Edge),
        5u);
}

TEST(MeshDerivedFieldAssetModule, ComputeDerivedFieldIdentityIncludesRecipeAndDeps)
{
    using namespace wz::engine::assets;

    const MeshAsset mesh{
        .output = wz::asset::AssetKey{
            .content_hash = { 1u, 2u },
            .schema_hash = { 3u, 4u },
            .compiler_hash = { 5u, 6u },
            .deps_hash = { 7u, 8u },
        },
    };
    const ComputePipelineAsset pipeline{
        .key = wz::asset::AssetKey{
            .content_hash = { 11u, 12u },
            .schema_hash = { 13u, 14u },
            .compiler_hash = { 15u, 16u },
            .deps_hash = { 17u, 18u },
        },
    };

    const MeshComputeDerivedFieldDesc first{
        .name = "compute_field",
        .source_mesh = mesh,
        .compute_pipeline = pipeline,
        .channels = {
            MeshDerivedFieldChannelDesc{
                .channel_id = 0x2000u,
                .value_type = MeshDerivedFieldValueType::Float1,
            },
        },
        .inputs = { MeshComputeInput::Positions },
        .root_constants = { 42u },
    };
    MeshComputeDerivedFieldDesc second = first;

    MeshComputeDerivedFieldDesc changed_params = first;
    changed_params.root_constants = { 43u };
    MeshComputeDerivedFieldDesc changed_channel = first;
    changed_channel.channels[0].channel_id = 0x2001u;
    MeshComputeDerivedFieldDesc changed_inputs = first;
    changed_inputs.inputs = { MeshComputeInput::Normals };
    MeshComputeDerivedFieldDesc changed_pipeline = first;
    changed_pipeline.compute_pipeline.key.content_hash = { 99u, 98u };

    const auto base_key =
        make_mesh_compute_derived_field_key(mesh.output, first);
    EXPECT_EQ(
        base_key,
        make_mesh_compute_derived_field_key(mesh.output, second));
    EXPECT_NE(
        base_key,
        make_mesh_compute_derived_field_key(mesh.output, changed_params));
    EXPECT_NE(
        base_key,
        make_mesh_compute_derived_field_key(mesh.output, changed_channel));
    EXPECT_NE(
        base_key,
        make_mesh_compute_derived_field_key(mesh.output, changed_inputs));

    // Swapping the kernel pipeline or the source mesh changes only deps.
    const auto changed_pipeline_key =
        make_mesh_compute_derived_field_key(mesh.output, changed_pipeline);
    EXPECT_NE(base_key, changed_pipeline_key);
    EXPECT_EQ(base_key.content_hash, changed_pipeline_key.content_hash);

    wz::asset::AssetKey other_mesh_key = mesh.output;
    other_mesh_key.content_hash = { 77u, 78u };
    const auto changed_mesh_key =
        make_mesh_compute_derived_field_key(other_mesh_key, first);
    EXPECT_NE(base_key, changed_mesh_key);
    EXPECT_EQ(base_key.content_hash, changed_mesh_key.content_hash);
}

TEST(MeshDerivedFieldAssetModule, RejectsInvalidComputeDerivedFieldDesc)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets(device, logger, "compute_invalid_desc");

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());

    const ComputePipelineAsset pipeline{
        .key = wz::asset::AssetKey{
            .content_hash = { 11u, 12u },
            .schema_hash = { 13u, 14u },
            .compiler_hash = { 15u, 16u },
            .deps_hash = { 17u, 18u },
        },
    };
    const std::vector<MeshDerivedFieldChannelDesc> valid_channels{
        MeshDerivedFieldChannelDesc{
            .channel_id = 0x2000u,
            .value_type = MeshDerivedFieldValueType::Float1,
        },
    };

    EXPECT_FALSE(assets.mesh_derived_fields().create_compute_derived_field({
        .name = "no_pipeline",
        .source_mesh = mesh,
        .channels = valid_channels,
    }).valid());
    EXPECT_FALSE(assets.mesh_derived_fields().create_compute_derived_field({
        .name = "no_channels",
        .source_mesh = mesh,
        .compute_pipeline = pipeline,
    }).valid());
    EXPECT_FALSE(assets.mesh_derived_fields().create_compute_derived_field({
        .name = "zero_channel_id",
        .source_mesh = mesh,
        .compute_pipeline = pipeline,
        .channels = {
            MeshDerivedFieldChannelDesc{ .channel_id = 0u },
        },
    }).valid());
    EXPECT_FALSE(assets.mesh_derived_fields().create_compute_derived_field({
        .name = "duplicate_channel_ids",
        .source_mesh = mesh,
        .compute_pipeline = pipeline,
        .channels = {
            MeshDerivedFieldChannelDesc{ .channel_id = 0x2000u },
            MeshDerivedFieldChannelDesc{ .channel_id = 0x2000u },
        },
    }).valid());
    EXPECT_FALSE(assets.mesh_derived_fields().create_compute_derived_field({
        .name = "channel_payload_not_allowed",
        .source_mesh = mesh,
        .compute_pipeline = pipeline,
        .channels = {
            MeshDerivedFieldChannelDesc{
                .channel_id = 0x2000u,
                .values = float_values({ 1.0f }),
            },
        },
    }).valid());

    EXPECT_TRUE(assets.mesh_derived_fields().create_compute_derived_field({
        .name = "valid",
        .source_mesh = mesh,
        .compute_pipeline = pipeline,
        .channels = valid_channels,
    }).valid());
}

TEST_F(MeshComputeFieldGpuFixture, ResolvesComputeDerivedFieldThroughGpuPath)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets{
        device,
        logger,
        resource_root(),
        EngineAssetCacheSettings{
            .root = resource_root(),
            .enabled = false,
        }};

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const ComputePipelineAsset pipeline =
        create_scaled_height_compute_pipeline(assets);
    ASSERT_TRUE(pipeline.valid());

    constexpr float kScale = 1.5f;
    constexpr uint32_t kHeightChannel = 0x2000u;
    constexpr uint32_t kXChannel = 0x2001u;
    const auto field =
        assets.mesh_derived_fields().create_compute_derived_field({
            .name = "cube_scaled_height",
            .source_mesh = mesh,
            .compute_pipeline = pipeline,
            .domain = MeshDerivedFieldDomain::Vertex,
            .channels = {
                MeshDerivedFieldChannelDesc{
                    .channel_id = kHeightChannel,
                    .value_type = MeshDerivedFieldValueType::Float1,
                },
                MeshDerivedFieldChannelDesc{
                    .channel_id = kXChannel,
                    .value_type = MeshDerivedFieldValueType::Float1,
                },
            },
            .inputs = { MeshComputeInput::Positions },
            .root_constants = { f32_bits(kScale) },
        });
    ASSERT_TRUE(field.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto handle =
        assets.mesh_derived_fields().get_mesh_derived_field(field);
    ASSERT_TRUE(handle.valid());
    const auto* data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(handle);
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->valid());
    EXPECT_EQ(data->source_mesh_key, mesh.output);
    EXPECT_EQ(data->domain, MeshDerivedFieldDomain::Vertex);
    EXPECT_EQ(data->element_count, 8u);
    ASSERT_EQ(data->channels.size(), 2u);
    EXPECT_EQ(data->channels[0].channel_id, kHeightChannel);
    EXPECT_EQ(data->channels[0].byte_offset, 0u);
    EXPECT_EQ(data->channels[0].byte_count, 8u * sizeof(float));
    EXPECT_EQ(data->channels[1].channel_id, kXChannel);
    EXPECT_EQ(data->channels[1].byte_offset, 8u * sizeof(float));
    EXPECT_EQ(data->channels[1].byte_count, 8u * sizeof(float));

    const auto* mesh_data =
        assets.meshes().get_mesh_data(assets.meshes().get_mesh(mesh));
    ASSERT_NE(mesh_data, nullptr);
    ASSERT_EQ(mesh_data->vertex_count(), 8u);

    const std::vector<float> heights =
        read_float_channel(*data, kHeightChannel);
    const std::vector<float> xs = read_float_channel(*data, kXChannel);
    for (uint32_t i = 0; i < 8u; ++i) {
        EXPECT_FLOAT_EQ(
            heights[i],
            mesh_data->vertices[i].position[1] * kScale);
        EXPECT_FLOAT_EQ(xs[i], mesh_data->vertices[i].position[0]);
    }

    // Compiled channels stay GPU-resident for downstream visualization.
    EXPECT_TRUE(
        assets.gpu_resident_fields().find(field.output, kHeightChannel)
            .valid());
    EXPECT_TRUE(
        assets.gpu_resident_fields().find(field.output, kXChannel).valid());
}

TEST_F(MeshComputeFieldGpuFixture, ComputeDerivedFieldSecondResolveIsDiskCacheHit)
{
    using namespace wz::engine::assets;

    constexpr uint32_t kHeightChannel = 0x2000u;
    std::vector<float> first_heights;
    {
        EngineAssetLibrary assets{
            device,
            logger,
            resource_root(),
            EngineAssetCacheSettings{
                .root = resource_root(),
                .enabled = true,
            }};

        const auto mesh = assets.meshes().create_procedural_mesh({
            .name = "cube",
            .kind = ProceduralMeshKind::Cube,
        });
        ASSERT_TRUE(mesh.valid());
        const ComputePipelineAsset pipeline =
            create_scaled_height_compute_pipeline(assets);
        ASSERT_TRUE(pipeline.valid());

        const auto field =
            assets.mesh_derived_fields().create_compute_derived_field({
                .name = "cube_scaled_height",
                .source_mesh = mesh,
                .compute_pipeline = pipeline,
                .channels = {
                    MeshDerivedFieldChannelDesc{
                        .channel_id = kHeightChannel,
                        .value_type = MeshDerivedFieldValueType::Float1,
                    },
                },
                .inputs = { MeshComputeInput::Positions },
                .root_constants = { f32_bits(2.0f) },
            });
        ASSERT_TRUE(field.valid());

        ASSERT_TRUE(assets.commit());
        ASSERT_TRUE(assets.resolve_all().ok());

        const auto* data =
            assets.mesh_derived_fields().get_mesh_derived_field_data(
                assets.mesh_derived_fields().get_mesh_derived_field(field));
        ASSERT_NE(data, nullptr);
        first_heights = read_float_channel(*data, kHeightChannel);
    }

    // Demand-resolve against the disk cache with no GPU device bound: a
    // cache hit short-circuits the compile, so no dispatch can occur and
    // the source mesh never needs to be compiled.
    wz::Logger cached_logger;
    wz::gpu::Device null_device{};
    EngineAssetLibrary cached_assets{
        null_device,
        cached_logger,
        resource_root(),
        EngineAssetCacheSettings{
            .root = resource_root(),
            .enabled = true,
        }};

    const auto cached_mesh = cached_assets.meshes().create_procedural_mesh({
        .name = "cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(cached_mesh.valid());
    const ComputePipelineAsset cached_pipeline =
        create_scaled_height_compute_pipeline(cached_assets);
    ASSERT_TRUE(cached_pipeline.valid());

    const auto cached_field =
        cached_assets.mesh_derived_fields().create_compute_derived_field({
            .name = "cube_scaled_height",
            .source_mesh = cached_mesh,
            .compute_pipeline = cached_pipeline,
            .channels = {
                MeshDerivedFieldChannelDesc{
                    .channel_id = kHeightChannel,
                    .value_type = MeshDerivedFieldValueType::Float1,
                },
            },
            .inputs = { MeshComputeInput::Positions },
            .root_constants = { f32_bits(2.0f) },
        });
    ASSERT_TRUE(cached_field.valid());
    ASSERT_TRUE(cached_assets.system().register_demand_root(
        wz::asset::DemandRoot::GPURuntime,
        { cached_field.output }));

    ASSERT_TRUE(cached_assets.commit());
    const auto report = cached_assets.resolve_demanded(
        wz::asset::ResolvePolicy::CachePreferred);
    ASSERT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 1u);
    EXPECT_FALSE(cached_assets.meshes().get_mesh(cached_mesh).valid());

    const auto* data =
        cached_assets.mesh_derived_fields().get_mesh_derived_field_data(
            cached_assets.mesh_derived_fields().get_mesh_derived_field(
                cached_field));
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(read_float_channel(*data, kHeightChannel), first_heights);
}

TEST_F(MeshComputeFieldGpuFixture, ComputeDerivedFieldMissingNormalsFailsCompile)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets{
        device,
        logger,
        resource_root(),
        EngineAssetCacheSettings{
            .root = resource_root(),
            .enabled = false,
        }};

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* const_mesh_data =
        assets.meshes().get_mesh_data(assets.meshes().get_mesh(mesh));
    ASSERT_NE(const_mesh_data, nullptr);
    const_cast<MeshData*>(const_mesh_data)->has_normals = false;

    const ComputePipelineAsset pipeline =
        create_normal_y_compute_pipeline(assets);
    ASSERT_TRUE(pipeline.valid());

    const auto field =
        assets.mesh_derived_fields().create_compute_derived_field({
            .name = "quad_normal_y",
            .source_mesh = mesh,
            .compute_pipeline = pipeline,
            .channels = {
                MeshDerivedFieldChannelDesc{
                    .channel_id = 0x2000u,
                    .value_type = MeshDerivedFieldValueType::Float1,
                },
            },
            .inputs = { MeshComputeInput::Normals },
        });
    ASSERT_TRUE(field.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    EXPECT_FALSE(report.ok());
    ASSERT_EQ(report.failures.size(), 1u);
    EXPECT_EQ(report.failures[0].key, field.output);
    EXPECT_FALSE(
        assets.mesh_derived_fields().get_mesh_derived_field(field).valid());
}

TEST(MeshDerivedFieldAssetModule, GpuResidentFieldTableFindsByFieldAndChannel)
{
    using namespace wz::engine::assets;

    const wz::asset::AssetKey field_key{
        .content_hash = { 1u, 2u },
        .schema_hash = { 3u, 4u },
        .compiler_hash = { 5u, 6u },
        .deps_hash = { 7u, 8u },
    };
    const wz::gpu::GPUHandle handle{
        .id = 11u,
        .epoch = 1u,
        .type = wz::gpu::kGPUMeshFieldBufferResourceType,
    };
    const wz::gpu::GPUHandle replacement{
        .id = 12u,
        .epoch = 1u,
        .type = wz::gpu::kGPUMeshFieldBufferResourceType,
    };

    GpuResidentFieldTable table{};
    EXPECT_FALSE(table.add({}));
    EXPECT_TRUE(table.add(GpuResidentFieldEntry{
        .field_key = field_key,
        .channel_id = MeshWaveletChannelID::kDetailCost,
        .gpu_resource = handle,
    }));
    EXPECT_EQ(table.size(), 1u);
    EXPECT_EQ(
        table.find(field_key, MeshWaveletChannelID::kDetailCost),
        handle);
    EXPECT_EQ(table.find(field_key, MeshWaveletChannelID::kDetailCost + 1u),
              wz::gpu::GPUHandle{});

    EXPECT_FALSE(table.add(GpuResidentFieldEntry{
        .field_key = field_key,
        .channel_id = MeshWaveletChannelID::kDetailCost,
        .gpu_resource = replacement,
    }));
    EXPECT_EQ(table.size(), 1u);
    EXPECT_EQ(
        table.find(field_key, MeshWaveletChannelID::kDetailCost),
        handle);

    table.clear();
    EXPECT_EQ(table.size(), 0u);
    EXPECT_EQ(
        table.find(field_key, MeshWaveletChannelID::kDetailCost),
        wz::gpu::GPUHandle{});
}
