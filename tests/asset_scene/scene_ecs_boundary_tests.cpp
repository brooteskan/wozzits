#include "scene_asset_module_test_support.h"

TEST(SceneECSBoundary, AuthoredIdsMapToRuntimeEntitiesAndBack)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "ecs_identity";

    SceneNodeAsset root{};
    root.id = "root";
    scene.nodes.push_back(std::move(root));

    SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "root";
    child.camera = SceneCameraAsset{};
    scene.nodes.push_back(std::move(child));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const auto& inst = result.instance;
    ASSERT_TRUE(inst.authored_to_runtime.contains("root"));
    ASSERT_TRUE(inst.authored_to_runtime.contains("child"));

    const wz::scene::RuntimeEntityId root_entity =
        inst.authored_to_runtime.at("root");
    const wz::scene::RuntimeEntityId child_entity =
        inst.authored_to_runtime.at("child");

    EXPECT_NE(root_entity, wz::scene::INVALID_RUNTIME_ENTITY);
    EXPECT_NE(child_entity, wz::scene::INVALID_RUNTIME_ENTITY);
    ASSERT_LT(root_entity, inst.runtime_to_authored.size());
    ASSERT_LT(child_entity, inst.runtime_to_authored.size());
    EXPECT_EQ(inst.runtime_to_authored[root_entity], "root");
    EXPECT_EQ(inst.runtime_to_authored[child_entity], "child");
}

TEST(SceneECSBoundary, SceneECSVocabularyIsSceneLayerOnly)
{
    static_assert(std::is_same_v<
        wz::scene::AuthoredEntityId,
        std::string>);
    static_assert(std::is_same_v<
        wz::scene::RuntimeEntityId,
        wz::core::graph::NodeHandle>);
    static_assert(std::is_same_v<
        decltype(wz::scene::RuntimeComponentRecord<int>{}.node),
        wz::scene::RuntimeEntityId>);

    wz::scene::RuntimeComponentRecord<int> record{};
    record.node = wz::scene::INVALID_RUNTIME_ENTITY;
    record.component = 7;

    EXPECT_EQ(record.node, wz::scene::INVALID_RUNTIME_ENTITY);
    EXPECT_EQ(record.component, 7);
    EXPECT_TRUE(wz::scene::is_runtime_relevant_component(
        wz::scene::SceneAuthoredComponentKind::GroundBoundary));
    EXPECT_TRUE(wz::scene::is_runtime_relevant_component(
        wz::scene::SceneAuthoredComponentKind::Collision));
    EXPECT_TRUE(wz::scene::is_runtime_relevant_component(
        wz::scene::SceneAuthoredComponentKind::Proximity));
    EXPECT_TRUE(wz::scene::is_runtime_relevant_component(
        wz::scene::SceneAuthoredComponentKind::Motion));
    EXPECT_TRUE(wz::scene::is_runtime_relevant_component(
        wz::scene::SceneAuthoredComponentKind::Behavior));
}

TEST(SceneECSBoundary, EmptySceneSummaryIsZeroed)
{
    const wz::engine::assets::SceneAssetData scene{};

    const auto summary =
        wz::engine::assets::summarize_authored_scene_components(scene);

    EXPECT_EQ(summary.nodes, 0u);
    EXPECT_EQ(summary.transforms, 0u);
    EXPECT_EQ(summary.visibility, 0u);
    EXPECT_EQ(summary.motion_types, 0u);
    EXPECT_EQ(summary.parent_links, 0u);
    EXPECT_EQ(summary.renderables, 0u);
    EXPECT_EQ(summary.cameras, 0u);
    EXPECT_EQ(summary.lights, 0u);
    EXPECT_EQ(summary.input_receivers, 0u);
    EXPECT_EQ(summary.flying_camera_controllers, 0u);
    EXPECT_EQ(summary.actor_movement_controllers, 0u);
    EXPECT_EQ(summary.ground_boundaries, 0u);
    EXPECT_EQ(summary.collisions, 0u);
    EXPECT_EQ(summary.proximities, 0u);
    EXPECT_EQ(summary.motions, 0u);
    EXPECT_EQ(summary.behaviors, 0u);
    EXPECT_EQ(summary.audio_listeners, 0u);
    EXPECT_EQ(summary.event_listeners, 0u);
    EXPECT_EQ(summary.auxiliary_visuals, 0u);
    EXPECT_EQ(summary.editor_handles, 0u);
}

TEST(SceneECSBoundary, EmptyRuntimeSummaryIsZeroed)
{
    const wz::engine::assets::SceneInstance instance{};

    const auto summary =
        wz::engine::assets::summarize_scene_instance_components(instance);

    EXPECT_EQ(summary.runtime_entities, 0u);
    EXPECT_EQ(summary.renderable_descriptor_slots, 0u);
    EXPECT_EQ(summary.lights, 0u);
    EXPECT_EQ(summary.input_receivers, 0u);
    EXPECT_EQ(summary.flying_camera_controllers, 0u);
    EXPECT_EQ(summary.actor_movement_controllers, 0u);
    EXPECT_EQ(summary.ground_boundaries, 0u);
    EXPECT_EQ(summary.collisions, 0u);
    EXPECT_EQ(summary.proximities, 0u);
    EXPECT_EQ(summary.motions, 0u);
    EXPECT_EQ(summary.behaviors, 0u);
    EXPECT_EQ(summary.audio_listeners, 0u);
    EXPECT_EQ(summary.event_listeners, 0u);
    EXPECT_EQ(summary.auxiliary_visuals, 0u);
    EXPECT_EQ(summary.editor_handles, 0u);
}

