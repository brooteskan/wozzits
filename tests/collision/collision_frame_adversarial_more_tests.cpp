#include "collision_frame_test_support.h"

TEST(CollisionFrameWorldBuilder, NodeWithoutCollisionComponentIsSkipped)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root = test_root("wz_collision_no_component_skipped");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_cube_no_component",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "collision_no_component";

    SceneNodeAsset node_a{};
    node_a.id = "visual_only";
    scene.nodes.push_back(std::move(node_a));

    SceneNodeAsset node_b{};
    node_b.id = "also_visual";
    scene.nodes.push_back(std::move(node_b));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    EXPECT_TRUE(result.instance.collisions.empty());

    CollisionFrameStorage frame{};
    build_collision_world(result.instance, assets.collisions(), frame);
    EXPECT_TRUE(frame.world.empty())
        << "Nodes without collision components should not appear in collision world";
}

TEST(CollisionFrameWorldBuilder, MultipleNodesShareSameCollisionAsset)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root = test_root("wz_collision_shared_asset");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_cube_shared",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto collision = assets.collisions().create_from_mesh({
        .name = "collision/cube_shared",
        .mesh = mesh,
        .build_method = CollisionBuildMethod::Bounds,
    });
    ASSERT_TRUE(collision.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "collision_shared_asset";

    for (int i = 0; i < 4; ++i) {
        SceneNodeAsset node{};
        node.id = "body_" + std::to_string(i);
        node.local.translation[0] = static_cast<float>(i) * 0.25f;
        node.collision = SceneCollisionAsset{
            .collision_asset = collision.output,
        };
        scene.nodes.push_back(std::move(node));
    }

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.collisions.size(), 4u);

    CollisionFrameStorage frame{};
    build_collision_frame(result.instance, assets.collisions(), frame);

    ASSERT_EQ(frame.world.size(), 4u);
    for (const auto& e : frame.world) {
        EXPECT_EQ(e.collision_asset, collision.output);
        EXPECT_NE(e.resolved, nullptr);
    }

    const std::size_t expected_pairs = (4 * 3) / 2;
    EXPECT_EQ(frame.current_pairs.size(), expected_pairs)
        << "4 overlapping nodes sharing one asset should produce 6 pairs";
    EXPECT_EQ(frame.events.size(), expected_pairs);
    for (const auto& ev : frame.events) {
        EXPECT_EQ(ev.kind, CollisionEventKind::Enter);
    }
}

TEST(CollisionFrameAdversarial, DoubleAdvanceWithoutRebuildDuplicatesEvents)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0, 0, 0, 1, 1, 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);

    // Call advance again WITHOUT rebuilding broadphase.
    // prev_pairs was set to current_pairs by the first advance.
    // current_pairs is still the same (not cleared).
    // This should produce Stay, not a second Enter.
    advance_collision_frame(frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Stay)
        << "Double-advance without broadphase rebuild should produce Stay, "
           "not a duplicate Enter";
}

