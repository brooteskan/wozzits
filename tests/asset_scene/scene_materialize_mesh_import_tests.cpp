#include "scene_authoring_materialize_test_support.h"

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>

#include <algorithm>
#include <cmath>

namespace
{
    std::vector<float> read_float_channel(
        const wz::engine::assets::MeshDerivedFieldData& field,
        uint32_t channel_id)
    {
        const auto found = std::find_if(
            field.channels.begin(),
            field.channels.end(),
            [channel_id](
                const wz::engine::assets::MeshDerivedFieldChannel& channel) {
                return channel.channel_id == channel_id;
            });
        if (found == field.channels.end()
            || found->value_type
                != wz::engine::assets::MeshDerivedFieldValueType::Float1)
        {
            return {};
        }

        std::vector<float> values(field.element_count, 0.0f);
        std::memcpy(
            values.data(),
            field.values.data() + found->byte_offset,
            values.size() * sizeof(float));
        return values;
    }

    std::vector<uint32_t> read_uint_channel(
        const wz::engine::assets::MeshDerivedFieldData& field,
        uint32_t channel_id)
    {
        const auto found = std::find_if(
            field.channels.begin(),
            field.channels.end(),
            [channel_id](
                const wz::engine::assets::MeshDerivedFieldChannel& channel) {
                return channel.channel_id == channel_id;
            });
        if (found == field.channels.end()
            || found->value_type
                != wz::engine::assets::MeshDerivedFieldValueType::UInt1)
        {
            return {};
        }

        std::vector<uint32_t> values(field.element_count, 0u);
        std::memcpy(
            values.data(),
            field.values.data() + found->byte_offset,
            values.size() * sizeof(uint32_t));
        return values;
    }
}

TEST(SceneAuthoringMaterialize, MeshSourceCreatesRenderableAsset)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_mesh_source_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_source";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .depth_test = true,
        .depth_write = true,
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_render_style.has_value());
    EXPECT_NE(
        scene.nodes[0].mesh_render_style->style_asset,
        wz::asset::AssetKey{});
    ASSERT_EQ(report.renderables_to_realize.size(), 1u);
    EXPECT_EQ(report.renderables_to_realize[0], *scene.nodes[0].renderable_asset);

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(renderable.valid());
    const auto* renderable_data =
        assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(renderable_data, nullptr);
    EXPECT_EQ(renderable_data->kind, RenderableKind::Mesh);
    EXPECT_EQ(
        renderable_data->companion_asset,
        scene.nodes[0].mesh_render_style->style_asset);
    EXPECT_EQ(
        renderable_data->program,
        BuiltinRenderProgram::MeshWireframeDepthDebug);
    EXPECT_TRUE(renderable_data->mesh_style.depth_test);
    EXPECT_TRUE(renderable_data->mesh_style.depth_write);
}

TEST(SceneAuthoringMaterialize, MeshProcessingCanPreviewClusterHierarchyLevel)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_hierarchy_preview_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_cluster_hierarchy_preview";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_processing = SceneMeshProcessingAsset{
        .enabled = true,
        .operation =
            SceneMeshProcessingOperation::MeshClusterHierarchyPreview,
        .preview_level_index = 0,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .depth_test = true,
        .depth_write = true,
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_processing.has_value());
    const auto& processing = *scene.nodes[0].mesh_processing;
    EXPECT_NE(processing.source_mesh_asset, wz::asset::AssetKey{});
    EXPECT_NE(processing.processed_mesh_asset, wz::asset::AssetKey{});
    EXPECT_NE(processing.hierarchy_asset, wz::asset::AssetKey{});
    EXPECT_EQ(
        processing.hierarchy_asset.schema_hash,
        detail::hash_u64(kMeshClusterHierarchySchema.value));
    EXPECT_EQ(
        processing.processed_mesh_asset.schema_hash,
        detail::hash_u64(kMeshClusterHierarchyPreviewMeshSchema.value));

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(renderable.valid());
    const auto* renderable_data =
        assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(renderable_data, nullptr);
    EXPECT_EQ(renderable_data->kind, RenderableKind::Mesh);
    EXPECT_EQ(
        renderable_data->source_asset.schema_hash,
        detail::hash_u64(kMeshClusterHierarchyPreviewMeshSchema.value));
    EXPECT_EQ(renderable_data->source_asset, processing.processed_mesh_asset);
}

TEST(SceneAuthoringMaterialize, MeshProcessingCanPreviewDebugTriangleStride)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_debug_triangle_stride_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_debug_triangle_stride";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralQuad,
    };
    node.mesh_processing = SceneMeshProcessingAsset{
        .enabled = true,
        .operation = SceneMeshProcessingOperation::DebugTriangleStride,
        .preview_level_index = 1,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .depth_test = true,
        .depth_write = true,
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_processing.has_value());
    const auto& processing = *scene.nodes[0].mesh_processing;
    EXPECT_NE(processing.source_mesh_asset, wz::asset::AssetKey{});
    EXPECT_NE(processing.processed_mesh_asset, wz::asset::AssetKey{});
    EXPECT_EQ(processing.hierarchy_asset, wz::asset::AssetKey{});
    EXPECT_EQ(
        processing.processed_mesh_asset.schema_hash,
        detail::hash_u64(kDebugTriangleStrideMeshSchema.value));

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto source_handle = assets.meshes().get_mesh(
        MeshAsset{ .output = processing.source_mesh_asset });
    const auto processed_handle = assets.meshes().get_mesh(
        MeshAsset{ .output = processing.processed_mesh_asset });
    ASSERT_TRUE(source_handle.valid());
    ASSERT_TRUE(processed_handle.valid());

    const auto* source_data = assets.meshes().get_mesh_data(source_handle);
    const auto* processed_data =
        assets.meshes().get_mesh_data(processed_handle);
    ASSERT_NE(source_data, nullptr);
    ASSERT_NE(processed_data, nullptr);
    EXPECT_EQ(source_data->index_count(), 6u);
    EXPECT_EQ(processed_data->index_count(), 3u);
    EXPECT_EQ(processed_data->vertex_count(), 3u);

    const auto renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(renderable.valid());
    const auto* renderable_data =
        assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(renderable_data, nullptr);
    EXPECT_EQ(renderable_data->source_asset, processing.processed_mesh_asset);
}

