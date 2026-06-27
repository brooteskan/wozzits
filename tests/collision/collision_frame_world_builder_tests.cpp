#include "collision_frame_test_support.h"

TEST(CollisionFrameWorldBuilder, ResolvesAssetsAndTransformsLocalBounds)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root = test_root("wz_collision_world_builder_bounds");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_cube_world_builder",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto collision = assets.collisions().create_from_mesh({
        .name = "collision/cube_world_builder",
        .mesh = mesh,
        .build_method = CollisionBuildMethod::Bounds,
    });
    ASSERT_TRUE(collision.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto collision_handle =
        assets.collisions().get_collision(collision);
    ASSERT_TRUE(collision_handle.valid());
    const auto* collision_data =
        assets.collisions().get_collision_data(collision_handle);
    ASSERT_NE(collision_data, nullptr);

    SceneAssetData scene{};
    scene.name = "collision_world_builder";

    SceneNodeAsset parent{};
    parent.id = "parent";
    parent.local.translation[0] = 10.0f;
    scene.nodes.push_back(std::move(parent));

    SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "parent";
    child.local.translation[1] = 2.0f;
    child.local.translation[2] = 3.0f;
    child.collision = SceneCollisionAsset{
        .collision_asset = collision.output,
        .layer_mask = 0x2u,
        .collides_with_mask = 0x4u,
        .is_trigger = true,
    };
    scene.nodes.push_back(std::move(child));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_TRUE(result.instance.authored_to_runtime.contains("child"));
    const auto child_h = result.instance.authored_to_runtime["child"];

    CollisionFrameStorage frame{};
    build_collision_world(result.instance, assets.collisions(), frame);

    ASSERT_EQ(frame.world.size(), 1u);
    EXPECT_EQ(frame.world[0].entity, child_h);
    EXPECT_EQ(frame.world[0].collision_asset, collision.output);
    EXPECT_EQ(frame.world[0].layer_mask, 0x2u);
    EXPECT_EQ(frame.world[0].collides_with_mask, 0x4u);
    EXPECT_TRUE(frame.world[0].is_trigger);
    EXPECT_TRUE(frame.world[0].enabled);
    EXPECT_EQ(frame.world[0].resolved, collision_data);
    EXPECT_FLOAT_EQ(frame.world[0].world_from_local.m[12], 10.0f);
    EXPECT_FLOAT_EQ(frame.world[0].world_from_local.m[13], 2.0f);
    EXPECT_FLOAT_EQ(frame.world[0].world_from_local.m[14], 3.0f);

    const wz::scene::AABB local_bounds{
        .min = {
            collision_data->bounds_min[0],
            collision_data->bounds_min[1],
            collision_data->bounds_min[2],
        },
        .max = {
            collision_data->bounds_max[0],
            collision_data->bounds_max[1],
            collision_data->bounds_max[2],
        },
    };
    const auto& runtime_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_h);
    expect_aabb_near(
        frame.world[0].world_bounds,
        wz::scene::transform_aabb(local_bounds, runtime_node.world));
}

