#include "renderable_asset_module_test_support.h"

#include <cstddef>
#include <asset/draft.h>
#include <asset/system.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/key_factories/renderable.h>
#include <engine/assets/schema_ids.h>
#include <cstring>
#include <vector>

namespace
{
    std::vector<std::byte> float_bytes(std::initializer_list<float> values)
    {
        std::vector<std::byte> bytes(values.size() * sizeof(float));
        std::memcpy(bytes.data(), values.begin(), bytes.size());
        return bytes;
    }

    std::vector<std::byte> uint_bytes(std::initializer_list<uint32_t> values)
    {
        std::vector<std::byte> bytes(values.size() * sizeof(uint32_t));
        std::memcpy(bytes.data(), values.begin(), bytes.size());
        return bytes;
    }

    constexpr wz::asset::SchemaID kRhiRenderableTestMeshSchema{
        0x7111'0000'0000'0001ull
    };

    constexpr wz::asset::SchemaID kRhiRenderableTestProgramSchema{
        0x7111'0000'0000'0002ull
    };

    wz::asset::AssetKey test_asset_key(uint64_t id)
    {
        return wz::asset::AssetKey{
            .content_hash = { id, 0x71110000ull },
            .schema_hash = { id ^ 0x1000ull, 0x71110001ull },
            .compiler_hash = { id ^ 0x2000ull, 0x71110002ull },
            .deps_hash = { id ^ 0x3000ull, 0x71110003ull },
        };
    }

    wz::asset::AssetNode test_source_node(
        wz::asset::AssetKey key,
        wz::asset::AssetType type,
        wz::asset::SchemaID schema)
    {
        wz::asset::AssetNode node{};
        node.key = key;
        node.type = type;
        node.schema = schema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        return node;
    }

    wz::engine::assets::MeshData test_triangle_mesh()
    {
        wz::engine::assets::MeshData mesh{};
        mesh.vertices.resize(3);
        mesh.vertices[0].position[0] = 0.0f;
        mesh.vertices[0].position[1] = 0.0f;
        mesh.vertices[0].position[2] = 0.0f;
        mesh.vertices[1].position[0] = 1.0f;
        mesh.vertices[1].position[1] = 0.0f;
        mesh.vertices[1].position[2] = 0.0f;
        mesh.vertices[2].position[0] = 0.0f;
        mesh.vertices[2].position[1] = 1.0f;
        mesh.vertices[2].position[2] = 0.0f;
        mesh.indices = { 0u, 1u, 2u };
        return mesh;
    }

    class NullMeshFieldComputeBackend final
        : public wz::engine::assets::MeshFieldComputeBackend
    {
    public:
        bool available() const noexcept override { return false; }

        wz::asset::ResourceHandle create_compute_pipeline(
            const wz::engine::assets::ComputePipelineData&,
            wz::asset::ResourceHandle) override
        {
            return {};
        }

        wz::asset::ResourceHandle create_structured_buffer(
            const BufferDesc&) override
        {
            return {};
        }

        wz::asset::ResourceHandle create_rw_structured_buffer(
            const BufferDesc&) override
        {
            return {};
        }

        bool dispatch(const DispatchDesc&) override { return false; }

        std::vector<std::byte> readback_buffer(
            wz::asset::ResourceHandle) override
        {
            return {};
        }

        bool release_buffer(wz::asset::ResourceHandle) override
        {
            return false;
        }

        bool release_pipeline(wz::asset::ResourceHandle) override
        {
            return false;
        }

        wz::asset::ResourceHandle create_field_visualization_from_gpu_source(
            wz::asset::ResourceHandle,
            uint64_t,
            uint32_t,
            uint32_t) override
        {
            return {};
        }

        bool release_field_visualization(wz::asset::ResourceHandle) override
        {
            return false;
        }
    };
}

