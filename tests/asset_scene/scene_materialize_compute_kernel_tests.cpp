#include "scene_authoring_materialize_test_support.h"

#include <engine/behavior/behavior_gpu_compute_executor.h>
#include <engine/assets/scene/scene_instance.h>
#include <window/window2.h>

namespace
{
    struct SceneComputeKernelMaterializeGpuFixture : public ::testing::Test
    {
        wz::Logger logger;
        wz::window::WindowHandle window{};
        wz::gpu::Device device{};
        wz::fs::Path root;

        void SetUp() override
        {
            root = wz::fs::join(
                wz::fs::temp_directory_path(),
                "wozzits_scene_materialize_compute_kernel_test");
            ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);
            ASSERT_EQ(
                wz::fs::create_directories(
                    wz::fs::join(root, "shaders/compute")),
                wz::fs::FileError::None);

            const wz::fs::Path shader_path = wz::fs::join(
                root,
                "shaders/compute/debug_multiply_u32_cs.hlsl");
            ASSERT_EQ(
                wz::fs::write_file_text(
                    shader_path,
                    "StructuredBuffer<uint> Input : register(t0);\n"
                    "RWStructuredBuffer<uint> Output : register(u0);\n"
                    "cbuffer Constants : register(b0) {\n"
                    "    uint Factor;\n"
                    "    uint Count;\n"
                    "};\n"
                    "[numthreads(64, 1, 1)]\n"
                    "void main(uint3 id : SV_DispatchThreadID) {\n"
                    "    if (id.x < Count) {\n"
                    "        Output[id.x] = Input[id.x] * Factor;\n"
                    "    }\n"
                    "}\n"),
                wz::fs::FileError::None);

            ASSERT_EQ(
                wz::fs::write_file_text(
                    wz::fs::join(
                        root,
                        "shaders/compute/publish_float_field_cs.hlsl"),
                    "StructuredBuffer<float> Input : register(t0);\n"
                    "RWStructuredBuffer<float> Output : register(u0);\n"
                    "cbuffer Constants : register(b0) {\n"
                    "    float Factor;\n"
                    "    uint Count;\n"
                    "};\n"
                    "[numthreads(64, 1, 1)]\n"
                    "void main(uint3 id : SV_DispatchThreadID) {\n"
                    "    if (id.x < Count) {\n"
                    "        Output[id.x] = saturate(Input[id.x] * Factor);\n"
                    "    }\n"
                    "}\n"),
                wz::fs::FileError::None);

            ASSERT_EQ(
                wz::fs::write_file_text(
                    wz::fs::join(
                        root,
                        "shaders/compute/publish_height_field_cs.hlsl"),
                    "StructuredBuffer<float3> Positions : register(t0);\n"
                    "RWStructuredBuffer<float> Output : register(u0);\n"
                    "cbuffer Constants : register(b0) {\n"
                    "    uint Count;\n"
                    "};\n"
                    "[numthreads(64, 1, 1)]\n"
                    "void main(uint3 id : SV_DispatchThreadID) {\n"
                    "    if (id.x < Count) {\n"
                    "        Output[id.x] =\n"
                    "            saturate(Positions[id.x].y * 0.5 + 0.5);\n"
                    "    }\n"
                    "}\n"),
                wz::fs::FileError::None);

            ASSERT_EQ(
                wz::fs::write_file_text(
                    wz::fs::join(
                        root,
                        "shaders/compute/publish_vertex_valence_cs.hlsl"),
                    "StructuredBuffer<uint> Indices : register(t0);\n"
                    "RWStructuredBuffer<float> Output : register(u0);\n"
                    "cbuffer Constants : register(b0) {\n"
                    "    uint TriangleCount;\n"
                    "    uint VertexCount;\n"
                    "};\n"
                    "[numthreads(64, 1, 1)]\n"
                    "void main(uint3 id : SV_DispatchThreadID) {\n"
                    "    if (id.x < VertexCount) {\n"
                    "        float valence = 0.0;\n"
                    "        for (uint t = 0; t < TriangleCount; ++t) {\n"
                    "            if (Indices[3 * t + 0] == id.x\n"
                    "                || Indices[3 * t + 1] == id.x\n"
                    "                || Indices[3 * t + 2] == id.x) {\n"
                    "                valence += 1.0;\n"
                    "            }\n"
                    "        }\n"
                    "        Output[id.x] = valence;\n"
                    "    }\n"
                    "}\n"),
                wz::fs::FileError::None);

            // Same valence kernel with a tiny thread group so dispatch-domain
            // tests on a quad (4 vertices, 6 indices) derive visibly
            // different group counts: VERTEX -> 2 groups, AUTO -> 3.
            ASSERT_EQ(
                wz::fs::write_file_text(
                    wz::fs::join(
                        root,
                        "shaders/compute/publish_vertex_valence_tg2_cs.hlsl"),
                    "StructuredBuffer<uint> Indices : register(t0);\n"
                    "RWStructuredBuffer<float> Output : register(u0);\n"
                    "cbuffer Constants : register(b0) {\n"
                    "    uint TriangleCount;\n"
                    "    uint VertexCount;\n"
                    "};\n"
                    "[numthreads(2, 1, 1)]\n"
                    "void main(uint3 id : SV_DispatchThreadID) {\n"
                    "    if (id.x < VertexCount) {\n"
                    "        float valence = 0.0;\n"
                    "        for (uint t = 0; t < TriangleCount; ++t) {\n"
                    "            if (Indices[3 * t + 0] == id.x\n"
                    "                || Indices[3 * t + 1] == id.x\n"
                    "                || Indices[3 * t + 2] == id.x) {\n"
                    "                valence += 1.0;\n"
                    "            }\n"
                    "        }\n"
                    "        Output[id.x] = valence;\n"
                    "    }\n"
                    "}\n"),
                wz::fs::FileError::None);