TEST(CollisionFrameAdversarial, BuildCollisionFrameCalledTwiceConsecutively)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root = test_root("wz_collision_double_build");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_double_build",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto collision = assets.collisions().create_from_mesh({
        .name = "collision/cube_double_build",
        .mesh = mesh,
        .build_method = CollisionBuildMethod::Bounds,
    });
    ASSERT_TRUE(collision.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "collision_double_build";

    SceneNodeAsset a{};
    a.id = "a";
    a.collision = SceneCollisionAsset{
        .collision_asset = collision.output,
    };
    a.event_listener = SceneEventListenerAsset{
        .channels = { "collision.enter" },
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

    CollisionFrameStorage frame{};

    // First call: Enter
    build_collision_frame(result.instance, assets.collisions(), frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);
    ASSERT_EQ(frame.routed_entity_events.size(), 1u);
    EXPECT_EQ(frame.routed_entity_events[0].entity,
        result.instance.authored_to_runtime["a"]);
    EXPECT_EQ(frame.routed_entity_events[0].kind, CollisionEventKind::Enter);

    // Second call immediately: Stay (not another Enter)
    build_collision_frame(result.instance, assets.collisions(), frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Stay)
        << "Calling build_collision_frame twice on the same storage should "
           "produce Stay on the second call, not another Enter";
    EXPECT_TRUE(frame.routed_entity_events.empty())
        << "collision.enter listeners should not receive Stay";
}

TEST(CollisionFrameAdversarial, ThreeBodyChainOnlyAdjacentPairsOverlap)
{
    CollisionFrameStorage frame{};
    // A overlaps B, B overlaps C, but A does not overlap C
    frame.world = {
        entry(1u, bounds(0, 0, 0, 2, 1, 1)),
        entry(2u, bounds(1, 0, 0, 3, 1, 1)),
        entry(3u, bounds(2.5f, 0, 0, 4, 1, 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    ASSERT_EQ(frame.current_pairs.size(), 2u);
    EXPECT_EQ(frame.current_pairs[0], make_collision_pair(1u, 2u));
    EXPECT_EQ(frame.current_pairs[1], make_collision_pair(2u, 3u));
    ASSERT_EQ(frame.events.size(), 2u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);
    EXPECT_EQ(frame.events[1].kind, CollisionEventKind::Enter);
}

TEST(CollisionFrameAdversarial, OneInvertedAxisRejectsOverlap)
{
    CollisionFrameStorage frame{};

    // Inverted on X only, valid on Y and Z
    frame.world = {
        entry(1u, bounds(0, 0, 0, 10, 10, 10)),
        entry(2u, bounds(5, 0, 0, 1, 10, 10)),
    };
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    EXPECT_TRUE(frame.current_pairs.empty()) << "X-inverted AABB should be rejected";

    // Inverted on Y only
    frame.world = {
        entry(1u, bounds(0, 0, 0, 10, 10, 10)),
        entry(2u, bounds(0, 5, 0, 10, 1, 10)),
    };
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    EXPECT_TRUE(frame.current_pairs.empty()) << "Y-inverted AABB should be rejected";

    // Inverted on Z only
    frame.world = {
        entry(1u, bounds(0, 0, 0, 10, 10, 10)),
        entry(2u, bounds(0, 0, 5, 10, 10, 1)),
    };
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    EXPECT_TRUE(frame.current_pairs.empty()) << "Z-inverted AABB should be rejected";
}

TEST(CollisionFrameAdversarial, NaNInSingleAxisRejectsOverlap)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CollisionFrameStorage frame{};

    // NaN only in min.x
    frame.world = {
        entry(1u, bounds(0, 0, 0, 10, 10, 10)),
        entry(2u, bounds(nan, 0, 0, 5, 5, 5)),
    };
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    EXPECT_TRUE(frame.current_pairs.empty())
        << "NaN in a single axis should reject the AABB";

    // NaN only in max.y
    frame.world = {
        entry(1u, bounds(0, 0, 0, 10, 10, 10)),
        entry(2u, bounds(0, 0, 0, 5, nan, 5)),
    };
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    EXPECT_TRUE(frame.current_pairs.empty())
        << "NaN in max.y should reject the AABB";
}

TEST(CollisionFrameAdversarial, NegativeInfMinPositiveInfMaxOverlaps)
{
    const float inf = std::numeric_limits<float>::infinity();
    CollisionFrameStorage frame{};

    frame.world = {
        entry(1u, bounds(-inf, -inf, -inf, inf, inf, inf)),
        entry(2u, bounds(-inf, -inf, -inf, inf, inf, inf)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    ASSERT_EQ(frame.current_pairs.size(), 1u)
        << "Two infinite-extent AABBs should overlap";
}

TEST(CollisionFrameAdversarial, LargeCoordinatesDoNotLosePrecision)
{
    const float big = 1e6f;
    const float half = 0.5f;

    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(big, big, big, big + 1, big + 1, big + 1)),
        entry(2u, bounds(big + half, big + half, big + half,
                         big + 1 + half, big + 1 + half, big + 1 + half)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    ASSERT_EQ(frame.current_pairs.size(), 1u)
        << "Overlapping boxes at large coordinates should still detect";

    // Clearly separated at large coordinates
    frame.world = {
        entry(1u, bounds(big, big, big, big + 1, big + 1, big + 1)),
        entry(2u, bounds(big + 2, big, big, big + 3, big + 1, big + 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    EXPECT_TRUE(frame.current_pairs.empty())
        << "Separated boxes at large coordinates should not detect";
}

TEST(CollisionFrameAdversarial, AllDisabledWorldProducesNoEvents)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1), 1, 0xffffffffu, false),
        entry(2u, bounds(0, 0, 0, 1, 1, 1), 1, 0xffffffffu, false),
        entry(3u, bounds(0, 0, 0, 1, 1, 1), 1, 0xffffffffu, false),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    EXPECT_TRUE(frame.current_pairs.empty());
    EXPECT_TRUE(frame.events.empty());
}

TEST(CollisionFrameAdversarial, EmptyPrevNonEmptyCurrentAllEnter)
{
    CollisionFrameStorage frame{};
    frame.current_pairs = {
        make_collision_pair(1u, 2u),
        make_collision_pair(3u, 4u),
        make_collision_pair(5u, 6u),
    };

    advance_collision_frame(frame);

    ASSERT_EQ(frame.events.size(), 3u);
    for (const auto& ev : frame.events) {
        EXPECT_EQ(ev.kind, CollisionEventKind::Enter);
    }
}

TEST(CollisionFrameAdversarial, NonEmptyPrevEmptyCurrentAllExit)
{
    CollisionFrameStorage frame{};
    frame.prev_pairs = {
        make_collision_pair(1u, 2u),
        make_collision_pair(3u, 4u),
        make_collision_pair(5u, 6u),
    };

    advance_collision_frame(frame);

    ASSERT_EQ(frame.events.size(), 3u);
    for (const auto& ev : frame.events) {
        EXPECT_EQ(ev.kind, CollisionEventKind::Exit);
    }
    EXPECT_TRUE(frame.prev_pairs.empty())
        << "After all exits, prev_pairs should be empty (copied from empty current)";
}

TEST(CollisionFrameAdversarial, DiffEventsPreservesEntityIdsFromPairs)
{
    std::vector<CollisionPair> prev = {
        make_collision_pair(100u, 200u),
    };
    std::vector<CollisionPair> current = {
        make_collision_pair(300u, 400u),
    };
    std::vector<CollisionEvent> events;

    diff_collision_events(prev, current, events);

    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].a, 100u);
    EXPECT_EQ(events[0].b, 200u);
    EXPECT_EQ(events[0].kind, CollisionEventKind::Exit);
    EXPECT_EQ(events[1].a, 300u);
    EXPECT_EQ(events[1].b, 400u);
    EXPECT_EQ(events[1].kind, CollisionEventKind::Enter);
}

TEST(CollisionFrameAdversarial, DiffEventsWithUnsortedInputViaAdvance)
{
    CollisionFrameStorage frame{};

    // Deliberately unsorted prev_pairs and current_pairs.
    // advance_collision_frame re-sorts before diffing.
    frame.prev_pairs = {
        make_collision_pair(5u, 6u),
        make_collision_pair(1u, 2u),
        make_collision_pair(3u, 4u),
    };
    frame.current_pairs = {
        make_collision_pair(3u, 4u),
        make_collision_pair(7u, 8u),
        make_collision_pair(1u, 2u),
    };

    advance_collision_frame(frame);

    ASSERT_EQ(frame.events.size(), 4u);
    EXPECT_EQ(frame.events[0].a, 1u);
    EXPECT_EQ(frame.events[0].b, 2u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Stay);
    EXPECT_EQ(frame.events[1].a, 3u);
    EXPECT_EQ(frame.events[1].b, 4u);
    EXPECT_EQ(frame.events[1].kind, CollisionEventKind::Stay);
    EXPECT_EQ(frame.events[2].a, 5u);
    EXPECT_EQ(frame.events[2].b, 6u);
    EXPECT_EQ(frame.events[2].kind, CollisionEventKind::Exit);
    EXPECT_EQ(frame.events[3].a, 7u);
    EXPECT_EQ(frame.events[3].b, 8u);
    EXPECT_EQ(frame.events[3].kind, CollisionEventKind::Enter);
}

TEST(CollisionFrameAdversarial, EventCountEqualsUnionOfPrevAndCurrentPairs)
{
    CollisionFrameStorage frame{};
    frame.prev_pairs = {
        make_collision_pair(1u, 2u),
        make_collision_pair(3u, 4u),
        make_collision_pair(5u, 6u),
    };
    frame.current_pairs = {
        make_collision_pair(3u, 4u),
        make_collision_pair(5u, 6u),
        make_collision_pair(7u, 8u),
        make_collision_pair(9u, 10u),
    };

    advance_collision_frame(frame);

    // Union: (1,2), (3,4), (5,6), (7,8), (9,10) = 5 unique pairs = 5 events
    EXPECT_EQ(frame.events.size(), 5u)
        << "Every pair in the union of prev and current should produce "
           "exactly one event";

    uint32_t enters = 0, stays = 0, exits = 0;
    for (const auto& ev : frame.events) {
        if (ev.kind == CollisionEventKind::Enter) ++enters;
        else if (ev.kind == CollisionEventKind::Stay) ++stays;
        else if (ev.kind == CollisionEventKind::Exit) ++exits;
    }
    EXPECT_EQ(enters, 2u);
    EXPECT_EQ(stays, 2u);
    EXPECT_EQ(exits, 1u);
}

TEST(CollisionFrameAdversarial, PrevPairsDuplicatedGetDeduped)
{
    CollisionFrameStorage frame{};
    frame.prev_pairs = {
        make_collision_pair(1u, 2u),
        make_collision_pair(1u, 2u),
        make_collision_pair(1u, 2u),
    };
    frame.current_pairs = {
        make_collision_pair(1u, 2u),
    };

    advance_collision_frame(frame);

    ASSERT_EQ(frame.events.size(), 1u)
        << "Duplicate prev_pairs should be deduped, producing single Stay";
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Stay);
}

TEST(CollisionFrameWorldBuilder, ParentCollisionChildNoCollision)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root = test_root("wz_collision_parent_only");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_cube_parent_only",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto collision = assets.collisions().create_from_mesh({
        .name = "collision/cube_parent_only",
        .mesh = mesh,
        .build_method = CollisionBuildMethod::Bounds,
    });
    ASSERT_TRUE(collision.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "collision_parent_only";

    SceneNodeAsset parent{};
    parent.id = "parent";
    parent.collision = SceneCollisionAsset{
        .collision_asset = collision.output,
    };
    scene.nodes.push_back(std::move(parent));

    SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "parent";
    child.local.translation[0] = 5.0f;
    scene.nodes.push_back(std::move(child));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    ASSERT_EQ(result.instance.collisions.size(), 1u)
        << "Only parent should have a collision component";

    CollisionFrameStorage frame{};
    build_collision_world(result.instance, assets.collisions(), frame);

    ASSERT_EQ(frame.world.size(), 1u);
    EXPECT_EQ(frame.world[0].entity,
        result.instance.authored_to_runtime["parent"]);
}

