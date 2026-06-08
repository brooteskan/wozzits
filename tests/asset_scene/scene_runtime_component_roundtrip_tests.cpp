#include "scene_asset_module_test_support.h"

TEST(SceneAssetModule, FlyCameraComponentDescriptors)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_fly_camera_desc_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "fly_camera_desc.json", kFlyCameraDescriptorSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "fly_camera_desc",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);

    // Verify parsed asset data
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    const auto& node = scene_data->nodes[0];
    EXPECT_EQ(node.id, "editor_fly_camera");
    ASSERT_TRUE(node.camera.has_value());
    ASSERT_TRUE(node.input_receiver.has_value());
    ASSERT_TRUE(node.flying_camera_controller.has_value());
    EXPECT_EQ(node.input_receiver->input_map,
        "asset://input_maps/editor_fly_camera");
    EXPECT_FLOAT_EQ(node.flying_camera_controller->move_speed, 20.0f);
    EXPECT_FLOAT_EQ(node.flying_camera_controller->look_speed, 0.0005f);
    EXPECT_FLOAT_EQ(node.flying_camera_controller->boost_multiplier, 3.0f);
    EXPECT_FLOAT_EQ(node.flying_camera_controller->roll_speed, 1.5f);

    // Node should NOT have audio/event listeners
    EXPECT_FALSE(node.audio_listener.has_value());
    EXPECT_FALSE(node.event_listener.has_value());

    // Instantiate
    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok());

    auto& inst = result.instance;

    // One node in the graph
    EXPECT_EQ(wz::core::graph::node_count(inst.storage.polytree), 1u);
    EXPECT_TRUE(inst.authored_to_runtime.contains("editor_fly_camera"));
    auto cam_h = inst.authored_to_runtime["editor_fly_camera"];

    // Active camera default view should still work
    EXPECT_NEAR(inst.default_view.camera_position.y, 5.0f, 1e-4f);
    EXPECT_NEAR(inst.default_view.camera_position.z, -20.0f, 1e-4f);
    EXPECT_NE(inst.default_view.projection.m[0], 0.0f);

    // Component records: one input receiver, one flying camera controller
    ASSERT_EQ(inst.input_receivers.size(), 1u);
    EXPECT_EQ(inst.input_receivers[0].node, cam_h);
    EXPECT_EQ(inst.input_receivers[0].component.input_map,
        "asset://input_maps/editor_fly_camera");

    ASSERT_EQ(inst.flying_camera_controllers.size(), 1u);
    EXPECT_EQ(inst.flying_camera_controllers[0].node, cam_h);
    EXPECT_FLOAT_EQ(
        inst.flying_camera_controllers[0].component.move_speed, 20.0f);
    EXPECT_FLOAT_EQ(
        inst.flying_camera_controllers[0].component.look_speed, 0.0005f);
    EXPECT_FLOAT_EQ(
        inst.flying_camera_controllers[0].component.boost_multiplier, 3.0f);
    EXPECT_FLOAT_EQ(
        inst.flying_camera_controllers[0].component.roll_speed, 1.5f);

    // No audio/event component records
    EXPECT_TRUE(inst.audio_listeners.empty());
    EXPECT_TRUE(inst.event_listeners.empty());

    // No renderable — node is camera+controller only
    EXPECT_EQ(inst.renderables[cam_h].node_class.role,
        wz::scene::SceneRole::None);
}