TEST(SceneAuthoringMaterialize, MeshProcessingDoesNotFeedFieldOrOperatorSources)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_processing_downstream_source_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_processing_downstream_source";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_processing = SceneMeshProcessingAsset{
        .enabled = true,
        .operation =
            SceneMeshProcessingOperation::MeshClusterHierarchyPreview,
        .preview_level_index = 0,
    };
    node.mesh_derived_field_source = SceneMeshDerivedFieldSourceAsset{
        .enabled = true,
        .field_id = "height",
        .domain = MeshDerivedFieldDomain::Vertex,
        .channel_id = 0x2000u,
        .value_type = MeshDerivedFieldValueType::Float1,
        .source_kind =
            SceneMeshDerivedFieldSourceKind::PositionGradient,
        .component = SceneMeshDerivedFieldComponent::Y,
        .normalize = true,
    };
    node.mesh_sparse_operator_source = SceneMeshSparseOperatorSourceAsset{
        .enabled = true,
        .operator_id = "uniform_laplacian",
        .kind = MeshSparseOperatorKind::UniformVertexLaplacian,
        .domain = MeshOperatorDomain::Vertex,
        .value_convention =
            MeshSparseOperatorValueConvention::NeighborWeights,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .field_visualization_enabled = true,
        .field_visualization_channel_id = 0x2000u,
        .field_visualization_value_min = 0.0f,
        .field_visualization_value_max = 1.0f,
        .field_visualization_gamma = 1.0f,
        .field_visualization_field_ref = "field:height",
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_processing.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_derived_field_source.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_sparse_operator_source.has_value());

    const auto& processing = *scene.nodes[0].mesh_processing;
    ASSERT_NE(processing.source_mesh_asset, wz::asset::AssetKey{});
    ASSERT_NE(processing.processed_mesh_asset, wz::asset::AssetKey{});
    ASSERT_NE(processing.hierarchy_asset, wz::asset::AssetKey{});
    EXPECT_NE(processing.source_mesh_asset, processing.processed_mesh_asset);

    const wz::asset::AssetKey field_key =
        scene.nodes[0].mesh_derived_field_source->resolved_field_asset;
    const wz::asset::AssetKey operator_key =
        scene.nodes[0]
            .mesh_sparse_operator_source->resolved_operator_asset;
    ASSERT_NE(field_key, wz::asset::AssetKey{});
    ASSERT_NE(operator_key, wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const MeshDerivedFieldHandle field_handle =
        assets.mesh_derived_fields().get_mesh_derived_field(
            MeshDerivedFieldAsset{ .output = field_key });
    ASSERT_TRUE(field_handle.valid());
    const MeshDerivedFieldData* field_data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(
            field_handle);
    ASSERT_NE(field_data, nullptr);
    EXPECT_EQ(field_data->source_mesh_key, processing.source_mesh_asset);
    EXPECT_NE(field_data->source_mesh_key, processing.processed_mesh_asset);

    const MeshSparseOperatorHandle operator_handle =
        assets.mesh_sparse_operators().get_sparse_operator(
            MeshSparseOperatorAsset{ .output = operator_key });
    ASSERT_TRUE(operator_handle.valid());
    const MeshSparseOperatorData* operator_data =
        assets.mesh_sparse_operators().get_sparse_operator_data(
            operator_handle);
    ASSERT_NE(operator_data, nullptr);
    EXPECT_EQ(operator_data->source_mesh_key, processing.source_mesh_asset);
    EXPECT_NE(operator_data->source_mesh_key, processing.processed_mesh_asset);

    const auto renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(renderable.valid());
    const auto* renderable_data =
        assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(renderable_data, nullptr);
    EXPECT_EQ(renderable_data->source_asset, processing.source_mesh_asset);
    EXPECT_EQ(renderable_data->mesh_field_visualization_asset, field_key);
}

TEST(SceneAuthoringMaterialize, MeshSourceRegeneratesStaleStyleAsset)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_stale_style_asset_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const wz::asset::AssetKey stale_style_key{
        .content_hash = { 1, 2 },
        .schema_hash = { 3, 4 },
        .compiler_hash = { 5, 6 },
        .deps_hash = { 7, 8 },
    };

    SceneAssetData scene{};
    scene.name = "mesh_source_stale_style";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .style_asset = stale_style_key,
        .depth_test = true,
        .depth_write = true,
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].mesh_render_style.has_value());
    EXPECT_NE(
        scene.nodes[0].mesh_render_style->style_asset,
        stale_style_key);
    EXPECT_NE(
        scene.nodes[0].mesh_render_style->style_asset,
        wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());
}

TEST(SceneAuthoringMaterialize, MeshWaveletAnalysisFeedsHeatmapRenderable)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_mesh_wavelet_analysis_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_wavelet_analysis";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_wavelet_analysis = SceneMeshWaveletAnalysisAsset{
        .enabled = true,
        .function = SceneMeshWaveletAnalysisFunction::BuiltinDetailHeatV0,
        .scale_count = 5,
        .lambda_max_estimate = 3.5f,
        .gamma = 0.75f,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .depth_test = true,
        .depth_write = true,
        .field_visualization_enabled = true,
        .field_visualization_channel_id = MeshWaveletChannelID::kDetailCost,
        .field_visualization_value_min = 0.0f,
        .field_visualization_value_max = 1.0f,
        .field_visualization_gamma = 0.75f,
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_render_style.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_wavelet_analysis.has_value());
    EXPECT_NE(
        scene.nodes[0].mesh_render_style->field_visualization_asset,
        wz::asset::AssetKey{});
    EXPECT_EQ(
        scene.nodes[0].mesh_wavelet_analysis->field_asset,
        scene.nodes[0].mesh_render_style->field_visualization_asset);

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const MeshDerivedFieldAsset field{
        .output =
            scene.nodes[0].mesh_render_style->field_visualization_asset,
    };
    const MeshDerivedFieldHandle field_handle =
        assets.mesh_derived_fields().get_mesh_derived_field(field);
    ASSERT_TRUE(field_handle.valid());
    const MeshDerivedFieldData* field_data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(
            field_handle);
    ASSERT_NE(field_data, nullptr);
    EXPECT_EQ(field_data->domain, MeshDerivedFieldDomain::Vertex);
    EXPECT_EQ(field_data->element_count, 8u);

    const auto detail_channel = std::find_if(
        field_data->channels.begin(),
        field_data->channels.end(),
        [](const MeshDerivedFieldChannel& channel) {
            return channel.channel_id == MeshWaveletChannelID::kDetailCost;
        });
    ASSERT_NE(detail_channel, field_data->channels.end());
    EXPECT_EQ(detail_channel->value_type, MeshDerivedFieldValueType::Float1);

    const auto renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(renderable.valid());
    const auto* renderable_data =
        assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(renderable_data, nullptr);
    EXPECT_EQ(
        renderable_data->mesh_field_visualization_asset,
        field.output);
    EXPECT_TRUE(renderable_data->mesh_style.field_visualization.enabled);
}

TEST(SceneAuthoringMaterialize, StaleComputeVisualizationChannelFallsBackToWaveletChannel)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_stale_compute_channel_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "stale_compute_visualization_channel";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_wavelet_analysis = SceneMeshWaveletAnalysisAsset{
        .enabled = true,
        .function = SceneMeshWaveletAnalysisFunction::BuiltinDetailHeatV0,
        .scale_count = 3u,
        .lambda_max_estimate = 2.0f,
        .gamma = 1.0f,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .field_visualization_enabled = true,
        .field_visualization_channel_id = 0x2000u,
        .field_visualization_value_min = 0.0f,
        .field_visualization_value_max = 1.0f,
        .field_visualization_gamma = 1.0f,
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].mesh_render_style.has_value());
    EXPECT_EQ(
        scene.nodes[0].mesh_render_style->field_visualization_channel_id,
        MeshWaveletChannelID::kDetailCost);
    ASSERT_TRUE(scene.nodes[0].mesh_wavelet_analysis.has_value());
    EXPECT_NE(
        scene.nodes[0].mesh_wavelet_analysis->field_asset,
        wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());
}

