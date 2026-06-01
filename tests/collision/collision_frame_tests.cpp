#include <engine/collision/collision_frame.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/frame_storage.h>

#include <jobs/dag_scheduler.h>
#include <jobs/frame_execution.h>
#include <jobs/job_graph_template.h>

#include <file/filesystem.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    using namespace wz::engine::collision;

    wz::scene::AABB bounds(
        float min_x,
        float min_y,
        float min_z,
        float max_x,
        float max_y,
        float max_z)
    {
        return wz::scene::AABB{
            .min = { min_x, min_y, min_z },
            .max = { max_x, max_y, max_z },
        };
    }

    CollisionWorldEntry entry(
        wz::scene::RuntimeEntityId entity,
        wz::scene::AABB world_bounds,
        uint32_t layer_mask = 1,
        uint32_t collides_with_mask = 0xffffffffu,
        bool enabled = true,
        bool is_trigger = false)
    {
        return CollisionWorldEntry{
            .entity = entity,
            .world_bounds = world_bounds,
            .layer_mask = layer_mask,
            .collides_with_mask = collides_with_mask,
            .is_trigger = is_trigger,
            .enabled = enabled,
        };
    }

    wz::engine::assets::SceneComponentRecord<
        wz::engine::assets::EventListenerComponent>
    listener(
        wz::scene::RuntimeEntityId entity,
        std::vector<std::string> channels)
    {
        return {
            .node = entity,
            .component = {
                .channels = std::move(channels),
            },
        };
    }

    wz::fs::Path test_root(const char* name)
    {
        return wz::fs::join(wz::fs::temp_directory_path(), name);
    }

    void expect_aabb_near(
        const wz::scene::AABB& actual,
        const wz::scene::AABB& expected)
    {
        constexpr float eps = 1e-5f;
        EXPECT_NEAR(actual.min.x, expected.min.x, eps);
        EXPECT_NEAR(actual.min.y, expected.min.y, eps);
        EXPECT_NEAR(actual.min.z, expected.min.z, eps);
        EXPECT_NEAR(actual.max.x, expected.max.x, eps);
        EXPECT_NEAR(actual.max.y, expected.max.y, eps);
        EXPECT_NEAR(actual.max.z, expected.max.z, eps);
    }

    struct CollisionFrameJobData
    {
        wz::engine::FrameStorage* frame = nullptr;
        const wz::engine::assets::SceneInstance* scene = nullptr;
        const wz::engine::assets::CollisionAssetModule* collisions = nullptr;
        bool* ran = nullptr;
    };

    void job_noop(wz::jobs::JobContext&)
    {
    }

    void job_build_collision_frame_for_test(wz::jobs::JobContext& ctx)
    {
        auto* data = static_cast<CollisionFrameJobData*>(ctx.frame_user);
        ASSERT_NE(data, nullptr);
        ASSERT_NE(data->frame, nullptr);
        ASSERT_NE(data->scene, nullptr);
        ASSERT_NE(data->collisions, nullptr);
        ASSERT_NE(data->ran, nullptr);

        build_collision_frame(
            *data->scene,
            *data->collisions,
            data->frame->collision);
        *data->ran = true;
    }
}

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