TEST(SceneECSBoundary, CoreNodeFieldsDoNotCountAsOptionalComponents)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "core_only";

    SceneNodeAsset root{};
    root.id = "root";
    scene.nodes.push_back(std::move(root));

    SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "root";
    child.local.translation[0] = 1.0f;
    child.visible = false;
    child.motion_type = wz::scene::TransformNode::MotionType::Animated;

    EXPECT_FALSE(has_authored_renderable_component(child));
    EXPECT_FALSE(has_authored_camera_component(child));
    EXPECT_FALSE(has_authored_editor_only_components(child));
    EXPECT_FALSE(has_authored_auxiliary_visual_component(child));
    EXPECT_FALSE(has_authored_debug_visual_component(child));
    EXPECT_FALSE(has_runtime_relevant_components(child));
    scene.nodes.push_back(std::move(child));

    const auto summary = summarize_authored_scene_components(scene);
    EXPECT_EQ(summary.nodes, 2u);
    EXPECT_EQ(summary.transforms, 2u);
    EXPECT_EQ(summary.visibility, 2u);
    EXPECT_EQ(summary.motion_types, 2u);
    EXPECT_EQ(summary.parent_links, 1u);
    EXPECT_EQ(summary.renderables, 0u);
    EXPECT_EQ(summary.cameras, 0u);
    EXPECT_EQ(summary.input_receivers, 0u);
    EXPECT_EQ(summary.flying_camera_controllers, 0u);
    EXPECT_EQ(summary.actor_movement_controllers, 0u);
    EXPECT_EQ(summary.ground_boundaries, 0u);
    EXPECT_EQ(summary.collisions, 0u);
    EXPECT_EQ(summary.proximities, 0u);
    EXPECT_EQ(summary.motions, 0u);
    EXPECT_EQ(summary.behaviors, 0u);
    EXPECT_EQ(summary.audio_listeners, 0u);
    EXPECT_EQ(summary.event_listeners, 0u);
    EXPECT_EQ(summary.auxiliary_visuals, 0u);
    EXPECT_EQ(summary.editor_handles, 0u);
}