TEST(RenderableAssetModule, RhiPullMeshRenderableRecipeCarriesMeshAndProgramKeys)
{
    using namespace wz::asset;
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    NullMeshFieldComputeBackend mesh_field_compute;

    ScalarFieldTable scalar_fields_table;
    VectorFieldTable vector_fields_table;
    CSVTable csv_table;
    JSONTable json_table;
    TOMLTable toml_table;
    MeshTable mesh_table;
    MeshDerivedFieldTable mesh_derived_field_table;
    MeshSparseOperatorTable mesh_sparse_operator_table;
    GpuSparseMeshTable gpu_sparse_mesh_table;
    MeshClusterHierarchyTable mesh_cluster_hierarchy_table;
    GpuResidentFieldTable gpu_resident_field_table;
    GpuResidentMeshDataTable gpu_resident_mesh_data_table;
    GpuResidentSparseOperatorTable gpu_resident_sparse_operator_table;
    GpuResidentSparseMeshTable gpu_resident_sparse_mesh_table;
    GpuResidentMeshClusterHierarchyTable
        gpu_resident_mesh_cluster_hierarchy_table;
    TerrainAssetTable terrain_table;
    TerrainVisualProxyTable terrain_visual_proxy_table;
    CollisionAssetTable collision_table;
    GaussianSplatCloudTable gaussian_splat_cloud_table;
    GaussianSplatColorLODTable gaussian_splat_color_lod_table;
    DataTable data_table;
    DiagnosticResampledTimeSeriesTable diagnostic_resampled_time_series_table;
    DiagnosticTimeframeSummaryTable diagnostic_timeframe_summary_table;
    CSVExportTable csv_export_table;
    MeshRenderStyleTable mesh_render_style_table;
    RenderableAssetTable renderable_table;
    RhiRenderableTable rhi_renderable_table;
    RenderProgramTable render_program_table;
    ComputePipelineTable compute_pipeline_table;
    DirectLightTable direct_light_table;
    AmbientLightingTable ambient_lighting_table;
    HDRIEnvironmentTable hdri_environment_table;
    SceneAssetTable scene_table;
    EngineAssetCacheSettings cache_settings{};

    CompilerRegistry registry;
    registry.register_compiler(AssetCompiler{
        .input_schema = kRhiRenderableTestMeshSchema,
        .output_type = kAssetTypeMesh,
        .compile = [&mesh_table](
            const AssetNode& input,
            std::span<const AssetNode>,
            std::span<const ResourceHandle>) -> AssetNode
        {
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = mesh_table.add(test_triangle_mesh());
            return out;
        },
    });
    registry.register_compiler(AssetCompiler{
        .input_schema = kRhiRenderableTestProgramSchema,
        .output_type = kAssetTypeRenderProgram,
        .compile = [&render_program_table](
            const AssetNode& input,
            std::span<const AssetNode>,
            std::span<const ResourceHandle>) -> AssetNode
        {
            RenderProgramData data{};
            data.binding_model = RenderBindingModel::MeshVertexPull;
            data.input_layout = InputLayoutKind::None;
            data.vertex_shader = ResourceHandle{
                .id = 1,
                .epoch = 1,
                .type = AssetType::Shader,
            };
            data.pixel_shader = ResourceHandle{
                .id = 2,
                .epoch = 1,
                .type = AssetType::Shader,
            };

            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = render_program_table.add(std::move(data));
            return out;
        },
    });
    registry.register_compiler(AssetCompiler{
        .input_schema = kGpuSparseMeshFromMeshSchema,
        .output_type = kAssetTypeGpuSparseMesh,
        .compile = [&gpu_sparse_mesh_table](
            const AssetNode& input,
            std::span<const AssetNode>,
            std::span<const ResourceHandle>) -> AssetNode
        {
            GpuSparseMeshData data{};
            data.source_mesh_key = test_asset_key(40);
            data.sparse_operator_key = test_asset_key(41);
            data.source_topology_hash = { 42, 43 };
            data.vertex_count = 3;
            data.index_count = 3;
            data.source_triangle_count = 1;
            data.bounds_min[0] = -1.0f;
            data.bounds_min[1] = -2.0f;
            data.bounds_min[2] = -3.0f;
            data.bounds_max[0] = 1.0f;
            data.bounds_max[1] = 2.0f;
            data.bounds_max[2] = 3.0f;

            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = gpu_sparse_mesh_table.add(data);
            return out;
        },
    });

    const internal::EngineAssetContext ctx{
        .device = device,
        .logger = logger,
        .mesh_field_compute = mesh_field_compute,
        .scalar_fields_table = scalar_fields_table,
        .vector_fields_table = vector_fields_table,
        .csv_table = csv_table,
        .json_table = json_table,
        .toml_table = toml_table,
        .mesh_table = mesh_table,
        .mesh_derived_field_table = mesh_derived_field_table,
        .mesh_sparse_operator_table = mesh_sparse_operator_table,
        .gpu_sparse_mesh_table = gpu_sparse_mesh_table,
        .mesh_cluster_hierarchy_table = mesh_cluster_hierarchy_table,
        .gpu_resident_field_table = gpu_resident_field_table,
        .gpu_resident_mesh_data_table = gpu_resident_mesh_data_table,
        .gpu_resident_sparse_operator_table =
            gpu_resident_sparse_operator_table,
        .gpu_resident_sparse_mesh_table =
            gpu_resident_sparse_mesh_table,
        .gpu_resident_mesh_cluster_hierarchy_table =
            gpu_resident_mesh_cluster_hierarchy_table,
        .terrain_table = terrain_table,
        .terrain_visual_proxy_table = terrain_visual_proxy_table,
        .collision_table = collision_table,
        .gaussian_splat_cloud_table = gaussian_splat_cloud_table,
        .gaussian_splat_color_lod_table = gaussian_splat_color_lod_table,
        .data_table = data_table,
        .diagnostic_resampled_time_series_table =
            diagnostic_resampled_time_series_table,
        .diagnostic_timeframe_summary_table =
            diagnostic_timeframe_summary_table,
        .csv_export_table = csv_export_table,
        .mesh_render_style_table = mesh_render_style_table,
        .renderable_table = renderable_table,
        .rhi_renderable_table = rhi_renderable_table,
        .render_program_table = render_program_table,
        .compute_pipeline_table = compute_pipeline_table,
        .direct_light_table = direct_light_table,
        .ambient_lighting_table = ambient_lighting_table,
        .hdri_environment_table = hdri_environment_table,
        .scene_table = scene_table,
        .cache_settings = cache_settings,
    };
    internal::register_renderable_compilers(registry, ctx);

    const AssetKey mesh_key = test_asset_key(1);
    const AssetKey program_key = test_asset_key(2);
    const AssetKey renderable_key =
        make_rhi_pull_mesh_renderable_key(
            "test/rhi_pull_mesh_renderable",
            mesh_key,
            program_key);

    AssetGraphDraft draft{};
    const AssetGraphDraftNodeId mesh_node =
        add_asset_graph_draft_node(
            draft,
            test_source_node(
                mesh_key,
                kAssetTypeMesh,
                kRhiRenderableTestMeshSchema),
            AssetGraphDraftNodeState::Existing);
    const AssetGraphDraftNodeId program_node =
        add_asset_graph_draft_node(
            draft,
            test_source_node(
                program_key,
                kAssetTypeRenderProgram,
                kRhiRenderableTestProgramSchema),
            AssetGraphDraftNodeState::Existing);

    AssetNode renderable_node =
        test_source_node(
            renderable_key,
            kAssetTypeRenderable,
            kRhiPullMeshRenderableSchema);
    renderable_node.meta = RhiPullMeshRenderableCompileDesc{
        .mesh_asset = mesh_key,
        .render_program_asset = program_key,
    };
    const AssetGraphDraftNodeId renderable_draft_node =
        add_asset_graph_draft_node(
            draft,
            std::move(renderable_node),
            AssetGraphDraftNodeState::Existing);

    ASSERT_NE(
        connect_asset_graph_draft_nodes(
            draft,
            mesh_node,
            renderable_draft_node,
            0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(
            draft,
            program_node,
            renderable_draft_node,
            1),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    ASSERT_TRUE(validate_asset_graph_draft(draft, registry));

    const std::vector<AssetGraphDraftRegistration> registrations =
        asset_graph_draft_to_registrations(draft, &registry);
    std::vector<AssetSystem::RegistrationEntry> entries;
    entries.reserve(registrations.size());
    for (const AssetGraphDraftRegistration& registration : registrations) {
        entries.push_back(AssetSystem::RegistrationEntry{
            .node = registration.node,
            .dep_keys = registration.dep_keys,
        });
    }

    const AssetKey sparse_mesh_key = test_asset_key(3);
    const AssetKey sparse_renderable_key =
        make_gpu_sparse_mesh_renderable_key(
            "test/gpu_sparse_mesh_renderable",
            sparse_mesh_key,
            program_key);

    AssetGraphDraft sparse_draft{};
    const AssetGraphDraftNodeId sparse_mesh_node =
        add_asset_graph_draft_node(
            sparse_draft,
            test_source_node(
                sparse_mesh_key,
                kAssetTypeGpuSparseMesh,
                kGpuSparseMeshFromMeshSchema),
            AssetGraphDraftNodeState::Existing);
    const AssetGraphDraftNodeId sparse_program_node =
        add_asset_graph_draft_node(
            sparse_draft,
            test_source_node(
                program_key,
                kAssetTypeRenderProgram,
                kRhiRenderableTestProgramSchema),
            AssetGraphDraftNodeState::Existing);

    AssetNode sparse_renderable_node =
        test_source_node(
            sparse_renderable_key,
            kAssetTypeRenderable,
            kGpuSparseMeshRenderableSchema);
    sparse_renderable_node.meta = GpuSparseMeshRenderableCompileDesc{
        .sparse_mesh_asset = sparse_mesh_key,
        .render_program_asset = program_key,
    };
    const AssetGraphDraftNodeId sparse_renderable_draft_node =
        add_asset_graph_draft_node(
            sparse_draft,
            std::move(sparse_renderable_node),
            AssetGraphDraftNodeState::Existing);

    ASSERT_NE(
        connect_asset_graph_draft_nodes(
            sparse_draft,
            sparse_mesh_node,
            sparse_renderable_draft_node,
            0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(
            sparse_draft,
            sparse_program_node,
            sparse_renderable_draft_node,
            1),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    if (!validate_asset_graph_draft(sparse_draft, registry)) {
        for (const AssetGraphDraftValidationMessage& message :
             sparse_draft.validation_messages)
        {
            ADD_FAILURE()
                << "draft validation error code="
                << static_cast<int>(message.code)
                << " node=" << message.node
                << " edge=" << message.edge
                << " port=" << message.input_port
                << " message=" << message.message;
        }
    }
    ASSERT_TRUE(sparse_draft.validation_messages.empty());

    const std::vector<AssetGraphDraftRegistration> sparse_registrations =
        asset_graph_draft_to_registrations(sparse_draft, &registry);
    std::vector<AssetSystem::RegistrationEntry> sparse_entries;
    sparse_entries.reserve(sparse_registrations.size());
    for (const AssetGraphDraftRegistration& registration :
         sparse_registrations)
    {
        sparse_entries.push_back(AssetSystem::RegistrationEntry{
            .node = registration.node,
            .dep_keys = registration.dep_keys,
        });
    }

    AssetSystem system(std::move(registry));
    ASSERT_TRUE(system.replace_registered_assets(std::move(entries)));

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(system.resolve_all(&errors), 3u);
    EXPECT_TRUE(errors.empty());

    const AssetSystem::CompiledAsset* compiled =
        system.find_compiled(renderable_key);
    ASSERT_NE(compiled, nullptr);
    ASSERT_TRUE(compiled->handle.valid());
    EXPECT_EQ(compiled->node->type, kAssetTypeRenderable);
    EXPECT_EQ(compiled->handle.type, kAssetTypeRhiRenderableRecipe);
    EXPECT_EQ(renderable_table.get(compiled->handle), nullptr);

    const RhiRenderableRecipe* recipe =
        rhi_renderable_table.get(compiled->handle);
    ASSERT_NE(recipe, nullptr);
    EXPECT_EQ(recipe->mesh_key, mesh_key);
    EXPECT_EQ(recipe->program_key, program_key);

    ASSERT_TRUE(system.replace_registered_assets(std::move(sparse_entries)));
    errors.clear();
    EXPECT_EQ(system.resolve_all(&errors), 3u);
    EXPECT_TRUE(errors.empty());

    compiled = system.find_compiled(sparse_renderable_key);
    ASSERT_NE(compiled, nullptr);
    ASSERT_TRUE(compiled->handle.valid());
    EXPECT_EQ(compiled->node->type, kAssetTypeRenderable);
    EXPECT_EQ(compiled->handle.type, kAssetTypeRenderable);

    const RenderableAssetData* sparse_renderable =
        renderable_table.get(compiled->handle);
    ASSERT_NE(sparse_renderable, nullptr);
    EXPECT_EQ(sparse_renderable->kind, RenderableKind::Mesh);
    EXPECT_EQ(sparse_renderable->source_asset, sparse_mesh_key);
    EXPECT_EQ(sparse_renderable->render_program.type, kAssetTypeRenderProgram);
    EXPECT_FLOAT_EQ(sparse_renderable->bounds_min[0], -1.0f);
    EXPECT_FLOAT_EQ(sparse_renderable->bounds_max[2], 3.0f);
}

TEST(RenderableAssetModule, ResolvesMeshWireframeRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_mesh_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/cube",
            .kind = ProceduralMeshKind::Cube,
            });

    ASSERT_TRUE(mesh.valid());

    const auto renderable =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/cube_wireframe",
            .mesh = mesh,
            });

    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle =
        assets.renderables().get_renderable(renderable);

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.handle.type, kAssetTypeRenderable);

    const auto* data =
        assets.renderables().get_renderable_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->kind, RenderableKind::Mesh);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshWireframeDebug);
    EXPECT_EQ(data->domain, RenderDomain::Debug);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_Wireframe) != 0);
    EXPECT_FLOAT_EQ(data->bounds_min[0], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[1], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[2], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[0], 1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[1], 1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[2], 1.0f);
}

