#include "collision_frame_test_support.h"

TEST(CollisionFrameCore, FrameStorageOwnsCollisionFrameStorage)
{
    static_assert(std::is_same_v<
        decltype(wz::engine::FrameStorage{}.collision),
        wz::engine::collision::CollisionFrameStorage>);
}

TEST(CollisionFrameCore, PairIdentityIsSortedAndRejectsSelfPairs)
{
    const CollisionPair pair = make_collision_pair(9u, 3u);
    EXPECT_EQ(pair.a, 3u);
    EXPECT_EQ(pair.b, 9u);
    EXPECT_TRUE(collision_pair_valid(pair));

    std::vector<CollisionPair> pairs{
        make_collision_pair(4u, 2u),
        make_collision_pair(2u, 4u),
        make_collision_pair(7u, 7u),
        CollisionPair{
            .a = wz::scene::INVALID_RUNTIME_ENTITY,
            .b = 1u,
        },
    };

    sort_unique_collision_pairs(pairs);
    ASSERT_EQ(pairs.size(), 1u);
    EXPECT_EQ(pairs[0].a, 2u);
    EXPECT_EQ(pairs[0].b, 4u);
}

TEST(CollisionFrameCore, NoOverlapProducesNoPairsOrEvents)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(2, 0, 0, 3, 1, 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    EXPECT_TRUE(frame.current_pairs.empty());
    EXPECT_TRUE(frame.events.empty());
    EXPECT_TRUE(frame.prev_pairs.empty());
}

TEST(CollisionFrameCore, FirstOverlapProducesEnter)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0.5f, 0, 0, 1.5f, 1, 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    ASSERT_EQ(frame.current_pairs.size(), 1u);
    EXPECT_EQ(frame.current_pairs[0], make_collision_pair(1u, 2u));
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].a, 1u);
    EXPECT_EQ(frame.events[0].b, 2u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);
    ASSERT_EQ(frame.prev_pairs.size(), 1u);
    EXPECT_EQ(frame.prev_pairs[0], make_collision_pair(1u, 2u));
}

TEST(CollisionFrameCore, ContinuingOverlapProducesStay)
{
    CollisionFrameStorage frame{};
    frame.prev_pairs = { make_collision_pair(1u, 2u) };
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0.5f, 0, 0, 1.5f, 1, 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].a, 1u);
    EXPECT_EQ(frame.events[0].b, 2u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Stay);
}

TEST(CollisionFrameCore, SeparatedAfterOverlapProducesExit)
{
    CollisionFrameStorage frame{};
    frame.prev_pairs = { make_collision_pair(1u, 2u) };
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(2, 0, 0, 3, 1, 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    EXPECT_TRUE(frame.current_pairs.empty());
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].a, 1u);
    EXPECT_EQ(frame.events[0].b, 2u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Exit);
    EXPECT_TRUE(frame.prev_pairs.empty());
}

TEST(CollisionFrameCore, EnterStayExitSequenceIsDeterministic)
{
    CollisionFrameStorage frame{};

    frame.world = {
        entry(7u, bounds(0, 0, 0, 1, 1, 1)),
        entry(3u, bounds(0.25f, 0, 0, 1.25f, 1, 1)),
    };
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].a, 3u);
    EXPECT_EQ(frame.events[0].b, 7u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].a, 3u);
    EXPECT_EQ(frame.events[0].b, 7u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Stay);

    frame.world[1].world_bounds = bounds(5, 0, 0, 6, 1, 1);
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].a, 3u);
    EXPECT_EQ(frame.events[0].b, 7u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Exit);
}

TEST(CollisionFrameCore, MaskFilteringIsBidirectional)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1), 0x1u, 0x2u),
        entry(2u, bounds(0, 0, 0, 1, 1, 1), 0x2u, 0x4u),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    EXPECT_TRUE(frame.current_pairs.empty());

    frame.world[1].collides_with_mask = 0x1u;
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    ASSERT_EQ(frame.current_pairs.size(), 1u);
    EXPECT_EQ(frame.current_pairs[0], make_collision_pair(1u, 2u));
}

