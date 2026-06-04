#include "behavior_test_support.h"

TEST(BehaviorModuleApi, NullEventHelpersReturnSentinels)
{
    EXPECT_EQ(wz_self(nullptr), WZ_INVALID_BEHAVIOR_ENTITY);
    EXPECT_EQ(wz_other(nullptr), WZ_INVALID_BEHAVIOR_ENTITY);
    EXPECT_EQ(wz_event_kind(nullptr), WZ_EVENT_NONE);
    EXPECT_EQ(wz_is_event(nullptr, WZ_EVENT_FRAME_UPDATE), 0u);
    EXPECT_EQ(wz_self_is_trigger(nullptr), 0u);
}

TEST(BehaviorModuleApi, LogInfofFormatsThroughLogCallback)
{
    std::string message;
    WzBehaviorFrameFacts facts{
        .log_user = &message,
        .log_info = [](void* user, const char* text)
        {
            auto* out = static_cast<std::string*>(user);
            ASSERT_NE(out, nullptr);
            ASSERT_NE(text, nullptr);
            *out = text;
        },
    };

    wz_log_infof(
        &facts,
        "controller=%u axis=%u value=%.2f",
        1u,
        WZ_CONTROLLER_AXIS_LEFT_X,
        0.25);

    EXPECT_EQ(message, "controller=1 axis=0 value=0.25");

    message = "unchanged";
    wz_log_infof(nullptr, "ignored");
    wz_log_infof(&facts, nullptr);
    EXPECT_EQ(message, "unchanged");
}

namespace
{
    wz::engine::assets::CollisionTriangleBounds triangle_bounds(
        const wz::engine::assets::CollisionPoint& a,
        const wz::engine::assets::CollisionPoint& b,
        const wz::engine::assets::CollisionPoint& c)
    {
        wz::engine::assets::CollisionTriangleBounds bounds{};
        for (int axis = 0; axis < 3; ++axis) {
            bounds.min[axis] =
                (std::min)({
                    a.position[axis],
                    b.position[axis],
                    c.position[axis],
                });
            bounds.max[axis] =
                (std::max)({
                    a.position[axis],
                    b.position[axis],
                    c.position[axis],
                });
        }
        return bounds;
    }

    wz::engine::assets::CollisionAssetData gridded_flat_surface(
        uint32_t cells,
        float size)
    {
        wz::engine::assets::CollisionAssetData surface{};
        surface.shape_kind =
            wz::engine::assets::CollisionShapeKind::TerrainMeshSurface;
        surface.occupancy.queryable = true;
        surface.bounds_min[0] = 0.0f;
        surface.bounds_min[1] = 0.0f;
        surface.bounds_min[2] = 0.0f;
        surface.bounds_max[0] = size;
        surface.bounds_max[1] = 0.0f;
        surface.bounds_max[2] = size;

        const float step = size / static_cast<float>(cells);
        for (uint32_t z = 0; z <= cells; ++z) {
            for (uint32_t x = 0; x <= cells; ++x) {
                surface.points.push_back(
                    wz::engine::assets::CollisionPoint{
                        .position = {
                            static_cast<float>(x) * step,
                            0.0f,
                            static_cast<float>(z) * step,
                        },
                    });
            }
        }

        const size_t cell_count = static_cast<size_t>(cells) * cells;
        std::vector<std::vector<uint32_t>> cell_triangles(cell_count);
        surface.surface_grid.origin_x = 0.0f;
        surface.surface_grid.origin_z = 0.0f;
        surface.surface_grid.cell_size_x = step;
        surface.surface_grid.cell_size_z = step;
        surface.surface_grid.cells_x = cells;
        surface.surface_grid.cells_z = cells;
        surface.surface_grid.cell_bounds.resize(cell_count);
        for (auto& bounds : surface.surface_grid.cell_bounds) {
            bounds.min[0] = std::numeric_limits<float>::max();
            bounds.min[1] = std::numeric_limits<float>::max();
            bounds.min[2] = std::numeric_limits<float>::max();
            bounds.max[0] = -std::numeric_limits<float>::max();
            bounds.max[1] = -std::numeric_limits<float>::max();
            bounds.max[2] = -std::numeric_limits<float>::max();
        }

        auto point_index = [cells](uint32_t x, uint32_t z)
        {
            return z * (cells + 1u) + x;
        };
        auto add_triangle_to_cell =
            [&](uint32_t cell, uint32_t ia, uint32_t ib, uint32_t ic)
        {
            const uint32_t tri =
                static_cast<uint32_t>(surface.indices.size() / 3u);
            surface.indices.push_back(ia);
            surface.indices.push_back(ib);
            surface.indices.push_back(ic);
            const auto bounds = triangle_bounds(
                surface.points[ia],
                surface.points[ib],
                surface.points[ic]);
            surface.triangle_bounds.push_back(bounds);
            cell_triangles[cell].push_back(tri);
            auto& cell_bounds = surface.surface_grid.cell_bounds[cell];
            for (int axis = 0; axis < 3; ++axis) {
                cell_bounds.min[axis] =
                    (std::min)(cell_bounds.min[axis], bounds.min[axis]);
                cell_bounds.max[axis] =
                    (std::max)(cell_bounds.max[axis], bounds.max[axis]);
            }
        };

        for (uint32_t z = 0; z < cells; ++z) {
            for (uint32_t x = 0; x < cells; ++x) {
                const uint32_t a = point_index(x, z);
                const uint32_t b = point_index(x + 1u, z);
                const uint32_t c = point_index(x, z + 1u);
                const uint32_t d = point_index(x + 1u, z + 1u);
                const uint32_t cell = z * cells + x;
                add_triangle_to_cell(cell, a, c, b);
                add_triangle_to_cell(cell, b, c, d);
            }
        }

        surface.surface_grid.cell_offsets.push_back(0u);
        for (const auto& tris : cell_triangles) {
            surface.surface_grid.cell_triangle_indices.insert(
                surface.surface_grid.cell_triangle_indices.end(),
                tris.begin(),
                tris.end());
            surface.surface_grid.cell_offsets.push_back(
                static_cast<uint32_t>(
                    surface.surface_grid.cell_triangle_indices.size()));
        }
        return surface;
    }
}