TEST(RenderableAssetModule, DuplicateMeshWireframeRegistrationReturnsSameAsset)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_duplicate_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/cube",
            .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(mesh.valid());

    const auto first =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/cube_wireframe",
            .mesh = mesh,
        });
    const auto second =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/cube_wireframe",
            .mesh = mesh,
        });

    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    EXPECT_EQ(second.output, first.output);
}

TEST(RenderableAssetModule, ResolvesDepthTestedMeshWireframeRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_depth_mesh_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/depth_cube",
            .kind = ProceduralMeshKind::Cube,
            });

    ASSERT_TRUE(mesh.valid());

    const auto renderable =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/depth_cube_wireframe",
            .mesh = mesh,
            .program = BuiltinRenderProgram::MeshWireframeDepthDebug,
            .domain = RenderDomain::Debug,
            .policy_flags =
                RenderPolicy_Wireframe
                | RenderPolicy_DepthTest
                | RenderPolicy_DepthWrite,
            });

    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle =
        assets.renderables().get_renderable(renderable);

    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->kind, RenderableKind::Mesh);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshWireframeDepthDebug);
    EXPECT_EQ(data->domain, RenderDomain::Debug);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_Wireframe) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthTest) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthWrite) != 0);
}

TEST(RenderableAssetModule, ResolvesStyledMeshWireframeRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_styled_mesh_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/styled_cube",
            .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(mesh.valid());

    MeshRenderStyleData style{};
    style.wireframe.color[0] = 0.05f;
    style.wireframe.color[1] = 0.9f;
    style.wireframe.color[2] = 0.2f;
    style.wireframe.color[3] = 0.75f;
    style.wireframe.emissive_strength = 3.0f;
    style.depth_test = true;
    style.depth_write = true;
    style.double_sided = false;
    style.hidden_line_prepass = true;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/battlezone_green",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/styled_cube_renderable",
            .mesh = mesh,
            .style = render_style,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 3u);

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const MeshHandle mesh_handle = assets.meshes().get_mesh(mesh);
    ASSERT_TRUE(mesh_handle.valid());
    const MeshData* mesh_data = assets.meshes().get_mesh_data(mesh_handle);
    ASSERT_NE(mesh_data, nullptr);
    EXPECT_TRUE(mesh_data->has_normals);
    EXPECT_FALSE(mesh_data->has_uv0);

    const auto* data =
        assets.renderables().get_renderable_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->kind, RenderableKind::Mesh);
    EXPECT_EQ(data->source_asset, mesh.output);
    EXPECT_EQ(data->companion_asset, render_style.output);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshWireframeDepthDebug);
    EXPECT_EQ(data->domain, RenderDomain::Opaque);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_Wireframe) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthTest) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthWrite) != 0);
    EXPECT_TRUE(data->mesh_style.wireframe.enabled);
    EXPECT_FALSE(data->mesh_style.surface.enabled);
    EXPECT_FLOAT_EQ(data->mesh_style.wireframe.color[1], 0.9f);
    EXPECT_FLOAT_EQ(data->mesh_style.wireframe.emissive_strength, 3.0f);
    EXPECT_FALSE(data->mesh_style.double_sided);
}