TEST(CollisionFrameCore, NarrowphaseRejectsBoundsAwayFromTerrainSurfaceTriangle)
{
    wz::engine::assets::CollisionAssetData bounds_data{};
    bounds_data.source_asset.content_hash.lo = 1;
    bounds_data.shape_kind = wz::engine::assets::CollisionShapeKind::Bounds;
    bounds_data.bounds_min[0] = 9.0f;
    bounds_data.bounds_min[1] = -0.5f;
    bounds_data.bounds_min[2] = 9.0f;
    bounds_data.bounds_max[0] = 10.0f;
    bounds_data.bounds_max[1] = 0.5f;
    bounds_data.bounds_max[2] = 10.0f;

    wz::engine::assets::CollisionAssetData terrain_data{};
    terrain_data.source_asset.content_hash.lo = 2;
    terrain_data.shape_kind =
        wz::engine::assets::CollisionShapeKind::TerrainMeshSurface;
    terrain_data.mesh.content_hash.lo = 3;
    terrain_data.bounds_min[0] = 0.0f;
    terrain_data.bounds_min[1] = 0.0f;
    terrain_data.bounds_min[2] = 0.0f;
    terrain_data.bounds_max[0] = 10.0f;
    terrain_data.bounds_max[1] = 0.0f;
    terrain_data.bounds_max[2] = 10.0f;
    terrain_data.source_triangle_count = 1;
    terrain_data.accepted_triangle_count = 1;
    terrain_data.points = {
        { .position = { 0.0f, 0.0f, 0.0f } },
        { .position = { 1.0f, 0.0f, 0.0f } },
        { .position = { 0.0f, 0.0f, 1.0f } },
    };
    terrain_data.indices = { 0, 1, 2 };
    terrain_data.triangle_bounds = {
        wz::engine::assets::CollisionTriangleBounds{
            .min = { 0.0f, 0.0f, 0.0f },
            .max = { 1.0f, 0.0f, 1.0f },
        },
    };

    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(9, -0.5f, 9, 10, 0.5f, 10)),
        entry(2u, bounds(0, 0, 0, 10, 0, 10)),
    };
    frame.world[0].resolved = &bounds_data;
    frame.world[1].resolved = &terrain_data;

    broadphase_aabb_overlap(frame.world, frame.broadphase_pairs);
    ASSERT_EQ(frame.broadphase_pairs.size(), 1u);

    narrowphase_filter_pairs(
        frame.world,
        frame.broadphase_pairs,
        frame);

    EXPECT_EQ(frame.narrowphase_tests, 1u);
    EXPECT_EQ(frame.triangle_bounds_tested, 1u);
    EXPECT_EQ(frame.triangle_bounds_rejected, 1u);
    EXPECT_EQ(frame.early_out_hits, 0u);
    EXPECT_TRUE(frame.current_pairs.empty());
}