TEST(SceneAuthoringMaterialize, MeshDerivedFieldSourceFeedsExplicitHeatmapRenderable)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_mesh_derived_field_source_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_derived_field_source";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_derived_field_source = SceneMeshDerivedFieldSourceAsset{
        .enabled = true,
        .field_id = "height",
        .domain = MeshDerivedFieldDomain::Vertex,
        .channel_id = 0x2000u,
        .value_type = MeshDerivedFieldValueType::Float1,
        .source_kind =
            SceneMeshDerivedFieldSourceKind::PositionGradient,
        .component = SceneMeshDerivedFieldComponent::Y,
        .normalize = true,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .depth_test = true,
        .depth_write = true,
        .field_visualization_enabled = true,
        .field_visualization_channel_id = 0x2000u,
        .field_visualization_value_min = 0.0f,
        .field_visualization_value_max = 1.0f,
        .field_visualization_gamma = 0.8f,
        .field_visualization_palette = MeshFieldVisualizationPalette::Diverging,
        .field_visualization_field_ref = "field:height",
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_render_style.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_derived_field_source.has_value());

    const wz::asset::AssetKey field_key =
        scene.nodes[0].mesh_derived_field_source->resolved_field_asset;
    ASSERT_NE(field_key, wz::asset::AssetKey{});
    EXPECT_EQ(
        scene.nodes[0].mesh_render_style->field_visualization_asset,
        field_key);

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const MeshDerivedFieldAsset field{ .output = field_key };
    const MeshDerivedFieldHandle field_handle =
        assets.mesh_derived_fields().get_mesh_derived_field(field);
    ASSERT_TRUE(field_handle.valid());
    const MeshDerivedFieldData* field_data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(
            field_handle);
    ASSERT_NE(field_data, nullptr);
    EXPECT_EQ(field_data->domain, MeshDerivedFieldDomain::Vertex);
    EXPECT_EQ(field_data->element_count, 8u);

    const std::vector<float> values =
        read_float_channel(*field_data, 0x2000u);
    ASSERT_EQ(values.size(), field_data->element_count);
    EXPECT_NE(std::find(values.begin(), values.end(), 0.0f), values.end());
    EXPECT_NE(std::find(values.begin(), values.end(), 1.0f), values.end());

    const auto renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(renderable.valid());
    const auto* renderable_data =
        assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(renderable_data, nullptr);
    EXPECT_EQ(renderable_data->mesh_field_visualization_asset, field_key);
    EXPECT_TRUE(renderable_data->mesh_style.field_visualization.enabled);
    EXPECT_EQ(
        renderable_data->mesh_style.field_visualization.channel_id,
        0x2000u);
    EXPECT_FLOAT_EQ(
        renderable_data->mesh_style.field_visualization.gamma,
        0.8f);
    EXPECT_EQ(
        renderable_data->mesh_style.field_visualization.palette,
        MeshFieldVisualizationPalette::Diverging);
}

TEST(SceneAuthoringMaterialize, MeshDerivedFieldSourceBuildsScalarRecipes)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_mesh_derived_field_recipes_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    auto make_field_node =
        [](const char* id,
            const char* field_id,
            uint32_t channel_id,
            SceneMeshDerivedFieldSourceKind kind,
            MeshDerivedFieldDomain domain =
                MeshDerivedFieldDomain::Vertex) {
            SceneNodeAsset node = make_scene_node(id);
            node.mesh_source = SceneMeshSourceAsset{
                .kind = SceneMeshSourceKind::ProceduralCube,
            };
            node.mesh_derived_field_source =
                SceneMeshDerivedFieldSourceAsset{
                    .enabled = true,
                    .field_id = field_id,
                    .domain = domain,
                    .channel_id = channel_id,
                    .value_type = MeshDerivedFieldValueType::Float1,
                    .source_kind = kind,
                    .component = SceneMeshDerivedFieldComponent::Y,
                    .normalize = true,
                    .constant_value = 0.5f,
                };
            return node;
        };

    SceneAssetData scene{};
    scene.name = "mesh_derived_field_recipes";
    scene.nodes.push_back(make_field_node(
        "constant",
        "constant",
        0x2001u,
        SceneMeshDerivedFieldSourceKind::Constant));
    scene.nodes.push_back(make_field_node(
        "vertex_index",
        "vertex_index",
        0x2002u,
        SceneMeshDerivedFieldSourceKind::VertexIndexGradient));
    scene.nodes.push_back(make_field_node(
        "corner_count",
        "corner_count",
        0x2003u,
        SceneMeshDerivedFieldSourceKind::TriangleCornerCount));
    scene.nodes.push_back(make_field_node(
        "vertex_area",
        "vertex_area",
        0x2004u,
        SceneMeshDerivedFieldSourceKind::VertexArea));
    scene.nodes.push_back(make_field_node(
        "triangle_area",
        "triangle_area",
        0x2008u,
        SceneMeshDerivedFieldSourceKind::TriangleArea,
        MeshDerivedFieldDomain::Face));
    scene.nodes.push_back(make_field_node(
        "mean_edge_length",
        "mean_edge_length",
        0x2005u,
        SceneMeshDerivedFieldSourceKind::MeanEdgeLength));
    scene.nodes.push_back(make_field_node(
        "mean_edge_length_face",
        "mean_edge_length_face",
        0x2009u,
        SceneMeshDerivedFieldSourceKind::MeanEdgeLength,
        MeshDerivedFieldDomain::Face));
    scene.nodes.push_back(make_field_node(
        "inverse_area_density",
        "inverse_area_density",
        0x2006u,
        SceneMeshDerivedFieldSourceKind::InverseAreaDensity));
    scene.nodes.push_back(make_field_node(
        "log_density",
        "log_density",
        0x2007u,
        SceneMeshDerivedFieldSourceKind::LogDensity));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_EQ(scene.nodes.size(), 9u);
    for (const SceneNodeAsset& node : scene.nodes) {
        ASSERT_TRUE(node.mesh_derived_field_source.has_value());
        EXPECT_NE(
            node.mesh_derived_field_source->resolved_field_asset,
            wz::asset::AssetKey{});
    }

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto field_data_for_node =
        [&assets](const SceneNodeAsset& node) {
            const MeshDerivedFieldAsset field{
                .output =
                    node.mesh_derived_field_source->resolved_field_asset,
            };
            const MeshDerivedFieldHandle field_handle =
                assets.mesh_derived_fields().get_mesh_derived_field(field);
            return assets.mesh_derived_fields().get_mesh_derived_field_data(
                field_handle);
        };

    const MeshDerivedFieldData* constant_field =
        field_data_for_node(scene.nodes[0]);
    ASSERT_NE(constant_field, nullptr);
    const std::vector<float> constant_values =
        read_float_channel(*constant_field, 0x2001u);
    ASSERT_EQ(constant_values.size(), constant_field->element_count);
    for (const float value : constant_values) {
        EXPECT_FLOAT_EQ(value, 0.5f);
    }

    const MeshDerivedFieldData* vertex_index_field =
        field_data_for_node(scene.nodes[1]);
    ASSERT_NE(vertex_index_field, nullptr);
    const std::vector<float> vertex_index_values =
        read_float_channel(*vertex_index_field, 0x2002u);
    ASSERT_EQ(vertex_index_values.size(), vertex_index_field->element_count);
    for (uint32_t i = 0;
        i < static_cast<uint32_t>(vertex_index_values.size());
        ++i)
    {
        const float expected =
            vertex_index_values.size() > 1u
                ? static_cast<float>(i)
                    / static_cast<float>(vertex_index_values.size() - 1u)
                : 0.0f;
        EXPECT_FLOAT_EQ(vertex_index_values[i], expected);
    }

    const MeshDerivedFieldData* corner_count_field =
        field_data_for_node(scene.nodes[2]);
    ASSERT_NE(corner_count_field, nullptr);
    const std::vector<float> corner_count_values =
        read_float_channel(*corner_count_field, 0x2003u);
    ASSERT_EQ(corner_count_values.size(), corner_count_field->element_count);

    const MeshHandle corner_mesh_handle = assets.meshes().get_mesh(
        MeshAsset{ .output = corner_count_field->source_mesh_key });
    ASSERT_TRUE(corner_mesh_handle.valid());
    const MeshData* corner_mesh =
        assets.meshes().get_mesh_data(corner_mesh_handle);
    ASSERT_NE(corner_mesh, nullptr);

    std::vector<float> expected_counts(corner_mesh->vertex_count(), 0.0f);
    for (const uint32_t index : corner_mesh->indices) {
        ASSERT_LT(index, expected_counts.size());
        expected_counts[index] += 1.0f;
    }
    const float max_count =
        *std::max_element(expected_counts.begin(), expected_counts.end());
    ASSERT_GT(max_count, 0.0f);
    for (float& value : expected_counts) {
        value /= max_count;
    }
    ASSERT_EQ(corner_count_values.size(), expected_counts.size());
    for (size_t i = 0; i < expected_counts.size(); ++i) {
        EXPECT_FLOAT_EQ(corner_count_values[i], expected_counts[i]);
    }

    for (size_t node_index = 3; node_index < scene.nodes.size(); ++node_index) {
        const MeshDerivedFieldData* field =
            field_data_for_node(scene.nodes[node_index]);
        ASSERT_NE(field, nullptr);
        ASSERT_EQ(field->channels.size(), 1u);
        const std::vector<float> field_values =
            read_float_channel(
                *field,
                scene.nodes[node_index]
                    .mesh_derived_field_source->channel_id);
        ASSERT_EQ(field_values.size(), field->element_count);

        bool has_signal = false;
        for (const float value : field_values) {
            EXPECT_TRUE(std::isfinite(value));
            has_signal = has_signal || std::fabs(value) > 0.0f;
        }
        EXPECT_TRUE(has_signal) << scene.nodes[node_index].id;
    }
}