TEST(RenderableAssetModule, StyledMeshWireframeDepthTestSelectsDepthProgram)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_hidden_line_mesh_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/hidden_line_cube",
            .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(mesh.valid());

    MeshRenderStyleData style{};
    style.depth_test = true;
    style.depth_write = false;
    style.hidden_line_prepass = true;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/hidden_line",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/hidden_line_cube_renderable",
            .mesh = mesh,
            .style = render_style,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshWireframeDepthDebug);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthTest) != 0);
    EXPECT_FALSE((data->policy_flags & RenderPolicy_DepthWrite) != 0);
    EXPECT_FALSE(data->mesh_style.hidden_line_prepass);
}

TEST(RenderableAssetModule, TransparentSurfaceLayerResolvesSurfaceRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_surface_style_fallback_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/surface_style_cube",
            .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(mesh.valid());

    MeshRenderStyleData style{};
    style.wireframe.enabled = false;
    style.surface.enabled = true;
    style.surface.color[0] = 0.2f;
    style.surface.color[1] = 0.6f;
    style.surface.color[2] = 1.0f;
    style.alpha = 0.35f;
    style.depth_test = true;
    style.depth_write = false;
    style.hidden_line_prepass = false;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/transparent_surface_fallback",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/surface_style_cube_renderable",
            .mesh = mesh,
            .style = render_style,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->kind, RenderableKind::Mesh);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshSurfaceAlpha);
    EXPECT_EQ(data->domain, RenderDomain::Transparent);
    EXPECT_FALSE((data->policy_flags & RenderPolicy_Wireframe) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_AlphaBlend) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthTest) != 0);
    EXPECT_FALSE((data->policy_flags & RenderPolicy_DepthWrite) != 0);
    EXPECT_TRUE(data->mesh_style.surface.enabled);
    EXPECT_FLOAT_EQ(data->mesh_style.alpha, 0.35f);
}