TEST(SceneECSBoundary, SummarizesAuthoredComponentInventory)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "component_inventory";

    SceneNodeAsset render_node{};
    render_node.id = "render_node";
    render_node.renderable = SceneRenderableBinding{};
    scene.nodes.push_back(std::move(render_node));

    SceneNodeAsset camera_node{};
    camera_node.id = "camera_node";
    camera_node.parent_id = "render_node";
    camera_node.camera = SceneCameraAsset{};
    camera_node.input_receiver = SceneInputReceiverAsset{
        .input_map = "asset://input_maps/editor",
    };
    camera_node.flying_camera_controller =
        SceneFlyingCameraControllerAsset{};
    camera_node.actor_movement_controller =
        SceneActorMovementControllerAsset{};
    camera_node.ground_boundary = SceneGroundBoundaryAsset{
        .min = { -5.0f, 0.0f, -5.0f },
        .max = { 5.0f, 0.0f, 5.0f },
    };
    camera_node.collision = SceneCollisionAsset{
        .layer_mask = 0x8u,
    };
    camera_node.audio_listener = SceneAudioListenerAsset{};
    camera_node.event_listener = SceneEventListenerAsset{
        .channels = { "editor" },
    };
    camera_node.proximity = SceneProximityAsset{
        .radius = 2.0f,
    };
    camera_node.motion = SceneMotionAsset{
        .linear_velocity = { 1.0f, 0.0f, 0.0f },
    };
    camera_node.behavior = SceneBehaviorAsset{
        .module = "gameplay",
        .name = "tick",
    };
    camera_node.debug_visual = SceneDebugVisualAsset{
        .kind = SceneDebugVisualKind::Axes,
    };
    camera_node.editor_handle = SceneEditorHandleAsset{};
    camera_node.scene_import_source = SceneImportSourceAsset{
        .kind = SceneImportSourceKind::GLB,
        .path = "models/editor.glb",
        .import_prefix = "editor",
    };
    camera_node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    camera_node.mesh_render_style = SceneMeshRenderStyleAsset{
        .depth_test = true,
    };
    camera_node.scalar_field_source = SceneScalarFieldSourceAsset{
        .kind = SceneScalarFieldSourceKind::ProceduralCheckerboard,
    };
    camera_node.vector_field_source = SceneVectorFieldSourceAsset{
        .kind = SceneVectorFieldSourceKind::RawF32,
        .path = "fields/normal.raw",
    };
    camera_node.ambient_lighting = SceneAmbientLightingAsset{};
    camera_node.hdri_environment = SceneHDRIEnvironmentAsset{};
    camera_node.sky_visual = SceneSkyVisualAsset{
        .kind = SceneSkyVisualKind::SolidColor,
    };
    camera_node.sky_surface = SceneSkySurfaceAsset{};
    camera_node.terrain = SceneTerrainAsset{};
    camera_node.terrain_render_style = SceneTerrainRenderStyleAsset{
        .path = SceneTerrainRenderPath::Surface,
    };
    camera_node.terrain_mesh_source = SceneTerrainMeshSourceAsset{
        .mode = SceneTerrainMeshSourceMode::MeshAsset,
    };
    camera_node.terrain_height_field_source =
        SceneTerrainHeightFieldSourceAsset{
            .mode = SceneTerrainHeightFieldSourceMode::ScalarFieldAsset,
        };

    EXPECT_TRUE(has_authored_camera_component(camera_node));
    EXPECT_TRUE(has_authored_editor_only_components(camera_node));
    EXPECT_TRUE(has_authored_auxiliary_visual_component(camera_node));
    EXPECT_TRUE(has_authored_debug_visual_component(camera_node));
    EXPECT_TRUE(has_runtime_relevant_components(camera_node));
    const auto components = authored_components_for_node(camera_node);
    EXPECT_NE(
        std::find(
            components.begin(),
            components.end(),
            wz::scene::SceneAuthoredComponentKind::Proximity),
        components.end());
    EXPECT_NE(
        std::find(
            components.begin(),
            components.end(),
            wz::scene::SceneAuthoredComponentKind::Motion),
        components.end());
    EXPECT_NE(
        std::find(
            components.begin(),
            components.end(),
            wz::scene::SceneAuthoredComponentKind::Behavior),
        components.end());
    scene.nodes.push_back(std::move(camera_node));

    scene.lights.push_back(SceneLightAsset{ .node_id = "render_node" });

    const auto summary = summarize_authored_scene_components(scene);
    EXPECT_EQ(summary.nodes, 2u);
    EXPECT_EQ(summary.transforms, 2u);
    EXPECT_EQ(summary.visibility, 2u);
    EXPECT_EQ(summary.motion_types, 2u);
    EXPECT_EQ(summary.parent_links, 1u);
    EXPECT_EQ(summary.renderables, 1u);
    EXPECT_EQ(summary.scene_import_sources, 1u);
    EXPECT_EQ(summary.mesh_sources, 1u);
    EXPECT_EQ(summary.mesh_render_styles, 1u);
    EXPECT_EQ(summary.scalar_field_sources, 1u);
    EXPECT_EQ(summary.vector_field_sources, 1u);
    EXPECT_EQ(summary.cameras, 1u);
    EXPECT_EQ(summary.lights, 1u);
    EXPECT_EQ(summary.ambient_lighting, 1u);
    EXPECT_EQ(summary.hdri_environments, 1u);
    EXPECT_EQ(summary.sky_visuals, 1u);
    EXPECT_EQ(summary.sky_surfaces, 1u);
    EXPECT_EQ(summary.input_receivers, 1u);
    EXPECT_EQ(summary.flying_camera_controllers, 1u);
    EXPECT_EQ(summary.actor_movement_controllers, 1u);
    EXPECT_EQ(summary.ground_boundaries, 1u);
    EXPECT_EQ(summary.collisions, 1u);
    EXPECT_EQ(summary.terrains, 1u);
    EXPECT_EQ(summary.terrain_render_styles, 1u);
    EXPECT_EQ(summary.terrain_mesh_sources, 1u);
    EXPECT_EQ(summary.terrain_height_field_sources, 1u);
    EXPECT_EQ(summary.proximities, 1u);
    EXPECT_EQ(summary.motions, 1u);
    EXPECT_EQ(summary.behaviors, 1u);
    EXPECT_EQ(summary.audio_listeners, 1u);
    EXPECT_EQ(summary.event_listeners, 1u);
    EXPECT_EQ(summary.auxiliary_visuals, 1u);
    EXPECT_EQ(summary.editor_handles, 1u);
}

TEST(SceneECSBoundary, AuthoredComponentsIncludeTerrainRenderStyle)
{
    using namespace wz::engine::assets;

    SceneNodeAsset node{};
    node.id = "terrain_style";
    node.terrain_render_style = SceneTerrainRenderStyleAsset{
        .path = SceneTerrainRenderPath::DebugWireframe,
    };

    const auto components = authored_components_for_node(node);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::TerrainRenderStyle), 1);
}

TEST(SceneECSBoundary, CountsLegacyAndAssetBackedRenderableComponents)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "mixed_renderable_component_sources";

    SceneNodeAsset legacy_node{};
    legacy_node.id = "legacy_renderable";
    legacy_node.renderable = SceneRenderableBinding{};
    EXPECT_TRUE(has_authored_renderable_component(legacy_node));
    scene.nodes.push_back(std::move(legacy_node));

    SceneNodeAsset asset_node{};
    asset_node.id = "asset_renderable";
    wz::asset::AssetKey renderable_key{};
    renderable_key.content_hash = { 0x58, 0x01 };
    asset_node.renderable_asset = renderable_key;
    EXPECT_TRUE(has_authored_renderable_component(asset_node));
    scene.nodes.push_back(std::move(asset_node));

    SceneNodeAsset empty_node{};
    empty_node.id = "empty";
    EXPECT_FALSE(has_authored_renderable_component(empty_node));
    scene.nodes.push_back(std::move(empty_node));

    const auto summary = summarize_authored_scene_components(scene);
    EXPECT_EQ(summary.nodes, 3u);
    EXPECT_EQ(summary.renderables, 2u);
}