TEST(SceneAssetModule, ListenerOnlyNodeDescriptors)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_listener_desc_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "listener_desc.json", kListenerOnlySceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "listener_desc",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);

    // Verify parsed asset data
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    const auto& node = scene_data->nodes[0];
    EXPECT_EQ(node.id, "listener");
    EXPECT_FALSE(node.renderable.has_value());
    EXPECT_FALSE(node.camera.has_value());
    ASSERT_TRUE(node.audio_listener.has_value());
    EXPECT_TRUE(node.audio_listener->active);
    ASSERT_TRUE(node.event_listener.has_value());
    ASSERT_EQ(node.event_listener->channels.size(), 2u);
    EXPECT_EQ(node.event_listener->channels[0], "gameplay");
    EXPECT_EQ(node.event_listener->channels[1], "ui");

    // No input/controller
    EXPECT_FALSE(node.input_receiver.has_value());
    EXPECT_FALSE(node.flying_camera_controller.has_value());

    // Instantiate
    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok());

    auto& inst = result.instance;

    EXPECT_EQ(wz::core::graph::node_count(inst.storage.polytree), 1u);
    EXPECT_TRUE(inst.authored_to_runtime.contains("listener"));
    auto listen_h = inst.authored_to_runtime["listener"];

    // No render output
    EXPECT_EQ(inst.renderables[listen_h].node_class.role,
        wz::scene::SceneRole::None);

    // Audio listener record
    ASSERT_EQ(inst.audio_listeners.size(), 1u);
    EXPECT_EQ(inst.audio_listeners[0].node, listen_h);
    EXPECT_TRUE(inst.audio_listeners[0].component.active);

    // Event listener record
    ASSERT_EQ(inst.event_listeners.size(), 1u);
    EXPECT_EQ(inst.event_listeners[0].node, listen_h);
    ASSERT_EQ(inst.event_listeners[0].component.channels.size(), 2u);
    EXPECT_EQ(inst.event_listeners[0].component.channels[0], "gameplay");
    EXPECT_EQ(inst.event_listeners[0].component.channels[1], "ui");
    EXPECT_EQ(inst.event_listeners[0].component.channel_mask, 0u);

    // No input/controller records
    EXPECT_TRUE(inst.input_receivers.empty());
    EXPECT_TRUE(inst.flying_camera_controllers.empty());

    // Compile with identity view — should produce zero render output
    wz::scene::ViewData view{};
    view.view = wz::math::Mat4::identity();
    view.projection = wz::math::Mat4::identity();
    view.view_projection = wz::math::Mat4::identity();

    wz::scene::CompiledSceneStorage compiled{};
    wz::scene::compile(
        compiled,
        inst.storage.polytree,
        inst.renderables,
        inst.lights,
        view);

    EXPECT_EQ(compiled.scene.opaque.size(), 0u);
}

TEST(SceneAssetModule, EventListenerInstantiationCompilesKnownChannelMask)
{
    using namespace wz::engine::assets;
    using namespace wz::engine::behavior;

    SceneAssetData asset{};
    asset.name = "event_listener_channel_mask";
    asset.nodes.push_back(SceneNodeAsset{
        .id = "listener",
        .event_listener = SceneEventListenerAsset{
            .channels = { "frame.update", "collision.*", "input.*" },
        },
    });

    auto result = instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.event_listeners.size(), 1u);
    EXPECT_EQ(
        result.instance.event_listeners[0].component.channel_mask,
        EventChannelFrameUpdate
            | kCollisionEventChannels
            | kInputEventChannels);
}

TEST(SceneAssetModule, AmbientLightingRuntimeSummaryCountsAmbientLightRecords)
{
    using namespace wz::engine::assets;

    SceneAssetData asset{};
    asset.name = "ambient_lighting_runtime_summary";
    asset.lights.push_back(SceneLightAsset{
        .node_id = "sun",
        .light = wz::scene::LightRecord{
            .type = wz::scene::LightType::Directional,
        },
    });
    asset.lights.push_back(SceneLightAsset{
        .node_id = "ambient",
        .light = wz::scene::LightRecord{
            .type = wz::scene::LightType::Ambient,
        },
    });

    auto result = instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const auto summary = summarize_scene_instance_components(result.instance);
    EXPECT_EQ(summary.lights, 2u);
    EXPECT_EQ(summary.ambient_lighting, 1u);
}

TEST(SceneAssetModule, SkyDrawRuntimeSummaryCountsMaterializedSkyDraws)
{
    using namespace wz::engine::assets;

    SceneAssetData asset{};
    asset.name = "sky_draw_runtime_summary";
    asset.sky_draws.push_back(SceneSkyDrawAsset{
        .surface_node = "sky_surface",
        .visual_node = "sky_visual",
        .visual_kind = SceneSkyVisualKind::Gradient,
        .projection = SceneSkyProjection::Sphere,
        .radius = 1000.0f,
        .visible_to_camera = true,
    });

    auto result = instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const auto summary = summarize_scene_instance_components(result.instance);
    EXPECT_EQ(summary.sky_draws, 1u);
    ASSERT_EQ(result.instance.sky_draws.size(), 1u);
    EXPECT_EQ(
        result.instance.sky_draws[0].visual_kind,
        SceneSkyVisualKind::Gradient);
}

