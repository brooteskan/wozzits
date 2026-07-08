#include "renderable_asset_module_test_support.h"

#include <cstddef>
#include <asset/draft.h>
#include <asset/system.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/key_factories/renderable.h>
#include <engine/assets/mesh_style_pull_program.h>
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

    constexpr wz::asset::SchemaID kRhiRenderableTestStyleSchema{
        0x7111'0000'0000'0004ull
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
    PlacementTable placement_table;
    PlacedFieldTable placed_field_table;
    AudioClipTable audio_clip_table;
    AudioClipBankTable audio_clip_bank_table;
    AudioRenderableTable audio_renderable_table;
    GaussianSplatCloudTable gaussian_splat_cloud_table;
    GaussianSplatColorLODTable gaussian_splat_color_lod_table;
    DataTable data_table;
    DiagnosticResampledTimeSeriesTable diagnostic_resampled_time_series_table;
    DiagnosticTimeframeSummaryTable diagnostic_timeframe_summary_table;
    CSVExportTable csv_export_table;
    MeshRenderStyleTable mesh_render_style_table;
    RenderBindingLayoutTable render_binding_layout_table;
    RenderableAssetTable renderable_table;
    RhiRenderableTable rhi_renderable_table;
    RenderProgramTable render_program_table;
    ComputePipelineTable compute_pipeline_table;
    DirectLightTable direct_light_table;
    AmbientLightingTable ambient_lighting_table;
    HDRIEnvironmentTable hdri_environment_table;
    SkyGaussianTable sky_gaussian_table;
    wz::engine::starfield::StarCatalogTable star_catalog_table;
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
        .placement_table = placement_table,
        .placed_field_table = placed_field_table,
        .audio_clip_table = audio_clip_table,
        .audio_clip_bank_table = audio_clip_bank_table,
        .audio_renderable_table = audio_renderable_table,
        .gaussian_splat_cloud_table = gaussian_splat_cloud_table,
        .gaussian_splat_color_lod_table = gaussian_splat_color_lod_table,
        .data_table = data_table,
        .diagnostic_resampled_time_series_table =
            diagnostic_resampled_time_series_table,
        .diagnostic_timeframe_summary_table =
            diagnostic_timeframe_summary_table,
        .csv_export_table = csv_export_table,
        .mesh_render_style_table = mesh_render_style_table,
        .render_binding_layout_table = render_binding_layout_table,
        .renderable_table = renderable_table,
        .rhi_renderable_table = rhi_renderable_table,
        .render_program_table = render_program_table,
        .compute_pipeline_table = compute_pipeline_table,
        .direct_light_table = direct_light_table,
        .ambient_lighting_table = ambient_lighting_table,
        .hdri_environment_table = hdri_environment_table,
        .sky_gaussian_table = sky_gaussian_table,
        .star_catalog_table = star_catalog_table,
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
    EXPECT_EQ(compiled->handle.type, kAssetTypeRhiRenderableRecipe);
    EXPECT_EQ(renderable_table.get(compiled->handle), nullptr);

    const RhiRenderableRecipe* sparse_recipe =
        rhi_renderable_table.get(compiled->handle);
    ASSERT_NE(sparse_recipe, nullptr);
    EXPECT_EQ(sparse_recipe->gpu_sparse_mesh_key, sparse_mesh_key);
    EXPECT_EQ(sparse_recipe->program_key, program_key);
    EXPECT_EQ(sparse_recipe->mesh_key, AssetKey{});
}