TEST(BehaviorModuleApi, SelfAddLocalTranslationWritesCommandForEventEntity)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleHelperProbe probe{};
    g_module_helper_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_helper_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "module_helper_test",
        "");
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.wrote_command, 1u);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    const auto& command = frame_storage.behavior_commands.commands[0];
    EXPECT_EQ(command.entity, 4u);
    EXPECT_EQ(command.kind, BehaviorCommandKind::AddLocalTranslation);
    EXPECT_FLOAT_EQ(command.values[0], 2.0f);
    EXPECT_FLOAT_EQ(command.values[1], 4.0f);
    EXPECT_FLOAT_EQ(command.values[2], 6.0f);

    g_module_helper_probe = nullptr;
}

TEST(BehaviorModuleApi, InputHelpersReadFrameSnapshotAndWriteVelocity)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    InputHelperProbe probe{};
    g_input_helper_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_input_helper_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "input_helper_test",
        "");
    wz::engine::FrameContext frame_context{};
    frame_context.input.keyboard.down[WZ_KEY_W] = true;
    frame_context.input.keyboard.down[WZ_KEY_D] = true;
    frame_context.input.keyboard.pressed[WZ_KEY_SPACE] = true;
    frame_context.input.keyboard.released[WZ_KEY_ESCAPE] = true;
    frame_context.input.mouse.x = 320;
    frame_context.input.mouse.y = 240;
    frame_context.input.mouse.dx = -3;
    frame_context.input.mouse.dy = 5;
    frame_context.input.mouse.down[WZ_MOUSE_BUTTON_LEFT] = true;
    frame_context.input.mouse.pressed[WZ_MOUSE_BUTTON_RIGHT] = true;
    frame_context.input.mouse.released[WZ_MOUSE_BUTTON_MIDDLE] = true;
    frame_context.input.window.focused = true;
    frame_context.input.window.width = 1280;
    frame_context.input.window.height = 720;
    frame_context.input.controllers.count = 4u;
    auto& controller =
        frame_context.input.controllers.controllers[1];
    controller.connected = true;
    controller.connected_pressed = true;
    controller.axes[WZ_CONTROLLER_AXIS_LEFT_X] = 0.25f;
    controller.buttons[WZ_CONTROLLER_BUTTON_DPAD_LEFT] = true;
    controller.buttons_pressed[WZ_CONTROLLER_BUTTON_DPAD_RIGHT] = true;
    controller.buttons_released[WZ_CONTROLLER_BUTTON_START] = true;

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 1u);
    EXPECT_EQ(probe.w_down, 1u);
    EXPECT_EQ(probe.space_pressed, 1u);
    EXPECT_EQ(probe.escape_released, 1u);
    EXPECT_EQ(probe.left_mouse_down, 1u);
    EXPECT_EQ(probe.right_mouse_pressed, 1u);
    EXPECT_EQ(probe.middle_mouse_released, 1u);
    EXPECT_EQ(probe.mouse_x, 320);
    EXPECT_EQ(probe.mouse_y, 240);
    EXPECT_EQ(probe.mouse_dx, -3);
    EXPECT_EQ(probe.mouse_dy, 5);
    EXPECT_EQ(probe.focused, 1u);
    EXPECT_EQ(probe.window_width, 1280);
    EXPECT_EQ(probe.window_height, 720);
    EXPECT_EQ(probe.controller_count, 4u);
    EXPECT_EQ(probe.controller_connected, 1u);
    EXPECT_EQ(probe.controller_connected_pressed, 1u);
    EXPECT_FLOAT_EQ(probe.left_axis_x, 0.25f);
    EXPECT_EQ(probe.controller_button, 1u);
    EXPECT_EQ(probe.controller_button_pressed, 1u);
    EXPECT_EQ(probe.controller_button_released, 1u);
    EXPECT_EQ(probe.invalid_key, 0u);
    EXPECT_EQ(probe.invalid_mouse, 0u);
    EXPECT_FLOAT_EQ(probe.invalid_axis, 0.0f);
    EXPECT_EQ(probe.invalid_controller, 0u);
    EXPECT_EQ(probe.wasd_result, 1u);
    EXPECT_NEAR(probe.wasd_axis.x, 0.70710677f, 1e-6f);
    EXPECT_FLOAT_EQ(probe.wasd_axis.y, 0.0f);
    EXPECT_NEAR(probe.wasd_axis.z, 0.70710677f, 1e-6f);
    EXPECT_EQ(probe.wrote_velocity, 1u);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    EXPECT_EQ(frame_storage.behavior_commands.commands[0].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[0].kind,
        BehaviorCommandKind::SetLinearVelocity);
    EXPECT_NEAR(
        frame_storage.behavior_commands.commands[0].values[0],
        0.70710677f,
        1e-6f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[0].values[1], 0.0f);
    EXPECT_NEAR(
        frame_storage.behavior_commands.commands[0].values[2],
        0.70710677f,
        1e-6f);

    g_input_helper_probe = nullptr;
}