            wz::window::WindowDesc desc{};
            desc.title = "scene_compute_kernel_materialize_test";
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
        }
    };
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    ComputeKernelMaterializesShaderAndPipelineAssets)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "compute_kernel_materialize";

    SceneNodeAsset node = make_scene_node("kernel");
    node.compute_kernel = SceneComputeKernelAsset{
        .kernel_id = "project/debug_multiply_u32",
        .hlsl_path = "shaders/compute/debug_multiply_u32_cs.hlsl",
        .entry = "main",
        .target = "cs_5_0",
        .thread_group_size_x = 64,
        .thread_group_size_y = 1,
        .thread_group_size_z = 1,
        .ports = {
            SceneComputeKernelPortAsset{
                .name = "input",
                .kind = SceneComputeKernelPortKind::StructuredBuffer,
                .direction = SceneComputeKernelPortDirection::Input,
                .binding_kind = SceneComputeKernelBindingKind::SRV,
                .shader_register = 0,
                .register_space = 0,
                .stride_bytes = 4,
            },
            SceneComputeKernelPortAsset{
                .name = "output",
                .kind = SceneComputeKernelPortKind::StructuredBuffer,
                .direction = SceneComputeKernelPortDirection::Output,
                .binding_kind = SceneComputeKernelBindingKind::UAV,
                .shader_register = 0,
                .register_space = 0,
                .stride_bytes = 4,
            },
            SceneComputeKernelPortAsset{
                .name = "factor",
                .kind = SceneComputeKernelPortKind::U32,
                .direction = SceneComputeKernelPortDirection::Input,
                .root_constant_offset = 0,
                .root_constant_dwords = 1,
            },
            SceneComputeKernelPortAsset{
                .name = "count",
                .kind = SceneComputeKernelPortKind::U32,
                .direction = SceneComputeKernelPortDirection::Input,
                .root_constant_offset = 1,
                .root_constant_dwords = 1,
            },
        },
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_EQ(scene.nodes.size(), 1u);
    ASSERT_TRUE(scene.nodes[0].compute_kernel.has_value());

    const auto& kernel = *scene.nodes[0].compute_kernel;
    EXPECT_NE(kernel.compute_shader_asset, wz::asset::AssetKey{});
    EXPECT_NE(kernel.compute_pipeline_asset, wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    const auto resolve_report = assets.resolve_all();
    if (!resolve_report.ok()) {
        ADD_FAILURE() << "resolve_all failed with "
                      << resolve_report.failures.size() << " failure(s)";
        for (const auto& failure : resolve_report.failures) {
            ADD_FAILURE() << "  error=" << static_cast<int>(failure.error);
        }
    }
    ASSERT_TRUE(resolve_report.ok());

    const ComputeShaderHandle shader =
        assets.shaders().get_compute_shader(
            ComputeShaderAsset{ .shader = kernel.compute_shader_asset });
    ASSERT_TRUE(shader.valid());

    const auto pipeline_handle =
        assets.compute_pipelines().get_compute_pipeline(
            ComputePipelineAsset{ .key = kernel.compute_pipeline_asset });
    ASSERT_TRUE(pipeline_handle.valid());

    const ComputePipelineData* pipeline_data =
        assets.compute_pipelines().get_compute_pipeline_data(
            pipeline_handle);
    ASSERT_NE(pipeline_data, nullptr);

    EXPECT_EQ(pipeline_data->name, "project/debug_multiply_u32");
    EXPECT_EQ(pipeline_data->compute_shader.id, shader.shader.id);
    EXPECT_EQ(pipeline_data->compute_shader.epoch, shader.shader.epoch);
    EXPECT_EQ(pipeline_data->compute_shader.type, shader.shader.type);
    EXPECT_EQ(pipeline_data->root_constant_dwords, 2u);
    EXPECT_EQ(pipeline_data->thread_group_size_x, 64u);
    EXPECT_EQ(pipeline_data->thread_group_size_y, 1u);
    EXPECT_EQ(pipeline_data->thread_group_size_z, 1u);

    ASSERT_EQ(pipeline_data->bindings.size(), 2u);
    EXPECT_EQ(
        pipeline_data->bindings[0].kind,
        ComputeBindingKind::StructuredBufferSRV);
    EXPECT_EQ(
        pipeline_data->bindings[0].semantic,
        ComputeBindingSemantic::Unknown);
    EXPECT_EQ(pipeline_data->bindings[0].shader_register, 0u);
    EXPECT_EQ(pipeline_data->bindings[0].register_space, 0u);
    EXPECT_EQ(pipeline_data->bindings[0].descriptor_count, 1u);
    EXPECT_EQ(pipeline_data->bindings[0].stride_bytes, 4u);

    EXPECT_EQ(
        pipeline_data->bindings[1].kind,
        ComputeBindingKind::StructuredBufferUAV);
    EXPECT_EQ(
        pipeline_data->bindings[1].semantic,
        ComputeBindingSemantic::Unknown);
    EXPECT_EQ(pipeline_data->bindings[1].shader_register, 0u);
    EXPECT_EQ(pipeline_data->bindings[1].register_space, 0u);
    EXPECT_EQ(pipeline_data->bindings[1].descriptor_count, 1u);
    EXPECT_EQ(pipeline_data->bindings[1].stride_bytes, 4u);
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    ComputeKernelMaterializesDerivedShaderBindingsWithoutAuthoredPorts)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "compute_kernel_derived_materialize";

    SceneNodeAsset node = make_scene_node("kernel");
    node.compute_kernel = SceneComputeKernelAsset{
        .kernel_id = "project/debug_multiply_u32",
        .hlsl_path = "shaders/compute/debug_multiply_u32_cs.hlsl",
        .entry = "main",
        .target = "cs_5_0",
        .thread_group_size_x = 64,
        .thread_group_size_y = 1,
        .thread_group_size_z = 1,
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_EQ(scene.nodes.size(), 1u);
    ASSERT_TRUE(scene.nodes[0].compute_kernel.has_value());

    const auto& kernel = *scene.nodes[0].compute_kernel;
    EXPECT_TRUE(kernel.ports.empty());
    EXPECT_NE(kernel.compute_shader_asset, wz::asset::AssetKey{});
    EXPECT_NE(kernel.compute_pipeline_asset, wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    const auto resolve_report = assets.resolve_all();
    ASSERT_TRUE(resolve_report.ok());

    const auto pipeline_handle =
        assets.compute_pipelines().get_compute_pipeline(
            ComputePipelineAsset{ .key = kernel.compute_pipeline_asset });
    ASSERT_TRUE(pipeline_handle.valid());

    const ComputePipelineData* pipeline_data =
        assets.compute_pipelines().get_compute_pipeline_data(
            pipeline_handle);
    ASSERT_NE(pipeline_data, nullptr);

    EXPECT_EQ(pipeline_data->root_constant_dwords, 2u);
    ASSERT_EQ(pipeline_data->bindings.size(), 2u);
    EXPECT_EQ(
        pipeline_data->bindings[0].kind,
        ComputeBindingKind::StructuredBufferSRV);
    EXPECT_EQ(pipeline_data->bindings[0].stride_bytes, 4u);
    EXPECT_EQ(
        pipeline_data->bindings[1].kind,
        ComputeBindingKind::StructuredBufferUAV);
    EXPECT_EQ(pipeline_data->bindings[1].stride_bytes, 4u);
}

namespace
{
    // Shared setup for the behavior publish tests: a quad mesh, an explicit
    // Float1 vertex field acting as the publish target, the
    // publish_float_field kernel materialized from scene authoring, and the
    // kernel library built from the scene.
    struct BehaviorPublishSetup
    {
        wz::engine::assets::MeshDerivedFieldAsset field{};
        uint32_t vertex_count = 0u;
        uint32_t channel_id = 0u;
        wz::engine::behavior::BehaviorGpuKernelLibrary library{};
        bool ok = false;
        std::string error;
    };

    BehaviorPublishSetup build_behavior_publish_setup(
        wz::gpu::Device& device,
        wz::engine::assets::EngineAssetLibrary& assets)
    {
        using namespace wz::engine::assets;
        namespace behavior = wz::engine::behavior;

        BehaviorPublishSetup setup{};

        const MeshAsset mesh = assets.meshes().create_procedural_mesh({
            .name = "publish_field_mesh",
            .kind = ProceduralMeshKind::Quad,
        });
        if (!mesh.valid()) {
            setup.error = "mesh creation failed";
            return setup;
        }

        if (!assets.commit() || !assets.resolve_all().ok()) {
            setup.error = "mesh resolve failed";
            return setup;
        }

        const MeshHandle mesh_handle = assets.meshes().get_mesh(mesh);
        const MeshData* mesh_data =
            assets.meshes().get_mesh_data(mesh_handle);
        if (!mesh_data || mesh_data->vertex_count() == 0u) {
            setup.error = "mesh data unavailable";
            return setup;
        }
        setup.vertex_count = mesh_data->vertex_count();
        setup.channel_id = MeshWaveletChannelID::kDetailCost;

        std::vector<std::byte> zeroes(
            static_cast<size_t>(setup.vertex_count) * sizeof(float),
            std::byte{ 0 });
        setup.field =
            assets.mesh_derived_fields().create_explicit_field({
                .name = "behavior/published_field",
                .source_mesh = mesh,
                .domain = MeshDerivedFieldDomain::Vertex,
                .element_count = setup.vertex_count,
                .channels = {{
                    .channel_id = setup.channel_id,
                    .value_type = MeshDerivedFieldValueType::Float1,
                    .values = zeroes,
                }},
            });
        if (!setup.field.valid()) {
            setup.error = "explicit field creation failed";
            return setup;
        }

        SceneAssetData scene{};
        scene.name = "behavior_publish_field";
        SceneNodeAsset node = make_scene_node("kernel");
        node.compute_kernel = SceneComputeKernelAsset{
            .kernel_id = "project/publish_float_field",
            .hlsl_path = "shaders/compute/publish_float_field_cs.hlsl",
            .entry = "main",
            .target = "cs_5_0",
            .thread_group_size_x = 64,
            .thread_group_size_y = 1,
            .thread_group_size_z = 1,
            .ports = {
                SceneComputeKernelPortAsset{
                    .name = "input",
                    .kind = SceneComputeKernelPortKind::StructuredBuffer,
                    .direction = SceneComputeKernelPortDirection::Input,
                    .binding_kind = SceneComputeKernelBindingKind::SRV,
                    .shader_register = 0,
                    .register_space = 0,
                    .stride_bytes = 4,
                },
                SceneComputeKernelPortAsset{
                    .name = "output",
                    .kind = SceneComputeKernelPortKind::StructuredBuffer,
                    .direction = SceneComputeKernelPortDirection::Output,
                    .binding_kind = SceneComputeKernelBindingKind::UAV,
                    .shader_register = 0,
                    .register_space = 0,
                    .stride_bytes = 4,
                },
                SceneComputeKernelPortAsset{
                    .name = "factor",
                    .kind = SceneComputeKernelPortKind::F32,
                    .direction = SceneComputeKernelPortDirection::Input,
                    .root_constant_offset = 0,
                    .root_constant_dwords = 1,
                },
                SceneComputeKernelPortAsset{
                    .name = "count",
                    .kind = SceneComputeKernelPortKind::U32,
                    .direction = SceneComputeKernelPortDirection::Input,
                    .root_constant_offset = 1,
                    .root_constant_dwords = 1,
                },
            },
        };
        scene.nodes.push_back(std::move(node));

        const auto materialize_report =
            materialize_scene_authoring_components(scene, assets);
        if (!materialize_report.ok) {
            setup.error = materialize_report.error;
            return setup;
        }

        if (!assets.commit() || !assets.resolve_all().ok()) {
            setup.error = "kernel resolve failed";
            return setup;
        }

        std::string kernel_error;
        if (!behavior::build_kernel_library_from_scene(
                device,
                scene,
                assets,
                setup.library,
                &kernel_error))
        {
            setup.error = kernel_error;
            return setup;
        }

        setup.ok = true;
        return setup;
    }

    // Builds the publish job. The input vector must outlive the dispatch;
    // the output port's element count follows the input size so tests can
    // produce deliberate mismatches against the target field.
    wz::engine::behavior::BehaviorGpuComputeJob make_publish_job(
        const std::vector<float>& input,
        uint32_t channel_id)
    {
        namespace behavior = wz::engine::behavior;

        const uint32_t element_count =
            static_cast<uint32_t>(input.size());

        behavior::BehaviorGpuComputeJob job{};
        job.work.value = 1u;
        job.entity = 0;
        job.kernel = "project/publish_float_field";
        job.group_count_x = (element_count + 63u) / 64u;
        job.group_count_y = 1u;
        job.group_count_z = 1u;
        job.ports = {
            behavior::BehaviorGpuPortValue{
                .name = "input",
                .kind = WZ_GPU_PORT_STRUCTURED_BUFFER,
                .direction = WZ_GPU_PORT_INPUT,
                .element_count = element_count,
                .stride_bytes = sizeof(float),
                .initial_data = {
                    reinterpret_cast<const std::byte*>(input.data()),
                    reinterpret_cast<const std::byte*>(input.data())
                        + input.size() * sizeof(float),
                },
            },
            behavior::BehaviorGpuPortValue{
                .name = "output",
                .kind = WZ_GPU_PORT_STRUCTURED_BUFFER,
                .direction = WZ_GPU_PORT_OUTPUT,
                .element_count = element_count,
                .stride_bytes = sizeof(float),
                .resource = WzGpuResourceRef{
                    .value = WZ_GPU_RESOURCE_REF_MESH_FIELD_VISUALIZATION,
                },
                .u32 = { channel_id, 0u, 0u, 0u },
            },
            behavior::BehaviorGpuPortValue{
                .name = "factor",
                .kind = WZ_GPU_PORT_F32,
                .direction = WZ_GPU_PORT_INPUT,
                .f32 = { 1.5f, 0.0f, 0.0f, 0.0f },
            },
            behavior::BehaviorGpuPortValue{
                .name = "count",
                .kind = WZ_GPU_PORT_U32,
                .direction = WZ_GPU_PORT_INPUT,
                .u32 = { element_count, 0u, 0u, 0u },
            },
        };
        return job;
    }
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    BehaviorComputeOutputPublishesMeshFieldVisualization)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };
    BehaviorPublishSetup setup =
        build_behavior_publish_setup(device, assets);
    ASSERT_TRUE(setup.ok) << setup.error;

    wz::engine::assets::SceneInstance instance{};
    instance.mesh_field_visualization_targets.push_back({
        .node = 0,
        .component = MeshFieldVisualizationTargetComponent{
            .field_asset = setup.field.output,
            .channel_id = setup.channel_id,
        },
    });

    const std::vector<float> input(setup.vertex_count, 0.5f);
    const behavior::BehaviorGpuComputeJob job =
        make_publish_job(input, setup.channel_id);

    const auto dispatch_report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            setup.library);
    EXPECT_EQ(dispatch_report.submitted, 1u);
    EXPECT_EQ(dispatch_report.dispatched, 1u);
    EXPECT_EQ(dispatch_report.failed, 0u);
    EXPECT_EQ(dispatch_report.published_mesh_fields, 1u);
    EXPECT_TRUE(dispatch_report.publish_failures.empty());

    const wz::gpu::GPUHandle first_handle =
        assets.gpu_resident_fields().find(
            setup.field.output,
            setup.channel_id);
    EXPECT_TRUE(first_handle.valid());

    // Published outputs stay GPU-resident; no CPU readback is produced for
    // the published port.
    EXPECT_TRUE(dispatch_report.readbacks.empty());

    // A second frame's publish must refresh the resident field in place:
    // same handle (so resolver-captured handles stay valid), no failure,
    // no leak.
    const auto second_report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            setup.library);
    EXPECT_EQ(second_report.dispatched, 1u);
    EXPECT_EQ(second_report.published_mesh_fields, 1u);
    EXPECT_TRUE(second_report.publish_failures.empty());

    const wz::gpu::GPUHandle second_handle =
        assets.gpu_resident_fields().find(
            setup.field.output,
            setup.channel_id);
    EXPECT_TRUE(second_handle.valid());
    EXPECT_EQ(first_handle.id, second_handle.id);
    EXPECT_EQ(first_handle.epoch, second_handle.epoch);

    (void)behavior::release_behavior_gpu_kernel_library(
        device,
        setup.library);
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    BehaviorComputePublishElementCountMismatchFailsWithDiagnostic)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };
    BehaviorPublishSetup setup =
        build_behavior_publish_setup(device, assets);
    ASSERT_TRUE(setup.ok) << setup.error;

    wz::engine::assets::SceneInstance instance{};
    instance.mesh_field_visualization_targets.push_back({
        .node = 0,
        .component = MeshFieldVisualizationTargetComponent{
            .field_asset = setup.field.output,
            .channel_id = setup.channel_id,
        },
    });

    // Output is larger than the target field's element count.
    const std::vector<float> input(setup.vertex_count + 64u, 0.5f);
    const behavior::BehaviorGpuComputeJob job =
        make_publish_job(input, setup.channel_id);

    const auto report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            setup.library);
    EXPECT_EQ(report.dispatched, 1u);
    EXPECT_EQ(report.published_mesh_fields, 0u);
    ASSERT_EQ(report.publish_failures.size(), 1u);
    EXPECT_EQ(report.publish_failures[0].port_name, "output");
    EXPECT_NE(
        report.publish_failures[0].reason.find(
            "does not match field element count"),
        std::string::npos)
        << report.publish_failures[0].reason;

    // Nothing was registered, and the failed publish falls back to the
    // normal CPU readback path.
    EXPECT_FALSE(
        assets.gpu_resident_fields()
            .find(setup.field.output, setup.channel_id)
            .valid());
    ASSERT_EQ(report.readbacks.size(), 1u);
    EXPECT_EQ(report.readbacks[0].port_name, "output");

    (void)behavior::release_behavior_gpu_kernel_library(
        device,
        setup.library);
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    BehaviorComputeEngineResolvedMeshPortsPublishWithoutPluginData)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };

    const MeshAsset mesh = assets.meshes().create_procedural_mesh({
        .name = "height_field_mesh",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const MeshHandle mesh_handle = assets.meshes().get_mesh(mesh);
    const MeshData* mesh_data = assets.meshes().get_mesh_data(mesh_handle);
    ASSERT_NE(mesh_data, nullptr);
    const uint32_t vertex_count = mesh_data->vertex_count();
    ASSERT_GT(vertex_count, 0u);

    const uint32_t channel_id = MeshWaveletChannelID::kDetailCost;
    std::vector<std::byte> zeroes(
        static_cast<size_t>(vertex_count) * sizeof(float),
        std::byte{ 0 });
    const MeshDerivedFieldAsset field =
        assets.mesh_derived_fields().create_explicit_field({
            .name = "behavior/height_field",
            .source_mesh = mesh,
            .domain = MeshDerivedFieldDomain::Vertex,
            .element_count = vertex_count,
            .channels = {{
                .channel_id = channel_id,
                .value_type = MeshDerivedFieldValueType::Float1,
                .values = zeroes,
            }},
        });
    ASSERT_TRUE(field.valid());

    SceneAssetData scene{};
    scene.name = "behavior_height_field";
    SceneNodeAsset node = make_scene_node("kernel");
    node.compute_kernel = SceneComputeKernelAsset{
        .kernel_id = "project/publish_height_field",
        .hlsl_path = "shaders/compute/publish_height_field_cs.hlsl",
        .entry = "main",
        .target = "cs_5_0",
        .thread_group_size_x = 64,
        .thread_group_size_y = 1,
        .thread_group_size_z = 1,
        .ports = {
            SceneComputeKernelPortAsset{
                .name = "positions",
                .kind = SceneComputeKernelPortKind::StructuredBuffer,
                .direction = SceneComputeKernelPortDirection::Input,
                .binding_kind = SceneComputeKernelBindingKind::SRV,
                .shader_register = 0,
                .register_space = 0,
                .stride_bytes = 12,
            },
            SceneComputeKernelPortAsset{
                .name = "output",
                .kind = SceneComputeKernelPortKind::StructuredBuffer,
                .direction = SceneComputeKernelPortDirection::Output,
                .binding_kind = SceneComputeKernelBindingKind::UAV,
                .shader_register = 0,
                .register_space = 0,
                .stride_bytes = 4,
            },
            SceneComputeKernelPortAsset{
                .name = "count",
                .kind = SceneComputeKernelPortKind::U32,
                .direction = SceneComputeKernelPortDirection::Input,
                .root_constant_offset = 0,
                .root_constant_dwords = 1,
            },
        },
    };
    scene.nodes.push_back(std::move(node));

    const auto materialize_report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(materialize_report.ok) << materialize_report.error;
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    behavior::BehaviorGpuKernelLibrary library{};
    std::string kernel_error;
    ASSERT_TRUE(
        behavior::build_kernel_library_from_scene(
            device,
            scene,
            assets,
            library,
            &kernel_error))
        << kernel_error;

    wz::engine::assets::SceneInstance instance{};
    instance.mesh_field_visualization_targets.push_back({
        .node = 0,
        .component = MeshFieldVisualizationTargetComponent{
            .field_asset = field.output,
            .channel_id = channel_id,
        },
    });

    // The plugin supplies no data, no counts, and no group sizes: positions,
    // the output size, the vertex-count constant, and the dispatch group
    // count are all resolved by the engine from the entity's mesh.
    behavior::BehaviorGpuComputeJob job{};
    job.work.value = 1u;
    job.entity = 0;
    job.kernel = "project/publish_height_field";
    job.group_count_x = 0u;
    job.group_count_y = 0u;
    job.group_count_z = 0u;
    job.ports = {
        behavior::BehaviorGpuPortValue{
            .name = "positions",
            .kind = WZ_GPU_PORT_STRUCTURED_BUFFER,
            .direction = WZ_GPU_PORT_INPUT,
            .element_count = 0u,
            .stride_bytes = 12u,
            .resource = WzGpuResourceRef{
                .value = WZ_GPU_RESOURCE_REF_MESH_VERTEX_POSITIONS,
            },
        },
        behavior::BehaviorGpuPortValue{
            .name = "output",
            .kind = WZ_GPU_PORT_STRUCTURED_BUFFER,
            .direction = WZ_GPU_PORT_OUTPUT,
            .element_count = 0u,
            .stride_bytes = sizeof(float),
            .resource = WzGpuResourceRef{
                .value = WZ_GPU_RESOURCE_REF_MESH_FIELD_VISUALIZATION,
            },
            .u32 = { channel_id, 0u, 0u, 0u },
        },
        behavior::BehaviorGpuPortValue{
            .name = "count",
            .kind = WZ_GPU_PORT_U32,
            .direction = WZ_GPU_PORT_INPUT,
            .resource = WzGpuResourceRef{
                .value = WZ_GPU_RESOURCE_REF_MESH_VERTEX_COUNT,
            },
        },
    };

    const auto report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            library);
    EXPECT_EQ(report.submitted, 1u);
    EXPECT_EQ(report.dispatched, 1u);
    EXPECT_EQ(report.failed, 0u);
    EXPECT_EQ(report.published_mesh_fields, 1u);
    EXPECT_TRUE(report.publish_failures.empty());
    for (const auto& failure : report.publish_failures) {
        ADD_FAILURE() << failure.port_name << ": " << failure.reason;
    }
    EXPECT_TRUE(
        assets.gpu_resident_fields().find(field.output, channel_id).valid());

    (void)behavior::release_behavior_gpu_kernel_library(device, library);
}