// Issue #195 slice A: an optional MeshRenderStyle dependency on the 0x706 pull
// mesh renderable bakes the style's SHADING constants (colours / emissive /
// alpha / layer-enable) into the recipe. This covers (a) styled compile lands
// the constants, (b) the no-style default is preserved when the port is absent.
TEST(RenderableAssetModule, RhiPullMeshRenderableBakesOptionalStyleShading)
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
    PlacementTable placement_table;
    PlacedFieldTable placed_field_table;
    AudioClipTable audio_clip_table;
    AudioClipBankTable audio_clip_bank_table;
    AudioRenderableTable audio_renderable_table;
    GaussianSplatCloudTable gaussian_splat_cloud_table;
    GaussianSplatColorLODTable gaussian_splat_color_lod_table;
    DataTable data_table;
    DiagnosticResampledTimeSeriesTable diagnostic_resampled_time_series_table;
    DiagnosticTimeframeSummaryTable diagnostic_timeframe_summary_table;
    CSVExportTable csv_export_table;
    MeshRenderStyleTable mesh_render_style_table;
    RenderBindingLayoutTable render_binding_layout_table;
    RenderableAssetTable renderable_table;
    RhiRenderableTable rhi_renderable_table;
    RenderProgramTable render_program_table;
    ComputePipelineTable compute_pipeline_table;
    DirectLightTable direct_light_table;
    AmbientLightingTable ambient_lighting_table;
    HDRIEnvironmentTable hdri_environment_table;
    SkyGaussianTable sky_gaussian_table;
    wz::engine::starfield::StarCatalogTable star_catalog_table;
    SceneAssetTable scene_table;
    EngineAssetCacheSettings cache_settings{};

    // Distinctive style values so we can assert each baked field individually.
    MeshRenderStyleData authored_style{};
    authored_style.wireframe.enabled = true;
    authored_style.wireframe.color[0] = 0.1f;
    authored_style.wireframe.color[1] = 0.2f;
    authored_style.wireframe.color[2] = 0.3f;
    authored_style.wireframe.color[3] = 0.4f;
    authored_style.wireframe.emissive_strength = 0.5f;
    authored_style.surface.enabled = false;
    authored_style.surface.color[0] = 0.6f;
    authored_style.surface.color[1] = 0.7f;
    authored_style.surface.color[2] = 0.8f;
    authored_style.surface.color[3] = 0.9f;
    authored_style.surface.emissive_strength = 0.25f;
    authored_style.alpha = 0.75f;

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
                .id = 1, .epoch = 1, .type = AssetType::Shader };
            data.pixel_shader = ResourceHandle{
                .id = 2, .epoch = 1, .type = AssetType::Shader };
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = render_program_table.add(std::move(data));
            return out;
        },
    });
    registry.register_compiler(AssetCompiler{
        .input_schema = kRhiRenderableTestStyleSchema,
        .output_type = kAssetTypeMeshRenderStyle,
        .compile = [&mesh_render_style_table, authored_style](
            const AssetNode& input,
            std::span<const AssetNode>,
            std::span<const ResourceHandle>) -> AssetNode
        {
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = mesh_render_style_table.add(authored_style);
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
        .gpu_resident_sparse_mesh_table = gpu_resident_sparse_mesh_table,
        .gpu_resident_mesh_cluster_hierarchy_table =
            gpu_resident_mesh_cluster_hierarchy_table,
        .terrain_table = terrain_table,
        .terrain_visual_proxy_table = terrain_visual_proxy_table,
        .collision_table = collision_table,
        .placement_table = placement_table,
        .placed_field_table = placed_field_table,
        .audio_clip_table = audio_clip_table,
        .audio_clip_bank_table = audio_clip_bank_table,
        .audio_renderable_table = audio_renderable_table,
        .gaussian_splat_cloud_table = gaussian_splat_cloud_table,
        .gaussian_splat_color_lod_table = gaussian_splat_color_lod_table,
        .data_table = data_table,
        .diagnostic_resampled_time_series_table =
            diagnostic_resampled_time_series_table,
        .diagnostic_timeframe_summary_table =
            diagnostic_timeframe_summary_table,
        .csv_export_table = csv_export_table,
        .mesh_render_style_table = mesh_render_style_table,
        .render_binding_layout_table = render_binding_layout_table,
        .renderable_table = renderable_table,
        .rhi_renderable_table = rhi_renderable_table,
        .render_program_table = render_program_table,
        .compute_pipeline_table = compute_pipeline_table,
        .direct_light_table = direct_light_table,
        .ambient_lighting_table = ambient_lighting_table,
        .hdri_environment_table = hdri_environment_table,
        .sky_gaussian_table = sky_gaussian_table,
        .star_catalog_table = star_catalog_table,
        .scene_table = scene_table,
        .cache_settings = cache_settings,
    };
    internal::register_renderable_compilers(registry, ctx);

    const AssetKey mesh_key = test_asset_key(11);
    const AssetKey program_key = test_asset_key(12);
    const AssetKey style_key = test_asset_key(13);

    // ── (a) styled compile: mesh + program + style ──────────────────────────
    const AssetKey styled_key =
        make_rhi_pull_mesh_renderable_key(
            "test/rhi_pull_mesh_styled", mesh_key, program_key, style_key);

    AssetGraphDraft draft{};
    const AssetGraphDraftNodeId mesh_node = add_asset_graph_draft_node(
        draft,
        test_source_node(mesh_key, kAssetTypeMesh, kRhiRenderableTestMeshSchema),
        AssetGraphDraftNodeState::Existing);
    const AssetGraphDraftNodeId program_node = add_asset_graph_draft_node(
        draft,
        test_source_node(
            program_key, kAssetTypeRenderProgram, kRhiRenderableTestProgramSchema),
        AssetGraphDraftNodeState::Existing);
    const AssetGraphDraftNodeId style_node = add_asset_graph_draft_node(
        draft,
        test_source_node(
            style_key, kAssetTypeMeshRenderStyle, kRhiRenderableTestStyleSchema),
        AssetGraphDraftNodeState::Existing);

    AssetNode styled_node =
        test_source_node(styled_key, kAssetTypeRenderable, kRhiPullMeshRenderableSchema);
    styled_node.meta = RhiPullMeshRenderableCompileDesc{
        .mesh_asset = mesh_key,
        .render_program_asset = program_key,
        .style_asset = style_key,
    };
    const AssetGraphDraftNodeId styled_draft_node = add_asset_graph_draft_node(
        draft, std::move(styled_node), AssetGraphDraftNodeState::Existing);

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh_node, styled_draft_node, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, program_node, styled_draft_node, 1),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, style_node, styled_draft_node, 2),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    ASSERT_TRUE(validate_asset_graph_draft(draft, registry));

    const std::vector<AssetGraphDraftRegistration> registrations =
        asset_graph_draft_to_registrations(draft, &registry);
    std::vector<AssetSystem::RegistrationEntry> entries;
    entries.reserve(registrations.size());
    for (const AssetGraphDraftRegistration& registration : registrations) {
        entries.push_back(AssetSystem::RegistrationEntry{
            .node = registration.node, .dep_keys = registration.dep_keys });
    }

    // ── (b) no-style default draft: mesh + program only, style unconnected.
    // Built + validated here (before the registry is moved into the system).
    const AssetKey unstyled_key =
        make_rhi_pull_mesh_renderable_key(
            "test/rhi_pull_mesh_unstyled", mesh_key, program_key);

    AssetGraphDraft draft2{};
    const AssetGraphDraftNodeId mesh_node2 = add_asset_graph_draft_node(
        draft2,
        test_source_node(mesh_key, kAssetTypeMesh, kRhiRenderableTestMeshSchema),
        AssetGraphDraftNodeState::Existing);
    const AssetGraphDraftNodeId program_node2 = add_asset_graph_draft_node(
        draft2,
        test_source_node(
            program_key, kAssetTypeRenderProgram, kRhiRenderableTestProgramSchema),
        AssetGraphDraftNodeState::Existing);
    AssetNode unstyled_node =
        test_source_node(unstyled_key, kAssetTypeRenderable, kRhiPullMeshRenderableSchema);
    unstyled_node.meta = RhiPullMeshRenderableCompileDesc{
        .mesh_asset = mesh_key,
        .render_program_asset = program_key,
    };
    const AssetGraphDraftNodeId unstyled_draft_node = add_asset_graph_draft_node(
        draft2, std::move(unstyled_node), AssetGraphDraftNodeState::Existing);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft2, mesh_node2, unstyled_draft_node, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft2, program_node2, unstyled_draft_node, 1),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_TRUE(validate_asset_graph_draft(draft2, registry));

    const std::vector<AssetGraphDraftRegistration> registrations2 =
        asset_graph_draft_to_registrations(draft2, &registry);
    std::vector<AssetSystem::RegistrationEntry> entries2;
    entries2.reserve(registrations2.size());
    for (const AssetGraphDraftRegistration& registration : registrations2) {
        entries2.push_back(AssetSystem::RegistrationEntry{
            .node = registration.node, .dep_keys = registration.dep_keys });
    }

    // Registry is consumed by the system from here on; all validation above is
    // complete.
    AssetSystem system(std::move(registry));
    ASSERT_TRUE(system.replace_registered_assets(std::move(entries)));

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(system.resolve_all(&errors), 4u);
    EXPECT_TRUE(errors.empty());

    const AssetSystem::CompiledAsset* compiled =
        system.find_compiled(styled_key);
    ASSERT_NE(compiled, nullptr);
    ASSERT_TRUE(compiled->handle.valid());
    const RhiRenderableRecipe* recipe =
        rhi_renderable_table.get(compiled->handle);
    ASSERT_NE(recipe, nullptr);
    EXPECT_EQ(recipe->mesh_key, mesh_key);
    EXPECT_EQ(recipe->program_key, program_key);
    EXPECT_TRUE(recipe->style.has_style);
    EXPECT_TRUE(recipe->style.wireframe_enabled);
    EXPECT_FALSE(recipe->style.surface_enabled);
    EXPECT_FLOAT_EQ(recipe->style.wireframe_color[0], 0.1f);
    EXPECT_FLOAT_EQ(recipe->style.wireframe_color[3], 0.4f);
    EXPECT_FLOAT_EQ(recipe->style.wireframe_emissive, 0.5f);
    EXPECT_FLOAT_EQ(recipe->style.surface_color[1], 0.7f);
    EXPECT_FLOAT_EQ(recipe->style.surface_emissive, 0.25f);
    EXPECT_FLOAT_EQ(recipe->style.alpha, 0.75f);

    ASSERT_TRUE(system.replace_registered_assets(std::move(entries2)));
    errors.clear();
    EXPECT_EQ(system.resolve_all(&errors), 3u);
    EXPECT_TRUE(errors.empty());

    compiled = system.find_compiled(unstyled_key);
    ASSERT_NE(compiled, nullptr);
    ASSERT_TRUE(compiled->handle.valid());
    const RhiRenderableRecipe* unstyled_recipe =
        rhi_renderable_table.get(compiled->handle);
    ASSERT_NE(unstyled_recipe, nullptr);
    EXPECT_EQ(unstyled_recipe->mesh_key, mesh_key);
    EXPECT_EQ(unstyled_recipe->program_key, program_key);
    EXPECT_FALSE(unstyled_recipe->style.has_style);
}