TEST(CollisionFrameJobIntegration, BoundSceneBuildsFrameThroughScheduler)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root = test_root("wz_collision_job_integration");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_cube_job_integration",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto collision = assets.collisions().create_from_mesh({
        .name = "collision/cube_job_integration",
        .mesh = mesh,
        .build_method = CollisionBuildMethod::Bounds,
    });
    ASSERT_TRUE(collision.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "collision_job_integration";

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

    wz::engine::FrameStorage frame{};
    bool collision_job_ran = false;
    CollisionFrameJobData collision_data{
        .frame = &frame,
        .scene = &result.instance,
        .collisions = &assets.collisions(),
        .ran = &collision_job_ran,
    };

    wz::jobs::JobGraphTemplate graph;
    const auto compile_scene = graph.add_job({
        .name = "compile_scene",
        .lane = wz::jobs::ExecutionLane::MainThread,
        .run = job_noop,
    });
    const auto build_collision_frame = graph.add_job({
        .name = "build_collision_frame",
        .lane = wz::jobs::ExecutionLane::MainThread,
        .run = job_build_collision_frame_for_test,
    });
    const auto build_render_ir = graph.add_job({
        .name = "build_render_ir",
        .lane = wz::jobs::ExecutionLane::MainThread,
        .run = job_noop,
    });

    ASSERT_TRUE(graph.add_dependency(compile_scene, build_collision_frame));
    ASSERT_TRUE(graph.add_dependency(build_collision_frame, build_render_ir));
    ASSERT_TRUE(graph.commit());

    wz::jobs::FrameExecution exec;
    exec.reset(graph);
    exec.bind(compile_scene, nullptr);
    exec.bind(build_collision_frame, &collision_data);
    exec.bind(build_render_ir, nullptr);

    wz::jobs::DagScheduler scheduler;
    scheduler.execute(graph, exec);

    EXPECT_TRUE(collision_job_ran);
    EXPECT_EQ(exec.remaining_jobs, 0u);
    EXPECT_EQ(exec.status[build_collision_frame], wz::jobs::JobStatus::Done);

    ASSERT_EQ(frame.collision.world.size(), 2u);
    ASSERT_EQ(frame.collision.events.size(), 1u);
    EXPECT_EQ(frame.collision.events[0].kind, CollisionEventKind::Enter);
}

// ─── Adversarial tests ──────────────────────────────────────────────────────

TEST(CollisionFrameAdversarial, EmptyWorldProducesNoEventsAcrossFrames)
{
    CollisionFrameStorage frame{};

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);
    EXPECT_TRUE(frame.events.empty());
    EXPECT_TRUE(frame.prev_pairs.empty());
    EXPECT_TRUE(frame.current_pairs.empty());

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);
    EXPECT_TRUE(frame.events.empty());
    EXPECT_TRUE(frame.prev_pairs.empty());
}

TEST(CollisionFrameAdversarial, SingleEntryCannotSelfCollide)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    EXPECT_TRUE(frame.current_pairs.empty());
    EXPECT_TRUE(frame.events.empty());
}

TEST(CollisionFrameAdversarial, EdgeTouchingAABBsDoOverlap)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(1, 0, 0, 2, 1, 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    ASSERT_EQ(frame.current_pairs.size(), 1u)
        << "AABBs sharing a face should report overlap (closed interval)";
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);
}

TEST(CollisionFrameAdversarial, CornerTouchingAABBsDoOverlap)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(1, 1, 1, 2, 2, 2)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);

    ASSERT_EQ(frame.current_pairs.size(), 1u)
        << "AABBs sharing a single corner should report overlap (closed interval)";
}

TEST(CollisionFrameAdversarial, ZeroVolumePointAABBOverlapsContainingBox)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 2, 2, 2)),
        entry(2u, bounds(1, 1, 1, 1, 1, 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    ASSERT_EQ(frame.current_pairs.size(), 1u)
        << "Degenerate zero-volume AABB inside another should overlap";
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);
}

TEST(CollisionFrameAdversarial, ZeroVolumePointAABBOutsideDoesNotOverlap)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(5, 5, 5, 5, 5, 5)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    EXPECT_TRUE(frame.current_pairs.empty());
}

TEST(CollisionFrameAdversarial, InvertedAABBDoesNotOverlapAnything)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 10, 10, 10)),
        entry(2u, bounds(5, 5, 5, 1, 1, 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);

    EXPECT_TRUE(frame.current_pairs.empty())
        << "AABB with min > max should not overlap anything";
}

TEST(CollisionFrameAdversarial, NaNBoundsDoNotOverlapAnything)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 10, 10, 10)),
        entry(2u, bounds(nan, 0, 0, nan, 1, 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);

    EXPECT_TRUE(frame.current_pairs.empty())
        << "NaN AABB comparisons should fail (IEEE 754), producing no pairs";
}

TEST(CollisionFrameAdversarial, InfBoundsOverlapEverything)
{
    const float inf = std::numeric_limits<float>::infinity();
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(100, 200, 300, 101, 201, 301)),
        entry(2u, bounds(-inf, -inf, -inf, inf, inf, inf)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);

    ASSERT_EQ(frame.current_pairs.size(), 1u)
        << "Infinite AABB should overlap any finite AABB";
}