TEST(SceneAssetModule, ActorMovementComponentDescriptorsRoundTrip)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_actor_movement_desc_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "actor_movement_desc.json", kActorMovementDescriptorSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "actor_movement_desc",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);

    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.input_receiver.has_value());
    ASSERT_TRUE(node.actor_movement_controller.has_value());
    EXPECT_EQ(node.input_receiver->input_map, "asset://input_maps/actor");
    EXPECT_TRUE(node.input_receiver->log_input);
    EXPECT_FLOAT_EQ(node.actor_movement_controller->move_speed, 7.5f);
    EXPECT_FLOAT_EQ(node.actor_movement_controller->boost_multiplier, 2.0f);
    EXPECT_EQ(
        node.actor_movement_controller->movement_space,
        SceneActorMovementSpace::Local);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    auto& inst = result.instance;
    ASSERT_TRUE(inst.authored_to_runtime.contains("movable_actor"));
    const auto actor_h = inst.authored_to_runtime["movable_actor"];
    ASSERT_EQ(inst.input_receivers.size(), 1u);
    EXPECT_EQ(inst.input_receivers[0].node, actor_h);
    EXPECT_TRUE(inst.input_receivers[0].component.log_input);
    ASSERT_EQ(inst.actor_movement_controllers.size(), 1u);
    EXPECT_EQ(inst.actor_movement_controllers[0].node, actor_h);
    EXPECT_FLOAT_EQ(
        inst.actor_movement_controllers[0].component.move_speed,
        7.5f);
    EXPECT_FLOAT_EQ(
        inst.actor_movement_controllers[0].component.boost_multiplier,
        2.0f);
    EXPECT_EQ(
        inst.actor_movement_controllers[0].component.movement_space,
        SceneActorMovementSpace::Local);

    const auto summary = summarize_scene_instance_components(inst);
    EXPECT_EQ(summary.input_receivers, 1u);
    EXPECT_EQ(summary.actor_movement_controllers, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(
        exported.find("\"actor_movement_controller\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"movement_space\""), std::string::npos);
    EXPECT_NE(exported.find("\"local\""), std::string::npos);
}

TEST(SceneAssetModule, GroundBoundaryComponentDescriptorsRoundTrip)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_ground_boundary_desc_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "ground_boundary_desc.json",
        kGroundBoundaryDescriptorSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "ground_boundary_desc",
            .path = rel_path,
        });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 2u);

    const auto& surface = scene_data->nodes[0];
    ASSERT_TRUE(surface.ground_boundary.has_value());
    EXPECT_FLOAT_EQ(surface.ground_boundary->min[0], -10.0f);
    EXPECT_FLOAT_EQ(surface.ground_boundary->min[1], 0.0f);
    EXPECT_FLOAT_EQ(surface.ground_boundary->min[2], -8.0f);
    EXPECT_FLOAT_EQ(surface.ground_boundary->max[0], 12.0f);
    EXPECT_FLOAT_EQ(surface.ground_boundary->max[1], 0.0f);
    EXPECT_FLOAT_EQ(surface.ground_boundary->max[2], 9.0f);
    EXPECT_TRUE(surface.ground_boundary->constrain_vertical);
    EXPECT_TRUE(surface.ground_boundary->enabled);

    const auto& actor = scene_data->nodes[1];
    ASSERT_TRUE(actor.input_receiver.has_value());
    ASSERT_TRUE(actor.actor_movement_controller.has_value());
    EXPECT_FALSE(actor.ground_boundary.has_value());

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    auto& inst = result.instance;
    ASSERT_TRUE(inst.authored_to_runtime.contains("terrain_surface"));
    ASSERT_TRUE(inst.authored_to_runtime.contains("movable_actor"));
    const auto surface_h = inst.authored_to_runtime["terrain_surface"];
    const auto actor_h = inst.authored_to_runtime["movable_actor"];

    ASSERT_EQ(inst.ground_boundaries.size(), 1u);
    EXPECT_EQ(inst.ground_boundaries[0].node, surface_h);
    EXPECT_FLOAT_EQ(inst.ground_boundaries[0].component.min[0], -10.0f);
    EXPECT_FLOAT_EQ(inst.ground_boundaries[0].component.max[2], 9.0f);
    EXPECT_TRUE(inst.ground_boundaries[0].component.constrain_vertical);
    EXPECT_TRUE(inst.ground_boundaries[0].component.enabled);

    ASSERT_EQ(inst.actor_movement_controllers.size(), 1u);
    EXPECT_EQ(inst.actor_movement_controllers[0].node, actor_h);

    const auto summary = summarize_scene_instance_components(inst);
    EXPECT_EQ(summary.ground_boundaries, 1u);
    EXPECT_EQ(summary.actor_movement_controllers, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"ground_boundary\""), std::string::npos);
    EXPECT_NE(exported.find("\"constrain_vertical\""), std::string::npos);
}