// ── Issue #195 rewrites of the deleted 0x700/0x705 legacy tests ─────────────
//
// The legacy suite that followed this point asserted 0x700 (mesh wireframe)
// and 0x705 (mesh styled) behaviors. Those schemas were deleted; the SAME
// intents are covered on the 0x706 path where representable:
//   - module create + resolve            -> the two draft-based 0x706 tests above
//   - duplicate-registration dedup       -> DuplicateRhiPullMeshRegistrationReturnsSameAsset
//   - identity folding (domain/style)    -> RhiPullMeshRenderableStyleParticipatesInIdentity
//   - depth/raster/blend program select  -> MeshStylePullProgramDerivesPipelineStateIdentity
//     (depth behavior is a PROGRAM property now: DepthMode/RasterMode/BlendMode
//     on the provisioned program asset, derived from the style)
//   - styled shading compile             -> RhiPullMeshRenderableBakesOptionalStyleShading
// Intents NOT representable on the new path were deleted with these reasons:
//   - BuiltinRenderProgram enum selection (ResolvesMeshWireframeRenderable,
//     ResolvesDepthTestedMeshWireframeRenderable, ResolvesStyledMeshWireframe-
//     Renderable, StyledMeshWireframeDepthTestSelectsDepthProgram, Transparent/
//     Opaque/NearOpaqueSurface* trio): the enum is retired; the equivalent
//     contract (pipeline state folds into program identity) is asserted below.
//   - field_visualization / mask compile behaviors (StyledMeshCanBindVertex-
//     DerivedFieldVisualization, StyledMeshIgnoresMissingVisualizationChannel,
//     StyledMeshAcceptsFace*/*Vertex* mask quintet, StyledMeshMaskOnlyKeeps-
//     ShowUnmatchedForVertexField, StyledMeshWithNoEnabledLayersResolvesAs-
//     NonDrawing): geometry-generating style parts are deliberately NOT ported
//     to the 0x706 recipe (#195 scope) — the style asset keeps the fields, the
//     recipe ignores them.
//   - RenderableAssetData bounds (MeshWireframeRenderableBoundsComeFromMesh-
//     Vertices): the rhi recipe carries no baked bounds; the renderer derives
//     geometry from the mesh asset directly.

