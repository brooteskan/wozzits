#include "collision_frame_test_support.h"

#include <engine/collision/collision_surface_sampling.h>

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

// #252 "live?" axis: parking a node (entity_active[node] = 0) drops its collider
// from the world exactly as component.enabled = false does, but via the orthogonal
// node-level active mask the host populates each frame. An empty mask = all live,
// so the FIRST build (no mask) still sees both.
TEST(CollisionFrameWorldBuilder, InactiveEntryDropsFromCollisionWorld)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root = test_root("wz_collision_world_inactive_drop");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_cube_inactive_drop",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto collision = assets.collisions().create_from_mesh({
        .name = "collision/cube_inactive_drop",
        .mesh = mesh,
        .build_method = CollisionBuildMethod::Bounds,
    });
    ASSERT_TRUE(collision.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "collision_inactive_drop";

    SceneNodeAsset a{};
    a.id = "a";
    a.collision = SceneCollisionAsset{ .collision_asset = collision.output };
    scene.nodes.push_back(std::move(a));

    SceneNodeAsset b{};
    b.id = "b";
    b.local.translation[0] = 0.25f;
    b.collision = SceneCollisionAsset{ .collision_asset = collision.output };
    scene.nodes.push_back(std::move(b));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.collisions.size(), 2u);

    // Empty mask => both colliders live => overlapping pair => Enter.
    CollisionFrameStorage frame{};
    build_collision_frame(result.instance, assets.collisions(), frame);
    ASSERT_EQ(frame.world.size(), 2u);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);

    // Park b via the active mask (all live except b's runtime entity).
    const auto b_runtime = result.instance.authored_to_runtime.at("b");
    result.instance.entity_active.assign(
        result.instance.runtime_to_authored.size(), std::uint8_t{ 1 });
    result.instance.entity_active[b_runtime] = 0u;

    build_collision_frame(result.instance, assets.collisions(), frame);
    ASSERT_EQ(frame.world.size(), 1u)
        << "a parked node's collider must drop from the collision world";
    EXPECT_TRUE(frame.current_pairs.empty());
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Exit);
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