TEST(BehaviorModuleApi, TransformQueriesReadSelfAndOtherSceneTransforms)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_transform_query_scene";

    wz::engine::assets::SceneNodeAsset root{};
    root.id = "root";
    root.local.translation[0] = 10.0f;
    root.local.translation[2] = 1.0f;
    asset.nodes.push_back(std::move(root));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.parent_id = "root";
    actor.local.translation[1] = 2.0f;
    actor.local.translation[2] = 3.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "transform_query_test",
        .name = "",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);
    const RuntimeEntityId root_id = scene.authored_to_runtime["root"];
    const RuntimeEntityId actor_id = scene.authored_to_runtime["actor"];
    subscribe_frame_update(scene, actor_id);

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    TransformQueryProbe probe{};
    g_transform_query_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_transform_query_pack));

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = actor_id,
            .other = root_id,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.frame_update_other_position_result, 0u);
    EXPECT_EQ(probe.self_local_position_result, 1u);
    EXPECT_EQ(probe.self_world_position_result, 1u);
    EXPECT_EQ(probe.other_world_position_result, 1u);
    EXPECT_EQ(probe.self_local_transform_result, 1u);
    EXPECT_EQ(probe.vector_self_to_other_result, 1u);
    EXPECT_EQ(probe.distance_self_to_other_result, 1u);
    EXPECT_EQ(probe.direction_self_to_other_result, 1u);
    EXPECT_EQ(probe.null_vector_result, 0u);
    EXPECT_EQ(probe.zero_direction_result, 0u);
    EXPECT_EQ(probe.invalid_entity_position_result, 0u);
    EXPECT_EQ(probe.null_out_transform_result, 0u);

    EXPECT_FLOAT_EQ(probe.self_local_position.x, 0.0f);
    EXPECT_FLOAT_EQ(probe.self_local_position.y, 2.0f);
    EXPECT_FLOAT_EQ(probe.self_local_position.z, 3.0f);
    EXPECT_FLOAT_EQ(probe.self_world_position.x, 10.0f);
    EXPECT_FLOAT_EQ(probe.self_world_position.y, 2.0f);
    EXPECT_FLOAT_EQ(probe.self_world_position.z, 4.0f);
    EXPECT_FLOAT_EQ(probe.other_world_position.x, 10.0f);
    EXPECT_FLOAT_EQ(probe.other_world_position.y, 0.0f);
    EXPECT_FLOAT_EQ(probe.other_world_position.z, 1.0f);
    EXPECT_FLOAT_EQ(probe.self_local_transform.m[12], 0.0f);
    EXPECT_FLOAT_EQ(probe.self_local_transform.m[13], 2.0f);
    EXPECT_FLOAT_EQ(probe.self_local_transform.m[14], 3.0f);
    EXPECT_FLOAT_EQ(probe.vector_self_to_other.x, 0.0f);
    EXPECT_FLOAT_EQ(probe.vector_self_to_other.y, -2.0f);
    EXPECT_FLOAT_EQ(probe.vector_self_to_other.z, -3.0f);
    EXPECT_FLOAT_EQ(probe.distance_self_to_other, std::sqrt(13.0f));
    EXPECT_FLOAT_EQ(probe.direction_self_to_other.x, 0.0f);
    EXPECT_FLOAT_EQ(
        probe.direction_self_to_other.y,
        -2.0f / std::sqrt(13.0f));
    EXPECT_FLOAT_EQ(
        probe.direction_self_to_other.z,
        -3.0f / std::sqrt(13.0f));
    EXPECT_FLOAT_EQ(probe.zero_direction.x, 0.0f);
    EXPECT_FLOAT_EQ(probe.zero_direction.y, 0.0f);
    EXPECT_FLOAT_EQ(probe.zero_direction.z, 0.0f);

    g_transform_query_probe = nullptr;
}

TEST(BehaviorModuleApi, CollisionSurfaceRayQuerySamplesTerrainMeshSurface)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_surface_query_scene";

    wz::engine::assets::SceneNodeAsset terrain{};
    terrain.id = "terrain";
    asset.nodes.push_back(std::move(terrain));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 2.0f;
    actor.local.translation[1] = 10.0f;
    actor.local.translation[2] = 2.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "surface_query_test",
        .name = "",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);
    const RuntimeEntityId terrain_id =
        scene.authored_to_runtime["terrain"];
    const RuntimeEntityId actor_id =
        scene.authored_to_runtime["actor"];
    subscribe_frame_update(scene, actor_id);

    wz::engine::assets::CollisionAssetData surface{};
    surface.shape_kind =
        wz::engine::assets::CollisionShapeKind::TerrainMeshSurface;
    surface.occupancy.queryable = true;
    surface.points = {
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 0.0f, 0.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 0.0f, 10.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 10.0f, 0.0f, 0.0f } },
    };
    surface.indices = { 0u, 1u, 2u };

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    SurfaceQueryProbe probe{};
    g_surface_query_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_surface_query_pack));

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.world.push_back(
        wz::engine::collision::CollisionWorldEntry{
            .entity = terrain_id,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &surface,
        });
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = actor_id,
            .other = terrain_id,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.hit_result, 1u);
    EXPECT_EQ(probe.short_range_result, 0u);
    EXPECT_EQ(probe.null_out_result, 0u);
    EXPECT_EQ(probe.zero_direction_result, 0u);
    EXPECT_EQ(probe.away_result, 0u);
    EXPECT_EQ(probe.wrong_entity_result, 0u);
    EXPECT_EQ(probe.sample.hit, 1u);
    EXPECT_EQ(probe.sample.surface_entity, terrain_id);
    EXPECT_NEAR(probe.sample.position.x, 2.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.position.y, 0.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.position.z, 2.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.x, 0.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.y, 1.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.z, 0.0f, 1e-5f);

    g_surface_query_probe = nullptr;
}