TEST(CollisionFrameCore, NarrowphaseAcceptsBoundsTouchingTerrainSurfaceTriangle)
{
    wz::engine::assets::CollisionAssetData bounds_data{};
    bounds_data.source_asset.content_hash.lo = 1;
    bounds_data.shape_kind = wz::engine::assets::CollisionShapeKind::Bounds;
    bounds_data.bounds_min[0] = 0.25f;
    bounds_data.bounds_min[1] = -0.5f;
    bounds_data.bounds_min[2] = 0.25f;
    bounds_data.bounds_max[0] = 0.75f;
    bounds_data.bounds_max[1] = 0.5f;
    bounds_data.bounds_max[2] = 0.75f;

    wz::engine::assets::CollisionAssetData terrain_data{};
    terrain_data.source_asset.content_hash.lo = 2;
    terrain_data.shape_kind =
        wz::engine::assets::CollisionShapeKind::TerrainMeshSurface;
    terrain_data.mesh.content_hash.lo = 3;
    terrain_data.bounds_min[0] = 0.0f;
    terrain_data.bounds_min[1] = 0.0f;
    terrain_data.bounds_min[2] = 0.0f;
    terrain_data.bounds_max[0] = 10.0f;
    terrain_data.bounds_max[1] = 0.0f;
    terrain_data.bounds_max[2] = 10.0f;
    terrain_data.source_triangle_count = 1;
    terrain_data.accepted_triangle_count = 1;
    terrain_data.points = {
        { .position = { 0.0f, 0.0f, 0.0f } },
        { .position = { 1.0f, 0.0f, 0.0f } },
        { .position = { 0.0f, 0.0f, 1.0f } },
    };
    terrain_data.indices = { 0, 1, 2 };
    terrain_data.triangle_bounds = {
        wz::engine::assets::CollisionTriangleBounds{
            .min = { 0.0f, 0.0f, 0.0f },
            .max = { 1.0f, 0.0f, 1.0f },
        },
    };

    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0.25f, -0.5f, 0.25f, 0.75f, 0.5f, 0.75f)),
        entry(2u, bounds(0, 0, 0, 10, 0, 10)),
    };
    frame.world[0].resolved = &bounds_data;
    frame.world[1].resolved = &terrain_data;

    broadphase_aabb_overlap(frame.world, frame.broadphase_pairs);
    narrowphase_filter_pairs(
        frame.world,
        frame.broadphase_pairs,
        frame);

    ASSERT_EQ(frame.current_pairs.size(), 1u);
    EXPECT_EQ(frame.current_pairs[0], make_collision_pair(1u, 2u));
    EXPECT_EQ(frame.narrowphase_tests, 1u);
    EXPECT_EQ(frame.triangle_bounds_tested, 1u);
    EXPECT_EQ(frame.triangle_bounds_rejected, 0u);
    EXPECT_EQ(frame.early_out_hits, 1u);
}

TEST(CollisionFrameCore, TriggerOverlapStillReportsEvent)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0, 0, 0, 1, 1, 1), 1, 0xffffffffu, true, true),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);
    EXPECT_TRUE(frame.world[1].is_trigger);
}

TEST(CollisionFrameCore, EntityEventsFanOutPairEventsInPairOrder)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(2u, bounds(0, 0, 0, 1, 1, 1),
            1, 0xffffffffu, true, true),
        entry(5u, bounds(0, 0, 0, 1, 1, 1)),
    };
    frame.events = {
        CollisionEvent{
            .a = 2u,
            .b = 5u,
            .kind = CollisionEventKind::Enter,
        },
    };

    fanout_collision_entity_events(
        frame.world,
        frame.events,
        frame.entity_events);

    ASSERT_EQ(frame.entity_events.size(), 2u);
    EXPECT_EQ(frame.entity_events[0].entity, 2u);
    EXPECT_EQ(frame.entity_events[0].other, 5u);
    EXPECT_EQ(frame.entity_events[0].kind, CollisionEventKind::Enter);
    EXPECT_TRUE(frame.entity_events[0].self_is_trigger);
    EXPECT_EQ(frame.entity_events[1].entity, 5u);
    EXPECT_EQ(frame.entity_events[1].other, 2u);
    EXPECT_EQ(frame.entity_events[1].kind, CollisionEventKind::Enter);
    EXPECT_FALSE(frame.entity_events[1].self_is_trigger);
}

