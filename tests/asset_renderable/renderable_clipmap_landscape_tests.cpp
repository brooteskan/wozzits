#include "renderable_asset_module_test_support.h"

#include <asset/draft.h>
#include <asset/system.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/key_factories/renderable.h>
#include <engine/assets/mesh/clipmap_lattice_mesh.h>
#include <engine/assets/schema_ids.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace
{
    // Test-only carrier schemas for the three clipmap landscape dependencies.
    constexpr wz::asset::SchemaID kClipmapTestLatticeSchema{
        0x7112'0000'0000'0001ull
    };
    constexpr wz::asset::SchemaID kClipmapTestHeightFieldSchema{
        0x7112'0000'0000'0002ull
    };
    constexpr wz::asset::SchemaID kClipmapTestProgramSchema{
        0x7112'0000'0000'0003ull
    };
    // Test-only carrier for an optional Placement dependency (issue #218 Phase 2).
    constexpr wz::asset::SchemaID kClipmapTestPlacementSchema{
        0x7112'0000'0000'0004ull
    };

    wz::asset::AssetKey clipmap_test_key(uint64_t id)
    {
        return wz::asset::AssetKey{
            .content_hash = { id, 0x71120000ull },
            .schema_hash = { id ^ 0x1000ull, 0x71120001ull },
            .compiler_hash = { id ^ 0x2000ull, 0x71120002ull },
            .deps_hash = { id ^ 0x3000ull, 0x71120003ull },
        };
    }

    wz::asset::AssetNode clipmap_test_source_node(
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

    wz::engine::assets::ScalarFieldData clipmap_test_height_field()
    {
        wz::engine::assets::ScalarFieldData field{};
        field.width = 4u;
        field.height = 4u;
        field.depth = 1u;
        field.format = wz::engine::assets::ScalarFieldFormat::Float32;
        field.values.assign(
            static_cast<size_t>(field.count()), 0.0f);
        return field;
    }

    // The clipmap landscape compiler does no mesh-field compute work; a backend
    // that reports unavailable and no-ops satisfies the EngineAssetContext.
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

// Authoring a clipmap landscape renderable over a lattice mesh + a height
// scalar field + a render program must compile to an RhiRenderableRecipe whose
// mesh_key is the lattice, height_texture_key is the scalar field, program_key
// is the render program, and whose clipmap settings round-trip intact.
TEST(RenderableAssetModule, ClipmapLandscapeRenderableRecipeCarriesKeysAndSettings)
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
    AudioClipTable audio_clip_table;
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
        .input_schema = kClipmapTestLatticeSchema,
        .output_type = kAssetTypeMesh,
        .compile = [&mesh_table](
            const AssetNode& input,
            std::span<const AssetNode>,
            std::span<const ResourceHandle>) -> AssetNode
        {
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = mesh_table.add(
                make_clipmap_lattice_mesh(ClipmapLatticeParams{
                    .level_count = 3u,
                    .base_resolution = 8u,
                    .cell_size = 1.0f,
                }));
            return out;
        },
    });
    registry.register_compiler(AssetCompiler{
        .input_schema = kClipmapTestHeightFieldSchema,
        .output_type = kAssetTypeScalarField,
        .compile = [&scalar_fields_table](
            const AssetNode& input,
            std::span<const AssetNode>,
            std::span<const ResourceHandle>) -> AssetNode
        {
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload =
                scalar_fields_table.add(clipmap_test_height_field());
            return out;
        },
    });
    registry.register_compiler(AssetCompiler{
        .input_schema = kClipmapTestProgramSchema,
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
        .audio_clip_table = audio_clip_table,
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

    const AssetKey lattice_key = clipmap_test_key(1);
    const AssetKey height_key = clipmap_test_key(2);
    const AssetKey program_key = clipmap_test_key(3);

    const ClipmapLandscapeRenderSettings settings{
        .world_origin = { -128.0f, -64.0f },
        .world_size = { 256.0f, 256.0f },
        .vertical_scale = 40.0f,
        .base_height = -5.0f,
        .lattice_world_cell_size = 2.0f,
    };

    const AssetKey renderable_key =
        make_clipmap_landscape_renderable_key(
            "test/clipmap_landscape_renderable",
            lattice_key,
            height_key,
            program_key,
            settings);

    AssetGraphDraft draft{};
    const AssetGraphDraftNodeId lattice_node =
        add_asset_graph_draft_node(
            draft,
            clipmap_test_source_node(
                lattice_key,
                kAssetTypeMesh,
                kClipmapTestLatticeSchema),
            AssetGraphDraftNodeState::Existing);
    const AssetGraphDraftNodeId height_node =
        add_asset_graph_draft_node(
            draft,
            clipmap_test_source_node(
                height_key,
                kAssetTypeScalarField,
                kClipmapTestHeightFieldSchema),
            AssetGraphDraftNodeState::Existing);
    const AssetGraphDraftNodeId program_node =
        add_asset_graph_draft_node(
            draft,
            clipmap_test_source_node(
                program_key,
                kAssetTypeRenderProgram,
                kClipmapTestProgramSchema),
            AssetGraphDraftNodeState::Existing);

    AssetNode renderable_node =
        clipmap_test_source_node(
            renderable_key,
            kAssetTypeRenderable,
            kClipmapLandscapeRenderableSchema);
    renderable_node.meta = ClipmapLandscapeRenderableCompileDesc{
        .lattice_mesh_asset = lattice_key,
        .height_field_asset = height_key,
        .render_program_asset = program_key,
        .settings = settings,
    };
    const AssetGraphDraftNodeId renderable_draft_node =
        add_asset_graph_draft_node(
            draft,
            std::move(renderable_node),
            AssetGraphDraftNodeState::Existing);

    // Edge port order must match the compiler's input ports:
    // lattice (0), height field (1), program (2).
    ASSERT_NE(
        connect_asset_graph_draft_nodes(
            draft, lattice_node, renderable_draft_node, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(
            draft, height_node, renderable_draft_node, 1),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(
            draft, program_node, renderable_draft_node, 2),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    if (!validate_asset_graph_draft(draft, registry)) {
        for (const AssetGraphDraftValidationMessage& message :
             draft.validation_messages)
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
    ASSERT_TRUE(draft.validation_messages.empty());

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

    AssetSystem system(std::move(registry));
    ASSERT_TRUE(system.replace_registered_assets(std::move(entries)));

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(system.resolve_all(&errors), 4u);
    EXPECT_TRUE(errors.empty());

    const AssetSystem::CompiledAsset* compiled =
        system.find_compiled(renderable_key);
    ASSERT_NE(compiled, nullptr);
    ASSERT_TRUE(compiled->handle.valid());
    EXPECT_EQ(compiled->node->type, kAssetTypeRenderable);
    EXPECT_EQ(compiled->handle.type, kAssetTypeRhiRenderableRecipe);
    // The clipmap landscape is an rhi recipe, not a legacy RenderableAssetData.
    EXPECT_EQ(renderable_table.get(compiled->handle), nullptr);

    const RhiRenderableRecipe* recipe =
        rhi_renderable_table.get(compiled->handle);
    ASSERT_NE(recipe, nullptr);
    EXPECT_EQ(recipe->mesh_key, lattice_key);
    EXPECT_EQ(recipe->height_texture_key, height_key);
    EXPECT_EQ(recipe->program_key, program_key);
    // gpu_sparse path is unused by the clipmap recipe.
    EXPECT_EQ(recipe->gpu_sparse_mesh_key, AssetKey{});
    EXPECT_TRUE(recipe->valid());

    EXPECT_FLOAT_EQ(recipe->clipmap.world_origin[0], -128.0f);
    EXPECT_FLOAT_EQ(recipe->clipmap.world_origin[1], -64.0f);
    EXPECT_FLOAT_EQ(recipe->clipmap.world_size[0], 256.0f);
    EXPECT_FLOAT_EQ(recipe->clipmap.world_size[1], 256.0f);
    EXPECT_FLOAT_EQ(recipe->clipmap.vertical_scale, 40.0f);
    EXPECT_FLOAT_EQ(recipe->clipmap.base_height, -5.0f);
    EXPECT_FLOAT_EQ(recipe->clipmap.lattice_world_cell_size, 2.0f);
    // No Placement connected: the footprint stays authored and the renderer is
    // told to keep deriving it from the scene-node transform (issue #218).
    EXPECT_FALSE(recipe->clipmap.placement_authoritative);
}

// #218 Phase 2: when a Placement asset is connected to the clipmap landscape
// renderable, it is authoritative for the texture->world footprint
// (world_origin/world_size/vertical_scale/base_height) — overriding the
// authored settings — and flags the recipe so the renderer uses the baked
// footprint instead of the scene-node transform. The lattice geometry snap
// (lattice_world_cell_size) is deliberately NOT touched by the placement.
//
// This is the alignment guarantee for terrain-stick: the collision-from-height
// -field recipe reads the SAME Placement and sets its size/origin from
// extent.xz/origin.xz, so collision footprint == clipmap world_size by
// construction.
TEST(RenderableAssetModule, ClipmapLandscapeRenderablePlacementDrivesFootprint)
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
    AudioClipTable audio_clip_table;
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
        .input_schema = kClipmapTestLatticeSchema,
        .output_type = kAssetTypeMesh,
        .compile = [&mesh_table](
            const AssetNode& input,
            std::span<const AssetNode>,
            std::span<const ResourceHandle>) -> AssetNode
        {
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = mesh_table.add(
                make_clipmap_lattice_mesh(ClipmapLatticeParams{
                    .level_count = 3u,
                    .base_resolution = 8u,
                    .cell_size = 1.0f,
                }));
            return out;
        },
    });
    registry.register_compiler(AssetCompiler{
        .input_schema = kClipmapTestHeightFieldSchema,
        .output_type = kAssetTypeScalarField,
        .compile = [&scalar_fields_table](
            const AssetNode& input,
            std::span<const AssetNode>,
            std::span<const ResourceHandle>) -> AssetNode
        {
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload =
                scalar_fields_table.add(clipmap_test_height_field());
            return out;
        },
    });
    registry.register_compiler(AssetCompiler{
        .input_schema = kClipmapTestProgramSchema,
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
    // Placement carrier: emits a PlacementData with a distinctive frame so the
    // override is unambiguous against the authored settings below.
    registry.register_compiler(AssetCompiler{
        .input_schema = kClipmapTestPlacementSchema,
        .output_type = kAssetTypePlacement,
        .compile = [&placement_table](
            const AssetNode& input,
            std::span<const AssetNode>,
            std::span<const ResourceHandle>) -> AssetNode
        {
            PlacementData data{};
            data.origin[0] = 10.0f;
            data.origin[1] = 5.0f;
            data.origin[2] = 20.0f;
            data.extent[0] = 500.0f;
            data.extent[1] = 30.0f;
            data.extent[2] = 600.0f;
            data.base_height = 5.0f;
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = placement_table.add(data);
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
        .audio_clip_table = audio_clip_table,
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

    const AssetKey lattice_key = clipmap_test_key(1);
    const AssetKey height_key = clipmap_test_key(2);
    const AssetKey program_key = clipmap_test_key(3);
    const AssetKey placement_key = clipmap_test_key(4);

    // Authored settings deliberately differ from the placement frame so the
    // override is observable. lattice_world_cell_size must survive untouched.
    const ClipmapLandscapeRenderSettings settings{
        .world_origin = { -128.0f, -64.0f },
        .world_size = { 256.0f, 256.0f },
        .vertical_scale = 40.0f,
        .base_height = -5.0f,
        .lattice_world_cell_size = 2.0f,
    };

    const AssetKey renderable_key =
        make_clipmap_landscape_renderable_key(
            "test/clipmap_landscape_renderable_placement",
            lattice_key,
            height_key,
            program_key,
            settings,
            placement_key);

    AssetGraphDraft draft{};
    const AssetGraphDraftNodeId lattice_node =
        add_asset_graph_draft_node(
            draft,
            clipmap_test_source_node(
                lattice_key, kAssetTypeMesh, kClipmapTestLatticeSchema),
            AssetGraphDraftNodeState::Existing);
    const AssetGraphDraftNodeId height_node =
        add_asset_graph_draft_node(
            draft,
            clipmap_test_source_node(
                height_key, kAssetTypeScalarField,
                kClipmapTestHeightFieldSchema),
            AssetGraphDraftNodeState::Existing);
    const AssetGraphDraftNodeId program_node =
        add_asset_graph_draft_node(
            draft,
            clipmap_test_source_node(
                program_key, kAssetTypeRenderProgram,
                kClipmapTestProgramSchema),
            AssetGraphDraftNodeState::Existing);
    const AssetGraphDraftNodeId placement_node =
        add_asset_graph_draft_node(
            draft,
            clipmap_test_source_node(
                placement_key, kAssetTypePlacement,
                kClipmapTestPlacementSchema),
            AssetGraphDraftNodeState::Existing);

    AssetNode renderable_node =
        clipmap_test_source_node(
            renderable_key,
            kAssetTypeRenderable,
            kClipmapLandscapeRenderableSchema);
    renderable_node.meta = ClipmapLandscapeRenderableCompileDesc{
        .lattice_mesh_asset = lattice_key,
        .height_field_asset = height_key,
        .render_program_asset = program_key,
        .placement_asset = placement_key,
        .settings = settings,
    };
    const AssetGraphDraftNodeId renderable_draft_node =
        add_asset_graph_draft_node(
            draft,
            std::move(renderable_node),
            AssetGraphDraftNodeState::Existing);

    // Port order matches the compiler's input ports: lattice (0), height (1),
    // program (2), placement (3, optional).
    ASSERT_NE(
        connect_asset_graph_draft_nodes(
            draft, lattice_node, renderable_draft_node, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(
            draft, height_node, renderable_draft_node, 1),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(
            draft, program_node, renderable_draft_node, 2),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(
            draft, placement_node, renderable_draft_node, 3),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    if (!validate_asset_graph_draft(draft, registry)) {
        for (const AssetGraphDraftValidationMessage& message :
             draft.validation_messages)
        {
            ADD_FAILURE()
                << "draft validation error code="
                << static_cast<int>(message.code)
                << " message=" << message.message;
        }
    }
    ASSERT_TRUE(draft.validation_messages.empty());

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

    AssetSystem system(std::move(registry));
    ASSERT_TRUE(system.replace_registered_assets(std::move(entries)));

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(system.resolve_all(&errors), 5u);
    EXPECT_TRUE(errors.empty());

    const AssetSystem::CompiledAsset* compiled =
        system.find_compiled(renderable_key);
    ASSERT_NE(compiled, nullptr);
    ASSERT_TRUE(compiled->handle.valid());

    const RhiRenderableRecipe* recipe =
        rhi_renderable_table.get(compiled->handle);
    ASSERT_NE(recipe, nullptr);
    EXPECT_TRUE(recipe->valid());

    // The PLACEMENT's frame wins over the authored settings.
    EXPECT_TRUE(recipe->clipmap.placement_authoritative);
    EXPECT_FLOAT_EQ(recipe->clipmap.world_origin[0], 10.0f);   // origin.x
    EXPECT_FLOAT_EQ(recipe->clipmap.world_origin[1], 20.0f);   // origin.z
    EXPECT_FLOAT_EQ(recipe->clipmap.world_size[0], 500.0f);    // extent.x
    EXPECT_FLOAT_EQ(recipe->clipmap.world_size[1], 600.0f);    // extent.z
    EXPECT_FLOAT_EQ(recipe->clipmap.vertical_scale, 30.0f);    // extent.y
    EXPECT_FLOAT_EQ(recipe->clipmap.base_height, 5.0f);

    // The lattice geometry snap is NOT governed by the placement — it stays the
    // authored (mesh-derived) value (issue #218 Phase 2 scope guard).
    EXPECT_FLOAT_EQ(recipe->clipmap.lattice_world_cell_size, 2.0f);
}

// A plain mesh / gpu_sparse recipe must stay backward-compatible: leaving the
// clipmap fields default keeps height_texture_key empty and valid() unchanged.
TEST(RenderableAssetModule, RhiRenderableRecipeClipmapFieldsAreBackwardCompatible)
{
    using namespace wz::asset;
    using namespace wz::engine::assets;

    RhiRenderableRecipe mesh_recipe{
        .mesh_key = AssetKey{ .content_hash = { 7u, 7u } },
        .program_key = AssetKey{ .content_hash = { 9u, 9u } },
    };
    EXPECT_TRUE(mesh_recipe.valid());
    EXPECT_EQ(mesh_recipe.height_texture_key, AssetKey{});
    EXPECT_FLOAT_EQ(mesh_recipe.clipmap.vertical_scale, 1.0f);
    EXPECT_FLOAT_EQ(mesh_recipe.clipmap.base_height, 0.0f);
    EXPECT_FLOAT_EQ(mesh_recipe.clipmap.lattice_world_cell_size, 1.0f);

    // No geometry -> invalid, exactly as before the clipmap fields existed.
    RhiRenderableRecipe empty_recipe{};
    EXPECT_FALSE(empty_recipe.valid());
}