TEST(SceneAuthoringMaterialize, MeshSparseOperatorSourceBuildsUniformLaplacian)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_mesh_sparse_operator_source_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_sparse_operator_source";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_sparse_operator_source = SceneMeshSparseOperatorSourceAsset{
        .enabled = true,
        .operator_id = "uniform_laplacian",
        .kind = MeshSparseOperatorKind::UniformVertexLaplacian,
        .domain = MeshOperatorDomain::Face,
        .value_convention =
            MeshSparseOperatorValueConvention::NeighborWeights,
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].mesh_sparse_operator_source.has_value());

    const wz::asset::AssetKey operator_key =
        scene.nodes[0]
            .mesh_sparse_operator_source->resolved_operator_asset;
    ASSERT_NE(operator_key, wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const MeshSparseOperatorAsset op{ .output = operator_key };
    const MeshSparseOperatorHandle op_handle =
        assets.mesh_sparse_operators().get_sparse_operator(op);
    ASSERT_TRUE(op_handle.valid());
    const MeshSparseOperatorData* op_data =
        assets.mesh_sparse_operators().get_sparse_operator_data(op_handle);
    ASSERT_NE(op_data, nullptr);
    EXPECT_EQ(
        op_data->kind,
        MeshSparseOperatorKind::UniformVertexLaplacian);
    EXPECT_EQ(op_data->domain, MeshOperatorDomain::Face);
    EXPECT_EQ(
        op_data->value_convention,
        MeshSparseOperatorValueConvention::NeighborWeights);
    EXPECT_EQ(op_data->row_count, 12u);
    ASSERT_EQ(op_data->row_offsets.size(), op_data->row_count + 1u);
    EXPECT_EQ(op_data->row_offsets.front(), 0u);
    EXPECT_EQ(op_data->row_offsets.back(), op_data->nonzero_count);
}

TEST(SceneAuthoringMaterialize, MeshSparseApplyFieldBuildsResidualField)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_mesh_sparse_apply_field_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_sparse_apply_field";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_derived_field_source = SceneMeshDerivedFieldSourceAsset{
        .enabled = true,
        .field_id = "constant",
        .domain = MeshDerivedFieldDomain::Vertex,
        .channel_id = 0x2001u,
        .value_type = MeshDerivedFieldValueType::Float1,
        .source_kind = SceneMeshDerivedFieldSourceKind::Constant,
        .component = SceneMeshDerivedFieldComponent::Y,
        .normalize = true,
        .constant_value = 0.5f,
    };
    node.mesh_sparse_operator_source = SceneMeshSparseOperatorSourceAsset{
        .enabled = true,
        .operator_id = "uniform_laplacian",
        .kind = MeshSparseOperatorKind::UniformVertexLaplacian,
        .domain = MeshOperatorDomain::Vertex,
        .value_convention =
            MeshSparseOperatorValueConvention::NeighborWeights,
    };
    node.mesh_sparse_apply_field = SceneMeshSparseApplyFieldAsset{
        .enabled = true,
        .operator_ref = "operator:uniform_laplacian",
        .input_field_ref = "field:constant",
        .input_channel_id = 0x2001u,
        .output_channel_id = 0x2100u,
        .apply_mode = SceneMeshSparseApplyMode::Residual,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .field_visualization_enabled = true,
        .field_visualization_channel_id = 0x2100u,
        .field_visualization_value_min = -1.0f,
        .field_visualization_value_max = 1.0f,
        .field_visualization_gamma = 1.0f,
        .field_visualization_field_ref = "field:residual",
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].mesh_sparse_apply_field.has_value());

    const wz::asset::AssetKey field_key =
        scene.nodes[0].mesh_sparse_apply_field->output_field_asset;
    ASSERT_NE(field_key, wz::asset::AssetKey{});
    ASSERT_TRUE(scene.nodes[0].mesh_render_style.has_value());
    EXPECT_EQ(
        scene.nodes[0].mesh_render_style->field_visualization_asset,
        field_key);

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const MeshDerivedFieldAsset field{ .output = field_key };
    const MeshDerivedFieldHandle field_handle =
        assets.mesh_derived_fields().get_mesh_derived_field(field);
    ASSERT_TRUE(field_handle.valid());
    const MeshDerivedFieldData* field_data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(
            field_handle);
    ASSERT_NE(field_data, nullptr);
    EXPECT_EQ(field_data->domain, MeshDerivedFieldDomain::Vertex);
    EXPECT_EQ(field_data->element_count, 8u);

    const std::vector<float> values =
        read_float_channel(*field_data, 0x2100u);
    ASSERT_EQ(values.size(), field_data->element_count);
    for (const float value : values) {
        EXPECT_FLOAT_EQ(value, 0.0f);
    }
}