TEST(BehaviorModuleApi, CollisionSurfaceRayQueryIgnoresUnqueryableSurfaces)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_surface_query_unqueryable_scene";

    wz::engine::assets::SceneNodeAsset terrain{};
    terrain.id = "terrain";
    asset.nodes.push_back(std::move(terrain));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 2.0f;
    actor.local.translation[1] = 10.0f;
    actor.local.translation[2] = 2.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "surface_query_test",
        .name = "",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);
    const RuntimeEntityId terrain_id =
        scene.authored_to_runtime["terrain"];
    const RuntimeEntityId actor_id =
        scene.authored_to_runtime["actor"];
    subscribe_frame_update(scene, actor_id);

    wz::engine::assets::CollisionAssetData surface{};
    surface.shape_kind =
        wz::engine::assets::CollisionShapeKind::TerrainMeshSurface;
    surface.occupancy.queryable = false;
    surface.points = {
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 0.0f, 0.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 0.0f, 10.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 10.0f, 0.0f, 0.0f } },
    };
    surface.indices = { 0u, 1u, 2u };

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    SurfaceQueryProbe probe{};
    g_surface_query_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_surface_query_pack));

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.world.push_back(
        wz::engine::collision::CollisionWorldEntry{
            .entity = terrain_id,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &surface,
        });
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = actor_id,
            .other = terrain_id,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.hit_result, 0u);
    EXPECT_EQ(probe.short_range_result, 0u);
    EXPECT_EQ(probe.null_out_result, 0u);

    g_surface_query_probe = nullptr;
}

TEST(BehaviorModuleApi, CollisionSurfaceRayQueryIgnoresNonTerrainSurfaceShapes)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_surface_query_non_terrain_scene";

    wz::engine::assets::SceneNodeAsset surface_node{};
    surface_node.id = "surface";
    asset.nodes.push_back(std::move(surface_node));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 2.0f;
    actor.local.translation[1] = 10.0f;
    actor.local.translation[2] = 2.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "surface_query_test",
        .name = "",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);
    const RuntimeEntityId surface_id =
        scene.authored_to_runtime["surface"];
    const RuntimeEntityId actor_id =
        scene.authored_to_runtime["actor"];
    subscribe_frame_update(scene, actor_id);

    wz::engine::assets::CollisionAssetData surface{};
    surface.shape_kind = wz::engine::assets::CollisionShapeKind::Bounds;
    surface.occupancy.queryable = true;
    surface.points = {
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 0.0f, 0.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 0.0f, 10.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 10.0f, 0.0f, 0.0f } },
    };
    surface.indices = { 0u, 1u, 2u };

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    SurfaceQueryProbe probe{};
    g_surface_query_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_surface_query_pack));

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.world.push_back(
        wz::engine::collision::CollisionWorldEntry{
            .entity = surface_id,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &surface,
        });
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = actor_id,
            .other = surface_id,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.hit_result, 0u);

    g_surface_query_probe = nullptr;
}

TEST(BehaviorModuleApi, CollisionSurfaceRayQueryReturnsNearestMatchingSurfaceHit)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_surface_query_nearest_scene";

    wz::engine::assets::SceneNodeAsset terrain{};
    terrain.id = "terrain";
    asset.nodes.push_back(std::move(terrain));

    wz::engine::assets::SceneNodeAsset other_terrain{};
    other_terrain.id = "other_terrain";
    asset.nodes.push_back(std::move(other_terrain));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 2.0f;
    actor.local.translation[1] = 10.0f;
    actor.local.translation[2] = 2.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "surface_query_test",
        .name = "",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);
    const RuntimeEntityId terrain_id =
        scene.authored_to_runtime["terrain"];
    const RuntimeEntityId other_terrain_id =
        scene.authored_to_runtime["other_terrain"];
    const RuntimeEntityId actor_id =
        scene.authored_to_runtime["actor"];
    subscribe_frame_update(scene, actor_id);

    wz::engine::assets::CollisionAssetData surface{};
    surface.shape_kind =
        wz::engine::assets::CollisionShapeKind::TerrainMeshSurface;
    surface.occupancy.queryable = true;
    surface.points = {
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 0.0f, 0.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 0.0f, 10.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 10.0f, 0.0f, 0.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 6.0f, 0.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 6.0f, 10.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 10.0f, 6.0f, 0.0f } },
    };
    surface.indices = { 0u, 1u, 2u, 3u, 4u, 5u };

    wz::engine::assets::CollisionAssetData decoy_surface = surface;
    decoy_surface.points[3].position[1] = 9.0f;
    decoy_surface.points[4].position[1] = 9.0f;
    decoy_surface.points[5].position[1] = 9.0f;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    SurfaceQueryProbe probe{};
    g_surface_query_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_surface_query_pack));

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.world.push_back(
        wz::engine::collision::CollisionWorldEntry{
            .entity = other_terrain_id,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &decoy_surface,
        });
    frame_storage.collision.world.push_back(
        wz::engine::collision::CollisionWorldEntry{
            .entity = terrain_id,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &surface,
        });
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = actor_id,
            .other = terrain_id,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.hit_result, 1u);
    EXPECT_EQ(probe.sample.surface_entity, terrain_id);
    EXPECT_NEAR(probe.sample.position.y, 6.0f, 1e-5f)
        << "query should return the nearest triangle on the requested entity";

    g_surface_query_probe = nullptr;
}