namespace
{
    // Shared setup for the index-exposure tests: a quad mesh with an
    // explicit Float1 vertex field as the publish target, plus the
    // vertex-valence kernel that consumes the engine-bound index buffer
    // and triangle/vertex count constants.
    struct IndexPortSetup
    {
        wz::engine::assets::MeshDerivedFieldAsset field{};
        wz::asset::AssetKey mesh_key{};
        uint32_t channel_id = 0u;
        wz::engine::behavior::BehaviorGpuKernelLibrary library{};
        bool ok = false;
        std::string error;
    };

    IndexPortSetup build_index_port_setup(
        wz::gpu::Device& device,
        wz::engine::assets::EngineAssetLibrary& assets,
        uint32_t thread_group_size = 64u)
    {
        using namespace wz::engine::assets;
        namespace behavior = wz::engine::behavior;

        IndexPortSetup setup{};
        const bool tg2 = thread_group_size == 2u;
        const char* kernel_id = tg2
            ? "project/publish_vertex_valence_tg2"
            : "project/publish_vertex_valence";
        const char* hlsl_path = tg2
            ? "shaders/compute/publish_vertex_valence_tg2_cs.hlsl"
            : "shaders/compute/publish_vertex_valence_cs.hlsl";

        const MeshAsset mesh = assets.meshes().create_procedural_mesh({
            .name = "valence_mesh",
            .kind = ProceduralMeshKind::Quad,
        });
        if (!mesh.valid()) {
            setup.error = "mesh creation failed";
            return setup;
        }
        if (!assets.commit() || !assets.resolve_all().ok()) {
            setup.error = "mesh resolve failed";
            return setup;
        }

        const MeshData* mesh_data =
            assets.meshes().get_mesh_data(assets.meshes().get_mesh(mesh));
        if (!mesh_data || mesh_data->vertex_count() == 0u) {
            setup.error = "mesh data unavailable";
            return setup;
        }

        setup.mesh_key = mesh.output;
        setup.channel_id = MeshWaveletChannelID::kDetailCost;
        std::vector<std::byte> zeroes(
            static_cast<size_t>(mesh_data->vertex_count()) * sizeof(float),
            std::byte{ 0 });
        setup.field = assets.mesh_derived_fields().create_explicit_field({
            .name = "behavior/valence_field",
            .source_mesh = mesh,
            .domain = MeshDerivedFieldDomain::Vertex,
            .element_count = mesh_data->vertex_count(),
            .channels = {{
                .channel_id = setup.channel_id,
                .value_type = MeshDerivedFieldValueType::Float1,
                .values = zeroes,
            }},
        });
        if (!setup.field.valid()) {
            setup.error = "explicit field creation failed";
            return setup;
        }

        SceneAssetData scene{};
        scene.name = "behavior_vertex_valence";
        SceneNodeAsset node = make_scene_node("kernel");
        node.compute_kernel = SceneComputeKernelAsset{
            .kernel_id = kernel_id,
            .hlsl_path = hlsl_path,
            .entry = "main",
            .target = "cs_5_0",
            .thread_group_size_x = thread_group_size,
            .thread_group_size_y = 1,
            .thread_group_size_z = 1,
            .ports = {
                SceneComputeKernelPortAsset{
                    .name = "indices",
                    .kind = SceneComputeKernelPortKind::StructuredBuffer,
                    .direction = SceneComputeKernelPortDirection::Input,
                    .binding_kind = SceneComputeKernelBindingKind::SRV,
                    .shader_register = 0,
                    .register_space = 0,
                    .stride_bytes = 4,
                },
                SceneComputeKernelPortAsset{
                    .name = "output",
                    .kind = SceneComputeKernelPortKind::StructuredBuffer,
                    .direction = SceneComputeKernelPortDirection::Output,
                    .binding_kind = SceneComputeKernelBindingKind::UAV,
                    .shader_register = 0,
                    .register_space = 0,
                    .stride_bytes = 4,
                },
                SceneComputeKernelPortAsset{
                    .name = "triangle_count",
                    .kind = SceneComputeKernelPortKind::U32,
                    .direction = SceneComputeKernelPortDirection::Input,
                    .root_constant_offset = 0,
                    .root_constant_dwords = 1,
                },
                SceneComputeKernelPortAsset{
                    .name = "vertex_count",
                    .kind = SceneComputeKernelPortKind::U32,
                    .direction = SceneComputeKernelPortDirection::Input,
                    .root_constant_offset = 1,
                    .root_constant_dwords = 1,
                },
            },
        };
        scene.nodes.push_back(std::move(node));

        const auto materialize_report =
            materialize_scene_authoring_components(scene, assets);
        if (!materialize_report.ok) {
            setup.error = materialize_report.error;
            return setup;
        }
        if (!assets.commit() || !assets.resolve_all().ok()) {
            setup.error = "kernel resolve failed";
            return setup;
        }

        std::string kernel_error;
        if (!behavior::build_kernel_library_from_scene(
                device,
                scene,
                assets,
                setup.library,
                &kernel_error))
        {
            setup.error = kernel_error;
            return setup;
        }

        setup.ok = true;
        return setup;
    }