TEST(RenderableAssetModule, DuplicateRhiPullMeshRegistrationReturnsSameAsset)
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

    // Registration-only (no resolve): the program dep is a fabricated key —
    // create_rhi_pull_mesh keys and dedups purely on (name, mesh, program,
    // style) identity.
    const RenderProgramAsset program{ .key = test_asset_key(77) };

    const auto first =
        assets.renderables().create_rhi_pull_mesh({
            .name = "debug/cube_pull",
            .mesh = mesh,
            .program = program,
        });
    const auto second =
        assets.renderables().create_rhi_pull_mesh({
            .name = "debug/cube_pull",
            .mesh = mesh,
            .program = program,
        });

    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    EXPECT_EQ(second.output, first.output);

    // A styled registration is a DISTINCT asset from the unstyled one, and
    // dedups against itself the same way.
    const auto style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "debug/cube_style",
            .style = MeshRenderStyleData{},
        });
    ASSERT_TRUE(style.valid());

    const auto styled_first =
        assets.renderables().create_rhi_pull_mesh({
            .name = "debug/cube_pull",
            .mesh = mesh,
            .program = program,
            .style = style,
        });
    const auto styled_second =
        assets.renderables().create_rhi_pull_mesh({
            .name = "debug/cube_pull",
            .mesh = mesh,
            .program = program,
            .style = style,
        });
    ASSERT_TRUE(styled_first.valid());
    EXPECT_EQ(styled_second.output, styled_first.output);
    EXPECT_NE(styled_first.output, first.output);
}