TEST(BehaviorModuleApi, CollisionSurfaceRayQuerySamplesGriddedTranslatedSurface)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_gridded_surface_query_scene";

    wz::engine::assets::SceneNodeAsset terrain{};
    terrain.id = "terrain";
    asset.nodes.push_back(std::move(terrain));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 14.0f;
    actor.local.translation[1] = 15.0f;
    actor.local.translation[2] = 24.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "surface_query_test",
        .name = "",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);
    const RuntimeEntityId terrain_id =
        scene.authored_to_runtime["terrain"];
    const RuntimeEntityId actor_id =
        scene.authored_to_runtime["actor"];
    subscribe_frame_update(scene, actor_id);

    wz::engine::assets::CollisionAssetData surface =
        gridded_flat_surface(10u, 10.0f);
    wz::math::Mat4 world_from_local = wz::math::Mat4::identity();
    world_from_local.m[0] = 2.0f;
    world_from_local.m[5] = 3.0f;
    world_from_local.m[10] = 2.0f;
    world_from_local.m[12] = 10.0f;
    world_from_local.m[13] = 5.0f;
    world_from_local.m[14] = 20.0f;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    SurfaceQueryProbe probe{};
    g_surface_query_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_surface_query_pack));

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.world.push_back(
        wz::engine::collision::CollisionWorldEntry{
            .entity = terrain_id,
            .world_from_local = world_from_local,
            .enabled = true,
            .resolved = &surface,
        });
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = actor_id,
            .other = terrain_id,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorSurfaceRayQueryStats stats{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
        .surface_ray_stats = &stats,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.hit_result, 1u);
    EXPECT_EQ(probe.sample.surface_entity, terrain_id);
    EXPECT_NEAR(probe.sample.position.x, 14.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.position.y, 5.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.position.z, 24.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.x, 0.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.y, 1.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.z, 0.0f, 1e-5f);
    EXPECT_GT(stats.grid_queries, 0u);

    g_surface_query_probe = nullptr;
}

TEST(BehaviorModuleApi, CollisionSurfaceRayQueryUsesGridCandidateRejection)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_gridded_surface_query_candidate_scene";

    wz::engine::assets::SceneNodeAsset terrain{};
    terrain.id = "terrain";
    asset.nodes.push_back(std::move(terrain));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 17.5f;
    actor.local.translation[1] = 10.0f;
    actor.local.translation[2] = 17.5f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "surface_query_test",
        .name = "",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);
    const RuntimeEntityId terrain_id =
        scene.authored_to_runtime["terrain"];
    const RuntimeEntityId actor_id =
        scene.authored_to_runtime["actor"];
    subscribe_frame_update(scene, actor_id);

    wz::engine::assets::CollisionAssetData surface =
        gridded_flat_surface(32u, 32.0f);
    const uint32_t total_triangles =
        static_cast<uint32_t>(surface.indices.size() / 3u);

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    SurfaceQueryProbe probe{};
    g_surface_query_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_surface_query_pack));

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.world.push_back(
        wz::engine::collision::CollisionWorldEntry{
            .entity = terrain_id,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &surface,
        });
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = actor_id,
            .other = terrain_id,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorSurfaceRayQueryStats stats{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
        .surface_ray_stats = &stats,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.hit_result, 1u);
    EXPECT_EQ(probe.sample.surface_entity, terrain_id);
    EXPECT_GT(stats.grid_queries, 0u);
    EXPECT_LT(stats.triangles_tested, total_triangles / 8u);
    EXPECT_LT(stats.triangle_bounds_tested, total_triangles / 8u);

    g_surface_query_probe = nullptr;
}