TEST(SceneAuthoringMaterialize, MeshSparseDiffusionBandsBuildsRenderableBands)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_mesh_sparse_diffusion_bands_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_sparse_diffusion_bands";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_derived_field_source = SceneMeshDerivedFieldSourceAsset{
        .enabled = true,
        .field_id = "constant",
        .domain = MeshDerivedFieldDomain::Vertex,
        .channel_id = 0x2001u,
        .value_type = MeshDerivedFieldValueType::Float1,
        .source_kind = SceneMeshDerivedFieldSourceKind::Constant,
        .component = SceneMeshDerivedFieldComponent::Y,
        .normalize = true,
        .constant_value = 0.5f,
    };
    node.mesh_sparse_operator_source = SceneMeshSparseOperatorSourceAsset{
        .enabled = true,
        .operator_id = "uniform_laplacian",
        .kind = MeshSparseOperatorKind::UniformVertexLaplacian,
        .domain = MeshOperatorDomain::Vertex,
        .value_convention =
            MeshSparseOperatorValueConvention::NeighborWeights,
    };
    node.mesh_sparse_diffusion_bands =
        SceneMeshSparseDiffusionBandsAsset{
            .enabled = true,
            .operator_ref = "operator:uniform_laplacian",
            .input_field_ref = "field:constant",
            .input_channel_id = 0x2001u,
            .output_base_channel_id = 0x2200u,
            .band_count = 2u,
            .iterations_per_band = 2u,
            .mode = SceneMeshSparseDiffusionMode::Smooth,
            .tau = 1.0f,
        };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .field_visualization_enabled = true,
        .field_visualization_channel_id = 0x2200u,
        .field_visualization_value_min = -1.0f,
        .field_visualization_value_max = 1.0f,
        .field_visualization_gamma = 1.0f,
        .field_visualization_palette =
            MeshFieldVisualizationPalette::Diverging,
        .field_visualization_field_ref = "field:diffusion_bands",
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].mesh_sparse_diffusion_bands.has_value());

    const wz::asset::AssetKey field_key =
        scene.nodes[0].mesh_sparse_diffusion_bands->output_field_asset;
    ASSERT_NE(field_key, wz::asset::AssetKey{});
    ASSERT_TRUE(scene.nodes[0].mesh_render_style.has_value());
    EXPECT_EQ(
        scene.nodes[0].mesh_render_style->field_visualization_asset,
        field_key);

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const MeshDerivedFieldAsset field{ .output = field_key };
    const MeshDerivedFieldHandle field_handle =
        assets.mesh_derived_fields().get_mesh_derived_field(field);
    ASSERT_TRUE(field_handle.valid());
    const MeshDerivedFieldData* field_data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(
            field_handle);
    ASSERT_NE(field_data, nullptr);
    EXPECT_EQ(field_data->domain, MeshDerivedFieldDomain::Vertex);
    EXPECT_EQ(field_data->element_count, 8u);
    ASSERT_EQ(field_data->channels.size(), 2u);
    EXPECT_EQ(field_data->channels[0].channel_id, 0x2200u);
    EXPECT_EQ(field_data->channels[1].channel_id, 0x2201u);

    for (uint32_t channel_id : { 0x2200u, 0x2201u }) {
        const std::vector<float> values =
            read_float_channel(*field_data, channel_id);
        ASSERT_EQ(values.size(), field_data->element_count);
        for (const float value : values) {
            EXPECT_FLOAT_EQ(value, 0.0f);
        }
    }

    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    const auto renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(renderable.valid());
    const auto* renderable_data =
        assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(renderable_data, nullptr);
    EXPECT_EQ(renderable_data->mesh_field_visualization_asset, field_key);
    EXPECT_EQ(
        renderable_data->mesh_style.field_visualization.channel_id,
        0x2200u);
    EXPECT_EQ(
        renderable_data->mesh_style.field_visualization.palette,
        MeshFieldVisualizationPalette::Diverging);
}

TEST(SceneAuthoringMaterialize, MeshSparseDiffusionBandsWrongInputChannelFallsBack)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_mesh_sparse_diffusion_bad_channel_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_sparse_diffusion_bands_bad_channel";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_derived_field_source = SceneMeshDerivedFieldSourceAsset{
        .enabled = true,
        .field_id = "constant",
        .domain = MeshDerivedFieldDomain::Vertex,
        .channel_id = 0x2001u,
        .value_type = MeshDerivedFieldValueType::Float1,
        .source_kind = SceneMeshDerivedFieldSourceKind::Constant,
        .component = SceneMeshDerivedFieldComponent::Y,
        .normalize = true,
        .constant_value = 0.5f,
    };
    node.mesh_sparse_operator_source = SceneMeshSparseOperatorSourceAsset{
        .enabled = true,
        .operator_id = "uniform_laplacian",
        .kind = MeshSparseOperatorKind::UniformVertexLaplacian,
        .domain = MeshOperatorDomain::Vertex,
        .value_convention =
            MeshSparseOperatorValueConvention::NeighborWeights,
    };
    node.mesh_sparse_diffusion_bands =
        SceneMeshSparseDiffusionBandsAsset{
            .enabled = true,
            .operator_ref = "operator:uniform_laplacian",
            .input_field_ref = "field:constant",
            .input_channel_id = 0x2999u,
            .output_base_channel_id = 0x2200u,
            .band_count = 2u,
            .iterations_per_band = 2u,
            .mode = SceneMeshSparseDiffusionMode::Smooth,
            .tau = 1.0f,
        };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .field_visualization_enabled = true,
        .field_visualization_channel_id = 0x2200u,
        .field_visualization_value_min = -1.0f,
        .field_visualization_value_max = 1.0f,
        .field_visualization_gamma = 1.0f,
        .field_visualization_palette =
            MeshFieldVisualizationPalette::Diverging,
        .field_visualization_field_ref = "field:diffusion_bands",
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;

    const wz::asset::AssetKey field_key =
        scene.nodes[0].mesh_sparse_diffusion_bands->output_field_asset;
    ASSERT_NE(field_key, wz::asset::AssetKey{});
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const MeshDerivedFieldHandle field_handle =
        assets.mesh_derived_fields().get_mesh_derived_field(
            MeshDerivedFieldAsset{ .output = field_key });
    ASSERT_TRUE(field_handle.valid());
    const MeshDerivedFieldData* field_data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(
            field_handle);
    ASSERT_NE(field_data, nullptr);
    ASSERT_EQ(field_data->channels.size(), 2u);

    for (uint32_t channel_id : { 0x2200u, 0x2201u }) {
        const std::vector<float> values =
            read_float_channel(*field_data, channel_id);
        ASSERT_EQ(values.size(), field_data->element_count);
        for (const float value : values) {
            EXPECT_FLOAT_EQ(value, 0.0f);
        }
    }
}