TEST(SceneECSBoundary, AssetBackedRenderableDoesNotEmbedAssetDefinition)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "asset_reference_boundary";

    wz::asset::AssetKey renderable_key{};
    renderable_key.content_hash = { 0x7100, 0x01 };
    renderable_key.schema_hash = { 0x7100, 0x02 };

    SceneNodeAsset node{};
    node.id = "landscape_or_actor_visual";
    node.renderable_asset = renderable_key;
    scene.nodes.push_back(node);

    const auto components = authored_components_for_node(scene.nodes[0]);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::Renderable), 1);

    EXPECT_TRUE(has_authored_renderable_component(scene.nodes[0]));
    EXPECT_FALSE(scene.nodes[0].renderable.has_value());
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    EXPECT_EQ(*scene.nodes[0].renderable_asset, renderable_key);

    const auto summary = summarize_authored_scene_components(scene);
    EXPECT_EQ(summary.nodes, 1u);
    EXPECT_EQ(summary.renderables, 1u);
    EXPECT_EQ(summary.cameras, 0u);
    EXPECT_EQ(summary.input_receivers, 0u);
    EXPECT_EQ(summary.actor_movement_controllers, 0u);
    EXPECT_EQ(summary.ground_boundaries, 0u);
    EXPECT_EQ(summary.editor_handles, 0u);
}

TEST(SceneECSBoundary, RuntimeReadySceneUsesAssetReferencesWithoutRecipes)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_runtime_ready_refs_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    EngineAssetLibrary assets{ device, logger, root };
    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "runtime_ready/cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto renderable = assets.renderables().create_mesh_wireframe({
        .name = "runtime_ready/cube_wireframe",
        .mesh = mesh,
    });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "runtime_ready_asset_refs";

    wz::asset::AssetKey terrain_key{};
    terrain_key.content_hash = { 0x7300, 0x01 };
    terrain_key.schema_hash = { 0x7300, 0x02 };

    SceneNodeAsset visual{};
    visual.id = "visual";
    visual.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(visual));

    SceneNodeAsset terrain{};
    terrain.id = "terrain";
    terrain.terrain = SceneTerrainAsset{
        .terrain_asset = terrain_key,
        .visible = true,
        .queryable = true,
        .constrain_movement = true,
    };
    scene.nodes.push_back(std::move(terrain));

    EXPECT_FALSE(has_asset_authoring_recipes(scene.nodes[0]));
    EXPECT_FALSE(has_asset_authoring_recipes(scene.nodes[1]));

    const auto recipe_summary = summarize_scene_asset_authoring_recipes(scene);
    EXPECT_EQ(recipe_summary.nodes_with_recipes, 0u);
    EXPECT_EQ(recipe_summary.total_recipes, 0u);

    const auto authored_summary = summarize_authored_scene_components(scene);
    EXPECT_EQ(authored_summary.renderables, 1u);
    EXPECT_EQ(authored_summary.terrains, 1u);
    EXPECT_EQ(authored_summary.mesh_sources, 0u);
    EXPECT_EQ(authored_summary.scalar_field_sources, 0u);
    EXPECT_EQ(authored_summary.vector_field_sources, 0u);
    EXPECT_EQ(authored_summary.terrain_mesh_sources, 0u);
    EXPECT_EQ(authored_summary.terrain_height_field_sources, 0u);

    TestRenderableResolver renderable_resolver(assets.renderables());
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
    };
    auto result = instantiate_scene(scene, context);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const auto runtime_summary =
        summarize_scene_instance_components(result.instance);
    EXPECT_EQ(runtime_summary.runtime_entities, 2u);
    EXPECT_EQ(runtime_summary.terrains, 1u);
    EXPECT_EQ(runtime_summary.terrain_mesh_sources, 0u);
    EXPECT_EQ(runtime_summary.terrain_height_field_sources, 0u);
    EXPECT_TRUE(result.instance.terrains[0].component.terrain_asset
        == terrain_key);
}