    // The plugin supplies no data, no counts, and no group sizes: the index
    // buffer, both count constants, and the dispatch group count are all
    // resolved by the engine from the entity's mesh.
    wz::engine::behavior::BehaviorGpuComputeJob make_valence_job(
        uint32_t channel_id,
        const char* kernel = "project/publish_vertex_valence")
    {
        namespace behavior = wz::engine::behavior;

        behavior::BehaviorGpuComputeJob job{};
        job.work.value = 1u;
        job.entity = 0;
        job.kernel = kernel;
        job.group_count_x = 0u;
        job.group_count_y = 0u;
        job.group_count_z = 0u;
        job.ports = {
            behavior::BehaviorGpuPortValue{
                .name = "indices",
                .kind = WZ_GPU_PORT_STRUCTURED_BUFFER,
                .direction = WZ_GPU_PORT_INPUT,
                .element_count = 0u,
                .stride_bytes = sizeof(uint32_t),
                .resource = WzGpuResourceRef{
                    .value = WZ_GPU_RESOURCE_REF_MESH_INDICES,
                },
            },
            behavior::BehaviorGpuPortValue{
                .name = "output",
                .kind = WZ_GPU_PORT_STRUCTURED_BUFFER,
                .direction = WZ_GPU_PORT_OUTPUT,
                .element_count = 0u,
                .stride_bytes = sizeof(float),
                .resource = WzGpuResourceRef{
                    .value = WZ_GPU_RESOURCE_REF_MESH_FIELD_VISUALIZATION,
                },
                .u32 = { channel_id, 0u, 0u, 0u },
            },
            behavior::BehaviorGpuPortValue{
                .name = "triangle_count",
                .kind = WZ_GPU_PORT_U32,
                .direction = WZ_GPU_PORT_INPUT,
                .resource = WzGpuResourceRef{
                    .value = WZ_GPU_RESOURCE_REF_MESH_TRIANGLE_COUNT,
                },
            },
            behavior::BehaviorGpuPortValue{
                .name = "vertex_count",
                .kind = WZ_GPU_PORT_U32,
                .direction = WZ_GPU_PORT_INPUT,
                .resource = WzGpuResourceRef{
                    .value = WZ_GPU_RESOURCE_REF_MESH_VERTEX_COUNT,
                },
            },
        };
        return job;
    }
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    BehaviorComputeEngineResolvedIndexPortsPublishWithoutPluginData)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };
    IndexPortSetup setup = build_index_port_setup(device, assets);
    ASSERT_TRUE(setup.ok) << setup.error;

    wz::engine::assets::SceneInstance instance{};
    instance.mesh_field_visualization_targets.push_back({
        .node = 0,
        .component = MeshFieldVisualizationTargetComponent{
            .field_asset = setup.field.output,
            .channel_id = setup.channel_id,
        },
    });

    const behavior::BehaviorGpuComputeJob job =
        make_valence_job(setup.channel_id);
    const auto report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            setup.library);
    EXPECT_EQ(report.submitted, 1u);
    EXPECT_EQ(report.dispatched, 1u);
    EXPECT_EQ(report.failed, 0u);
    EXPECT_EQ(report.published_mesh_fields, 1u);
    for (const auto& failure : report.publish_failures) {
        ADD_FAILURE() << failure.port_name << ": " << failure.reason;
    }
    EXPECT_TRUE(
        assets.gpu_resident_fields()
            .find(setup.field.output, setup.channel_id)
            .valid());

    (void)behavior::release_behavior_gpu_kernel_library(
        device,
        setup.library);
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    BehaviorComputeReusesResidentMeshBuffersAcrossDispatches)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };
    IndexPortSetup setup = build_index_port_setup(device, assets);
    ASSERT_TRUE(setup.ok) << setup.error;

    wz::engine::assets::SceneInstance instance{};
    instance.mesh_field_visualization_targets.push_back({
        .node = 0,
        .component = MeshFieldVisualizationTargetComponent{
            .field_asset = setup.field.output,
            .channel_id = setup.channel_id,
        },
    });

    EXPECT_EQ(assets.gpu_resident_mesh_data().size(), 0u);

    const behavior::BehaviorGpuComputeJob job =
        make_valence_job(setup.channel_id);
    const auto first_report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            setup.library);
    EXPECT_EQ(first_report.dispatched, 1u);
    EXPECT_EQ(first_report.published_mesh_fields, 1u);

    // The first dispatch uploaded the index buffer once and registered it
    // under the source mesh key.
    ASSERT_EQ(assets.gpu_resident_mesh_data().size(), 1u);
    const auto* entry =
        assets.gpu_resident_mesh_data().find(setup.mesh_key);
    ASSERT_NE(entry, nullptr);
    ASSERT_TRUE(entry->indices.valid());
    EXPECT_EQ(entry->index_count, 6u);
    EXPECT_EQ(entry->triangle_count, 2u);
    const wz::gpu::GPUHandle first_indices = entry->indices;

    // The second dispatch binds the same resident buffer: no re-upload, no
    // new table entry, identical handle.
    const auto second_report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            setup.library);
    EXPECT_EQ(second_report.dispatched, 1u);
    EXPECT_EQ(second_report.published_mesh_fields, 1u);
    EXPECT_EQ(assets.gpu_resident_mesh_data().size(), 1u);
    const auto* second_entry =
        assets.gpu_resident_mesh_data().find(setup.mesh_key);
    ASSERT_NE(second_entry, nullptr);
    EXPECT_EQ(second_entry->indices, first_indices);

    (void)behavior::release_behavior_gpu_kernel_library(
        device,
        setup.library);
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    BehaviorComputeMeshIndicesWithoutMeshFailsWithDiagnostic)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };
    IndexPortSetup setup = build_index_port_setup(device, assets);
    ASSERT_TRUE(setup.ok) << setup.error;

    // No mesh field visualization targets registered for the entity, so
    // the engine cannot resolve a mesh for the index buffer.
    wz::engine::assets::SceneInstance instance{};

    const behavior::BehaviorGpuComputeJob job =
        make_valence_job(setup.channel_id);
    const auto report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            setup.library);
    EXPECT_EQ(report.dispatched, 0u);
    EXPECT_EQ(report.failed, 1u);
    EXPECT_EQ(report.published_mesh_fields, 0u);
    ASSERT_FALSE(report.publish_failures.empty());
    EXPECT_NE(
        report.publish_failures[0].reason.find("unavailable"),
        std::string::npos)
        << report.publish_failures[0].reason;
    EXPECT_NE(
        report.publish_failures[0].reason.find(
            "no resolvable mesh field visualization target"),
        std::string::npos)
        << report.publish_failures[0].reason;
    EXPECT_FALSE(
        assets.gpu_resident_fields()
            .find(setup.field.output, setup.channel_id)
            .valid());

    (void)behavior::release_behavior_gpu_kernel_library(
        device,
        setup.library);
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    BehaviorComputeVertexDispatchDomainAvoidsIndexOverDispatch)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };
    IndexPortSetup setup = build_index_port_setup(device, assets, 2u);
    ASSERT_TRUE(setup.ok) << setup.error;

    wz::engine::assets::SceneInstance instance{};
    instance.mesh_field_visualization_targets.push_back({
        .node = 0,
        .component = MeshFieldVisualizationTargetComponent{
            .field_asset = setup.field.output,
            .channel_id = setup.channel_id,
        },
    });

    // A vertex-domain kernel that reads indices: VERTEX derives the group
    // count from the 4 vertices. Topology ports deliberately do not feed
    // the legacy AUTO derivation, so AUTO resolves the same vertex-sized
    // counts (vertex-count constant, publish target) and agrees — reading
    // the 6 indices must not inflate the dispatch.
    behavior::BehaviorGpuComputeJob vertex_job = make_valence_job(
        setup.channel_id,
        "project/publish_vertex_valence_tg2");
    vertex_job.dispatch_domain = WZ_GPU_DISPATCH_DOMAIN_VERTEX;
    const auto vertex_report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{
                &vertex_job, 1u },
            setup.library);
    EXPECT_EQ(vertex_report.dispatched, 1u);
    EXPECT_EQ(vertex_report.published_mesh_fields, 1u);
    ASSERT_EQ(vertex_report.derived_dispatches.size(), 1u);
    EXPECT_EQ(
        vertex_report.derived_dispatches[0].dispatch_domain,
        WZ_GPU_DISPATCH_DOMAIN_VERTEX);
    EXPECT_EQ(vertex_report.derived_dispatches[0].element_count, 4u);
    EXPECT_EQ(vertex_report.derived_dispatches[0].group_count_x, 2u);

    behavior::BehaviorGpuComputeJob auto_job = make_valence_job(
        setup.channel_id,
        "project/publish_vertex_valence_tg2");
    auto_job.dispatch_domain = WZ_GPU_DISPATCH_DOMAIN_AUTO;
    const auto auto_report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{
                &auto_job, 1u },
            setup.library);
    EXPECT_EQ(auto_report.dispatched, 1u);
    ASSERT_EQ(auto_report.derived_dispatches.size(), 1u);
    EXPECT_EQ(auto_report.derived_dispatches[0].element_count, 4u);
    EXPECT_EQ(auto_report.derived_dispatches[0].group_count_x, 2u);

    (void)behavior::release_behavior_gpu_kernel_library(
        device,
        setup.library);
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    BehaviorComputeTopologyOnlyAutoFailsAndFaceDomainResolves)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };
    IndexPortSetup setup = build_index_port_setup(device, assets);
    ASSERT_TRUE(setup.ok) << setup.error;

    wz::engine::assets::SceneInstance instance{};
    instance.mesh_field_visualization_targets.push_back({
        .node = 0,
        .component = MeshFieldVisualizationTargetComponent{
            .field_asset = setup.field.output,
            .channel_id = setup.channel_id,
        },
    });

    // Only topology is engine-resolved: the output is a plain plugin-sized
    // port and the vertex count is a plain authored constant, so nothing
    // feeds AUTO and the job must declare its iteration domain.
    const auto make_topology_job = [&]()
    {
        behavior::BehaviorGpuComputeJob job =
            make_valence_job(setup.channel_id);
        job.ports[1].resource = WzGpuResourceRef{};
        job.ports[1].element_count = 4u;
        job.ports[3].resource = WzGpuResourceRef{};
        job.ports[3].u32[0] = 4u;
        return job;
    };

    behavior::BehaviorGpuComputeJob auto_job = make_topology_job();
    auto_job.dispatch_domain = WZ_GPU_DISPATCH_DOMAIN_AUTO;
    const auto auto_report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{
                &auto_job, 1u },
            setup.library);
    EXPECT_EQ(auto_report.dispatched, 0u);
    EXPECT_EQ(auto_report.failed, 1u);
    ASSERT_EQ(auto_report.publish_failures.size(), 1u);
    EXPECT_EQ(auto_report.publish_failures[0].port_name, "dispatch_domain");
    EXPECT_NE(
        auto_report.publish_failures[0].reason.find(
            "dispatch group count unresolved"),
        std::string::npos)
        << auto_report.publish_failures[0].reason;

    behavior::BehaviorGpuComputeJob face_job = make_topology_job();
    face_job.dispatch_domain = WZ_GPU_DISPATCH_DOMAIN_FACE;
    const auto face_report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{
                &face_job, 1u },
            setup.library);
    EXPECT_EQ(face_report.dispatched, 1u);
    EXPECT_EQ(face_report.failed, 0u);
    ASSERT_EQ(face_report.derived_dispatches.size(), 1u);
    EXPECT_EQ(
        face_report.derived_dispatches[0].dispatch_domain,
        WZ_GPU_DISPATCH_DOMAIN_FACE);
    EXPECT_EQ(face_report.derived_dispatches[0].element_count, 2u);

    ASSERT_EQ(face_report.readbacks.size(), 1u);
    ASSERT_EQ(face_report.readbacks[0].bytes.size(), 4u * sizeof(float));
    float values[4]{};
    std::memcpy(
        values,
        face_report.readbacks[0].bytes.data(),
        sizeof(values));
    EXPECT_FLOAT_EQ(values[0], 2.0f);
    EXPECT_FLOAT_EQ(values[1], 1.0f);
    EXPECT_FLOAT_EQ(values[2], 2.0f);
    EXPECT_FLOAT_EQ(values[3], 1.0f);

    (void)behavior::release_behavior_gpu_kernel_library(
        device,
        setup.library);
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    BehaviorComputeEdgeDispatchDomainIsReservedAndFails)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };
    IndexPortSetup setup = build_index_port_setup(device, assets);
    ASSERT_TRUE(setup.ok) << setup.error;

    wz::engine::assets::SceneInstance instance{};
    instance.mesh_field_visualization_targets.push_back({
        .node = 0,
        .component = MeshFieldVisualizationTargetComponent{
            .field_asset = setup.field.output,
            .channel_id = setup.channel_id,
        },
    });

    behavior::BehaviorGpuComputeJob job =
        make_valence_job(setup.channel_id);
    job.dispatch_domain = WZ_GPU_DISPATCH_DOMAIN_EDGE;
    const auto report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            setup.library);
    EXPECT_EQ(report.dispatched, 0u);
    EXPECT_EQ(report.failed, 1u);
    ASSERT_EQ(report.publish_failures.size(), 1u);
    EXPECT_EQ(report.publish_failures[0].port_name, "dispatch_domain");
    EXPECT_NE(
        report.publish_failures[0].reason.find("reserved"),
        std::string::npos)
        << report.publish_failures[0].reason;
    EXPECT_NE(
        report.publish_failures[0].reason.find("resident mesh topology"),
        std::string::npos)
        << report.publish_failures[0].reason;

    (void)behavior::release_behavior_gpu_kernel_library(
        device,
        setup.library);
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    BehaviorComputeOutputDispatchDomainReadsBackExactValenceValues)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };
    IndexPortSetup setup = build_index_port_setup(device, assets);
    ASSERT_TRUE(setup.ok) << setup.error;

    wz::engine::assets::SceneInstance instance{};
    instance.mesh_field_visualization_targets.push_back({
        .node = 0,
        .component = MeshFieldVisualizationTargetComponent{
            .field_asset = setup.field.output,
            .channel_id = setup.channel_id,
        },
    });

    // Plain (non-publish) output port sized by the plugin: OUTPUT domain
    // derives the group count from it, and the executor's readback path
    // returns the bytes so the test can validate actual kernel output, not
    // just successful publication.
    behavior::BehaviorGpuComputeJob job =
        make_valence_job(setup.channel_id);
    job.dispatch_domain = WZ_GPU_DISPATCH_DOMAIN_OUTPUT;
    job.ports[1].resource = WzGpuResourceRef{};
    job.ports[1].element_count = 4u;

    const auto report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            setup.library);
    EXPECT_EQ(report.dispatched, 1u);
    EXPECT_EQ(report.failed, 0u);
    EXPECT_EQ(report.published_mesh_fields, 0u);
    ASSERT_EQ(report.derived_dispatches.size(), 1u);
    EXPECT_EQ(
        report.derived_dispatches[0].dispatch_domain,
        WZ_GPU_DISPATCH_DOMAIN_OUTPUT);
    EXPECT_EQ(report.derived_dispatches[0].element_count, 4u);

    // Quad triangle list {0,1,2, 0,2,3}: vertices 0 and 2 touch both
    // triangles, vertices 1 and 3 touch one each.
    ASSERT_EQ(report.readbacks.size(), 1u);
    const auto& readback = report.readbacks[0];
    EXPECT_EQ(readback.port_name, "output");
    ASSERT_EQ(readback.bytes.size(), 4u * sizeof(float));
    float values[4]{};
    std::memcpy(values, readback.bytes.data(), sizeof(values));
    EXPECT_FLOAT_EQ(values[0], 2.0f);
    EXPECT_FLOAT_EQ(values[1], 1.0f);
    EXPECT_FLOAT_EQ(values[2], 2.0f);
    EXPECT_FLOAT_EQ(values[3], 1.0f);

    (void)behavior::release_behavior_gpu_kernel_library(
        device,
        setup.library);
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    BehaviorComputePublishWithoutTargetFailsWithDiagnostic)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };
    BehaviorPublishSetup setup =
        build_behavior_publish_setup(device, assets);
    ASSERT_TRUE(setup.ok) << setup.error;

    // No mesh field visualization targets registered for the entity.
    wz::engine::assets::SceneInstance instance{};

    const std::vector<float> input(setup.vertex_count, 0.5f);
    const behavior::BehaviorGpuComputeJob job =
        make_publish_job(input, setup.channel_id);

    const auto report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            setup.library);
    EXPECT_EQ(report.dispatched, 1u);
    EXPECT_EQ(report.published_mesh_fields, 0u);
    ASSERT_EQ(report.publish_failures.size(), 1u);
    EXPECT_NE(
        report.publish_failures[0].reason.find(
            "no mesh field visualization target"),
        std::string::npos)
        << report.publish_failures[0].reason;
    EXPECT_FALSE(
        assets.gpu_resident_fields()
            .find(setup.field.output, setup.channel_id)
            .valid());

    (void)behavior::release_behavior_gpu_kernel_library(
        device,
        setup.library);
}