TEST(SceneAuthoringMaterialize, MeshSparseOperatorDomainFollowsInputField)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_sparse_domain_infer_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_sparse_domain_infer";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_derived_field_source = SceneMeshDerivedFieldSourceAsset{
        .enabled = true,
        .field_id = "height",
        .domain = MeshDerivedFieldDomain::Vertex,
        .channel_id = 0x2200u,
        .value_type = MeshDerivedFieldValueType::Float1,
        .source_kind = SceneMeshDerivedFieldSourceKind::PositionGradient,
        .component = SceneMeshDerivedFieldComponent::Y,
        .normalize = true,
        .constant_value = 0.5f,
    };
    node.mesh_sparse_operator_source = SceneMeshSparseOperatorSourceAsset{
        .enabled = true,
        .operator_id = "face_laplacian",
        .kind = MeshSparseOperatorKind::UniformVertexLaplacian,
        .domain = MeshOperatorDomain::Face,
        .value_convention =
            MeshSparseOperatorValueConvention::NeighborWeights,
    };
    node.mesh_sparse_diffusion_bands = SceneMeshSparseDiffusionBandsAsset{
        .enabled = true,
        .operator_ref = "operator:face_laplacian",
        .input_field_ref = "field:height",
        .input_channel_id = 0x2200u,
        .output_base_channel_id = 0x2300u,
        .band_count = 2u,
        .iterations_per_band = 1u,
        .mode = SceneMeshSparseDiffusionMode::Smooth,
        .tau = 1.0f,
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].mesh_sparse_operator_source.has_value());
    EXPECT_EQ(
        scene.nodes[0].mesh_sparse_operator_source->domain,
        MeshOperatorDomain::Vertex);

    const wz::asset::AssetKey field_key =
        scene.nodes[0].mesh_sparse_diffusion_bands->output_field_asset;
    ASSERT_NE(field_key, wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const MeshDerivedFieldHandle field_handle =
        assets.mesh_derived_fields().get_mesh_derived_field(
            MeshDerivedFieldAsset{ .output = field_key });
    ASSERT_TRUE(field_handle.valid());
    const MeshDerivedFieldData* field_data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(
            field_handle);
    ASSERT_NE(field_data, nullptr);
    ASSERT_TRUE(field_data->valid());
    EXPECT_EQ(field_data->domain, MeshDerivedFieldDomain::Vertex);
}

TEST(SceneAuthoringMaterialize, MeshLevelMaskSourceBuildsFaceMaskField)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_mesh_level_mask_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_level_mask";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_derived_field_source = SceneMeshDerivedFieldSourceAsset{
        .enabled = true,
        .field_id = "face_constant",
        .domain = MeshDerivedFieldDomain::Face,
        .channel_id = 0x2200u,
        .value_type = MeshDerivedFieldValueType::Float1,
        .source_kind = SceneMeshDerivedFieldSourceKind::Constant,
        .component = SceneMeshDerivedFieldComponent::Y,
        .normalize = true,
        .constant_value = 0.5f,
    };
    node.mesh_level_mask_source = SceneMeshLevelMaskSourceAsset{
        .enabled = true,
        .input_field_ref = "field:face_constant",
        .output_field_id = "masks",
        .domain = MeshDerivedFieldDomain::Vertex,
        .regions = {
            SceneMeshLevelMaskRegionAsset{
                .input_channel_id = 0x2200u,
                .output_channel_id = 0x3000u,
                .min_value = 0.25f,
                .max_value = 0.75f,
            },
            SceneMeshLevelMaskRegionAsset{
                .input_channel_id = 0x2200u,
                .output_channel_id = 0x3001u,
                .min_value = 0.75f,
                .max_value = 1.0f,
            },
        },
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].mesh_level_mask_source.has_value());

    const wz::asset::AssetKey field_key =
        scene.nodes[0].mesh_level_mask_source->output_field_asset;
    ASSERT_NE(field_key, wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const MeshDerivedFieldHandle field_handle =
        assets.mesh_derived_fields().get_mesh_derived_field(
            MeshDerivedFieldAsset{ .output = field_key });
    ASSERT_TRUE(field_handle.valid());
    const MeshDerivedFieldData* field_data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(
            field_handle);
    ASSERT_NE(field_data, nullptr);
    ASSERT_TRUE(field_data->valid());
    EXPECT_EQ(field_data->domain, MeshDerivedFieldDomain::Face);
    ASSERT_EQ(field_data->element_count, 12u);
    ASSERT_EQ(field_data->channels.size(), 2u);

    const std::vector<uint32_t> selected =
        read_uint_channel(*field_data, 0x3000u);
    ASSERT_EQ(selected.size(), field_data->element_count);
    for (const uint32_t value : selected) {
        EXPECT_EQ(value, 1u);
    }

    const std::vector<uint32_t> rejected =
        read_uint_channel(*field_data, 0x3001u);
    ASSERT_EQ(rejected.size(), field_data->element_count);
    for (const uint32_t value : rejected) {
        EXPECT_EQ(value, 0u);
    }
}

TEST(SceneAuthoringMaterialize, MeshMaskRenderStyleDoesNotReattachBaseStyle)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_mesh_mask_without_base_style_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_mask_without_base_style";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_derived_field_source = SceneMeshDerivedFieldSourceAsset{
        .enabled = true,
        .field_id = "face_density",
        .domain = MeshDerivedFieldDomain::Face,
        .channel_id = 0x2200u,
        .value_type = MeshDerivedFieldValueType::Float1,
        .source_kind = SceneMeshDerivedFieldSourceKind::Constant,
        .component = SceneMeshDerivedFieldComponent::Y,
        .normalize = true,
        .constant_value = 0.5f,
    };
    node.mesh_mask_render_style = SceneMeshMaskRenderStyleAsset{
        .enabled = true,
        .source_field_ref = "field:face_density",
        .mask = MeshMaskRenderStyleData{
            .enabled = true,
            .domain = MeshMaskDomain::Face,
            .projection_mode = MeshMaskProjectionMode::Direct,
            .overlap_mode = MeshMaskOverlapMode::Priority,
            .rules = {
                MeshMaskRule{
                    .input_channel_id = 0x2200u,
                    .lo = 0.25f,
                    .hi = 0.75f,
                    .color = { 1.0f, 0.15f, 0.05f, 1.0f },
                    .priority = 0,
                },
            },
        },
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_FALSE(scene.nodes[0].mesh_render_style.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_mask_render_style.has_value());
    ASSERT_NE(
        scene.nodes[0].mesh_mask_render_style->source_field_asset,
        wz::asset::AssetKey{});
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(renderable.valid());
    const auto* renderable_data =
        assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(renderable_data, nullptr);
    EXPECT_EQ(renderable_data->program, BuiltinRenderProgram::MeshMaskStyle);
    EXPECT_TRUE(renderable_data->mesh_style.wireframe.enabled);
    EXPECT_FLOAT_EQ(renderable_data->mesh_style.wireframe.color[3], 0.5f);
    EXPECT_FALSE(renderable_data->mesh_style.surface.enabled);
    EXPECT_TRUE(renderable_data->mesh_style.mask.enabled);
    ASSERT_EQ(renderable_data->mesh_style.mask.rules.size(), 1u);
    EXPECT_EQ(
        renderable_data->mesh_style.mask.rules[0].input_channel_id,
        0x2200u);
}