TEST(SceneECSBoundary, TerrainSourceCandidateHelpersMatchEditorRules)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "terrain_source_candidates";

    SceneNodeAsset terrain = make_scene_node("terrain");
    terrain.terrain = SceneTerrainAsset{};
    scene.nodes.push_back(std::move(terrain));

    SceneNodeAsset child_mesh = make_scene_node("child_mesh");
    child_mesh.parent_id = "terrain";
    child_mesh.mesh_source = SceneMeshSourceAsset{};
    scene.nodes.push_back(std::move(child_mesh));

    SceneNodeAsset child_field = make_scene_node("child_field");
    child_field.parent_id = "terrain";
    child_field.scalar_field_source = SceneScalarFieldSourceAsset{};
    scene.nodes.push_back(std::move(child_field));

    SceneNodeAsset sibling_mesh = make_scene_node("sibling_mesh");
    sibling_mesh.mesh_source = SceneMeshSourceAsset{};
    scene.nodes.push_back(std::move(sibling_mesh));

    const auto* terrain_node = find_scene_node(scene, "terrain");
    ASSERT_NE(terrain_node, nullptr);

    const auto mesh_candidates =
        terrain_mesh_source_candidate_nodes(scene, *terrain_node);
    ASSERT_EQ(mesh_candidates.size(), 1u);
    EXPECT_EQ(mesh_candidates[0], "child_mesh");

    const auto height_candidates =
        terrain_height_field_source_candidate_nodes(scene, *terrain_node);
    ASSERT_EQ(height_candidates.size(), 1u);
    EXPECT_EQ(height_candidates[0], "child_field");

    EXPECT_TRUE(is_terrain_mesh_source_node_candidate(
        *terrain_node,
        scene.nodes[1]));
    EXPECT_FALSE(is_terrain_mesh_source_node_candidate(
        *terrain_node,
        scene.nodes[3]));
}

TEST(SceneECSBoundary, ExclusiveTerrainSourceAttachHelpersClearOppositeSource)
{
    using namespace wz::engine::assets;

    SceneNodeAsset node = make_scene_node("terrain");
    attach_terrain_mesh_source(node, SceneTerrainMeshSourceAsset{});
    attach_exclusive_terrain_height_field_source(
        node,
        SceneTerrainHeightFieldSourceAsset{});

    EXPECT_FALSE(node.terrain_mesh_source.has_value());
    EXPECT_TRUE(node.terrain_height_field_source.has_value());

    attach_exclusive_terrain_mesh_source(node, SceneTerrainMeshSourceAsset{});

    EXPECT_TRUE(node.terrain_mesh_source.has_value());
    EXPECT_FALSE(node.terrain_height_field_source.has_value());
}

TEST(SceneECSBoundary, AuthoredLightDirectionUsesNodeLocalNegativeY)
{
    using namespace wz::engine::assets;

    SceneNodeAsset node = make_scene_node("sun");

    float direction[3]{};
    authored_light_direction_from_node(node, direction);
    EXPECT_FLOAT_EQ(direction[0], 0.0f);
    EXPECT_FLOAT_EQ(direction[1], -1.0f);
    EXPECT_FLOAT_EQ(direction[2], 0.0f);

    const float target[3]{ 0.0f, 0.0f, 1.0f };
    set_node_rotation_from_authored_light_direction(node, target);
    authored_light_direction_from_node(node, direction);

    EXPECT_NEAR(direction[0], target[0], 1e-5f);
    EXPECT_NEAR(direction[1], target[1], 1e-5f);
    EXPECT_NEAR(direction[2], target[2], 1e-5f);
}

TEST(SceneECSBoundary, DetachesStaleMaterializedRenderables)
{
    using namespace wz::engine::assets;

    wz::asset::AssetKey renderable_key{};
    renderable_key.content_hash = { 0x9898, 0x1234 };

    SceneAssetData scene{};
    scene.name = "detach_stale_materialized_renderables";

    SceneNodeAsset terrain = make_scene_node("terrain");
    terrain.terrain = SceneTerrainAsset{};
    terrain.terrain_mesh_source = SceneTerrainMeshSourceAsset{
        .mode = SceneTerrainMeshSourceMode::SceneNode,
        .source_node = "source_mesh",
    };
    terrain.renderable_asset = renderable_key;
    scene.nodes.push_back(std::move(terrain));

    SceneNodeAsset source = make_scene_node("source_mesh");
    source.parent_id = "terrain";
    source.mesh_source = SceneMeshSourceAsset{};
    source.renderable_asset = renderable_key;
    scene.nodes.push_back(std::move(source));

    SceneNodeAsset stale = make_scene_node("empty");
    stale.renderable_asset = renderable_key;
    scene.nodes.push_back(std::move(stale));

    EXPECT_TRUE(can_own_materialized_renderable_asset(scene, scene.nodes[0]));
    EXPECT_FALSE(can_own_materialized_renderable_asset(scene, scene.nodes[1]));
    EXPECT_FALSE(can_own_materialized_renderable_asset(scene, scene.nodes[2]));

    EXPECT_EQ(detach_stale_materialized_renderable_assets(scene), 2u);
    EXPECT_TRUE(scene.nodes[0].renderable_asset.has_value());
    EXPECT_FALSE(scene.nodes[1].renderable_asset.has_value());
    EXPECT_FALSE(scene.nodes[2].renderable_asset.has_value());
}