TEST(CollisionFrameWorldBuilder, ResolvesTerrainConstraintSurfaceProxy)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        test_root("wz_collision_world_terrain_constraint_proxy");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto field =
        assets.scalar_fields().create_procedural_scalar_field({
            .name = "terrain/proxy_height",
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
    terrain_desc.name = "terrain/proxy_terrain";
    terrain_desc.height_field = field;
    terrain_desc.size[0] = 16.0f;
    terrain_desc.size[1] = 16.0f;
    const auto terrain =
        assets.terrains().create_from_height_field(terrain_desc);
    ASSERT_TRUE(terrain.valid());

    const auto constraint_surface =
        assets.collisions().create_from_terrain({
            .name = "collision/proxy_constraint_surface",
            .terrain = terrain,
        });
    ASSERT_TRUE(constraint_surface.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto collision_handle =
        assets.collisions().get_collision(constraint_surface);
    ASSERT_TRUE(collision_handle.valid());
    const auto* collision_data =
        assets.collisions().get_collision_data(collision_handle);
    ASSERT_NE(collision_data, nullptr);

    SceneAssetData scene{};
    scene.name = "terrain_constraint_proxy_scene";
    SceneNodeAsset terrain_node{};
    terrain_node.id = "terrain";
    terrain_node.local.translation[0] = 3.0f;
    terrain_node.local.translation[2] = 4.0f;
    terrain_node.terrain = SceneTerrainAsset{
        .terrain_asset = terrain.output,
        .constraint_surface_asset = constraint_surface.output,
        .constrain_movement = true,
    };
    scene.nodes.push_back(std::move(terrain_node));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_TRUE(result.instance.authored_to_runtime.contains("terrain"));
    const auto terrain_h = result.instance.authored_to_runtime["terrain"];

    CollisionFrameStorage frame{};
    build_collision_world(result.instance, assets.collisions(), frame);

    EXPECT_TRUE(frame.world.empty())
        << "constraint proxies must not become collision event participants";
    ASSERT_EQ(frame.terrain_constraint_surfaces.size(), 1u);
    EXPECT_EQ(frame.terrain_constraint_surfaces[0].entity, terrain_h);
    EXPECT_EQ(
        frame.terrain_constraint_surfaces[0].collision_asset,
        constraint_surface.output);
    EXPECT_EQ(frame.terrain_constraint_surfaces[0].resolved, collision_data);
    EXPECT_TRUE(frame.terrain_constraint_surfaces[0].enabled);
    EXPECT_FLOAT_EQ(
        frame.terrain_constraint_surfaces[0].world_from_local.m[12],
        3.0f);
    EXPECT_FLOAT_EQ(
        frame.terrain_constraint_surfaces[0].world_from_local.m[14],
        4.0f);
}

TEST(CollisionFrameWorldBuilder, ResolvesConstrainMovementCollisionSurface)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        test_root("wz_collision_world_constrain_movement_height_field");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto field =
        assets.scalar_fields().create_procedural_scalar_field({
            .name = "collision/constraint_height",
            .width = 4,
            .height = 4,
            .depth = 1,
            .generator = ScalarFieldGenerator::GradientX,
        });
    ASSERT_TRUE(field.valid());

    // Constraint surface built DIRECTLY from the scalar field, no TerrainAsset.
    const auto constraint_surface =
        assets.collisions().create_from_height_field({
            .name = "collision/constraint_height_surface",
            .height_field = field,
            .origin = { 0.0f, 0.0f },
            .size = { 16.0f, 16.0f },
            .vertical_scale = 2.0f,
            .base_height = 0.0f,
        });
    ASSERT_TRUE(constraint_surface.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto collision_handle =
        assets.collisions().get_collision(constraint_surface);
    ASSERT_TRUE(collision_handle.valid());
    const auto* collision_data =
        assets.collisions().get_collision_data(collision_handle);
    ASSERT_NE(collision_data, nullptr);

    SceneAssetData scene{};
    scene.name = "constrain_movement_collision_scene";
    SceneNodeAsset node{};
    node.id = "ground";
    node.local.translation[0] = 5.0f;
    node.local.translation[2] = 7.0f;
    node.collision = SceneCollisionAsset{
        .collision_asset = constraint_surface.output,
        .constrain_movement = true,
    };
    scene.nodes.push_back(std::move(node));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_TRUE(result.instance.authored_to_runtime.contains("ground"));
    const auto node_h = result.instance.authored_to_runtime["ground"];
    ASSERT_EQ(result.instance.collisions.size(), 1u);
    EXPECT_TRUE(result.instance.collisions[0].component.constrain_movement);

    CollisionFrameStorage frame{};
    build_collision_world(result.instance, assets.collisions(), frame);

    ASSERT_EQ(frame.terrain_constraint_surfaces.size(), 1u);
    EXPECT_EQ(frame.terrain_constraint_surfaces[0].entity, node_h);
    EXPECT_EQ(
        frame.terrain_constraint_surfaces[0].collision_asset,
        constraint_surface.output);
    EXPECT_EQ(frame.terrain_constraint_surfaces[0].resolved, collision_data);
    EXPECT_TRUE(frame.terrain_constraint_surfaces[0].enabled);
    EXPECT_FLOAT_EQ(
        frame.terrain_constraint_surfaces[0].world_from_local.m[12],
        5.0f);
    EXPECT_FLOAT_EQ(
        frame.terrain_constraint_surfaces[0].world_from_local.m[14],
        7.0f);
}

TEST(CollisionFrameWorldBuilder, DisabledEntryEmitsExitWhenItDisappears)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root = test_root("wz_collision_world_disabled_exit");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_cube_disabled_exit",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto collision = assets.collisions().create_from_mesh({
        .name = "collision/cube_disabled_exit",
        .mesh = mesh,
        .build_method = CollisionBuildMethod::Bounds,
    });
    ASSERT_TRUE(collision.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "collision_disabled_exit";

    SceneNodeAsset a{};
    a.id = "a";
    a.collision = SceneCollisionAsset{
        .collision_asset = collision.output,
    };
    scene.nodes.push_back(std::move(a));

    SceneNodeAsset b{};
    b.id = "b";
    b.local.translation[0] = 0.25f;
    b.collision = SceneCollisionAsset{
        .collision_asset = collision.output,
    };
    scene.nodes.push_back(std::move(b));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.collisions.size(), 2u);

    CollisionFrameStorage frame{};
    build_collision_frame(result.instance, assets.collisions(), frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);

    result.instance.collisions[1].component.enabled = false;
    build_collision_frame(result.instance, assets.collisions(), frame);

    ASSERT_EQ(frame.world.size(), 1u);
    EXPECT_TRUE(frame.current_pairs.empty());
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Exit);
    EXPECT_TRUE(frame.prev_pairs.empty());
}