TEST(CollisionFrameAdversarial, LayerMaskZeroIsInvisibleToAll)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1), 0x0u, 0xffffffffu),
        entry(2u, bounds(0, 0, 0, 1, 1, 1), 0xffffffffu, 0xffffffffu),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);

    EXPECT_TRUE(frame.current_pairs.empty())
        << "Entity with layer_mask=0 should be invisible to all collides_with checks";
}

TEST(CollisionFrameAdversarial, CollidesWithMaskZeroHitsNothing)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1), 0xffffffffu, 0x0u),
        entry(2u, bounds(0, 0, 0, 1, 1, 1), 0xffffffffu, 0xffffffffu),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);

    EXPECT_TRUE(frame.current_pairs.empty())
        << "Entity with collides_with_mask=0 cannot match any layer";
}

TEST(CollisionFrameAdversarial, DuplicateEntityIdsSuppressedAsSelfPairs)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(5u, bounds(0, 0, 0, 1, 1, 1)),
        entry(5u, bounds(0, 0, 0, 1, 1, 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);

    EXPECT_TRUE(frame.current_pairs.empty())
        << "Two world entries with same entity ID should produce a self-pair "
           "that gets filtered by sort_unique_collision_pairs";
}

TEST(CollisionFrameAdversarial, RapidReenableCycleProducesCorrectEvents)
{
    CollisionFrameStorage frame{};

    // Frame 1: both enabled, overlapping → Enter
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0.5f, 0, 0, 1.5f, 1, 1)),
    };
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);

    // Frame 2: entity 2 disabled → Exit
    frame.world[1].enabled = false;
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Exit);

    // Frame 3: entity 2 re-enabled → Enter (not Stay)
    frame.world[1].enabled = true;
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter)
        << "Re-enabling after disable must produce Enter, not Stay";
}

TEST(CollisionFrameAdversarial, MaskChangeBreaksExistingOverlap)
{
    CollisionFrameStorage frame{};

    // Frame 1: overlapping, compatible masks → Enter
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1), 0x1u, 0x2u),
        entry(2u, bounds(0, 0, 0, 1, 1, 1), 0x2u, 0x1u),
    };
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);

    // Frame 2: change mask so they no longer match → Exit
    frame.world[1].collides_with_mask = 0x4u;
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Exit)
        << "Mask change while still overlapping spatially must produce Exit";
}

TEST(CollisionFrameAdversarial, ManyBodyAllOverlappingProducesCorrectPairCount)
{
    CollisionFrameStorage frame{};
    constexpr uint32_t N = 10;
    for (uint32_t i = 0; i < N; ++i) {
        frame.world.push_back(entry(i + 1u, bounds(0, 0, 0, 1, 1, 1)));
    }

    broadphase_aabb_overlap(frame.world, frame.current_pairs);

    const std::size_t expected_pairs = (N * (N - 1)) / 2;
    EXPECT_EQ(frame.current_pairs.size(), expected_pairs)
        << "N fully-overlapping entries should produce N*(N-1)/2 pairs";

    for (const auto& pair : frame.current_pairs) {
        EXPECT_TRUE(collision_pair_valid(pair));
        EXPECT_LT(pair.a, pair.b) << "Pairs must be normalized (a < b)";
    }

    for (std::size_t i = 1; i < frame.current_pairs.size(); ++i) {
        EXPECT_LT(frame.current_pairs[i - 1], frame.current_pairs[i])
            << "Pairs must be strictly sorted";
    }
}