TEST(SceneAssetModule, MeshComponentDescriptorsRoundTrip)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_desc_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "mesh_desc.json", kMeshDescriptorSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "mesh_desc",
            .path = rel_path,
        });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);

    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.mesh_source.has_value());
    EXPECT_EQ(node.mesh_source->kind, SceneMeshSourceKind::GLB);
    EXPECT_EQ(node.mesh_source->path, "gltf/low_poly_rock.glb");
    EXPECT_EQ(node.mesh_source->mesh_index, 1u);

    ASSERT_TRUE(node.mesh_render_style.has_value());
    EXPECT_TRUE(node.mesh_render_style->wireframe.enabled);
    EXPECT_FALSE(node.mesh_render_style->surface.enabled);
    EXPECT_FLOAT_EQ(node.mesh_render_style->wireframe.color[1], 1.0f);
    EXPECT_FLOAT_EQ(node.mesh_render_style->wireframe.color[2], 0.2f);
    EXPECT_FLOAT_EQ(node.mesh_render_style->wireframe.color[3], 0.75f);
    EXPECT_FLOAT_EQ(
        node.mesh_render_style->wireframe.emissive_strength,
        2.5f);
    EXPECT_TRUE(node.mesh_render_style->depth_test);
    EXPECT_TRUE(node.mesh_render_style->depth_write);
    EXPECT_FALSE(node.mesh_render_style->double_sided);
    EXPECT_FALSE(node.mesh_render_style->hidden_line_prepass);

    const auto summary = summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(summary.mesh_sources, 1u);
    EXPECT_EQ(summary.mesh_render_styles, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"mesh_source\""), std::string::npos);
    EXPECT_NE(exported.find("\"mesh_render_style\""), std::string::npos);
    EXPECT_NE(exported.find("\"gltf/low_poly_rock.glb\""), std::string::npos);
    EXPECT_NE(exported.find("\"color\""), std::string::npos);
    EXPECT_NE(exported.find("\"emissive_strength\""), std::string::npos);
    EXPECT_NE(exported.find("\"depth_test\""), std::string::npos);

    const wz::fs::Path reparse_root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_desc_reparse_test");

    ASSERT_EQ(
        wz::fs::create_directories(reparse_root),
        wz::fs::FileError::None);

    wz::Logger reparse_logger;
    wz::gpu::Device reparse_device{};
    wz::engine::assets::EngineAssetLibrary reparse_assets{
        reparse_device, reparse_logger, reparse_root };

    auto exported_rel_path = write_scene_json(
        reparse_root, "mesh_desc_exported.json", exported);
    const auto exported_scene_asset =
        reparse_assets.scenes().create_scene_from_json({
            .name = "mesh_desc_exported",
            .path = exported_rel_path,
        });
    ASSERT_TRUE(exported_scene_asset.valid());
    ASSERT_TRUE(reparse_assets.commit());
    ASSERT_TRUE(reparse_assets.resolve_all().ok());

    const auto* reparsed_scene_data = reparse_assets.scenes().get_scene_data(
        reparse_assets.scenes().get_scene(exported_scene_asset));
    ASSERT_NE(reparsed_scene_data, nullptr);
    ASSERT_EQ(reparsed_scene_data->nodes.size(), 1u);
    ASSERT_TRUE(reparsed_scene_data->nodes[0].mesh_source.has_value());
    ASSERT_TRUE(
        reparsed_scene_data->nodes[0].mesh_render_style.has_value());
}