TEST(CollisionFrameWorldBuilder, UnresolvedEntryEmitsExitWhenItDisappears)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root = test_root("wz_collision_world_unresolved_exit");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_cube_unresolved_exit",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto collision = assets.collisions().create_from_mesh({
        .name = "collision/cube_unresolved_exit",
        .mesh = mesh,
        .build_method = CollisionBuildMethod::Bounds,
    });
    ASSERT_TRUE(collision.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    wz::asset::AssetKey missing_collision_key{};
    missing_collision_key.content_hash = { 0xBADu, 0xC011u };

    SceneAssetData scene{};
    scene.name = "collision_unresolved_exit";

    SceneNodeAsset a{};
    a.id = "a";
    a.collision = SceneCollisionAsset{
        .collision_asset = collision.output,
    };
    scene.nodes.push_back(std::move(a));

    SceneNodeAsset b{};
    b.id = "b";
    b.collision = SceneCollisionAsset{
        .collision_asset = missing_collision_key,
    };
    scene.nodes.push_back(std::move(b));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_TRUE(result.instance.authored_to_runtime.contains("a"));
    ASSERT_TRUE(result.instance.authored_to_runtime.contains("b"));

    CollisionFrameStorage frame{};
    frame.prev_pairs = {
        make_collision_pair(
            result.instance.authored_to_runtime["a"],
            result.instance.authored_to_runtime["b"]),
    };

    build_collision_frame(result.instance, assets.collisions(), frame);

    ASSERT_EQ(frame.world.size(), 1u);
    EXPECT_EQ(frame.world[0].entity, result.instance.authored_to_runtime["a"]);
    EXPECT_TRUE(frame.current_pairs.empty());
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].a, result.instance.authored_to_runtime["a"]);
    EXPECT_EQ(frame.events[0].b, result.instance.authored_to_runtime["b"]);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Exit);
}