TEST(RenderableAssetModule, RhiPullMeshRenderableStyleParticipatesInIdentity)
{
    using namespace wz::asset;
    using namespace wz::engine::assets;

    const AssetKey mesh_key = test_asset_key(21);
    const AssetKey program_key = test_asset_key(22);
    const AssetKey other_program_key = test_asset_key(23);
    const AssetKey style_key = test_asset_key(24);

    const AssetKey base =
        make_rhi_pull_mesh_renderable_key("r", mesh_key, program_key);

    // The program folds into identity (the 0x700-era "domain participates in
    // identity" contract, restated for the program-property world).
    EXPECT_FALSE(
        make_rhi_pull_mesh_renderable_key("r", mesh_key, other_program_key)
        == base);

    // The optional style folds into identity...
    EXPECT_FALSE(
        make_rhi_pull_mesh_renderable_key("r", mesh_key, program_key, style_key)
        == base);

    // ...and an EMPTY style reproduces the styleless key bit-for-bit, so
    // pre-#195 unstyled pull meshes keep their identity.
    EXPECT_TRUE(
        make_rhi_pull_mesh_renderable_key(
            "r", mesh_key, program_key, AssetKey{})
        == base);
}

// Depth/raster/blend are PROGRAM properties now: the provisioning helper
// derives them from the style and folds them into the program's identity, so
// styles that need different pipeline state get different program assets while
// identical state dedups to one. This is the 0x705 program-selection matrix
// (wireframe vs depth-tested vs transparent vs opaque surface) restated as the
// recipe-level contract. Deviceless on purpose: provisioning only REGISTERS
// assets (staging shader files + shader pair + program), it never resolves.
TEST(RenderableAssetModule, MeshStylePullProgramDerivesPipelineStateIdentity)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_mesh_style_pull_program_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    MeshRenderStyleData wireframe_style{};
    wireframe_style.wireframe.enabled = true;
    wireframe_style.surface.enabled = false;
    wireframe_style.depth_test = true;
    wireframe_style.depth_write = false;

    MeshRenderStyleData surface_style{};
    surface_style.wireframe.enabled = false;
    surface_style.surface.enabled = true;
    surface_style.depth_test = true;
    surface_style.depth_write = true;

    MeshRenderStyleData no_depth_surface = surface_style;
    no_depth_surface.depth_test = false;
    no_depth_surface.depth_write = false;

    MeshRenderStyleData transparent_surface = surface_style;
    transparent_surface.alpha = 0.5f;

    const auto wire_program = ensure_mesh_style_pull_program(
        logger, assets.files(), assets.shaders(), assets.render_programs(),
        wireframe_style);
    const auto surface_program = ensure_mesh_style_pull_program(
        logger, assets.files(), assets.shaders(), assets.render_programs(),
        surface_style);
    const auto no_depth_program = ensure_mesh_style_pull_program(
        logger, assets.files(), assets.shaders(), assets.render_programs(),
        no_depth_surface);
    const auto transparent_program = ensure_mesh_style_pull_program(
        logger, assets.files(), assets.shaders(), assets.render_programs(),
        transparent_surface);
    const auto wire_again = ensure_mesh_style_pull_program(
        logger, assets.files(), assets.shaders(), assets.render_programs(),
        wireframe_style);

    ASSERT_TRUE(wire_program.valid());
    ASSERT_TRUE(surface_program.valid());
    ASSERT_TRUE(no_depth_program.valid());
    ASSERT_TRUE(transparent_program.valid());

    // Wireframe raster vs solid raster -> distinct programs.
    EXPECT_FALSE(wire_program.key == surface_program.key);
    // Depth on vs off -> distinct programs (the 0x705 depth-test selection).
    EXPECT_FALSE(surface_program.key == no_depth_program.key);
    // Opaque vs alpha-blend -> distinct programs (the transparent-surface case).
    EXPECT_FALSE(surface_program.key == transparent_program.key);
    // Identical style-derived state dedups to ONE program asset.
    EXPECT_TRUE(wire_again.key == wire_program.key);

    // The canonical shader source was staged into the project (write-if-
    // missing) so the registered shader pair can resolve there.
    EXPECT_TRUE(wz::fs::exists(
        wz::fs::join(root, "shaders/mesh_style/mesh_style_pull_vs.hlsl")));
    EXPECT_TRUE(wz::fs::exists(
        wz::fs::join(root, "shaders/mesh_style/mesh_style_pull_ps.hlsl")));
}