TEST(RenderableAssetModule, OpaqueSurfaceMeshStyleResolvesSurfaceRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_opaque_surface_style_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/opaque_surface_cube",
            .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(mesh.valid());

    MeshRenderStyleData style{};
    style.wireframe.enabled = false;
    style.surface.enabled = true;
    style.surface.color[0] = 0.8f;
    style.surface.color[1] = 0.2f;
    style.surface.color[2] = 0.1f;
    style.depth_test = true;
    style.depth_write = true;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/opaque_surface",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/opaque_surface_cube_renderable",
            .mesh = mesh,
            .style = render_style,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->kind, RenderableKind::Mesh);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshSurface);
    EXPECT_EQ(data->domain, RenderDomain::Opaque);
    EXPECT_FALSE((data->policy_flags & RenderPolicy_Wireframe) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthTest) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthWrite) != 0);
    EXPECT_TRUE(data->mesh_style.surface.enabled);
    EXPECT_FLOAT_EQ(data->mesh_style.surface.color[0], 0.8f);
}

TEST(RenderableAssetModule, NearOpaqueSurfaceAlphaResolvesOpaqueDepthWritingRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_near_opaque_surface_style_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/near_opaque_surface_cube",
            .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(mesh.valid());

    MeshRenderStyleData style{};
    style.wireframe.enabled = false;
    style.surface.enabled = true;
    style.alpha = 0.9995f;
    style.depth_test = false;
    style.depth_write = false;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/near_opaque_surface",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/near_opaque_surface_cube_renderable",
            .mesh = mesh,
            .style = render_style,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshSurface);
    EXPECT_EQ(data->domain, RenderDomain::Opaque);
    EXPECT_FALSE((data->policy_flags & RenderPolicy_AlphaBlend) != 0);
    EXPECT_FALSE((data->policy_flags & RenderPolicy_DepthTest) != 0);
    EXPECT_FALSE((data->policy_flags & RenderPolicy_DepthWrite) != 0);
    EXPECT_FLOAT_EQ(data->mesh_style.alpha, 1.0f);
}