TEST(SceneAuthoringMaterialize, MeshMaskRenderStyleCanPreviewProcessedMesh)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_mesh_mask_processed_mesh_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_mask_processed_mesh";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_processing = SceneMeshProcessingAsset{
        .enabled = true,
        .operation =
            SceneMeshProcessingOperation::MeshClusterHierarchyPreview,
        .preview_level_index = 0,
    };
    node.mesh_derived_field_source = SceneMeshDerivedFieldSourceAsset{
        .enabled = true,
        .field_id = "face_density",
        .domain = MeshDerivedFieldDomain::Face,
        .channel_id = 0x2200u,
        .value_type = MeshDerivedFieldValueType::Float1,
        .source_kind = SceneMeshDerivedFieldSourceKind::Constant,
        .component = SceneMeshDerivedFieldComponent::Y,
        .normalize = true,
        .constant_value = 0.5f,
    };
    node.mesh_mask_render_style = SceneMeshMaskRenderStyleAsset{
        .enabled = true,
        .mesh_input = SceneMeshMaskRenderMeshInput::Processed,
        .source_field_ref = "field:face_density",
        .mask = MeshMaskRenderStyleData{
            .enabled = true,
            .domain = MeshMaskDomain::Face,
            .projection_mode = MeshMaskProjectionMode::Direct,
            .overlap_mode = MeshMaskOverlapMode::Priority,
            .rules = {
                MeshMaskRule{
                    .enabled = false,
                    .input_channel_id = 0x2200u,
                    .lo = 0.25f,
                    .hi = 0.75f,
                    .color = { 1.0f, 0.15f, 0.05f, 1.0f },
                    .priority = 0,
                },
            },
        },
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_processing.has_value());
    const auto& processing = *scene.nodes[0].mesh_processing;
    ASSERT_NE(processing.source_mesh_asset, wz::asset::AssetKey{});
    ASSERT_NE(processing.processed_mesh_asset, wz::asset::AssetKey{});
    EXPECT_NE(processing.source_mesh_asset, processing.processed_mesh_asset);
    ASSERT_TRUE(scene.nodes[0].mesh_mask_render_style.has_value());
    ASSERT_NE(
        scene.nodes[0].mesh_mask_render_style->source_field_asset,
        wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(renderable.valid());
    const auto* renderable_data =
        assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(renderable_data, nullptr);
    EXPECT_EQ(renderable_data->program, BuiltinRenderProgram::MeshMaskStyle);
    EXPECT_EQ(renderable_data->source_asset, processing.processed_mesh_asset);
    EXPECT_EQ(
        renderable_data->mesh_field_visualization_asset,
        scene.nodes[0].mesh_mask_render_style->source_field_asset);
}

TEST(SceneAuthoringMaterialize, MeshLevelMaskSourceDomainFollowsInputField)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_mesh_level_mask_domain_mismatch_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_level_mask_domain_mismatch";
    SceneNodeAsset node = make_scene_node("empty_2");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_derived_field_source = SceneMeshDerivedFieldSourceAsset{
        .enabled = true,
        .field_id = "face_constant",
        .domain = MeshDerivedFieldDomain::Face,
        .channel_id = 0x2200u,
        .value_type = MeshDerivedFieldValueType::Float1,
        .source_kind = SceneMeshDerivedFieldSourceKind::Constant,
        .component = SceneMeshDerivedFieldComponent::Y,
        .normalize = true,
        .constant_value = 0.5f,
    };
    node.mesh_sparse_operator_source = SceneMeshSparseOperatorSourceAsset{
        .enabled = true,
        .operator_id = "face_laplacian",
        .kind = MeshSparseOperatorKind::UniformVertexLaplacian,
        .domain = MeshOperatorDomain::Face,
        .value_convention =
            MeshSparseOperatorValueConvention::NeighborWeights,
    };
    node.mesh_sparse_diffusion_bands = SceneMeshSparseDiffusionBandsAsset{
        .enabled = true,
        .operator_ref = "operator:face_laplacian",
        .input_field_ref = "field:face_constant",
        .input_channel_id = 0x2200u,
        .output_base_channel_id = 0x2300u,
        .band_count = 2u,
        .iterations_per_band = 1u,
        .mode = SceneMeshSparseDiffusionMode::Smooth,
        .tau = 1.0f,
    };
    node.mesh_level_mask_source = SceneMeshLevelMaskSourceAsset{
        .enabled = true,
        .input_field_ref = "field:diffusion_bands",
        .output_field_id = "masks",
        .domain = MeshDerivedFieldDomain::Vertex,
        .regions = {
            SceneMeshLevelMaskRegionAsset{
                .input_channel_id = 0x2300u,
                .output_channel_id = 0x3000u,
                .min_value = 0.25f,
                .max_value = 0.75f,
            },
        },
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].mesh_level_mask_source.has_value());
    EXPECT_EQ(
        scene.nodes[0].mesh_level_mask_source->domain,
        MeshDerivedFieldDomain::Face);

    const wz::asset::AssetKey field_key =
        scene.nodes[0].mesh_level_mask_source->output_field_asset;
    ASSERT_NE(field_key, wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const MeshDerivedFieldHandle field_handle =
        assets.mesh_derived_fields().get_mesh_derived_field(
            MeshDerivedFieldAsset{ .output = field_key });
    ASSERT_TRUE(field_handle.valid());
    const MeshDerivedFieldData* field_data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(
            field_handle);
    ASSERT_NE(field_data, nullptr);
    ASSERT_TRUE(field_data->valid());
    EXPECT_EQ(field_data->domain, MeshDerivedFieldDomain::Face);
}

TEST(SceneAuthoringMaterialize, MeshFieldVisualizationWrongChannelFallsBack)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_bad_field_channel_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "bad_field_channel";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_derived_field_source = SceneMeshDerivedFieldSourceAsset{
        .enabled = true,
        .field_id = "constant",
        .domain = MeshDerivedFieldDomain::Vertex,
        .channel_id = 0x2001u,
        .value_type = MeshDerivedFieldValueType::Float1,
        .source_kind = SceneMeshDerivedFieldSourceKind::Constant,
        .component = SceneMeshDerivedFieldComponent::Y,
        .normalize = true,
        .constant_value = 0.5f,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .field_visualization_enabled = true,
        .field_visualization_channel_id = 0x2300u,
        .field_visualization_value_min = 0.0f,
        .field_visualization_value_max = 1.0f,
        .field_visualization_gamma = 1.0f,
        .field_visualization_field_ref = "field:constant",
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].mesh_render_style.has_value());
    ASSERT_NE(
        scene.nodes[0].mesh_render_style->field_visualization_asset,
        wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    const auto renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(renderable.valid());
    const auto* renderable_data =
        assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(renderable_data, nullptr);
    EXPECT_EQ(
        renderable_data->mesh_field_visualization_asset,
        wz::asset::AssetKey{});
    EXPECT_FALSE(renderable_data->mesh_style.field_visualization.enabled);
    EXPECT_NE(renderable_data->program, BuiltinRenderProgram::MeshFieldHeatmap);
}