TEST(CollisionFrameAdversarial, StalePrevPairsProduceExitForVanishedEntities)
{
    CollisionFrameStorage frame{};

    // Prev frame had pairs with entities 10 and 20
    frame.prev_pairs = {
        make_collision_pair(10u, 20u),
        make_collision_pair(10u, 30u),
    };

    // Current world has completely different entities
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(5, 0, 0, 6, 1, 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    EXPECT_TRUE(frame.current_pairs.empty());
    ASSERT_EQ(frame.events.size(), 2u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Exit);
    EXPECT_EQ(frame.events[0].a, 10u);
    EXPECT_EQ(frame.events[0].b, 20u);
    EXPECT_EQ(frame.events[1].kind, CollisionEventKind::Exit);
    EXPECT_EQ(frame.events[1].a, 10u);
    EXPECT_EQ(frame.events[1].b, 30u);
}

TEST(CollisionFrameAdversarial, AllThreeEventKindsInSingleFrame)
{
    CollisionFrameStorage frame{};

    // Prev: (1,2) staying, (3,4) will exit
    frame.prev_pairs = {
        make_collision_pair(1u, 2u),
        make_collision_pair(3u, 4u),
    };

    // Current: (1,2) still overlapping, (3,4) separated, (5,6) new overlap
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0, 0, 0, 1, 1, 1)),
        entry(3u, bounds(10, 0, 0, 11, 1, 1)),
        entry(4u, bounds(20, 0, 0, 21, 1, 1)),
        entry(5u, bounds(30, 0, 0, 31, 1, 1)),
        entry(6u, bounds(30, 0, 0, 31, 1, 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    ASSERT_EQ(frame.events.size(), 3u);
    EXPECT_EQ(frame.events[0].a, 1u);
    EXPECT_EQ(frame.events[0].b, 2u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Stay);
    EXPECT_EQ(frame.events[1].a, 3u);
    EXPECT_EQ(frame.events[1].b, 4u);
    EXPECT_EQ(frame.events[1].kind, CollisionEventKind::Exit);
    EXPECT_EQ(frame.events[2].a, 5u);
    EXPECT_EQ(frame.events[2].b, 6u);
    EXPECT_EQ(frame.events[2].kind, CollisionEventKind::Enter);
}

TEST(CollisionFrameAdversarial, ReversedWorldInsertionOrderProducesSamePairs)
{
    CollisionFrameStorage frame_a{};
    frame_a.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0, 0, 0, 1, 1, 1)),
        entry(3u, bounds(0, 0, 0, 1, 1, 1)),
    };

    CollisionFrameStorage frame_b{};
    frame_b.world = {
        entry(3u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0, 0, 0, 1, 1, 1)),
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
    };

    broadphase_aabb_overlap(frame_a.world, frame_a.current_pairs);
    broadphase_aabb_overlap(frame_b.world, frame_b.current_pairs);

    ASSERT_EQ(frame_a.current_pairs.size(), frame_b.current_pairs.size());
    for (std::size_t i = 0; i < frame_a.current_pairs.size(); ++i) {
        EXPECT_EQ(frame_a.current_pairs[i], frame_b.current_pairs[i])
            << "Pair ordering must be deterministic regardless of world "
               "insertion order";
    }
}

TEST(CollisionFrameAdversarial, BothTriggersOverlapReportsEvent)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1), 1, 0xffffffffu, true, true),
        entry(2u, bounds(0, 0, 0, 1, 1, 1), 1, 0xffffffffu, true, true),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    ASSERT_EQ(frame.events.size(), 1u)
        << "Two triggers overlapping should still report an event";
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);
}

TEST(CollisionFrameAdversarial, ConsecutiveEmptyFramesClearState)
{
    CollisionFrameStorage frame{};

    // Start with an overlap
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0, 0, 0, 1, 1, 1)),
    };
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);

    // Completely empty world
    frame.world.clear();
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Exit);

    // Another empty frame — should be completely silent
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);
    EXPECT_TRUE(frame.events.empty())
        << "Second empty frame after Exit should produce no events";
    EXPECT_TRUE(frame.prev_pairs.empty());
}

TEST(CollisionFrameAdversarial, PairSwapProducesCorrectExitAndEnter)
{
    CollisionFrameStorage frame{};

    // Frame 1: (1,2) overlap
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0, 0, 0, 1, 1, 1)),
        entry(3u, bounds(10, 0, 0, 11, 1, 1)),
    };
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);
    EXPECT_EQ(frame.events[0].a, 1u);
    EXPECT_EQ(frame.events[0].b, 2u);

    // Frame 2: entity 2 teleports away, entity 3 teleports in
    // (1,2) should Exit, (1,3) should Enter
    frame.world[1].world_bounds = bounds(10, 0, 0, 11, 1, 1);
    frame.world[2].world_bounds = bounds(0, 0, 0, 1, 1, 1);
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    ASSERT_EQ(frame.events.size(), 2u);
    EXPECT_EQ(frame.events[0].a, 1u);
    EXPECT_EQ(frame.events[0].b, 2u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Exit);
    EXPECT_EQ(frame.events[1].a, 1u);
    EXPECT_EQ(frame.events[1].b, 3u);
    EXPECT_EQ(frame.events[1].kind, CollisionEventKind::Enter);
}