// Placement-driven collision (issue #218/#224): the compiled data is already
// world-frame, so a non-unit carrying-node scale must NOT be composed on top.
// The entry's world_from_local is identity, and the collision footprint equals
// the placement extent regardless of node scale.
TEST(CollisionFrameWorldBuilder, PlacementDrivenSurfaceIgnoresNodeTransform)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        test_root("wz_collision_world_placement_ignores_node_transform");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto field =
        assets.scalar_fields().create_procedural_scalar_field({
            .name = "collision/placement_frame_height",
            .width = 4,
            .height = 4,
            .depth = 1,
            .generator = ScalarFieldGenerator::GradientX,
        });
    ASSERT_TRUE(field.valid());

    // Placement footprint 4096 x 4096 at the origin, vertical 300, base 0.
    const auto placement = assets.placements().create_placement({
        .name = "collision/placement_frame",
        .origin = { 0.0f, 0.0f, 0.0f },
        .extent = { 4096.0f, 300.0f, 4096.0f },
        .base_height = 0.0f,
    });
    ASSERT_TRUE(placement.valid());

    const auto constraint_surface =
        assets.collisions().create_from_height_field({
            .name = "collision/placement_constraint_surface",
            .height_field = field,
            .placement = placement,
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
    ASSERT_TRUE(collision_data->placement_driven);

    // Carrying node with a large non-unit scale, which pre-#224 would have
    // double-applied on top of the world-frame placement mapping.
    SceneAssetData scene{};
    scene.name = "placement_driven_scale_scene";
    SceneNodeAsset node{};
    node.id = "ground";
    node.local.scale[0] = 512.0f;
    node.local.scale[1] = 512.0f;
    node.local.scale[2] = 512.0f;
    node.collision = SceneCollisionAsset{
        .collision_asset = constraint_surface.output,
        .constrain_movement = true,
    };
    scene.nodes.push_back(std::move(node));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_TRUE(result.instance.authored_to_runtime.contains("ground"));
    const auto node_h = result.instance.authored_to_runtime["ground"];

    CollisionFrameStorage frame{};
    build_collision_world(result.instance, assets.collisions(), frame);

    ASSERT_EQ(frame.terrain_constraint_surfaces.size(), 1u);
    const auto& surface = frame.terrain_constraint_surfaces[0];
    EXPECT_EQ(surface.entity, node_h);
    EXPECT_EQ(surface.resolved, collision_data);

    // The carrying node's scale is discarded: world_from_local is identity.
    const wz::math::Mat4 identity = wz::math::Mat4::identity();
    for (int i = 0; i < 16; ++i) {
        EXPECT_FLOAT_EQ(surface.world_from_local.m[i], identity.m[i])
            << "world_from_local element " << i
            << " must be identity for placement-driven collision";
    }

    // A probe near the placement extent's edge (world x ~= 0.9 * 4096) hits at
    // the placement-mapped footprint -- proving the footprint == placement
    // extent (4096), NOT the node-scale-inflated 512 * 4096.
    const CollisionWorldEntry entry{
        .entity = surface.entity,
        .collision_asset = surface.collision_asset,
        .world_from_local = surface.world_from_local,
        .enabled = surface.enabled,
        .resolved = surface.resolved,
    };
    CollisionSurfaceSample sample{};
    const float edge_x = 0.9f * 4096.0f;
    const float edge_z = 0.9f * 4096.0f;
    EXPECT_TRUE(sample_terrain_surface(entry, edge_x, edge_z, sample));
    EXPECT_TRUE(sample.hit);
    EXPECT_NEAR(sample.position.x, edge_x, 1e-2f);
    EXPECT_NEAR(sample.position.z, edge_z, 1e-2f);

    // Just past the placement extent (world x > 4096) there is no surface.
    CollisionSurfaceSample outside{};
    EXPECT_FALSE(
        sample_terrain_surface(entry, 4096.0f + 100.0f, edge_z, outside));
}

// Mirror of the above protecting #216: a NON-placement heightfield collision
// still composes the carrying node's transform (world_from_local == node.world),
// so a non-unit node scale still scales the footprint.
TEST(CollisionFrameWorldBuilder, NonPlacementSurfaceComposesNodeTransform)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        test_root("wz_collision_world_non_placement_composes_transform");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto field =
        assets.scalar_fields().create_procedural_scalar_field({
            .name = "collision/non_placement_frame_height",
            .width = 4,
            .height = 4,
            .depth = 1,
            .generator = ScalarFieldGenerator::GradientX,
        });
    ASSERT_TRUE(field.valid());

    // No placement connected: the collision uses its own world mapping.
    const auto constraint_surface =
        assets.collisions().create_from_height_field({
            .name = "collision/non_placement_constraint_surface",
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
    ASSERT_FALSE(collision_data->placement_driven);

    SceneAssetData scene{};
    scene.name = "non_placement_scale_scene";
    SceneNodeAsset node{};
    node.id = "ground";
    node.local.scale[0] = 512.0f;
    node.local.scale[1] = 512.0f;
    node.local.scale[2] = 512.0f;
    node.collision = SceneCollisionAsset{
        .collision_asset = constraint_surface.output,
        .constrain_movement = true,
    };
    scene.nodes.push_back(std::move(node));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_TRUE(result.instance.authored_to_runtime.contains("ground"));
    const auto node_h = result.instance.authored_to_runtime["ground"];

    CollisionFrameStorage frame{};
    build_collision_world(result.instance, assets.collisions(), frame);

    ASSERT_EQ(frame.terrain_constraint_surfaces.size(), 1u);
    const auto& surface = frame.terrain_constraint_surfaces[0];
    EXPECT_EQ(surface.entity, node_h);

    // #216 behaviour preserved: the node transform IS composed, so the scale
    // shows up on world_from_local's diagonal.
    const auto& runtime_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        node_h);
    EXPECT_FLOAT_EQ(
        surface.world_from_local.m[0],
        runtime_node.world.m[0]);
    EXPECT_FLOAT_EQ(surface.world_from_local.m[0], 512.0f);
    EXPECT_FLOAT_EQ(surface.world_from_local.m[5], 512.0f);
    EXPECT_FLOAT_EQ(surface.world_from_local.m[10], 512.0f);
}

// The observer is written by the HOST before build_collision_frame runs, so the
// build must carry it rather than reset it. Nothing in the build has any reason
// to touch the field today -- which is exactly why it could acquire a
// storage-clearing line later and silently drop the camera, leaving a clipmap
// reconstruction quietly answering for an observer at the origin.
TEST(CollisionFrameWorldBuilder, BuildPreservesTheObserverWrittenByTheHost)
{
    using namespace wz::engine::assets;

    CollisionFrameStorage frame{};
    // Defaulted storages -- unit tests, headless runs -- get a well-defined
    // observer at the origin rather than an uninitialised one.
    EXPECT_FLOAT_EQ(frame.observer_world_position.x, 0.0f);
    EXPECT_FLOAT_EQ(frame.observer_world_position.y, 0.0f);
    EXPECT_FLOAT_EQ(frame.observer_world_position.z, 0.0f);

    frame.observer_world_position =
        wz::math::Vec3{ .x = 130.0f, .y = 42.0f, .z = -77.5f };

    SceneAssetData scene{};
    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, {} };
    build_collision_frame(result.instance, assets.collisions(), frame);

    EXPECT_FLOAT_EQ(frame.observer_world_position.x, 130.0f);
    EXPECT_FLOAT_EQ(frame.observer_world_position.y, 42.0f);
    EXPECT_FLOAT_EQ(frame.observer_world_position.z, -77.5f);
}