TEST(BehaviorModuleApi, TerrainSurfaceSampleSamplesHeightField)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_terrain_height_sample_scene";

    wz::engine::assets::SceneNodeAsset terrain{};
    terrain.id = "terrain";
    asset.nodes.push_back(std::move(terrain));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 5.0f;
    actor.local.translation[1] = 10.0f;
    actor.local.translation[2] = 5.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "terrain_sample_test",
        .name = "",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);
    const RuntimeEntityId terrain_id =
        scene.authored_to_runtime["terrain"];
    const RuntimeEntityId actor_id =
        scene.authored_to_runtime["actor"];
    subscribe_frame_update(scene, actor_id);

    wz::engine::assets::CollisionAssetData surface{};
    surface.shape_kind =
        wz::engine::assets::CollisionShapeKind::TerrainHeightField;
    surface.occupancy.queryable = true;
    surface.origin[0] = 0.0f;
    surface.origin[1] = 0.0f;
    surface.size[0] = 10.0f;
    surface.size[1] = 10.0f;
    surface.resolution_x = 2u;
    surface.resolution_y = 2u;
    surface.base_height = -1.0f;
    surface.vertical_scale = 2.0f;
    surface.height_samples = {
        0.0f, 1.0f,
        2.0f, 3.0f,
    };

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    TerrainSampleProbe probe{};
    g_terrain_sample_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_terrain_sample_pack));

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.world.push_back(
        wz::engine::collision::CollisionWorldEntry{
            .entity = terrain_id,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &surface,
        });
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = actor_id,
            .other = terrain_id,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.sample_result, 1u);
    EXPECT_EQ(probe.null_out_result, 0u);
    EXPECT_EQ(probe.wrong_entity_result, 0u);
    EXPECT_EQ(probe.out_of_bounds_result, 0u);
    EXPECT_EQ(probe.out_of_bounds_sample.hit, 0u);
    EXPECT_EQ(probe.sample.hit, 1u);
    EXPECT_EQ(probe.sample.surface_entity, terrain_id);
    EXPECT_NEAR(probe.sample.position.x, 5.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.position.y, 2.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.position.z, 5.0f, 1e-5f);

    const float normal_scale =
        1.0f / std::sqrt(0.2f * 0.2f + 1.0f + 0.4f * 0.4f);
    EXPECT_NEAR(probe.sample.normal.x, -0.2f * normal_scale, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.y, normal_scale, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.z, -0.4f * normal_scale, 1e-5f);

    g_terrain_sample_probe = nullptr;
}

TEST(BehaviorModuleApi, TerrainSurfaceSampleSamplesMeshSurfaceCell)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_terrain_mesh_sample_scene";

    wz::engine::assets::SceneNodeAsset terrain{};
    terrain.id = "terrain";
    asset.nodes.push_back(std::move(terrain));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 4.0f;
    actor.local.translation[1] = 10.0f;
    actor.local.translation[2] = 4.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "terrain_sample_test",
        .name = "",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);
    const RuntimeEntityId terrain_id =
        scene.authored_to_runtime["terrain"];
    const RuntimeEntityId actor_id =
        scene.authored_to_runtime["actor"];
    subscribe_frame_update(scene, actor_id);

    wz::engine::assets::CollisionAssetData surface =
        gridded_flat_surface(4u, 8.0f);
    surface.bounds_min[1] = 0.0f;
    surface.bounds_max[1] = 0.0f;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    TerrainSampleProbe probe{};
    g_terrain_sample_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_terrain_sample_pack));

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.world.push_back(
        wz::engine::collision::CollisionWorldEntry{
            .entity = terrain_id,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &surface,
        });
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = actor_id,
            .other = terrain_id,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.sample_result, 1u);
    EXPECT_EQ(probe.sample.hit, 1u);
    EXPECT_EQ(probe.sample.surface_entity, terrain_id);
    EXPECT_NEAR(probe.sample.position.x, 4.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.position.y, 0.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.position.z, 4.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.x, 0.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.y, 1.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.z, 0.0f, 1e-5f);

    g_terrain_sample_probe = nullptr;
}

TEST(BehaviorModuleApi, TerrainSurfaceSampleIgnoresNonTerrainShapes)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_terrain_sample_non_terrain_scene";

    wz::engine::assets::SceneNodeAsset bounds_node{};
    bounds_node.id = "bounds";
    asset.nodes.push_back(std::move(bounds_node));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 1.0f;
    actor.local.translation[1] = 10.0f;
    actor.local.translation[2] = 1.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "terrain_sample_test",
        .name = "",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);
    const RuntimeEntityId bounds_id =
        scene.authored_to_runtime["bounds"];
    const RuntimeEntityId actor_id =
        scene.authored_to_runtime["actor"];
    subscribe_frame_update(scene, actor_id);

    wz::engine::assets::CollisionAssetData surface{};
    surface.shape_kind = wz::engine::assets::CollisionShapeKind::Bounds;
    surface.occupancy.queryable = true;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    TerrainSampleProbe probe{};
    g_terrain_sample_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_terrain_sample_pack));

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.world.push_back(
        wz::engine::collision::CollisionWorldEntry{
            .entity = bounds_id,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &surface,
        });
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = actor_id,
            .other = bounds_id,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.sample_result, 0u);
    EXPECT_EQ(probe.sample.hit, 0u);

    g_terrain_sample_probe = nullptr;
}

