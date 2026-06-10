#include "scene_authoring_materialize_test_support.h"

#include <engine/assets/key_factories/mesh_derived_field.h>
#include <window/window2.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace
{
    uint32_t f32_bits(float value)
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    struct SceneMeshComputeFieldMaterializeGpuFixture : public ::testing::Test
    {
        wz::Logger logger;
        wz::window::WindowHandle window{};
        wz::gpu::Device device{};
        wz::fs::Path root;

        void SetUp() override
        {
            root = wz::fs::join(
                wz::fs::temp_directory_path(),
                "wozzits_scene_materialize_mesh_compute_field_test");
            // A fresh root each run: a leftover disk cache would turn the
            // compile into a cache hit, which intentionally skips the GPU
            // dispatch this test asserts on (via GPU-resident channels).
            std::error_code ec;
            std::filesystem::remove_all(std::filesystem::path(root), ec);
            ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);
            ASSERT_EQ(
                wz::fs::create_directories(
                    wz::fs::join(root, "shaders/compute")),
                wz::fs::FileError::None);

            // Channel layout: [0, VertexCount) holds position.y * Scale.
            // The first three root constants are engine-filled
            // (vertex_count, index_count, triangle_count); Scale is the
            // single authored param.
            ASSERT_EQ(
                wz::fs::write_file_text(
                    wz::fs::join(
                        root,
                        "shaders/compute/scaled_height_cs.hlsl"),
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
                    "    }\n"
                    "}\n"),
                wz::fs::FileError::None);

            wz::window::WindowDesc desc{};
            desc.title = "scene_mesh_compute_field_materialize_test";
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
            std::filesystem::remove_all(std::filesystem::path(root), ec);
        }
    };

    wz::engine::assets::SceneMeshComputeFieldAsset
    make_scaled_height_component(uint32_t channel_id, float scale)
    {
        using namespace wz::engine::assets;
        return SceneMeshComputeFieldAsset{
            .kernel_id = "project/scaled_height",
            .hlsl_path = "shaders/compute/scaled_height_cs.hlsl",
            .entry = "main",
            .target = "cs_5_0",
            .thread_group_size_x = 64,
            .thread_group_size_y = 1,
            .thread_group_size_z = 1,
            .inputs = { MeshComputeInput::Positions },
            .channels = {
                SceneMeshComputeFieldChannelAsset{
                    .channel_id = channel_id,
                    .value_type = MeshDerivedFieldValueType::Float1,
                },
            },
            .params = { f32_bits(scale) },
        };
    }
}

TEST_F(
    SceneMeshComputeFieldMaterializeGpuFixture,
    MeshComputeFieldMaterializesCachedFieldAssetAndVisualization)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets{
        device,
        logger,
        root,
        EngineAssetCacheSettings{
            .root = root,
            .enabled = true,
        }};

    constexpr uint32_t kHeightChannel = 0x2000u;
    constexpr float kScale = 1.5f;

    SceneAssetData scene{};
    scene.name = "mesh_compute_field_materialize";
    SceneNodeAsset node = make_scene_node("field_mesh");
    attach_mesh_source(node, SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    });
    attach_mesh_compute_field(
        node,
        make_scaled_height_component(kHeightChannel, kScale));
    SceneMeshRenderStyleAsset style{};
    style.field_visualization_enabled = true;
    style.field_visualization_channel_id = kHeightChannel;
    attach_mesh_render_style(node, style);
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_EQ(scene.nodes.size(), 1u);

    const auto& materialized = scene.nodes[0];
    ASSERT_TRUE(materialized.mesh_compute_field.has_value());
    const wz::asset::AssetKey field_key =
        materialized.mesh_compute_field->field_asset;
    EXPECT_NE(field_key, wz::asset::AssetKey{});

    // The render style visualizes the channel produced by the kernel.
    ASSERT_TRUE(materialized.mesh_render_style.has_value());
    EXPECT_EQ(
        materialized.mesh_render_style->field_visualization_asset,
        field_key);
    ASSERT_TRUE(materialized.renderable_asset.has_value());
    EXPECT_FALSE(report.renderables_to_realize.empty());

    ASSERT_TRUE(assets.commit());
    const auto resolve_report = assets.resolve_all();
    ASSERT_TRUE(resolve_report.ok());

    const MeshDerivedFieldAsset field{ .output = field_key };
    const auto handle =
        assets.mesh_derived_fields().get_mesh_derived_field(field);
    ASSERT_TRUE(handle.valid());
    const auto* data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(handle);
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->valid());
    EXPECT_EQ(data->domain, MeshDerivedFieldDomain::Vertex);
    EXPECT_EQ(data->element_count, 8u);
    ASSERT_EQ(data->channels.size(), 1u);
    EXPECT_EQ(data->channels[0].channel_id, kHeightChannel);
    EXPECT_EQ(
        data->channels[0].value_type,
        MeshDerivedFieldValueType::Float1);
    EXPECT_EQ(data->channels[0].byte_count, 8u * sizeof(float));

    const MeshAsset mesh{ .output = data->source_mesh_key };
    const auto* mesh_data =
        assets.meshes().get_mesh_data(assets.meshes().get_mesh(mesh));
    ASSERT_NE(mesh_data, nullptr);
    for (uint32_t i = 0; i < data->element_count; ++i) {
        float value = 0.0f;
        std::memcpy(
            &value,
            data->values.data() + i * sizeof(float),
            sizeof(float));
        EXPECT_FLOAT_EQ(value, mesh_data->vertices[i].position[1] * kScale);
    }

    // The compiled channel is resolvable for mesh_field_visualization.
    EXPECT_TRUE(
        assets.gpu_resident_fields().find(field_key, kHeightChannel)
            .valid());

    // Re-materializing the same scene is stable: the recipe maps to the
    // same asset key, so nothing rebuilds.
    const auto second_report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(second_report.ok) << second_report.error;
    EXPECT_EQ(
        scene.nodes[0].mesh_compute_field->field_asset,
        field_key);
}

TEST_F(
    SceneMeshComputeFieldMaterializeGpuFixture,
    DisabledMeshComputeFieldMaterializesNoFieldAsset)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_compute_field_disabled";
    SceneNodeAsset node = make_scene_node("field_mesh");
    attach_mesh_source(node, SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    });
    auto component = make_scaled_height_component(0x2000u, 1.0f);
    component.enabled = false;
    attach_mesh_compute_field(node, std::move(component));
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].mesh_compute_field.has_value());
    EXPECT_EQ(
        scene.nodes[0].mesh_compute_field->field_asset,
        wz::asset::AssetKey{});
}