TEST(SceneAssetModule, TerrainComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_terrain_component_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const auto field =
        assets.scalar_fields().create_procedural_scalar_field({
            .name = "terrain/scene_height",
            .width = 4,
            .height = 4,
            .depth = 1,
            .generator = ScalarFieldGenerator::GradientX,
            .frequency = 1.0f,
            .amplitude = 1.0f,
            .format = ScalarFieldFormat::Float32,
            .domain_kind = ScalarFieldDomainKind::Spatial2D,
        });
    ASSERT_TRUE(field.valid());

    TerrainFromHeightFieldDesc terrain_desc{};
    terrain_desc.name = "terrain/scene_terrain";
    terrain_desc.height_field = field;
    terrain_desc.size[0] = 16.0f;
    terrain_desc.size[1] = 16.0f;

    const auto terrain =
        assets.terrains().create_from_height_field(terrain_desc);
    ASSERT_TRUE(terrain.valid());

    const auto constraint_surface =
        assets.collisions().create_from_terrain({
            .name = "terrain/scene_constraint_surface",
            .terrain = terrain,
        });
    ASSERT_TRUE(constraint_surface.valid());

    const wz::asset::AssetKey visual_proxy_key{
        .content_hash = { 11, 12 },
        .schema_hash = { 13, 14 },
        .compiler_hash = { 15, 16 },
        .deps_hash = { 17, 18 },
    };

    SceneAssetData authored{};
    authored.name = "terrain_component_scene";
    SceneNodeAsset node{};
    node.id = "landscape";
    node.terrain = SceneTerrainAsset{
        .terrain_asset = terrain.output,
        .visual_proxy_asset = visual_proxy_key,
        .constraint_surface_asset = constraint_surface.output,
        .calculate_constraint_surface = true,
        .visible = true,
        .queryable = true,
        .constrain_movement = false,
    };
    node.terrain_render_style = SceneTerrainRenderStyleAsset{
        .path = SceneTerrainRenderPath::DebugWireframe,
        .depth_test = true,
        .depth_write = false,
        .lighting_source = SceneTerrainLightingSource::Hybrid,
        .directional_light_node = "sun",
        .ambient_light_node = "sky_ambient",
        .environment_node = "sky_hdri",
        .ambient_strength = 0.75f,
        .sky_visibility_strength = 0.5f,
        .normal_lighting_strength = 0.9f,
        .terrain_bounce_strength = 0.2f,
        .target_pixels_per_triangle = 3.5f,
        .enable_surfel_lods = true,
        .surfel_target_coverage_px = 4.0f,
        .max_asset_triangle_density = 9.25f,
        .max_screen_triangle_density = 0.75f,
        .visual_chunk_count = 2048u,
    };
    authored.nodes.push_back(std::move(node));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"terrain\""), std::string::npos);
    EXPECT_NE(exported.find("\"terrain_render_style\""), std::string::npos);
    EXPECT_NE(exported.find("\"debug_wireframe\""), std::string::npos);
    EXPECT_NE(exported.find("\"directional_light_node\""), std::string::npos);
    EXPECT_NE(exported.find("\"ambient_light_node\""), std::string::npos);
    EXPECT_NE(exported.find("\"lighting_source\""), std::string::npos);
    EXPECT_NE(exported.find("\"hybrid\""), std::string::npos);
    EXPECT_NE(exported.find("\"environment_node\""), std::string::npos);
    EXPECT_NE(exported.find("\"terrain_bounce_strength\""), std::string::npos);
    EXPECT_NE(
        exported.find("\"target_pixels_per_triangle\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"visual_chunk_count\""), std::string::npos);
    EXPECT_NE(exported.find("\"enable_surfel_lods\""), std::string::npos);
    EXPECT_NE(exported.find("\"surfel_target_coverage_px\""), std::string::npos);
    EXPECT_NE(
        exported.find("\"max_asset_triangle_density\""),
        std::string::npos);
    EXPECT_NE(
        exported.find("\"max_screen_triangle_density\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"asset\""), std::string::npos);
    EXPECT_NE(exported.find("\"visual_proxy\""), std::string::npos);
    EXPECT_NE(exported.find("\"constraint_surface\""), std::string::npos);
    EXPECT_NE(
        exported.find("\"calculate_constraint_surface\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"queryable\""), std::string::npos);
    EXPECT_NE(exported.find("\"constrain_movement\""), std::string::npos);
    EXPECT_NE(exported.find("asset-key:"), std::string::npos);

    auto rel_path = write_scene_json(
        root, "terrain_component.scene.json", exported);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "terrain_component",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].terrain.has_value());
    ASSERT_TRUE(scene_data->nodes[0].terrain_render_style.has_value());
    EXPECT_EQ(scene_data->nodes[0].terrain->terrain_asset, terrain.output);
    EXPECT_EQ(
        scene_data->nodes[0].terrain->visual_proxy_asset,
        visual_proxy_key);
    EXPECT_EQ(
        scene_data->nodes[0].terrain->constraint_surface_asset,
        constraint_surface.output);
    EXPECT_TRUE(
        scene_data->nodes[0].terrain->calculate_constraint_surface);
    EXPECT_TRUE(scene_data->nodes[0].terrain->visible);
    EXPECT_TRUE(scene_data->nodes[0].terrain->queryable);
    EXPECT_FALSE(scene_data->nodes[0].terrain->constrain_movement);
    EXPECT_EQ(
        scene_data->nodes[0].terrain_render_style->path,
        SceneTerrainRenderPath::DebugWireframe);
    EXPECT_TRUE(scene_data->nodes[0].terrain_render_style->depth_test);
    EXPECT_FALSE(scene_data->nodes[0].terrain_render_style->depth_write);
    EXPECT_EQ(
        scene_data->nodes[0].terrain_render_style->lighting_source,
        SceneTerrainLightingSource::Hybrid);
    EXPECT_EQ(
        scene_data->nodes[0].terrain_render_style->directional_light_node,
        "sun");
    EXPECT_EQ(
        scene_data->nodes[0].terrain_render_style->ambient_light_node,
        "sky_ambient");
    EXPECT_EQ(
        scene_data->nodes[0].terrain_render_style->environment_node,
        "sky_hdri");
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].terrain_render_style->ambient_strength,
        0.75f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].terrain_render_style->sky_visibility_strength,
        0.5f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].terrain_render_style->normal_lighting_strength,
        0.9f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].terrain_render_style->terrain_bounce_strength,
        0.2f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].terrain_render_style->target_pixels_per_triangle,
        3.5f);
    EXPECT_EQ(
        scene_data->nodes[0].terrain_render_style->visual_chunk_count,
        2048u);
    EXPECT_TRUE(
        scene_data->nodes[0].terrain_render_style->enable_surfel_lods);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].terrain_render_style->surfel_target_coverage_px,
        4.0f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].terrain_render_style->max_asset_triangle_density,
        9.25f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].terrain_render_style->max_screen_triangle_density,
        0.75f);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.terrains.size(), 1u);
    EXPECT_EQ(
        result.instance.terrains[0].component.terrain_asset,
        terrain.output);
    EXPECT_EQ(
        result.instance.terrains[0].component.visual_proxy_asset,
        visual_proxy_key);
    EXPECT_EQ(
        result.instance.terrains[0].component.constraint_surface_asset,
        constraint_surface.output);
    EXPECT_FALSE(result.instance.terrains[0].component.constrain_movement);
}