TEST(SceneECSBoundary, SummarizesRuntimeProjectionInventory)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "runtime_inventory";

    SceneNodeAsset root{};
    root.id = "root";
    scene.nodes.push_back(std::move(root));

    SceneNodeAsset camera{};
    camera.id = "camera";
    camera.parent_id = "root";
    camera.camera = SceneCameraAsset{};
    camera.input_receiver = SceneInputReceiverAsset{
        .input_map = "asset://input_maps/editor",
    };
    camera.flying_camera_controller =
        SceneFlyingCameraControllerAsset{};
    camera.actor_movement_controller =
        SceneActorMovementControllerAsset{};
    camera.ground_boundary = SceneGroundBoundaryAsset{
        .min = { -5.0f, 0.0f, -5.0f },
        .max = { 5.0f, 0.0f, 5.0f },
    };
    camera.collision = SceneCollisionAsset{
        .layer_mask = 0x10u,
    };
    camera.audio_listener = SceneAudioListenerAsset{};
    camera.event_listener = SceneEventListenerAsset{
        .channels = { "editor" },
    };
    camera.debug_visual = SceneDebugVisualAsset{
        .kind = SceneDebugVisualKind::Axes,
    };
    camera.editor_handle = SceneEditorHandleAsset{};
    scene.nodes.push_back(std::move(camera));

    scene.lights.push_back(SceneLightAsset{ .node_id = "camera" });

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const auto summary = summarize_scene_instance_components(result.instance);
    EXPECT_EQ(summary.runtime_entities, 2u);
    EXPECT_EQ(summary.renderable_descriptor_slots, 2u);
    EXPECT_EQ(summary.lights, 1u);
    EXPECT_EQ(summary.input_receivers, 1u);
    EXPECT_EQ(summary.flying_camera_controllers, 1u);
    EXPECT_EQ(summary.actor_movement_controllers, 1u);
    EXPECT_EQ(summary.ground_boundaries, 1u);
    EXPECT_EQ(summary.collisions, 1u);
    EXPECT_EQ(summary.audio_listeners, 1u);
    EXPECT_EQ(summary.event_listeners, 1u);
    EXPECT_EQ(summary.auxiliary_visuals, 1u);
    EXPECT_EQ(summary.editor_handles, 1u);
}

TEST(SceneECSBoundary, SummaryCountsDeclaredLightsWithoutResolvingNodeIds)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "declared_lights";

    SceneNodeAsset node{};
    node.id = "real_node";
    scene.nodes.push_back(std::move(node));

    scene.lights.push_back(SceneLightAsset{ .node_id = "real_node" });
    scene.lights.push_back(SceneLightAsset{ .node_id = "missing_node" });

    const auto summary = summarize_authored_scene_components(scene);
    EXPECT_EQ(summary.nodes, 1u);
    EXPECT_EQ(summary.lights, 2u);
}

TEST(SceneECSBoundary, DuplicateAuthoredIdsAreRejected)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "duplicate_ids";

    SceneNodeAsset first{};
    first.id = "dup";
    scene.nodes.push_back(std::move(first));

    SceneNodeAsset second{};
    second.id = "dup";
    scene.nodes.push_back(std::move(second));

    auto result = instantiate_scene(scene);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::DuplicateNodeId);
    EXPECT_NE(result.error_detail.find("id='dup'"), std::string::npos);
    EXPECT_NE(result.error_detail.find("name=''"), std::string::npos);
}

TEST(SceneECSBoundary, FingerprintTracksAuthoredComponentData)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "fingerprint_scene";

    SceneNodeAsset node{};
    node.id = "camera";
    node.camera = SceneCameraAsset{};
    scene.nodes.push_back(std::move(node));

    const uint64_t original = scene_asset_fingerprint(scene);

    scene.nodes[0].camera->fov_y = 0.75f;
    const uint64_t changed = scene_asset_fingerprint(scene);

    EXPECT_NE(original, changed);
}

TEST(SceneECSBoundary, FingerprintTracksCollisionComponentData)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "fingerprint_collision_scene";

    wz::asset::AssetKey collision_key{};
    collision_key.content_hash = { 0xc011, 0x510u };

    SceneNodeAsset node{};
    node.id = "body";
    node.collision = SceneCollisionAsset{
        .collision_asset = collision_key,
        .layer_mask = 0x2u,
        .collides_with_mask = 0x4u,
    };
    scene.nodes.push_back(std::move(node));

    const uint64_t original = scene_asset_fingerprint(scene);

    scene.nodes[0].collision->collides_with_mask = 0x8u;
    const uint64_t changed = scene_asset_fingerprint(scene);

    EXPECT_NE(original, changed);
}

TEST(SceneECSBoundary, FingerprintTracksMotionComponentData)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "fingerprint_motion_scene";

    SceneNodeAsset node{};
    node.id = "mover";
    node.motion = SceneMotionAsset{
        .linear_velocity = { 1.0f, 0.0f, 0.0f },
    };
    scene.nodes.push_back(std::move(node));

    const uint64_t original = scene_asset_fingerprint(scene);

    scene.nodes[0].motion->angular_velocity[1] = 0.5f;
    const uint64_t angular_changed = scene_asset_fingerprint(scene);
    EXPECT_NE(original, angular_changed);

    scene.nodes[0].motion->angular_velocity[1] = 0.0f;
    scene.nodes[0].motion->space = SceneMotionSpace::Local;
    const uint64_t space_changed = scene_asset_fingerprint(scene);
    EXPECT_NE(original, space_changed);
}