TEST(CollisionFrameWorldBuilder, OverlappingThenReinstantiatedFarApart)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root = test_root("wz_collision_reinstantiate");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_cube_reinstantiate",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto collision = assets.collisions().create_from_mesh({
        .name = "collision/cube_reinstantiate",
        .mesh = mesh,
        .build_method = CollisionBuildMethod::Bounds,
    });
    ASSERT_TRUE(collision.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    // Scene 1: nodes close together → Enter
    SceneAssetData scene_close{};
    scene_close.name = "collision_reinstantiate_close";

    SceneNodeAsset a1{};
    a1.id = "a";
    a1.collision = SceneCollisionAsset{
        .collision_asset = collision.output,
    };
    scene_close.nodes.push_back(std::move(a1));

    SceneNodeAsset b1{};
    b1.id = "b";
    b1.local.translation[0] = 0.25f;
    b1.collision = SceneCollisionAsset{
        .collision_asset = collision.output,
    };
    scene_close.nodes.push_back(std::move(b1));

    auto result_close = instantiate_scene(scene_close);
    ASSERT_TRUE(result_close.ok()) << result_close.error_detail;

    CollisionFrameStorage frame{};
    build_collision_frame(result_close.instance, assets.collisions(), frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);

    // Scene 2: same structure but far apart → Exit
    // Simulates a scene reload or hot-swap with different transforms.
    SceneAssetData scene_far{};
    scene_far.name = "collision_reinstantiate_far";

    SceneNodeAsset a2{};
    a2.id = "a";
    a2.collision = SceneCollisionAsset{
        .collision_asset = collision.output,
    };
    scene_far.nodes.push_back(std::move(a2));

    SceneNodeAsset b2{};
    b2.id = "b";
    b2.local.translation[0] = 100.0f;
    b2.collision = SceneCollisionAsset{
        .collision_asset = collision.output,
    };
    scene_far.nodes.push_back(std::move(b2));

    auto result_far = instantiate_scene(scene_far);
    ASSERT_TRUE(result_far.ok()) << result_far.error_detail;

    build_collision_frame(result_far.instance, assets.collisions(), frame);

    // The prev_pairs still hold the pair from the close scene.
    // The new scene has different runtime entity IDs, so
    // the old pair should Exit and no new pair should form.
    EXPECT_TRUE(frame.current_pairs.empty());
    ASSERT_GE(frame.events.size(), 1u);
    for (const auto& ev : frame.events) {
        EXPECT_EQ(ev.kind, CollisionEventKind::Exit);
    }
}