TEST(SceneAssetModule, CollisionComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_collision_component_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto collision = assets.collisions().create_from_mesh({
        .name = "collision/cube_sensor",
        .mesh = mesh,
        .build_method = CollisionBuildMethod::Bounds,
        .occupancy = CollisionOccupancyData{
            .kind = CollisionOccupancyKind::Sensor,
            .blocks_movement = false,
            .queryable = true,
        },
    });
    ASSERT_TRUE(collision.valid());

    SceneAssetData authored{};
    authored.name = "collision_component_scene";
    SceneNodeAsset node{};
    node.id = "sensor";
    node.collision = SceneCollisionAsset{
        .collision_asset = collision.output,
        .layer_mask = 0x2u,
        .collides_with_mask = 0x5u,
        .is_trigger = true,
        .enabled = false,
    };
    authored.nodes.push_back(std::move(node));

    const auto authored_components =
        authored_components_for_node(authored.nodes[0]);
    EXPECT_EQ(std::count(
        authored_components.begin(),
        authored_components.end(),
        wz::scene::SceneAuthoredComponentKind::Collision), 1);

    const auto authored_summary =
        summarize_authored_scene_components(authored);
    EXPECT_EQ(authored_summary.collisions, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"collision\""), std::string::npos);
    EXPECT_NE(exported.find("\"layer_mask\""), std::string::npos);
    EXPECT_NE(exported.find("\"collides_with_mask\""), std::string::npos);
    EXPECT_NE(exported.find("\"is_trigger\""), std::string::npos);
    EXPECT_NE(exported.find("\"enabled\""), std::string::npos);
    EXPECT_NE(exported.find("asset-key:"), std::string::npos);

    auto rel_path = write_scene_json(
        root, "collision_component.scene.json", exported);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "collision_component",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].collision.has_value());
    EXPECT_EQ(
        scene_data->nodes[0].collision->collision_asset,
        collision.output);
    EXPECT_EQ(scene_data->nodes[0].collision->layer_mask, 0x2u);
    EXPECT_EQ(scene_data->nodes[0].collision->collides_with_mask, 0x5u);
    EXPECT_TRUE(scene_data->nodes[0].collision->is_trigger);
    EXPECT_FALSE(scene_data->nodes[0].collision->enabled);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.collisions.size(), 1u);
    ASSERT_TRUE(result.instance.authored_to_runtime.contains("sensor"));
    EXPECT_EQ(
        result.instance.collisions[0].node,
        result.instance.authored_to_runtime["sensor"]);
    EXPECT_EQ(
        result.instance.collisions[0].component.collision_asset,
        collision.output);
    EXPECT_EQ(result.instance.collisions[0].component.layer_mask, 0x2u);
    EXPECT_EQ(result.instance.collisions[0].component.collides_with_mask, 0x5u);
    EXPECT_TRUE(result.instance.collisions[0].component.is_trigger);
    EXPECT_FALSE(result.instance.collisions[0].component.enabled);

    const auto runtime_summary =
        summarize_scene_instance_components(result.instance);
    EXPECT_EQ(runtime_summary.collisions, 1u);
}