TEST(CollisionFrameAdversarial, AxisAlignedNearMissOnEachAxis)
{
    const float eps = 1e-4f;
    CollisionFrameStorage frame{};

    // Separated by epsilon on X only
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(1 + eps, 0, 0, 2 + eps, 1, 1)),
    };
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    EXPECT_TRUE(frame.current_pairs.empty()) << "X-axis near miss should not overlap";

    // Separated by epsilon on Y only
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0, 1 + eps, 0, 1, 2 + eps, 1)),
    };
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    EXPECT_TRUE(frame.current_pairs.empty()) << "Y-axis near miss should not overlap";

    // Separated by epsilon on Z only
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0, 0, 1 + eps, 1, 1, 2 + eps)),
    };
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    EXPECT_TRUE(frame.current_pairs.empty()) << "Z-axis near miss should not overlap";
}

TEST(CollisionFrameAdversarial, OverlapOnTwoAxesButNotThirdIsNotCollision)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0.5f, 0.5f, 5, 1.5f, 1.5f, 6)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    EXPECT_TRUE(frame.current_pairs.empty())
        << "Overlap on X and Y but not Z must not report collision";
}

TEST(CollisionFrameAdversarial, LargeEntityIdNearUint32Max)
{
    const uint32_t big_a = 0xFFFFFFFEu;
    const uint32_t big_b = 0xFFFFFFFDu;

    const CollisionPair pair = make_collision_pair(big_a, big_b);
    EXPECT_EQ(pair.a, big_b);
    EXPECT_EQ(pair.b, big_a);
    EXPECT_TRUE(collision_pair_valid(pair));

    CollisionFrameStorage frame{};
    frame.world = {
        entry(big_a, bounds(0, 0, 0, 1, 1, 1)),
        entry(big_b, bounds(0, 0, 0, 1, 1, 1)),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);

    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].a, big_b);
    EXPECT_EQ(frame.events[0].b, big_a);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);
}

TEST(CollisionFrameAdversarial, MultiFrameStabilityUnderNoChange)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1)),
        entry(2u, bounds(0, 0, 0, 1, 1, 1)),
    };

    // Frame 1: Enter
    broadphase_aabb_overlap(frame.world, frame.current_pairs);
    advance_collision_frame(frame);
    ASSERT_EQ(frame.events.size(), 1u);
    EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Enter);

    // Frames 2-10: all Stay, no spurious Enter/Exit
    for (int i = 0; i < 9; ++i) {
        broadphase_aabb_overlap(frame.world, frame.current_pairs);
        advance_collision_frame(frame);
        ASSERT_EQ(frame.events.size(), 1u) << "Frame " << (i + 2);
        EXPECT_EQ(frame.events[0].kind, CollisionEventKind::Stay)
            << "Frame " << (i + 2) << " should be Stay, not a spurious event";
    }
}

TEST(CollisionFrameAdversarial, OneSidedMaskMatchDoesNotCollide)
{
    CollisionFrameStorage frame{};
    frame.world = {
        entry(1u, bounds(0, 0, 0, 1, 1, 1), 0x1u, 0x2u),
        entry(2u, bounds(0, 0, 0, 1, 1, 1), 0x2u, 0x8u),
    };

    broadphase_aabb_overlap(frame.world, frame.current_pairs);

    EXPECT_TRUE(frame.current_pairs.empty())
        << "A sees B (collides_with & B.layer != 0) but B does not see A "
           "(collides_with & A.layer == 0). Bidirectional check must reject.";
}

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

// ─── Adversarial tests: round 2 ─────────────────────────────────────────────

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
