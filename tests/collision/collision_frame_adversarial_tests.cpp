#include "collision_frame_test_support.h"

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