TEST(SceneAssetModule, ProximityComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_scene_proximity_roundtrip");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string json = R"({
  "schema": "wozzits.scene.v0",
  "name": "proximity_scene",
  "nodes": [
    {
      "id": "sensor",
      "proximity": {
        "radius": 12.5,
        "layer_mask": 2,
        "detects_with_mask": 4,
        "enabled": true
      }
    }
  ]
})";

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const wz::fs::Path path = wz::fs::join(root, "proximity.scene.json");
    ASSERT_EQ(wz::fs::write_file_text(path, json), wz::fs::FileError::None);

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "proximity_scene",
        .path = "proximity.scene.json",
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto handle = assets.scenes().get_scene(scene_asset);
    ASSERT_TRUE(handle.valid());
    const auto* scene_data = assets.scenes().get_scene_data(handle);
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].proximity.has_value());
    EXPECT_FLOAT_EQ(scene_data->nodes[0].proximity->radius, 12.5f);
    EXPECT_EQ(scene_data->nodes[0].proximity->layer_mask, 2u);
    EXPECT_EQ(scene_data->nodes[0].proximity->detects_with_mask, 4u);
    EXPECT_TRUE(scene_data->nodes[0].proximity->enabled);

    const auto result = wz::engine::assets::instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.proximities.size(), 1u);
    EXPECT_FLOAT_EQ(result.instance.proximities[0].component.radius, 12.5f);

    const std::string exported = wz::json::serialize_json(
        wz::engine::assets::export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"proximity\""), std::string::npos);
    EXPECT_NE(exported.find("\"detects_with_mask\""), std::string::npos);
}

TEST(SceneAssetModule, MotionComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_scene_motion_roundtrip");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string json = R"({
  "schema": "wozzits.scene.v0",
  "name": "motion_scene",
  "nodes": [
    {
      "id": "actor",
      "motion": {
        "linear_velocity": [1.5, -2.0, 3.25],
        "angular_velocity": [0.25, 0.5, -0.75],
        "space": "local",
        "terrain_constrained": true,
        "terrain_ride_height": 0.35,
        "terrain_footprint_radius": 1.25,
        "terrain_align_to_surface": true,
        "terrain_alignment_strength": 0.5,
        "enabled": true
      }
    }
  ]
})";

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const wz::fs::Path path = wz::fs::join(root, "motion.scene.json");
    ASSERT_EQ(wz::fs::write_file_text(path, json), wz::fs::FileError::None);

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "motion_scene",
        .path = "motion.scene.json",
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto handle = assets.scenes().get_scene(scene_asset);
    ASSERT_TRUE(handle.valid());
    const auto* scene_data = assets.scenes().get_scene_data(handle);
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].motion.has_value());
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->linear_velocity[0], 1.5f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->linear_velocity[1], -2.0f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->linear_velocity[2], 3.25f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->angular_velocity[0], 0.25f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->angular_velocity[1], 0.5f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->angular_velocity[2], -0.75f);
    EXPECT_EQ(
        scene_data->nodes[0].motion->space,
        wz::engine::assets::SceneMotionSpace::Local);
    EXPECT_TRUE(scene_data->nodes[0].motion->terrain_constrained);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->terrain_ride_height, 0.35f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].motion->terrain_footprint_radius,
        1.25f);
    EXPECT_TRUE(scene_data->nodes[0].motion->terrain_align_to_surface);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].motion->terrain_alignment_strength,
        0.5f);
    EXPECT_TRUE(scene_data->nodes[0].motion->enabled);

    const auto result = wz::engine::assets::instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.motions.size(), 1u);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.linear_velocity[0],
        1.5f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.linear_velocity[1],
        -2.0f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.linear_velocity[2],
        3.25f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.angular_velocity[0],
        0.25f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.angular_velocity[1],
        0.5f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.angular_velocity[2],
        -0.75f);
    EXPECT_EQ(
        result.instance.motions[0].component.space,
        wz::engine::assets::SceneMotionSpace::Local);
    EXPECT_TRUE(result.instance.motions[0].component.terrain_constrained);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.terrain_ride_height,
        0.35f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.terrain_footprint_radius,
        1.25f);
    EXPECT_TRUE(
        result.instance.motions[0].component.terrain_align_to_surface);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.terrain_alignment_strength,
        0.5f);
    EXPECT_TRUE(result.instance.motions[0].component.enabled);

    const std::string exported = wz::json::serialize_json(
        wz::engine::assets::export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"motion\""), std::string::npos);
    EXPECT_NE(exported.find("\"linear_velocity\""), std::string::npos);
    EXPECT_NE(exported.find("\"angular_velocity\""), std::string::npos);
    EXPECT_NE(exported.find("\"space\""), std::string::npos);
    EXPECT_NE(exported.find("\"terrain_constrained\""), std::string::npos);
    EXPECT_NE(exported.find("\"terrain_ride_height\""), std::string::npos);
    EXPECT_NE(
        exported.find("\"terrain_footprint_radius\""),
        std::string::npos);
    EXPECT_NE(
        exported.find("\"terrain_align_to_surface\""),
        std::string::npos);
    EXPECT_NE(
        exported.find("\"terrain_alignment_strength\""),
        std::string::npos);
}