TEST(RenderableAssetModule, StyledMeshCanBindVertexDerivedFieldVisualization)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_mesh_field_visualization_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/field_quad",
            .kind = ProceduralMeshKind::Quad,
        });
    ASSERT_TRUE(mesh.valid());

    const auto field =
        assets.mesh_derived_fields().create_explicit_field({
            .name = "debug/field_quad_detail",
            .source_mesh = mesh,
            .domain = MeshDerivedFieldDomain::Vertex,
            .element_count = 4u,
            .channels = {
                MeshDerivedFieldChannelDesc{
                    .channel_id = MeshWaveletChannelID::kDetailCost,
                    .value_type = MeshDerivedFieldValueType::Float1,
                    .values = float_bytes({ 0.0f, 0.25f, 0.75f, 1.0f }),
                },
            },
        });
    ASSERT_TRUE(field.valid());

    MeshRenderStyleData style{};
    style.wireframe.enabled = false;
    style.surface.enabled = true;
    style.field_visualization.enabled = true;
    style.field_visualization.channel_id = MeshWaveletChannelID::kDetailCost;
    style.field_visualization.value_min = 0.0f;
    style.field_visualization.value_max = 1.0f;
    style.field_visualization.gamma = 0.8f;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/wavelet_detail_heat",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/field_quad_renderable",
            .mesh = mesh,
            .style = render_style,
            .mesh_field_visualization = field,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->kind, RenderableKind::Mesh);
    EXPECT_EQ(data->source_asset, mesh.output);
    EXPECT_EQ(data->mesh_field_visualization_asset, field.output);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshFieldHeatmap);
    EXPECT_TRUE(data->mesh_style.field_visualization.enabled);
    EXPECT_EQ(
        data->mesh_style.field_visualization.channel_id,
        MeshWaveletChannelID::kDetailCost);
    EXPECT_FLOAT_EQ(data->mesh_style.field_visualization.gamma, 0.8f);
}

TEST(RenderableAssetModule, StyledMeshIgnoresMissingVisualizationChannel)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_missing_mesh_field_channel_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/missing_field_channel_quad",
            .kind = ProceduralMeshKind::Quad,
        });
    ASSERT_TRUE(mesh.valid());

    const auto field =
        assets.mesh_derived_fields().create_explicit_field({
            .name = "debug/missing_field_channel_detail",
            .source_mesh = mesh,
            .domain = MeshDerivedFieldDomain::Vertex,
            .element_count = 4u,
            .channels = {
                MeshDerivedFieldChannelDesc{
                    .channel_id = 0x2200u,
                    .value_type = MeshDerivedFieldValueType::Float1,
                    .values = float_bytes({ 0.0f, 0.25f, 0.75f, 1.0f }),
                },
            },
        });
    ASSERT_TRUE(field.valid());

    MeshRenderStyleData style{};
    style.wireframe.enabled = false;
    style.surface.enabled = true;
    style.field_visualization.enabled = true;
    style.field_visualization.channel_id = 0x2300u;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/missing_field_channel_heat",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/missing_field_channel_renderable",
            .mesh = mesh,
            .style = render_style,
            .mesh_field_visualization = field,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->mesh_field_visualization_asset, wz::asset::AssetKey{});
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshSurface);
    EXPECT_FALSE(data->mesh_style.field_visualization.enabled);
}

TEST(RenderableAssetModule, StyledMeshAcceptsFaceFieldVisualization)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_face_mesh_field_visualization_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/face_field_quad",
            .kind = ProceduralMeshKind::Quad,
        });
    ASSERT_TRUE(mesh.valid());

    const auto field =
        assets.mesh_derived_fields().create_explicit_field({
            .name = "debug/face_field_quad_detail",
            .source_mesh = mesh,
            .domain = MeshDerivedFieldDomain::Face,
            .element_count = 2u,
            .channels = {
                MeshDerivedFieldChannelDesc{
                    .channel_id = MeshWaveletChannelID::kDetailCost,
                    .value_type = MeshDerivedFieldValueType::Float1,
                    .values = float_bytes({ 0.0f, 1.0f }),
                },
            },
        });
    ASSERT_TRUE(field.valid());

    MeshRenderStyleData style{};
    style.field_visualization.enabled = true;
    style.field_visualization.channel_id = MeshWaveletChannelID::kDetailCost;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/bad_wavelet_detail_heat",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/face_field_quad_renderable",
            .mesh = mesh,
            .style = render_style,
            .mesh_field_visualization = field,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->mesh_field_visualization_asset, field.output);
    EXPECT_TRUE(data->mesh_style.field_visualization.enabled);
}

TEST(RenderableAssetModule, StyledMeshAcceptsFaceUIntMaskVisualization)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_face_mask_visualization_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/face_mask_quad",
            .kind = ProceduralMeshKind::Quad,
        });
    ASSERT_TRUE(mesh.valid());

    const auto field =
        assets.mesh_derived_fields().create_explicit_field({
            .name = "debug/face_mask_quad_masks",
            .source_mesh = mesh,
            .domain = MeshDerivedFieldDomain::Face,
            .element_count = 2u,
            .channels = {
                MeshDerivedFieldChannelDesc{
                    .channel_id = 0x3000u,
                    .value_type = MeshDerivedFieldValueType::UInt1,
                    .values = uint_bytes({ 1u, 0u }),
                },
            },
        });
    ASSERT_TRUE(field.valid());

    MeshRenderStyleData style{};
    style.field_visualization.enabled = true;
    style.field_visualization.channel_id = 0x3000u;
    style.field_visualization.value_min = 0.0f;
    style.field_visualization.value_max = 1.0f;
    style.field_visualization.gamma = 1.0f;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/face_mask_heat",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/face_mask_quad_renderable",
            .mesh = mesh,
            .style = render_style,
            .mesh_field_visualization = field,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->mesh_field_visualization_asset, field.output);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshFieldHeatmap);
    EXPECT_TRUE(data->mesh_style.field_visualization.enabled);
    EXPECT_EQ(data->mesh_style.field_visualization.channel_id, 0x3000u);
}