TEST(CollisionFrameCore, EntityEventsDefaultMissingExitEntryToNotTrigger)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(5u, bounds(0, 0, 0, 1, 1, 1),
            1, 0xffffffffu, true, true),
    };
    frame.events = {
        CollisionEvent{
            .a = 2u,
            .b = 5u,
            .kind = CollisionEventKind::Exit,
        },
    };

    fanout_collision_entity_events(
        frame.world,
        frame.events,
        frame.entity_events);

    ASSERT_EQ(frame.entity_events.size(), 2u);
    EXPECT_EQ(frame.entity_events[0].entity, 2u);
    EXPECT_EQ(frame.entity_events[0].other, 5u);
    EXPECT_EQ(frame.entity_events[0].kind, CollisionEventKind::Exit);
    EXPECT_FALSE(frame.entity_events[0].self_is_trigger);
    EXPECT_EQ(frame.entity_events[1].entity, 5u);
    EXPECT_EQ(frame.entity_events[1].other, 2u);
    EXPECT_EQ(frame.entity_events[1].kind, CollisionEventKind::Exit);
    EXPECT_TRUE(frame.entity_events[1].self_is_trigger);
}

TEST(CollisionFrameCore, DisabledEntryRemovesPreviousOverlap)
{
    CollisionFrameStorage frame{};
    frame.prev_pairs = { make_collision_pair(1u, 2u) };
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0, 0, 0, 1, 1, 1), 1, 0xffffffffu, false),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    EXPECT_TRUE(frame.current_pairs.empty());
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Exit);
}

TEST(CollisionFrameCore, AdvanceCollisionFrameEmitsEntityEvents)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0, 0, 0, 1, 1, 1),
            1, 0xffffffffu, true, true),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    ASSERT_EQ(frame.events.size(), 1u);
    ASSERT_EQ(frame.entity_events.size(), 2u);
    EXPECT_EQ(frame.entity_events[0].entity, 1u);
    EXPECT_EQ(frame.entity_events[0].other, 2u);
    EXPECT_EQ(frame.entity_events[0].kind, CollisionEventKind::Enter);
    EXPECT_FALSE(frame.entity_events[0].self_is_trigger);
    EXPECT_EQ(frame.entity_events[1].entity, 2u);
    EXPECT_EQ(frame.entity_events[1].other, 1u);
    EXPECT_EQ(frame.entity_events[1].kind, CollisionEventKind::Enter);
    EXPECT_TRUE(frame.entity_events[1].self_is_trigger);
}

TEST(CollisionFrameCore, RouteCollisionEntityEventsRequiresListener)
{
    std::vector<CollisionEntityEvent> events{
        CollisionEntityEvent{
            .entity = 1u,
            .other = 2u,
            .kind = CollisionEventKind::Enter,
        },
    };
    std::vector<wz::engine::assets::SceneComponentRecord<
        wz::engine::assets::EventListenerComponent>> listeners;
    std::vector<CollisionEntityEvent> routed;

    route_collision_entity_events(events, listeners, routed);

    EXPECT_TRUE(routed.empty());
}

TEST(CollisionFrameCore, RouteCollisionEntityEventsMatchesExplicitKind)
{
    std::vector<CollisionEntityEvent> events{
        CollisionEntityEvent{
            .entity = 1u,
            .other = 2u,
            .kind = CollisionEventKind::Enter,
        },
        CollisionEntityEvent{
            .entity = 1u,
            .other = 3u,
            .kind = CollisionEventKind::Stay,
        },
        CollisionEntityEvent{
            .entity = 1u,
            .other = 4u,
            .kind = CollisionEventKind::Exit,
        },
    };
    std::vector listeners{
        listener(1u, { "collision.enter", "collision.exit" }),
    };
    std::vector<CollisionEntityEvent> routed;

    route_collision_entity_events(events, listeners, routed);

    ASSERT_EQ(routed.size(), 2u);
    EXPECT_EQ(routed[0].kind, CollisionEventKind::Enter);
    EXPECT_EQ(routed[0].other, 2u);
    EXPECT_EQ(routed[1].kind, CollisionEventKind::Exit);
    EXPECT_EQ(routed[1].other, 4u);
}