TEST(SceneAssetModule, MotionComponentDefaultsMissingLinearVelocity)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_scene_motion_default_velocity");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string json = R"({
  "schema": "wozzits.scene.v0",
  "name": "motion_default_scene",
  "nodes": [
    {
      "id": "actor",
      "motion": {
        "enabled": false
      }
    }
  ]
})";

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const wz::fs::Path path = wz::fs::join(root, "motion_default.scene.json");
    ASSERT_EQ(wz::fs::write_file_text(path, json), wz::fs::FileError::None);

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "motion_default_scene",
        .path = "motion_default.scene.json",
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto handle = assets.scenes().get_scene(scene_asset);
    ASSERT_TRUE(handle.valid());
    const auto* scene_data = assets.scenes().get_scene_data(handle);
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].motion.has_value());
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->linear_velocity[0], 0.0f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->linear_velocity[1], 0.0f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->linear_velocity[2], 0.0f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->angular_velocity[0], 0.0f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->angular_velocity[1], 0.0f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->angular_velocity[2], 0.0f);
    EXPECT_EQ(
        scene_data->nodes[0].motion->space,
        wz::engine::assets::SceneMotionSpace::World);
    EXPECT_FALSE(scene_data->nodes[0].motion->terrain_constrained);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->terrain_ride_height, 0.0f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->terrain_footprint_radius, 0.0f);
    EXPECT_FALSE(scene_data->nodes[0].motion->terrain_align_to_surface);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].motion->terrain_alignment_strength,
        1.0f);
    EXPECT_FALSE(scene_data->nodes[0].motion->enabled);
}

TEST(SceneAssetModule, MotionComponentRejectsInvalidSpace)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_scene_motion_invalid_space");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string json = R"({
  "schema": "wozzits.scene.v0",
  "name": "motion_invalid_space_scene",
  "nodes": [
    {
      "id": "actor",
      "motion": {
        "space": "screen"
      }
    }
  ]
})";

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const wz::fs::Path path =
        wz::fs::join(root, "motion_invalid_space.scene.json");
    ASSERT_EQ(wz::fs::write_file_text(path, json), wz::fs::FileError::None);

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "motion_invalid_space_scene",
        .path = "motion_invalid_space.scene.json",
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    EXPECT_FALSE(assets.resolve_all().ok());
}

TEST(SceneAssetModule, CollisionComponentResolvesSymbolicSceneReference)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_collision_symbolic_ref_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_cube_symbolic",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto collision = assets.collisions().create_from_mesh({
        .name = "collision/cube_symbolic",
        .mesh = mesh,
        .build_method = CollisionBuildMethod::Bounds,
    });
    ASSERT_TRUE(collision.valid());

    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "collision_symbolic_reference_scene",
  "nodes": [
    {
      "id": "body",
      "collision": {
        "asset": "asset://collisions/cube_symbolic",
        "layer_mask": 4,
        "collides_with_mask": 7,
        "is_trigger": false
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "collision_symbolic.scene.json", json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "collision_symbolic",
            .path = rel_path,
            .collision_asset_references = {
                SceneAssetReferenceBinding{
                    .uri = "asset://collisions/cube_symbolic",
                    .key = collision.output,
                },
            },
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].collision.has_value());
    EXPECT_EQ(
        scene_data->nodes[0].collision->collision_asset,
        collision.output);
    EXPECT_EQ(scene_data->nodes[0].collision->layer_mask, 4u);
    EXPECT_EQ(scene_data->nodes[0].collision->collides_with_mask, 7u);
    EXPECT_FALSE(scene_data->nodes[0].collision->is_trigger);
    EXPECT_TRUE(scene_data->nodes[0].collision->enabled);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.collisions.size(), 1u);
    EXPECT_EQ(
        result.instance.collisions[0].component.collision_asset,
        collision.output);
    EXPECT_EQ(result.instance.collisions[0].component.layer_mask, 4u);
    EXPECT_EQ(result.instance.collisions[0].component.collides_with_mask, 7u);
    EXPECT_FALSE(result.instance.collisions[0].component.is_trigger);
    EXPECT_TRUE(result.instance.collisions[0].component.enabled);
}