TEST(BehaviorModuleApi, SelfTransformCommandHelpersWriteCommandsForEventEntity)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    TransformCommandProbe probe{};
    g_transform_command_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_transform_command_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "transform_command_test",
        "");
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    wz::engine::FrameContext frame_context{};
    frame_context.frame.index = 42u;
    frame_context.frame.interval.start = 0u;
    frame_context.frame.interval.end =
        wz::time::TimeSource::ticks_per_second() / 2u;
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.wrote_set_scale, 1u);
    EXPECT_EQ(probe.wrote_add_scale, 1u);
    EXPECT_EQ(probe.wrote_set_rotation, 1u);
    EXPECT_EQ(probe.wrote_set_world_translation, 1u);
    EXPECT_EQ(probe.wrote_add_world_translation, 1u);
    EXPECT_EQ(probe.wrote_other_set_world_translation, 1u);
    EXPECT_EQ(probe.wrote_other_add_world_translation, 1u);
    EXPECT_EQ(probe.wrote_set_linear_velocity, 1u);
    EXPECT_NEAR(probe.delta_seconds, 0.5f, 1e-6f);
    EXPECT_EQ(probe.frame_index, 42u);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 8u);
    EXPECT_EQ(frame_storage.behavior_commands.commands[0].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[0].kind,
        BehaviorCommandKind::SetLocalScale);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[0].values[0], 2.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[0].values[1], 3.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[0].values[2], 4.0f);
    EXPECT_EQ(frame_storage.behavior_commands.commands[1].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[1].kind,
        BehaviorCommandKind::AddLocalScale);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[1].values[0], 1.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[1].values[1], 1.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[1].values[2], 1.0f);
    EXPECT_EQ(frame_storage.behavior_commands.commands[2].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[2].kind,
        BehaviorCommandKind::SetLocalRotation);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[2].values[0], 0.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[2].values[1], 0.0f);
    EXPECT_FLOAT_EQ(
        frame_storage.behavior_commands.commands[2].values[2],
        0.70710677f);
    EXPECT_FLOAT_EQ(
        frame_storage.behavior_commands.commands[2].values[3],
        0.70710677f);
    EXPECT_EQ(frame_storage.behavior_commands.commands[3].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[3].kind,
        BehaviorCommandKind::SetWorldTranslation);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[3].values[0], 10.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[3].values[1], 20.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[3].values[2], 30.0f);
    EXPECT_EQ(frame_storage.behavior_commands.commands[4].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[4].kind,
        BehaviorCommandKind::AddWorldTranslation);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[4].values[0], 1.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[4].values[1], 2.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[4].values[2], 3.0f);
    EXPECT_EQ(frame_storage.behavior_commands.commands[5].entity, 9u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[5].kind,
        BehaviorCommandKind::SetWorldTranslation);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[5].values[0], 40.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[5].values[1], 50.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[5].values[2], 60.0f);
    EXPECT_EQ(frame_storage.behavior_commands.commands[6].entity, 9u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[6].kind,
        BehaviorCommandKind::AddWorldTranslation);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[6].values[0], 4.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[6].values[1], 5.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[6].values[2], 6.0f);
    EXPECT_EQ(frame_storage.behavior_commands.commands[7].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[7].kind,
        BehaviorCommandKind::SetLinearVelocity);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[7].values[0], 7.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[7].values[1], 8.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[7].values[2], 9.0f);

    g_transform_command_probe = nullptr;
}

TEST(BehaviorDispatch, RunsEnabledSceneBehaviorAndWritesCommands)
{
    BehaviorRegistry registry;
    CallCounter counter{};
    registry.register_behavior("count", count_behavior, &counter);

    SceneInstance scene = scene_with_behavior(7u, "", "count");
    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(counter.calls, 1u);
    EXPECT_EQ(counter.last_entity, 7u);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    const auto& command = frame_storage.behavior_commands.commands[0];
    EXPECT_EQ(command.entity, 7u);
    EXPECT_EQ(command.kind, BehaviorCommandKind::AddLocalTranslation);
    EXPECT_FLOAT_EQ(command.values[0], 1.0f);
    EXPECT_FLOAT_EQ(command.values[1], 2.0f);
    EXPECT_FLOAT_EQ(command.values[2], 3.0f);
}

TEST(BehaviorDispatch, SkipsDisabledOrMissingBehaviors)
{
    BehaviorRegistry registry;
    CallCounter counter{};
    registry.register_behavior("count", count_behavior, &counter);

    SceneInstance scene{};
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 1u,
        .component = BehaviorComponent{
            .name = "count",
            .enabled = false,
        },
    });
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 2u,
        .component = BehaviorComponent{
            .name = "missing",
            .enabled = true,
        },
    });

    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    frame_storage.behavior_commands.add_local_translation(
        99u,
        1.0f,
        1.0f,
        1.0f);
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(counter.calls, 0u);
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty())
        << "dispatch clears stale behavior commands each frame";
}

TEST(BehaviorDispatch, BehaviorConsumesRoutedCollisionFacts)
{
    BehaviorRegistry registry;
    registry.register_behavior(
        "gameplay",
        "bounce_on_collision",
        bounce_on_collision_enter);

    SceneInstance scene =
        scene_with_behavior(4u, "gameplay", "bounce_on_collision");
    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
        wz::engine::collision::CollisionEntityEvent{
            .entity = 5u,
            .other = 4u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    const auto& command = frame_storage.behavior_commands.commands[0];
    EXPECT_EQ(command.entity, 4u);
    EXPECT_EQ(command.kind, BehaviorCommandKind::AddLocalTranslation);
    EXPECT_FLOAT_EQ(command.values[0], 0.0f);
    EXPECT_FLOAT_EQ(command.values[1], 8.0f);
    EXPECT_FLOAT_EQ(command.values[2], 0.0f);
}

TEST(BehaviorDispatch, AbiSampleBounceConsumesRoutedCollisionFacts)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    wz::Logger logger{};
    register_builtin_behaviors(registry, plugins, logger);

    SceneInstance scene = scene_with_behavior(
        4u,
        kSampleBehaviorModule,
        kBounceOnCollisionEnterBehavior);
    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Stay,
        },
        wz::engine::collision::CollisionEntityEvent{
            .entity = 5u,
            .other = 4u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    const auto& command = frame_storage.behavior_commands.commands[0];
    EXPECT_EQ(command.entity, 4u);
    EXPECT_EQ(command.kind, BehaviorCommandKind::AddLocalTranslation);
    EXPECT_FLOAT_EQ(command.values[0], 0.0f);
    EXPECT_FLOAT_EQ(command.values[1], 1.0f);
    EXPECT_FLOAT_EQ(command.values[2], 0.0f);
}

