#include "scene_authoring_materialize_test_support.h"

#include <engine/behavior/behavior_gpu_compute_executor.h>
#include <engine/assets/scene/scene_instance.h>
#include <gpu/mesh_field_visualization.h>
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

            // NeighborWeights sparse apply (#150/#151): isolated rows output
            // zero detail, and mismatched operator metadata writes a
            // sentinel so the value tests catch a mis-filled info constant.
            ASSERT_EQ(
                wz::fs::write_file_text(
                    wz::fs::join(
                        root,
                        "shaders/compute/laplacian_residual_cs.hlsl"),
                    "StructuredBuffer<uint>  RowOffsets : register(t0);\n"
                    "StructuredBuffer<uint>  ColIndices : register(t1);\n"
                    "StructuredBuffer<float> Weights    : register(t2);\n"
                    "StructuredBuffer<float> VertexMass : register(t3);\n"
                    "StructuredBuffer<float> Signal     : register(t4);\n"
                    "RWStructuredBuffer<float> Output   : register(u0);\n"
                    "cbuffer Constants : register(b0) {\n"
                    "    uint RowCount;\n"
                    "    uint NonzeroCount;\n"
                    "    uint OperatorKind;\n"
                    "    uint ValueConvention;\n"
                    "};\n"
                    "[numthreads(64, 1, 1)]\n"
                    "void main(uint3 id : SV_DispatchThreadID) {\n"
                    "    if (id.x >= RowCount) { return; }\n"
                    "    if (OperatorKind != 0u || ValueConvention != 0u) {\n"
                    "        Output[id.x] = -999.0;\n"
                    "        return;\n"
                    "    }\n"
                    "    uint row_begin = RowOffsets[id.x];\n"
                    "    uint row_end = RowOffsets[id.x + 1u];\n"
                    "    if (row_begin == row_end) {\n"
                    "        Output[id.x] = 0.0;\n"
                    "        return;\n"
                    "    }\n"
                    "    float smooth = 0.0;\n"
                    "    for (uint e = row_begin; e < row_end; ++e) {\n"
                    "        smooth += Weights[e] * Signal[ColIndices[e]];\n"
                    "    }\n"
                    "    Output[id.x] = (Signal[id.x] - smooth)\n"
                    "        / VertexMass[id.x];\n"
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

namespace
{
    // Shared setup for the Laplacian consumer tests (#151): a quad mesh
    // (optionally mutated so vertex 3 is isolated), an explicit Float1
    // vertex field as the publish target, the compiled uniform-Laplacian
    // sparse operator, and the residual kernel library.
    struct LaplacianConsumerSetup
    {
        wz::engine::assets::MeshDerivedFieldAsset field{};
        wz::engine::assets::MeshSparseOperatorAsset op{};
        uint32_t vertex_count = 0u;
        uint32_t channel_id = 0u;
        uint32_t signal_channel_id = 0u;
        std::vector<float> signal_values;
        wz::engine::behavior::BehaviorGpuKernelLibrary library{};
        bool ok = false;
        std::string error;
    };

    LaplacianConsumerSetup build_laplacian_consumer_setup(
        wz::gpu::Device& device,
        wz::engine::assets::EngineAssetLibrary& assets,
        bool isolate_vertex_three,
        bool create_operator = true)
    {
        using namespace wz::engine::assets;
        namespace behavior = wz::engine::behavior;

        LaplacianConsumerSetup setup{};

        const MeshAsset mesh = assets.meshes().create_procedural_mesh({
            .name = "laplacian_mesh",
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
        if (isolate_vertex_three) {
            const_cast<MeshData*>(mesh_data)->indices =
                { 0u, 1u, 2u, 2u, 2u, 2u };
        }
        setup.vertex_count = mesh_data->vertex_count();
        setup.channel_id = MeshWaveletChannelID::kDetailCost;
        setup.signal_channel_id = MeshWaveletChannelID::kPositionEnergyBase;
        setup.signal_values.resize(setup.vertex_count);
        for (uint32_t i = 0u; i < setup.vertex_count; ++i) {
            setup.signal_values[i] = static_cast<float>(i);
        }

        std::vector<std::byte> zeroes(
            static_cast<size_t>(setup.vertex_count) * sizeof(float),
            std::byte{ 0 });
        std::vector<std::byte> signal_bytes(
            setup.signal_values.size() * sizeof(float));
        std::memcpy(
            signal_bytes.data(),
            setup.signal_values.data(),
            signal_bytes.size());
        setup.field = assets.mesh_derived_fields().create_explicit_field({
            .name = "behavior/laplacian_residual_field",
            .source_mesh = mesh,
            .domain = MeshDerivedFieldDomain::Vertex,
            .element_count = setup.vertex_count,
            .channels = {
                MeshDerivedFieldChannelDesc{
                    .channel_id = setup.signal_channel_id,
                    .value_type = MeshDerivedFieldValueType::Float1,
                    .values = signal_bytes,
                },
                MeshDerivedFieldChannelDesc{
                    .channel_id = setup.channel_id,
                    .value_type = MeshDerivedFieldValueType::Float1,
                    .values = zeroes,
                },
            },
        });
        if (!setup.field.valid()) {
            setup.error = "explicit field creation failed";
            return setup;
        }

        if (create_operator) {
            setup.op =
                assets.mesh_sparse_operators().create_sparse_operator({
                    .name = "laplacian_operator",
                    .source_mesh = mesh,
                });
            if (!setup.op.valid()) {
                setup.error = "sparse operator creation failed";
                return setup;
            }
        }

        SceneAssetData scene{};
        scene.name = "behavior_laplacian_residual";
        SceneNodeAsset node = make_scene_node("kernel");
        node.compute_kernel = SceneComputeKernelAsset{
            .kernel_id = "project/laplacian_residual",
            .hlsl_path = "shaders/compute/laplacian_residual_cs.hlsl",
            .entry = "main",
            .target = "cs_5_0",
            .thread_group_size_x = 64,
            .thread_group_size_y = 1,
            .thread_group_size_z = 1,
            .ports = {
                SceneComputeKernelPortAsset{
                    .name = "row_offsets",
                    .kind = SceneComputeKernelPortKind::StructuredBuffer,
                    .direction = SceneComputeKernelPortDirection::Input,
                    .binding_kind = SceneComputeKernelBindingKind::SRV,
                    .shader_register = 0,
                    .stride_bytes = 4,
                },
                SceneComputeKernelPortAsset{
                    .name = "col_indices",
                    .kind = SceneComputeKernelPortKind::StructuredBuffer,
                    .direction = SceneComputeKernelPortDirection::Input,
                    .binding_kind = SceneComputeKernelBindingKind::SRV,
                    .shader_register = 1,
                    .stride_bytes = 4,
                },
                SceneComputeKernelPortAsset{
                    .name = "weights",
                    .kind = SceneComputeKernelPortKind::StructuredBuffer,
                    .direction = SceneComputeKernelPortDirection::Input,
                    .binding_kind = SceneComputeKernelBindingKind::SRV,
                    .shader_register = 2,
                    .stride_bytes = 4,
                },
                SceneComputeKernelPortAsset{
                    .name = "vertex_mass",
                    .kind = SceneComputeKernelPortKind::StructuredBuffer,
                    .direction = SceneComputeKernelPortDirection::Input,
                    .binding_kind = SceneComputeKernelBindingKind::SRV,
                    .shader_register = 3,
                    .stride_bytes = 4,
                },
                SceneComputeKernelPortAsset{
                    .name = "signal",
                    .kind = SceneComputeKernelPortKind::StructuredBuffer,
                    .direction = SceneComputeKernelPortDirection::Input,
                    .binding_kind = SceneComputeKernelBindingKind::SRV,
                    .shader_register = 4,
                    .stride_bytes = 4,
                },
                SceneComputeKernelPortAsset{
                    .name = "output",
                    .kind = SceneComputeKernelPortKind::StructuredBuffer,
                    .direction = SceneComputeKernelPortDirection::Output,
                    .binding_kind = SceneComputeKernelBindingKind::UAV,
                    .shader_register = 0,
                    .stride_bytes = 4,
                },
                SceneComputeKernelPortAsset{
                    .name = "info",
                    .kind = SceneComputeKernelPortKind::U32,
                    .direction = SceneComputeKernelPortDirection::Input,
                    .root_constant_offset = 0,
                    .root_constant_dwords = 4,
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
            setup.error = "kernel/operator resolve failed";
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

    // Residual job using engine-resolved sparse-operator ports and an
    // explicit VERTEX dispatch domain (the kernel reads CSR arrays whose
    // element counts far exceed the iteration domain).
    wz::engine::behavior::BehaviorGpuComputeJob make_laplacian_job(
        const std::vector<float>& signal,
        uint32_t channel_id,
        bool publish)
    {
        namespace behavior = wz::engine::behavior;

        behavior::BehaviorGpuComputeJob job{};
        job.work.value = 1u;
        job.entity = 0;
        job.kernel = "project/laplacian_residual";
        job.group_count_x = 0u;
        job.group_count_y = 0u;
        job.group_count_z = 0u;
        job.dispatch_domain = WZ_GPU_DISPATCH_DOMAIN_VERTEX;

        const auto sparse_port =
            [](const char* name, uint32_t component)
        {
            return behavior::BehaviorGpuPortValue{
                .name = name,
                .kind = WZ_GPU_PORT_STRUCTURED_BUFFER,
                .direction = WZ_GPU_PORT_INPUT,
                .element_count = 0u,
                .stride_bytes = 4u,
                .resource = WzGpuResourceRef{
                    .value = WZ_GPU_RESOURCE_REF_MESH_SPARSE_OPERATOR,
                },
                .u32 = { component, 0u, 0u, 0u },
            };
        };

        job.ports = {
            sparse_port(
                "row_offsets",
                WZ_GPU_SPARSE_OPERATOR_ROW_OFFSETS),
            sparse_port(
                "col_indices",
                WZ_GPU_SPARSE_OPERATOR_COL_INDICES),
            sparse_port("weights", WZ_GPU_SPARSE_OPERATOR_WEIGHTS),
            sparse_port(
                "vertex_mass",
                WZ_GPU_SPARSE_OPERATOR_VERTEX_MASS),
            behavior::BehaviorGpuPortValue{
                .name = "signal",
                .kind = WZ_GPU_PORT_STRUCTURED_BUFFER,
                .direction = WZ_GPU_PORT_INPUT,
                .element_count = static_cast<uint32_t>(signal.size()),
                .stride_bytes = sizeof(float),
                .initial_data = {
                    reinterpret_cast<const std::byte*>(signal.data()),
                    reinterpret_cast<const std::byte*>(signal.data())
                        + signal.size() * sizeof(float),
                },
            },
            behavior::BehaviorGpuPortValue{
                .name = "output",
                .kind = WZ_GPU_PORT_STRUCTURED_BUFFER,
                .direction = WZ_GPU_PORT_OUTPUT,
                .element_count = publish
                    ? 0u
                    : static_cast<uint32_t>(signal.size()),
                .stride_bytes = sizeof(float),
                .resource = WzGpuResourceRef{
                    .value = publish
                        ? static_cast<uint64_t>(
                            WZ_GPU_RESOURCE_REF_MESH_FIELD_VISUALIZATION)
                        : static_cast<uint64_t>(WZ_GPU_RESOURCE_REF_NONE),
                },
                .u32 = { publish ? channel_id : 0u, 0u, 0u, 0u },
            },
            behavior::BehaviorGpuPortValue{
                .name = "info",
                .kind = WZ_GPU_PORT_U32,
                .direction = WZ_GPU_PORT_INPUT,
                .resource = WzGpuResourceRef{
                    .value =
                        WZ_GPU_RESOURCE_REF_MESH_SPARSE_OPERATOR_INFO,
                },
            },
        };
        return job;
    }

    wz::engine::behavior::BehaviorGpuComputeJob
    make_laplacian_field_signal_job(
        uint32_t signal_channel_id,
        uint32_t output_channel_id,
        uint32_t output_element_count,
        bool publish)
    {
        namespace behavior = wz::engine::behavior;

        behavior::BehaviorGpuComputeJob job =
            make_laplacian_job({}, output_channel_id, publish);
        job.ports[4] = behavior::BehaviorGpuPortValue{
            .name = "signal",
            .kind = WZ_GPU_PORT_STRUCTURED_BUFFER,
            .direction = WZ_GPU_PORT_INPUT,
            .element_count = 0u,
            .stride_bytes = sizeof(float),
            .resource = WzGpuResourceRef{
                .value = WZ_GPU_RESOURCE_REF_MESH_DERIVED_FIELD_CHANNEL,
            },
            .u32 = {
                signal_channel_id,
                WZ_GPU_MESH_FIELD_VALUE_FLOAT1,
                WZ_GPU_MESH_FIELD_COMPONENT_ALL,
                WZ_GPU_SPARSE_APPLY_RESIDUAL,
            },
        };
        if (!publish) {
            job.ports[5].element_count = output_element_count;
        }
        return job;
    }

    // CPU reference application of the NeighborWeights convention with the
    // isolated-row contract: rows with no neighbors output zero detail.
    std::vector<float> reference_residual(
        const wz::engine::assets::MeshSparseOperatorData& op,
        const std::vector<float>& signal)
    {
        std::vector<float> out(op.row_count, 0.0f);
        for (uint32_t row = 0; row < op.row_count; ++row) {
            const uint32_t begin = op.row_offsets[row];
            const uint32_t end = op.row_offsets[row + 1u];
            if (begin == end) {
                continue;
            }
            float smooth = 0.0f;
            for (uint32_t e = begin; e < end; ++e) {
                smooth += op.weights[e] * signal[op.col_indices[e]];
            }
            out[row] =
                (signal[row] - smooth) / op.vertex_mass[row];
        }
        return out;
    }
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    LaplacianResidualMatchesCpuReferenceAndStaysResident)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };
    LaplacianConsumerSetup setup =
        build_laplacian_consumer_setup(device, assets, false);
    ASSERT_TRUE(setup.ok) << setup.error;

    wz::engine::assets::SceneInstance instance{};
    instance.mesh_field_visualization_targets.push_back({
        .node = 0,
        .component = MeshFieldVisualizationTargetComponent{
            .field_asset = setup.field.output,
            .channel_id = setup.channel_id,
        },
    });

    const auto* op_data =
        assets.mesh_sparse_operators().get_sparse_operator_data(
            assets.mesh_sparse_operators().get_sparse_operator(setup.op));
    ASSERT_NE(op_data, nullptr);

    const std::vector<float> signal{ 0.0f, 1.0f, 2.0f, 3.0f };
    const behavior::BehaviorGpuComputeJob job =
        make_laplacian_job(signal, setup.channel_id, false);
    const auto report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            setup.library);
    EXPECT_EQ(report.dispatched, 1u);
    EXPECT_EQ(report.failed, 0u);
    for (const auto& failure : report.publish_failures) {
        ADD_FAILURE() << failure.port_name << ": " << failure.reason;
    }

    ASSERT_EQ(report.derived_dispatches.size(), 1u);
    EXPECT_EQ(
        report.derived_dispatches[0].dispatch_domain,
        WZ_GPU_DISPATCH_DOMAIN_VERTEX);
    EXPECT_EQ(report.derived_dispatches[0].element_count, 4u);

    // GPU residual equals the CPU reference application (including the
    // metadata guard: a mis-filled info constant writes -999 sentinels).
    const std::vector<float> expected =
        reference_residual(*op_data, signal);
    ASSERT_EQ(report.readbacks.size(), 1u);
    ASSERT_EQ(
        report.readbacks[0].bytes.size(),
        signal.size() * sizeof(float));
    for (size_t i = 0; i < signal.size(); ++i) {
        float value = 0.0f;
        std::memcpy(
            &value,
            report.readbacks[0].bytes.data() + i * sizeof(float),
            sizeof(float));
        EXPECT_NEAR(value, expected[i], 1.0e-5f) << "vertex " << i;
    }

    // The four CSR buffers were uploaded once into the resident operator
    // table; a second dispatch binds the identical handles.
    ASSERT_EQ(assets.gpu_resident_sparse_operators().size(), 1u);
    const auto* resident =
        assets.gpu_resident_sparse_operators().find(setup.op.output);
    ASSERT_NE(resident, nullptr);
    EXPECT_TRUE(resident->row_offsets.valid());
    EXPECT_TRUE(resident->col_indices.valid());
    EXPECT_TRUE(resident->weights.valid());
    EXPECT_TRUE(resident->vertex_mass.valid());
    EXPECT_EQ(resident->row_count, 4u);
    EXPECT_EQ(resident->nonzero_count, 10u);
    const wz::gpu::GPUHandle first_weights = resident->weights;

    const auto second_report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            setup.library);
    EXPECT_EQ(second_report.dispatched, 1u);
    EXPECT_EQ(assets.gpu_resident_sparse_operators().size(), 1u);
    EXPECT_EQ(
        assets.gpu_resident_sparse_operators().find(setup.op.output)
            ->weights,
        first_weights);

    (void)behavior::release_behavior_gpu_kernel_library(
        device,
        setup.library);
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    LaplacianResidualPublishesMeshFieldVisualization)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };
    LaplacianConsumerSetup setup =
        build_laplacian_consumer_setup(device, assets, false);
    ASSERT_TRUE(setup.ok) << setup.error;

    wz::engine::assets::SceneInstance instance{};
    instance.mesh_field_visualization_targets.push_back({
        .node = 0,
        .component = MeshFieldVisualizationTargetComponent{
            .field_asset = setup.field.output,
            .channel_id = setup.channel_id,
        },
    });

    const std::vector<float> signal{ 0.0f, 1.0f, 2.0f, 3.0f };
    const behavior::BehaviorGpuComputeJob job =
        make_laplacian_job(signal, setup.channel_id, true);
    const auto report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            setup.library);
    EXPECT_EQ(report.dispatched, 1u);
    EXPECT_EQ(report.published_mesh_fields, 1u);
    for (const auto& failure : report.publish_failures) {
        ADD_FAILURE() << failure.port_name << ": " << failure.reason;
    }
    // Published outputs stay GPU-resident; no CPU readback.
    EXPECT_TRUE(report.readbacks.empty());
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
    LaplacianResidualConsumesMeshDerivedFieldSignalChannel)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };
    LaplacianConsumerSetup setup =
        build_laplacian_consumer_setup(device, assets, false);
    ASSERT_TRUE(setup.ok) << setup.error;

    wz::engine::assets::SceneInstance instance{};
    instance.mesh_field_visualization_targets.push_back({
        .node = 0,
        .component = MeshFieldVisualizationTargetComponent{
            .field_asset = setup.field.output,
            .channel_id = setup.channel_id,
        },
    });

    const auto* op_data =
        assets.mesh_sparse_operators().get_sparse_operator_data(
            assets.mesh_sparse_operators().get_sparse_operator(setup.op));
    ASSERT_NE(op_data, nullptr);

    const wz::gpu::GPUHandle signal_buffer =
        wz::gpu::create_structured_buffer(device, {
            .element_count = setup.vertex_count,
            .stride_bytes = sizeof(float),
            .initial_data = setup.signal_values.data(),
            .initial_data_bytes =
                setup.signal_values.size() * sizeof(float),
        });
    ASSERT_TRUE(signal_buffer.valid());
    const wz::gpu::GPUHandle signal_field =
        wz::gpu::create_mesh_field_visualization_from_gpu_source(
            device,
            signal_buffer,
            0u,
            setup.vertex_count,
            sizeof(float));
    EXPECT_TRUE(wz::gpu::release_compute_buffer(device, signal_buffer));
    ASSERT_TRUE(signal_field.valid());
    ASSERT_TRUE(assets.gpu_resident_fields().add(GpuResidentFieldEntry{
        .field_key = setup.field.output,
        .channel_id = setup.signal_channel_id,
        .gpu_resource = signal_field,
    }));

    const behavior::BehaviorGpuComputeJob readback_job =
        make_laplacian_field_signal_job(
            setup.signal_channel_id,
            setup.channel_id,
            setup.vertex_count,
            false);
    const auto report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{
                &readback_job,
                1u,
            },
            setup.library);
    EXPECT_EQ(report.dispatched, 1u);
    EXPECT_EQ(report.failed, 0u);
    for (const auto& failure : report.publish_failures) {
        ADD_FAILURE() << failure.port_name << ": " << failure.reason;
    }

    const std::vector<float> expected =
        reference_residual(*op_data, setup.signal_values);
    ASSERT_EQ(report.readbacks.size(), 1u);
    ASSERT_EQ(
        report.readbacks[0].bytes.size(),
        setup.vertex_count * sizeof(float));
    for (size_t i = 0; i < expected.size(); ++i) {
        float value = 0.0f;
        std::memcpy(
            &value,
            report.readbacks[0].bytes.data() + i * sizeof(float),
            sizeof(float));
        EXPECT_NEAR(value, expected[i], 1.0e-5f) << "vertex " << i;
    }

    const behavior::BehaviorGpuComputeJob publish_job =
        make_laplacian_field_signal_job(
            setup.signal_channel_id,
            setup.channel_id,
            setup.vertex_count,
            true);
    const auto publish_report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{
                &publish_job,
                1u,
            },
            setup.library);
    EXPECT_EQ(publish_report.dispatched, 1u);
    EXPECT_EQ(publish_report.failed, 0u);
    EXPECT_EQ(publish_report.published_mesh_fields, 1u);
    EXPECT_TRUE(publish_report.readbacks.empty());
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
    LaplacianResidualConstantSignalIsZeroIncludingIsolatedVertex)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };
    LaplacianConsumerSetup setup =
        build_laplacian_consumer_setup(device, assets, true);
    ASSERT_TRUE(setup.ok) << setup.error;

    wz::engine::assets::SceneInstance instance{};
    instance.mesh_field_visualization_targets.push_back({
        .node = 0,
        .component = MeshFieldVisualizationTargetComponent{
            .field_asset = setup.field.output,
            .channel_id = setup.channel_id,
        },
    });

    // Constant signal: connected rows cancel exactly (rows sum to 1), and
    // the isolated vertex 3 must output 0 — not its raw signal value,
    // which is what a kernel without the empty-row guard would produce.
    const std::vector<float> signal(4u, 0.75f);
    const behavior::BehaviorGpuComputeJob job =
        make_laplacian_job(signal, setup.channel_id, false);
    const auto report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            setup.library);
    EXPECT_EQ(report.dispatched, 1u);
    EXPECT_EQ(report.failed, 0u);

    ASSERT_EQ(report.readbacks.size(), 1u);
    ASSERT_EQ(report.readbacks[0].bytes.size(), 4u * sizeof(float));
    for (size_t i = 0; i < 4u; ++i) {
        float value = 0.0f;
        std::memcpy(
            &value,
            report.readbacks[0].bytes.data() + i * sizeof(float),
            sizeof(float));
        EXPECT_NEAR(value, 0.0f, 1.0e-6f) << "vertex " << i;
    }

    (void)behavior::release_behavior_gpu_kernel_library(
        device,
        setup.library);
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    LaplacianResidualFieldSignalReportsClearDiagnostics)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };
    LaplacianConsumerSetup setup =
        build_laplacian_consumer_setup(device, assets, false);
    ASSERT_TRUE(setup.ok) << setup.error;

    const auto field_handle =
        assets.mesh_derived_fields().get_mesh_derived_field(setup.field);
    const MeshDerivedFieldData* setup_field =
        assets.mesh_derived_fields().get_mesh_derived_field_data(
            field_handle);
    ASSERT_NE(setup_field, nullptr);
    const MeshAsset source_mesh{ .output = setup_field->source_mesh_key };

    auto dispatch_with_target =
        [&](MeshDerivedFieldAsset field,
            behavior::BehaviorGpuComputeJob job)
    {
        wz::engine::assets::SceneInstance instance{};
        instance.mesh_field_visualization_targets.push_back({
            .node = 0,
            .component = MeshFieldVisualizationTargetComponent{
                .field_asset = field.output,
                .channel_id = setup.channel_id,
            },
        });
        return behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            setup.library);
    };

    behavior::BehaviorGpuComputeJob missing_channel =
        make_laplacian_field_signal_job(
            setup.signal_channel_id + 77u,
            setup.channel_id,
            setup.vertex_count,
            false);
    auto missing_report =
        dispatch_with_target(setup.field, missing_channel);
    EXPECT_EQ(missing_report.dispatched, 0u);
    EXPECT_EQ(missing_report.failed, 1u);
    ASSERT_FALSE(missing_report.publish_failures.empty());
    EXPECT_NE(
        missing_report.publish_failures[0].reason.find("has no channel"),
        std::string::npos)
        << missing_report.publish_failures[0].reason;

    std::vector<float> vec2_values(
        static_cast<size_t>(setup.vertex_count) * 2u,
        1.0f);
    std::vector<std::byte> vec2_bytes(
        vec2_values.size() * sizeof(float));
    std::memcpy(
        vec2_bytes.data(),
        vec2_values.data(),
        vec2_bytes.size());
    std::vector<std::byte> output_zeroes(
        static_cast<size_t>(setup.vertex_count) * sizeof(float),
        std::byte{ 0 });
    const MeshDerivedFieldAsset type_mismatch_field =
        assets.mesh_derived_fields().create_explicit_field({
            .name = "behavior/type_mismatch_signal_field",
            .source_mesh = source_mesh,
            .domain = MeshDerivedFieldDomain::Vertex,
            .element_count = setup.vertex_count,
            .channels = {
                MeshDerivedFieldChannelDesc{
                    .channel_id = setup.signal_channel_id,
                    .value_type = MeshDerivedFieldValueType::Float2,
                    .values = vec2_bytes,
                },
                MeshDerivedFieldChannelDesc{
                    .channel_id = setup.channel_id,
                    .value_type = MeshDerivedFieldValueType::Float1,
                    .values = output_zeroes,
                },
            },
        });
    ASSERT_TRUE(type_mismatch_field.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    behavior::BehaviorGpuComputeJob type_mismatch =
        make_laplacian_field_signal_job(
            setup.signal_channel_id,
            setup.channel_id,
            setup.vertex_count,
            false);
    auto type_report =
        dispatch_with_target(type_mismatch_field, type_mismatch);
    EXPECT_EQ(type_report.dispatched, 0u);
    EXPECT_EQ(type_report.failed, 1u);
    ASSERT_FALSE(type_report.publish_failures.empty());
    EXPECT_NE(
        type_report.publish_failures[0].reason.find("type mismatch"),
        std::string::npos)
        << type_report.publish_failures[0].reason;

    const uint32_t face_count = 2u;
    std::vector<std::byte> face_values(face_count * sizeof(float));
    std::vector<std::byte> face_output(face_count * sizeof(float));
    const MeshDerivedFieldAsset domain_mismatch_field =
        assets.mesh_derived_fields().create_explicit_field({
            .name = "behavior/domain_mismatch_signal_field",
            .source_mesh = source_mesh,
            .domain = MeshDerivedFieldDomain::Face,
            .element_count = face_count,
            .channels = {
                MeshDerivedFieldChannelDesc{
                    .channel_id = setup.signal_channel_id,
                    .value_type = MeshDerivedFieldValueType::Float1,
                    .values = face_values,
                },
                MeshDerivedFieldChannelDesc{
                    .channel_id = setup.channel_id,
                    .value_type = MeshDerivedFieldValueType::Float1,
                    .values = face_output,
                },
            },
        });
    ASSERT_TRUE(domain_mismatch_field.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    behavior::BehaviorGpuComputeJob domain_mismatch =
        make_laplacian_field_signal_job(
            setup.signal_channel_id,
            setup.channel_id,
            setup.vertex_count,
            false);
    auto domain_report =
        dispatch_with_target(domain_mismatch_field, domain_mismatch);
    EXPECT_EQ(domain_report.dispatched, 0u);
    EXPECT_EQ(domain_report.failed, 1u);
    ASSERT_FALSE(domain_report.publish_failures.empty());
    EXPECT_NE(
        domain_report.publish_failures[0].reason.find("domain mismatch"),
        std::string::npos)
        << domain_report.publish_failures[0].reason;

    (void)behavior::release_behavior_gpu_kernel_library(
        device,
        setup.library);
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    LaplacianResidualWithoutCompiledOperatorFailsWithDiagnostic)
{
    using namespace wz::engine::assets;
    namespace behavior = wz::engine::behavior;

    EngineAssetLibrary assets{ device, logger, root };
    LaplacianConsumerSetup setup =
        build_laplacian_consumer_setup(device, assets, false, false);
    ASSERT_TRUE(setup.ok) << setup.error;

    wz::engine::assets::SceneInstance instance{};
    instance.mesh_field_visualization_targets.push_back({
        .node = 0,
        .component = MeshFieldVisualizationTargetComponent{
            .field_asset = setup.field.output,
            .channel_id = setup.channel_id,
        },
    });

    const std::vector<float> signal(4u, 0.5f);
    const behavior::BehaviorGpuComputeJob job =
        make_laplacian_job(signal, setup.channel_id, false);
    const auto report =
        behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            assets,
            instance,
            std::span<const behavior::BehaviorGpuComputeJob>{ &job, 1u },
            setup.library);
    EXPECT_EQ(report.dispatched, 0u);
    EXPECT_EQ(report.failed, 1u);
    ASSERT_FALSE(report.publish_failures.empty());
    EXPECT_NE(
        report.publish_failures[0].reason.find(
            "mesh sparse operator unavailable"),
        std::string::npos)
        << report.publish_failures[0].reason;

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