TEST(RenderableAssetModule, StyledMeshAcceptsFaceMaskStyle)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_face_mesh_mask_style_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/face_mask_style_quad",
            .kind = ProceduralMeshKind::Quad,
        });
    ASSERT_TRUE(mesh.valid());

    const auto field =
        assets.mesh_derived_fields().create_explicit_field({
            .name = "debug/face_mask_style_field",
            .source_mesh = mesh,
            .domain = MeshDerivedFieldDomain::Face,
            .element_count = 2u,
            .channels = {
                MeshDerivedFieldChannelDesc{
                    .channel_id = 0x3300u,
                    .value_type = MeshDerivedFieldValueType::UInt1,
                    .values = uint_bytes({ 1u, 0u }),
                },
            },
        });
    ASSERT_TRUE(field.valid());

    MeshRenderStyleData style{};
    style.wireframe.enabled = false;
    style.surface.enabled = true;
    style.mask.enabled = true;
    style.mask.rules = {
        MeshMaskRule{
            .input_channel_id = 0x3300u,
            .lo = 1.0f,
            .hi = 1.0f,
            .color = { 0.2f, 0.8f, 1.0f, 1.0f },
            .priority = 7,
        },
    };

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/face_mask_style",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/face_mask_style_renderable",
            .mesh = mesh,
            .style = render_style,
            .mesh_field_visualization = field,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->mesh_field_visualization_asset, field.output);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshMaskStyle);
    EXPECT_TRUE(data->mesh_style.mask.enabled);
    ASSERT_EQ(data->mesh_style.mask.rules.size(), 1u);
    EXPECT_EQ(data->mesh_style.mask.rules[0].input_channel_id, 0x3300u);
}

TEST(RenderableAssetModule, StyledMeshAcceptsVertexMaskStyle)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_vertex_mesh_mask_style_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/vertex_mask_style_quad",
            .kind = ProceduralMeshKind::Quad,
        });
    ASSERT_TRUE(mesh.valid());

    const auto field =
        assets.mesh_derived_fields().create_explicit_field({
            .name = "debug/vertex_mask_style_field",
            .source_mesh = mesh,
            .domain = MeshDerivedFieldDomain::Vertex,
            .element_count = 4u,
            .channels = {
                MeshDerivedFieldChannelDesc{
                    .channel_id = 0x3300u,
                    .value_type = MeshDerivedFieldValueType::Float1,
                    .values = float_bytes({ 0.0f, 0.25f, 0.5f, 1.0f }),
                },
            },
        });
    ASSERT_TRUE(field.valid());

    MeshRenderStyleData style{};
    style.wireframe.enabled = false;
    style.surface.enabled = true;
    style.mask.enabled = true;
    style.mask.domain = MeshMaskDomain::Vertex;
    style.mask.rules = {
        MeshMaskRule{
            .input_channel_id = 0x3300u,
            .lo = 0.25f,
            .hi = 0.75f,
            .color = { 0.2f, 0.8f, 1.0f, 1.0f },
            .priority = 7,
        },
    };

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/vertex_mask_style",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/vertex_mask_style_renderable",
            .mesh = mesh,
            .style = render_style,
            .mesh_field_visualization = field,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->mesh_field_visualization_asset, field.output);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshMaskStyle);
    EXPECT_TRUE(data->mesh_style.mask.enabled);
    EXPECT_EQ(data->mesh_style.mask.domain, MeshMaskDomain::Vertex);
    ASSERT_EQ(data->mesh_style.mask.rules.size(), 1u);
    EXPECT_EQ(data->mesh_style.mask.rules[0].input_channel_id, 0x3300u);
}

TEST(RenderableAssetModule, StyledMeshAcceptsFaceMaskStyleWithoutBaseLayers)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_face_mesh_mask_only_style_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/face_mask_only_style_quad",
            .kind = ProceduralMeshKind::Quad,
        });
    ASSERT_TRUE(mesh.valid());

    const auto field =
        assets.mesh_derived_fields().create_explicit_field({
            .name = "debug/face_mask_only_style_field",
            .source_mesh = mesh,
            .domain = MeshDerivedFieldDomain::Face,
            .element_count = 2u,
            .channels = {
                MeshDerivedFieldChannelDesc{
                    .channel_id = 0x3300u,
                    .value_type = MeshDerivedFieldValueType::UInt1,
                    .values = uint_bytes({ 1u, 0u }),
                },
            },
        });
    ASSERT_TRUE(field.valid());

    MeshRenderStyleData style{};
    style.wireframe.enabled = false;
    style.surface.enabled = false;
    style.mask.enabled = true;
    style.mask.rules = {
        MeshMaskRule{
            .input_channel_id = 0x3300u,
            .lo = 1.0f,
            .hi = 1.0f,
            .color = { 0.2f, 0.8f, 1.0f, 1.0f },
            .priority = 7,
        },
    };

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/face_mask_only_style",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/face_mask_only_style_renderable",
            .mesh = mesh,
            .style = render_style,
            .mesh_field_visualization = field,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->mesh_field_visualization_asset, field.output);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshMaskStyle);
    EXPECT_FALSE(data->mesh_style.wireframe.enabled);
    EXPECT_FALSE(data->mesh_style.surface.enabled);
    EXPECT_TRUE(data->mesh_style.mask.enabled);
}