TEST(SceneAuthoringMaterialize, SceneImportSourceAppendsGLBHierarchy)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, WZ_TEST_FIXTURE_DIR };

    SceneAssetData scene{};
    scene.name = "glb_scene_import";
    SceneNodeAsset anchor = make_scene_node("tank_anchor");
    anchor.scene_import_source = SceneImportSourceAsset{
        .kind = SceneImportSourceKind::GLB,
        .path = "gltf/tank1.glb",
        .import_prefix = "tank_anchor/tank1",
    };
    scene.nodes.push_back(std::move(anchor));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_EQ(scene.nodes.size(), 4u);

    const SceneNodeAsset* body =
        find_scene_node(scene, "tank_anchor/tank1/body");
    ASSERT_NE(body, nullptr);
    ASSERT_TRUE(body->parent_id.has_value());
    EXPECT_EQ(*body->parent_id, "tank_anchor");
    ASSERT_TRUE(body->mesh_source.has_value());
    EXPECT_EQ(body->mesh_source->kind, SceneMeshSourceKind::GLB);
    EXPECT_EQ(body->mesh_source->mesh_index, 2u);
    ASSERT_TRUE(body->renderable_asset.has_value());
    ASSERT_TRUE(body->imported_node.has_value());
    EXPECT_FALSE(body->imported_node->missing_source);

    const SceneNodeAsset* turret =
        find_scene_node(scene, "tank_anchor/tank1/turret");
    ASSERT_NE(turret, nullptr);
    ASSERT_TRUE(turret->parent_id.has_value());
    EXPECT_EQ(*turret->parent_id, "tank_anchor/tank1/body");
    ASSERT_TRUE(turret->mesh_source.has_value());
    EXPECT_EQ(turret->mesh_source->mesh_index, 1u);
    ASSERT_TRUE(turret->renderable_asset.has_value());

    const SceneNodeAsset* gun =
        find_scene_node(scene, "tank_anchor/tank1/gun");
    ASSERT_NE(gun, nullptr);
    ASSERT_TRUE(gun->parent_id.has_value());
    EXPECT_EQ(*gun->parent_id, "tank_anchor/tank1/turret");
    ASSERT_TRUE(gun->mesh_source.has_value());
    EXPECT_EQ(gun->mesh_source->mesh_index, 0u);
    ASSERT_TRUE(gun->renderable_asset.has_value());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());
}

TEST(SceneAuthoringMaterialize, SceneImportSourceRebuildPreservesChildBehavior)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};

    SceneAssetData scene{};
    scene.name = "glb_scene_import_rebuild";
    SceneNodeAsset anchor = make_scene_node("tank_anchor");
    anchor.scene_import_source = SceneImportSourceAsset{
        .kind = SceneImportSourceKind::GLB,
        .path = "gltf/tank1.glb",
        .import_prefix = "tank_anchor/tank1",
    };
    scene.nodes.push_back(std::move(anchor));

    {
        EngineAssetLibrary assets{ device, logger, WZ_TEST_FIXTURE_DIR };
        const auto report =
            materialize_scene_authoring_components(scene, assets);
        ASSERT_TRUE(report.ok) << report.error;
    }

    SceneNodeAsset* turret =
        find_scene_node(scene, "tank_anchor/tank1/turret");
    ASSERT_NE(turret, nullptr);
    turret->behavior = SceneBehaviorAsset{
        .module = "tank",
        .name = "rotate_turret",
        .enabled = true,
        .config = {
            SceneBehaviorConfigValue{
                .key = "speed",
                .kind = SceneBehaviorConfigValueKind::Number,
                .number_value = 2.0,
            },
        },
    };

    {
        EngineAssetLibrary assets{ device, logger, WZ_TEST_FIXTURE_DIR };
        const auto report =
            materialize_scene_authoring_components(scene, assets);
        ASSERT_TRUE(report.ok) << report.error;
        ASSERT_TRUE(assets.commit());
        ASSERT_TRUE(assets.resolve_all().ok());
    }

    ASSERT_EQ(scene.nodes.size(), 4u);
    turret = find_scene_node(scene, "tank_anchor/tank1/turret");
    ASSERT_NE(turret, nullptr);
    ASSERT_TRUE(turret->behavior.has_value());
    EXPECT_EQ(turret->behavior->module, "tank");
    EXPECT_EQ(turret->behavior->name, "rotate_turret");
    ASSERT_EQ(turret->behavior->config.size(), 1u);
    EXPECT_EQ(turret->behavior->config[0].key, "speed");
    EXPECT_DOUBLE_EQ(turret->behavior->config[0].number_value, 2.0);
    ASSERT_TRUE(turret->imported_node.has_value());
    EXPECT_FALSE(turret->imported_node->missing_source);
}

TEST(SceneAuthoringMaterialize, SceneImportSourceRejectsNodeIdCollision)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, WZ_TEST_FIXTURE_DIR };

    SceneAssetData scene{};
    scene.name = "glb_scene_import_collision";
    SceneNodeAsset anchor = make_scene_node("tank_anchor");
    anchor.scene_import_source = SceneImportSourceAsset{
        .kind = SceneImportSourceKind::GLB,
        .path = "gltf/tank1.glb",
        .import_prefix = "tank_anchor/tank1",
    };
    scene.nodes.push_back(std::move(anchor));
    scene.nodes.push_back(make_scene_node("tank_anchor/tank1/body"));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    EXPECT_FALSE(report.ok);
    EXPECT_NE(
        report.error.find("collides with existing authored node"),
        std::string::npos);
}

TEST(SceneAuthoringMaterialize, SceneImportSourceMarksMissingNodes)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};

    SceneAssetData scene{};
    scene.name = "glb_scene_import_missing_source";
    SceneNodeAsset anchor = make_scene_node("tank_anchor");
    anchor.scene_import_source = SceneImportSourceAsset{
        .kind = SceneImportSourceKind::GLB,
        .path = "gltf/tank1.glb",
        .import_prefix = "tank_anchor/import",
    };
    scene.nodes.push_back(std::move(anchor));

    {
        EngineAssetLibrary assets{ device, logger, WZ_TEST_FIXTURE_DIR };
        const auto report =
            materialize_scene_authoring_components(scene, assets);
        ASSERT_TRUE(report.ok) << report.error;
    }

    ASSERT_NE(find_scene_node(scene, "tank_anchor/import/body"), nullptr);
    ASSERT_NE(find_scene_node(scene, "tank_anchor/import/turret"), nullptr);
    ASSERT_NE(find_scene_node(scene, "tank_anchor/import/gun"), nullptr);

    SceneNodeAsset* anchor_node = find_scene_node(scene, "tank_anchor");
    ASSERT_NE(anchor_node, nullptr);
    ASSERT_TRUE(anchor_node->scene_import_source.has_value());
    anchor_node->scene_import_source->path = "gltf/cube.glb";

    {
        EngineAssetLibrary assets{ device, logger, WZ_TEST_FIXTURE_DIR };
        const auto report =
            materialize_scene_authoring_components(scene, assets);
        ASSERT_TRUE(report.ok) << report.error;
    }

    const SceneNodeAsset* body =
        find_scene_node(scene, "tank_anchor/import/body");
    const SceneNodeAsset* turret =
        find_scene_node(scene, "tank_anchor/import/turret");
    const SceneNodeAsset* gun =
        find_scene_node(scene, "tank_anchor/import/gun");
    ASSERT_NE(body, nullptr);
    ASSERT_NE(turret, nullptr);
    ASSERT_NE(gun, nullptr);
    ASSERT_TRUE(body->imported_node.has_value());
    ASSERT_TRUE(turret->imported_node.has_value());
    ASSERT_TRUE(gun->imported_node.has_value());
    EXPECT_TRUE(body->imported_node->missing_source);
    EXPECT_TRUE(turret->imported_node->missing_source);
    EXPECT_TRUE(gun->imported_node->missing_source);

    bool found_current_import_node = false;
    for (const auto& node : scene.nodes) {
        if (!node.imported_node || node.imported_node->missing_source) {
            continue;
        }
        if (node.imported_node->anchor_node == "tank_anchor"
            && node.imported_node->import_prefix == "tank_anchor/import")
        {
            found_current_import_node = true;
        }
    }
    EXPECT_TRUE(found_current_import_node);
}