TEST(BehaviorPluginAbi, CollisionViewAndCommandWriterRejectBoundaries)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    AbiBoundaryProbe probe{};
    g_boundary_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_boundary_pack));
    g_boundary_probe = nullptr;

    SceneInstance scene =
        scene_with_behavior(7u, "test", "boundary_probe");
    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 1u);
    EXPECT_EQ(probe.observed_count, 0u);
    EXPECT_TRUE(probe.read_was_present);
    EXPECT_EQ(probe.out_of_range_read, 0u);
    EXPECT_EQ(probe.null_out_read, 0u);
    EXPECT_TRUE(probe.out_event_preserved);
    EXPECT_EQ(probe.none_write, 0u);
    EXPECT_EQ(probe.bad_write, 0u);
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty());
}

TEST(BehaviorPluginAbi, SceneQueryAndConfigCallbacksReadAuthoredSceneData)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    SceneQueryConfigProbe probe{};
    g_scene_query_config_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_scene_query_config_pack));
    g_scene_query_config_probe = nullptr;

    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_scene_query_config";

    wz::engine::assets::SceneNodeAsset terrain{};
    terrain.id = "terrain";
    terrain.name = "Landscape";
    asset.nodes.push_back(std::move(terrain));

    wz::engine::assets::SceneNodeAsset player{};
    player.id = "player";
    player.name = "PlayerCube";
    player.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "test",
        .name = "scene_query_config",
        .enabled = true,
        .config = {
            wz::engine::assets::SceneBehaviorConfigValue{
                .key = "enabled",
                .kind = wz::engine::assets::SceneBehaviorConfigValueKind::Bool,
                .bool_value = true,
            },
            wz::engine::assets::SceneBehaviorConfigValue{
                .key = "speed",
                .kind =
                    wz::engine::assets::SceneBehaviorConfigValueKind::Number,
                .number_value = 4.5,
            },
            wz::engine::assets::SceneBehaviorConfigValue{
                .key = "terrain_id",
                .kind =
                    wz::engine::assets::SceneBehaviorConfigValueKind::String,
                .string_value = "terrain",
            },
        },
    };
    asset.nodes.push_back(std::move(player));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);

    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 1u);
    EXPECT_EQ(probe.find_player_by_id, 1u);
    EXPECT_EQ(probe.find_terrain_by_name, 1u);
    EXPECT_EQ(probe.player_entity, scene.authored_to_runtime.at("player"));
    EXPECT_EQ(probe.terrain_entity, scene.authored_to_runtime.at("terrain"));
    EXPECT_EQ(probe.enabled_read, 1u);
    EXPECT_EQ(probe.enabled_value, 1u);
    EXPECT_EQ(probe.speed_read, 1u);
    EXPECT_DOUBLE_EQ(probe.speed_value, 4.5);
    EXPECT_EQ(probe.terrain_id_read, 1u);
    EXPECT_EQ(probe.terrain_id_required, 8u);
    EXPECT_EQ(std::string(probe.terrain_id), "terrain");
    EXPECT_EQ(probe.missing_read, 0u);
}

TEST(BehaviorPluginAbi, MultipleAbiBehaviorsReadSameFrameView)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    wz::Logger logger{};
    register_builtin_behaviors(registry, plugins, logger);

    SceneInstance scene{};
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 4u,
        .component = BehaviorComponent{
            .module = kSampleBehaviorModule,
            .name = kBounceOnCollisionEnterBehavior,
            .enabled = true,
        },
    });
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 5u,
        .component = BehaviorComponent{
            .module = kSampleBehaviorModule,
            .name = kBounceOnCollisionEnterBehavior,
            .enabled = true,
        },
    });

    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
        wz::engine::collision::CollisionEntityEvent{
            .entity = 5u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 2u);
    EXPECT_EQ(frame_storage.behavior_commands.commands[0].entity, 4u);
    EXPECT_EQ(frame_storage.behavior_commands.commands[1].entity, 5u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[0].kind,
        BehaviorCommandKind::AddLocalTranslation);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[1].kind,
        BehaviorCommandKind::AddLocalTranslation);
}

TEST(BehaviorPluginAbi, DebugLogBehaviorWritesThroughLogCallback)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    LogCapture capture{};
    wz::Logger logger{};
    ASSERT_TRUE(wz::logging::init_logger(
        logger,
        wz::logging::LoggerDesc{
            .min_level = wz::LogLevel::Debug,
            .enable_stderr_sink = false,
        }));
    wz::logging::set_log_sink(logger, capture_log, &capture);
    register_builtin_behaviors(registry, plugins, logger);

    SceneInstance scene =
        scene_with_behavior(4u, kDebugBehaviorModule, kLogCollisionEventsBehavior);
    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
            .self_is_trigger = true,
        },
    };
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);
    wz::logging::wait_until_idle(logger);
    wz::logging::shutdown_logger(logger);

    const auto found = std::find_if(
        capture.messages.begin(),
        capture.messages.end(),
        [](const std::string& message) {
            return message.find("collision.enter") != std::string::npos
                && message.find("entity=4") != std::string::npos
                && message.find("other=9") != std::string::npos;
        });
    EXPECT_NE(found, capture.messages.end());
}