TEST(RenderableAssetModule, StyledMeshMaskOnlyKeepsShowUnmatchedForVertexField)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_mask_unmatched_vertex_field_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/mask_unmatched_vertex_quad",
            .kind = ProceduralMeshKind::Quad,
        });
    ASSERT_TRUE(mesh.valid());

    const auto field =
        assets.mesh_derived_fields().create_explicit_field({
            .name = "debug/mask_unmatched_vertex_field",
            .source_mesh = mesh,
            .domain = MeshDerivedFieldDomain::Vertex,
            .element_count = 4u,
            .channels = {
                MeshDerivedFieldChannelDesc{
                    .channel_id = 0x3300u,
                    .value_type = MeshDerivedFieldValueType::Float1,
                    .values = float_bytes({ 0.0f, 0.25f, 0.5f, 1.0f }),
                },
            },
        });
    ASSERT_TRUE(field.valid());

    MeshRenderStyleData style{};
    style.wireframe.enabled = false;
    style.surface.enabled = false;
    style.mask.enabled = true;
    style.mask.show_unmatched = true;
    style.mask.rules = {
        MeshMaskRule{
            .input_channel_id = 0x3300u,
            .lo = 0.25f,
            .hi = 0.75f,
            .color = { 0.2f, 0.8f, 1.0f, 1.0f },
            .priority = 7,
        },
    };

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/mask_unmatched_vertex_field",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/mask_unmatched_vertex_renderable",
            .mesh = mesh,
            .style = render_style,
            .mesh_field_visualization = field,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshMaskStyle);
    EXPECT_FALSE(data->mesh_style.wireframe.enabled);
    EXPECT_FALSE(data->mesh_style.surface.enabled);
    EXPECT_TRUE(data->mesh_style.mask.enabled);
    EXPECT_TRUE(data->mesh_style.mask.show_unmatched);
}

TEST(RenderableAssetModule, StyledMeshWithNoEnabledLayersResolvesAsNonDrawing)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_empty_mesh_style_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/empty_style_cube",
            .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(mesh.valid());

    MeshRenderStyleData style{};
    style.wireframe.enabled = false;
    style.surface.enabled = false;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/empty",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/empty_style_cube_renderable",
            .mesh = mesh,
            .style = render_style,
        });
    ASSERT_TRUE(renderable.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle = assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data = assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_FALSE(data->mesh_style.wireframe.enabled);
    EXPECT_FALSE(data->mesh_style.surface.enabled);
    EXPECT_EQ(data->policy_flags, RenderPolicy_None);
    EXPECT_EQ(data->mesh_field_visualization_asset, wz::asset::AssetKey{});
}

TEST(RenderableAssetModule, MeshWireframeRenderableDomainParticipatesInIdentity)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_mesh_domain_identity_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/shared_quad",
            .kind = ProceduralMeshKind::Quad,
        });

    ASSERT_TRUE(mesh.valid());

    const auto debug_renderable =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/shared_quad_wireframe",
            .mesh = mesh,
            .domain = RenderDomain::Debug,
        });

    const auto opaque_renderable =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/shared_quad_wireframe",
            .mesh = mesh,
            .domain = RenderDomain::Opaque,
        });

    ASSERT_TRUE(debug_renderable.valid());
    ASSERT_TRUE(opaque_renderable.valid());
    EXPECT_FALSE(debug_renderable.output == opaque_renderable.output);

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 3u);

    const auto debug_handle =
        assets.renderables().get_renderable(debug_renderable);
    const auto opaque_handle =
        assets.renderables().get_renderable(opaque_renderable);

    ASSERT_TRUE(debug_handle.valid());
    ASSERT_TRUE(opaque_handle.valid());

    const auto* debug_data =
        assets.renderables().get_renderable_data(debug_handle);
    const auto* opaque_data =
        assets.renderables().get_renderable_data(opaque_handle);

    ASSERT_NE(debug_data, nullptr);
    ASSERT_NE(opaque_data, nullptr);
    EXPECT_EQ(debug_data->domain, RenderDomain::Debug);
    EXPECT_EQ(opaque_data->domain, RenderDomain::Opaque);
}

TEST(RenderableAssetModule, MeshWireframeRenderableBoundsComeFromMeshVertices)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_mesh_bounds_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/quad",
            .kind = ProceduralMeshKind::Quad,
        });

    ASSERT_TRUE(mesh.valid());

    const auto renderable =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/quad_wireframe",
            .mesh = mesh,
        });

    ASSERT_TRUE(renderable.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle =
        assets.renderables().get_renderable(renderable);

    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_FLOAT_EQ(data->bounds_min[0], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[1], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[2], 0.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[0], 1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[1], 1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[2], 0.0f);
}