TEST(SceneECSBoundary, FingerprintTracksEditorAuthoringDrafts)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "fingerprint_authoring_draft_scene";

    wz::asset::AssetKey mesh_key{};
    mesh_key.content_hash = { 0x1111, 0x2222 };
    wz::asset::AssetKey scalar_key{};
    scalar_key.content_hash = { 0x3333, 0x4444 };
    wz::asset::AssetKey vector_key{};
    vector_key.content_hash = { 0x5555, 0x6666 };

    SceneNodeAsset node{};
    node.id = "rock";
    node.scene_import_source = SceneImportSourceAsset{
        .kind = SceneImportSourceKind::GLB,
        .path = "gltf/tank1.glb",
        .import_prefix = "tank",
        .scene_index = 0u,
    };
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::GLB,
        .path = "gltf/low_poly_rock.glb",
        .mesh_index = 0,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .depth_test = true,
        .depth_write = false,
    };
    node.scalar_field_source = SceneScalarFieldSourceAsset{
        .kind = SceneScalarFieldSourceKind::ProceduralSineWaves,
        .scalar_field_asset = scalar_key,
        .width = 32,
        .height = 16,
        .frequency = 2.0f,
        .amplitude = 0.5f,
    };
    node.vector_field_source = SceneVectorFieldSourceAsset{
        .kind = SceneVectorFieldSourceKind::RawF32,
        .vector_field_asset = vector_key,
        .path = "fields/normal.raw",
        .width = 32,
        .height = 16,
        .components_per_channel = 3,
        .channels = { VectorFieldChannelDesc{ .name = "normal" } },
    };
    node.terrain_render_style = SceneTerrainRenderStyleAsset{
        .path = SceneTerrainRenderPath::Surface,
        .depth_test = true,
        .depth_write = true,
    };
    node.terrain_mesh_source = SceneTerrainMeshSourceAsset{
        .mode = SceneTerrainMeshSourceMode::MeshAsset,
        .mesh_asset = mesh_key,
        .min_surface_normal_y = 0.4f,
        .include_backfaces = true,
    };
    node.terrain_height_field_source = SceneTerrainHeightFieldSourceAsset{
        .mode = SceneTerrainHeightFieldSourceMode::ScalarFieldAsset,
        .scalar_field_asset = scalar_key,
        .origin = { -1.0f, -2.0f },
        .size = { 8.0f, 9.0f },
        .vertical_scale = 3.0f,
        .base_height = -0.25f,
    };
    scene.nodes.push_back(std::move(node));

    const uint64_t original = scene_asset_fingerprint(scene);

    scene.nodes[0].scene_import_source->import_prefix = "tank_alt";
    EXPECT_NE(original, scene_asset_fingerprint(scene));
    scene.nodes[0].scene_import_source->import_prefix = "tank";

    scene.nodes[0].mesh_source->mesh_index = 1;
    EXPECT_NE(original, scene_asset_fingerprint(scene));
    scene.nodes[0].mesh_source->mesh_index = 0;

    scene.nodes[0].mesh_render_style->depth_write = true;
    EXPECT_NE(original, scene_asset_fingerprint(scene));
    scene.nodes[0].mesh_render_style->depth_write = false;

    scene.nodes[0].scalar_field_source->amplitude = 1.0f;
    EXPECT_NE(original, scene_asset_fingerprint(scene));
    scene.nodes[0].scalar_field_source->amplitude = 0.5f;

    scene.nodes[0].vector_field_source->components_per_channel = 2;
    EXPECT_NE(original, scene_asset_fingerprint(scene));
    scene.nodes[0].vector_field_source->components_per_channel = 3;

    scene.nodes[0].terrain_render_style->path =
        SceneTerrainRenderPath::DebugWireframe;
    EXPECT_NE(original, scene_asset_fingerprint(scene));
    scene.nodes[0].terrain_render_style->path =
        SceneTerrainRenderPath::Surface;

    scene.nodes[0].terrain_mesh_source->include_backfaces = false;
    EXPECT_NE(original, scene_asset_fingerprint(scene));
    scene.nodes[0].terrain_mesh_source->include_backfaces = true;

    scene.nodes[0].terrain_height_field_source->vertical_scale = 4.0f;
    EXPECT_NE(original, scene_asset_fingerprint(scene));
}

