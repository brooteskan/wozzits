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