TEST(CollisionFrameCore, RouteCollisionEntityEventsWildcardIsCollisionOnly)
{
    std::vector<CollisionEntityEvent> events{
        CollisionEntityEvent{
            .entity = 1u,
            .other = 2u,
            .kind = CollisionEventKind::Enter,
        },
        CollisionEntityEvent{
            .entity = 1u,
            .other = 3u,
            .kind = CollisionEventKind::Stay,
        },
        CollisionEntityEvent{
            .entity = 1u,
            .other = 4u,
            .kind = CollisionEventKind::Exit,
        },
    };
    std::vector listeners{
        listener(1u, { "collision.*" }),
    };
    std::vector<CollisionEntityEvent> routed;

    route_collision_entity_events(events, listeners, routed);

    ASSERT_EQ(routed.size(), 3u);
    EXPECT_EQ(routed[0].kind, CollisionEventKind::Enter);
    EXPECT_EQ(routed[1].kind, CollisionEventKind::Stay);
    EXPECT_EQ(routed[2].kind, CollisionEventKind::Exit);

    listeners = {
        listener(1u, { "*.enter", "collision.e*" }),
    };
    route_collision_entity_events(events, listeners, routed);
    EXPECT_TRUE(routed.empty())
        << "collision.* is a hardcoded token, not a general glob";
}

TEST(CollisionFrameCore, RouteCollisionEntityEventsPreservesPerspective)
{
    std::vector<CollisionEntityEvent> events{
        CollisionEntityEvent{
            .entity = 1u,
            .other = 2u,
            .kind = CollisionEventKind::Enter,
            .self_is_trigger = true,
        },
        CollisionEntityEvent{
            .entity = 2u,
            .other = 1u,
            .kind = CollisionEventKind::Enter,
            .self_is_trigger = false,
        },
    };
    std::vector listeners{
        listener(2u, { "collision.enter" }),
    };
    std::vector<CollisionEntityEvent> routed;

    route_collision_entity_events(events, listeners, routed);

    ASSERT_EQ(routed.size(), 1u);
    EXPECT_EQ(routed[0].entity, 2u);
    EXPECT_EQ(routed[0].other, 1u);
    EXPECT_FALSE(routed[0].self_is_trigger);
}

TEST(CollisionFrameCore, RouteCollisionEntityEventsIgnoresUnknownChannels)
{
    std::vector<CollisionEntityEvent> events{
        CollisionEntityEvent{
            .entity = 1u,
            .other = 2u,
            .kind = CollisionEventKind::Enter,
        },
    };
    std::vector listeners{
        listener(1u, { "collision", "physics.enter", "input.key" }),
    };
    std::vector<CollisionEntityEvent> routed;

    route_collision_entity_events(events, listeners, routed);

    EXPECT_TRUE(routed.empty());
}

TEST(CollisionFrameCore, MultiplePairsEmitSortedEvents)
{
    CollisionFrameStorage frame{};
    frame.prev_pairs = {
        make_collision_pair(1u, 3u),
        make_collision_pair(4u, 9u),
    };
    frame.world = {
        entry(5u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0, 0, 0, 1, 1, 1)),
        entry(1u, bounds(4, 0, 0, 5, 1, 1)),
        entry(3u, bounds(4, 0, 0, 5, 1, 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    ASSERT_EQ(frame.current_pairs.size(), 2u);
    EXPECT_EQ(frame.current_pairs[0], make_collision_pair(1u, 3u));
    EXPECT_EQ(frame.current_pairs[1], make_collision_pair(2u, 5u));

    ASSERT_EQ(frame.events.size(), 3u);
    EXPECT_EQ(frame.events[0].a, 1u);
    EXPECT_EQ(frame.events[0].b, 3u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Stay);
    EXPECT_EQ(frame.events[1].a, 2u);
    EXPECT_EQ(frame.events[1].b, 5u);
    EXPECT_EQ(frame.events[1].kind, CollisionEventKind::Enter);
    EXPECT_EQ(frame.events[2].a, 4u);
    EXPECT_EQ(frame.events[2].b, 9u);
    EXPECT_EQ(frame.events[2].kind, CollisionEventKind::Exit);
}