TEST(SceneECSBoundary, EditorAuthoringDraftsDoNotInstantiateRuntimeComponents)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "source_drafts_are_authored_only";

    wz::asset::AssetKey mesh_key{};
    mesh_key.content_hash = { 0x5151, 0x6161 };
    wz::asset::AssetKey scalar_key{};
    scalar_key.content_hash = { 0x7171, 0x8181 };
    wz::asset::AssetKey vector_key{};
    vector_key.content_hash = { 0x9191, 0xA1A1 };

    SceneNodeAsset node{};
    node.id = "drafts";
    node.scene_import_source = SceneImportSourceAsset{
        .kind = SceneImportSourceKind::GLB,
        .path = "gltf/tank1.glb",
        .import_prefix = "tank",
    };
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .depth_test = true,
    };
    node.scalar_field_source = SceneScalarFieldSourceAsset{
        .kind = SceneScalarFieldSourceKind::ProceduralCheckerboard,
        .scalar_field_asset = scalar_key,
    };
    node.vector_field_source = SceneVectorFieldSourceAsset{
        .kind = SceneVectorFieldSourceKind::RawF32,
        .vector_field_asset = vector_key,
        .path = "fields/normal.raw",
    };
    node.terrain_render_style = SceneTerrainRenderStyleAsset{
        .path = SceneTerrainRenderPath::DebugWireframe,
    };
    node.terrain_mesh_source = SceneTerrainMeshSourceAsset{
        .mode = SceneTerrainMeshSourceMode::MeshAsset,
        .mesh_asset = mesh_key,
    };
    node.terrain_height_field_source = SceneTerrainHeightFieldSourceAsset{
        .mode = SceneTerrainHeightFieldSourceMode::ScalarFieldAsset,
        .scalar_field_asset = scalar_key,
    };

    EXPECT_FALSE(has_runtime_relevant_components(node));
    EXPECT_TRUE(has_asset_authoring_recipes(node));

    const auto components = authored_components_for_node(node);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::SceneImportSource), 1);

    scene.nodes.push_back(std::move(node));

    const auto recipe_summary =
        summarize_scene_asset_authoring_recipes(scene);
    EXPECT_EQ(recipe_summary.nodes_with_recipes, 1u);
    EXPECT_EQ(recipe_summary.total_recipes, 8u);
    EXPECT_EQ(recipe_summary.scene_import_sources, 1u);
    EXPECT_EQ(recipe_summary.mesh_sources, 1u);
    EXPECT_EQ(recipe_summary.mesh_render_styles, 1u);
    EXPECT_EQ(recipe_summary.scalar_field_sources, 1u);
    EXPECT_EQ(recipe_summary.vector_field_sources, 1u);
    EXPECT_EQ(recipe_summary.terrain_render_styles, 1u);
    EXPECT_EQ(recipe_summary.terrain_mesh_sources, 1u);
    EXPECT_EQ(recipe_summary.terrain_height_field_sources, 1u);

    const auto authored_summary = summarize_authored_scene_components(scene);
    EXPECT_EQ(authored_summary.scene_import_sources, 1u);
    EXPECT_EQ(authored_summary.mesh_sources, 1u);
    EXPECT_EQ(authored_summary.mesh_render_styles, 1u);
    EXPECT_EQ(authored_summary.scalar_field_sources, 1u);
    EXPECT_EQ(authored_summary.vector_field_sources, 1u);
    EXPECT_EQ(authored_summary.terrain_render_styles, 1u);
    EXPECT_EQ(authored_summary.terrain_mesh_sources, 1u);
    EXPECT_EQ(authored_summary.terrain_height_field_sources, 1u);

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const auto runtime_summary =
        summarize_scene_instance_components(result.instance);
    EXPECT_EQ(runtime_summary.terrains, 0u);
    EXPECT_EQ(runtime_summary.terrain_mesh_sources, 0u);
    EXPECT_EQ(runtime_summary.terrain_height_field_sources, 0u);
    EXPECT_EQ(runtime_summary.renderable_descriptor_slots, 1u);
    EXPECT_TRUE(result.instance.input_receivers.empty());
    EXPECT_TRUE(result.instance.ground_boundaries.empty());
    EXPECT_TRUE(result.instance.auxiliary_visuals.empty());
}

TEST(SceneECSBoundary, SummarizesAuthoringRecipesAcrossMultipleNodes)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "multi_node_recipes";

    SceneNodeAsset first{};
    first.id = "first";
    first.scene_import_source = SceneImportSourceAsset{
        .kind = SceneImportSourceKind::GLB,
        .path = "models/first.glb",
    };
    first.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    scene.nodes.push_back(std::move(first));

    SceneNodeAsset second{};
    second.id = "second";
    scene.nodes.push_back(std::move(second));

    SceneNodeAsset third{};
    third.id = "third";
    third.scalar_field_source = SceneScalarFieldSourceAsset{
        .kind = SceneScalarFieldSourceKind::ProceduralCheckerboard,
    };
    scene.nodes.push_back(std::move(third));

    const auto summary = summarize_scene_asset_authoring_recipes(scene);
    EXPECT_EQ(summary.nodes_with_recipes, 2u);
    EXPECT_EQ(summary.total_recipes, 3u);
    EXPECT_EQ(summary.scene_import_sources, 1u);
    EXPECT_EQ(summary.mesh_sources, 1u);
    EXPECT_EQ(summary.scalar_field_sources, 1u);
}

TEST(SceneECSBoundary, FingerprintIgnoresRuntimeOwnerIdentity)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "runtime_identity_independent";

    SceneNodeAsset node{};
    node.id = "node";
    node.debug_visual = SceneDebugVisualAsset{
        .kind = SceneDebugVisualKind::Axes,
    };
    scene.nodes.push_back(std::move(node));

    const uint64_t before = scene_asset_fingerprint(scene);

    auto first = instantiate_scene(scene);
    auto second = instantiate_scene(scene);
    ASSERT_TRUE(first.ok()) << first.error_detail;
    ASSERT_TRUE(second.ok()) << second.error_detail;
    EXPECT_NE(&first.instance.storage, &second.instance.storage);

    const uint64_t after = scene_asset_fingerprint(scene);
    EXPECT_EQ(before, after);
}

