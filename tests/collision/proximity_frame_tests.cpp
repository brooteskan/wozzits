#include "collision_frame_test_support.h"

TEST(ProximityFrameCore, RadiusOverlapProducesDeterministicEnterStayExit)
{
    CollisionFrameStorage frame{};
    frame.proximity_world = {
        ProximityWorldEntry{
            .entity = 1u,
            .center = { 0.0f, 0.0f, 0.0f },
            .radius = 2.0f,
        },
        ProximityWorldEntry{
            .entity = 2u,
            .center = { 3.0f, 0.0f, 0.0f },
            .radius = 2.0f,
        },
    };

    broadphase_proximity_overlap(
        frame.proximity_world,
        frame.proximity_current_pairs,
        frame.proximity_pairs_tested,
        frame.proximity_pairs_rejected,
        frame.proximity_pairs_matched);
    advance_proximity_frame(frame);

    EXPECT_EQ(frame.proximity_pairs_tested, 1u);
    EXPECT_EQ(frame.proximity_pairs_rejected, 0u);
    EXPECT_EQ(frame.proximity_pairs_matched, 1u);
    ASSERT_EQ(frame.proximity_events.size(), 1u);
    EXPECT_EQ(frame.proximity_events[0].kind, ProximityEventKind::Enter);
    ASSERT_EQ(frame.proximity_entity_events.size(), 2u);
    EXPECT_EQ(frame.proximity_entity_events[0].entity, 1u);
    EXPECT_EQ(frame.proximity_entity_events[0].other, 2u);

    broadphase_proximity_overlap(
        frame.proximity_world,
        frame.proximity_current_pairs,
        frame.proximity_pairs_tested,
        frame.proximity_pairs_rejected,
        frame.proximity_pairs_matched);
    advance_proximity_frame(frame);
    ASSERT_EQ(frame.proximity_events.size(), 1u);
    EXPECT_EQ(frame.proximity_events[0].kind, ProximityEventKind::Stay);

    frame.proximity_world[1].center.x = 10.0f;
    broadphase_proximity_overlap(
        frame.proximity_world,
        frame.proximity_current_pairs,
        frame.proximity_pairs_tested,
        frame.proximity_pairs_rejected,
        frame.proximity_pairs_matched);
    advance_proximity_frame(frame);
    EXPECT_EQ(frame.proximity_pairs_tested, 1u);
    EXPECT_EQ(frame.proximity_pairs_rejected, 1u);
    EXPECT_EQ(frame.proximity_pairs_matched, 0u);
    ASSERT_EQ(frame.proximity_events.size(), 1u);
    EXPECT_EQ(frame.proximity_events[0].kind, ProximityEventKind::Exit);
}

TEST(ProximityFrameCore, MaskFilteringAndRoutingUseProximityChannels)
{
    std::vector<ProximityWorldEntry> world{
        ProximityWorldEntry{
            .entity = 1u,
            .center = { 0.0f, 0.0f, 0.0f },
            .radius = 5.0f,
            .layer_mask = 0x1u,
            .detects_with_mask = 0x2u,
        },
        ProximityWorldEntry{
            .entity = 2u,
            .center = { 1.0f, 0.0f, 0.0f },
            .radius = 5.0f,
            .layer_mask = 0x4u,
            .detects_with_mask = 0x1u,
        },
    };
    std::vector<CollisionPair> pairs;
    broadphase_proximity_overlap(world, pairs);
    EXPECT_TRUE(pairs.empty());

    world[1].layer_mask = 0x2u;
    broadphase_proximity_overlap(world, pairs);
    ASSERT_EQ(pairs.size(), 1u);

    std::vector<ProximityEvent> pair_events{
        ProximityEvent{
            .a = 1u,
            .b = 2u,
            .kind = ProximityEventKind::Enter,
        },
    };
    std::vector<ProximityEntityEvent> entity_events;
    fanout_proximity_entity_events(pair_events, entity_events);

    std::vector<wz::engine::assets::SceneComponentRecord<
        wz::engine::assets::EventListenerComponent>> listeners{
        listener(1u, { "proximity.enter" }),
        listener(2u, { "collision.enter", "proximity.*" }),
    };
    std::vector<ProximityEntityEvent> routed;
    route_proximity_entity_events(entity_events, listeners, routed);

    ASSERT_EQ(routed.size(), 2u);
    EXPECT_EQ(routed[0].entity, 1u);
    EXPECT_EQ(routed[1].entity, 2u);
}

