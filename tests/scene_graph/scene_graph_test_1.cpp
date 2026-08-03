#include <gtest/gtest.h>
#include <scene/scene_graph.h>
#include <math/mat4.h>

using namespace wz::scene;
using namespace wz::core::graph;
using namespace wz::math;

// ─── Fixtures ─────────────────────────────────────────────────────────────────
//
// Scene hierarchy:
//
//   root (static)
//   ├── child_a (static)
//   │   └── grandchild (static)
//   └── child_b (animated)
//
// Transforms are simple translations along X for easy mental arithmetic:
//   root:       x=1
//   child_a:    x=2  → world x=3
//   grandchild: x=3  → world x=6
//   child_b:    x=4  → world x=5

namespace {

    Mat4 translation(float x)
    {
        Mat4 m = Mat4::identity();
        m.m[12] = x; // column 3, row 0 → index 12
        return m;
    }
    
    float world_x(const SceneGraph& g, NodeHandle n)
    {
        return node_data(g, n).world.m[12];
    }

    SceneStorage make_scene()
    {
        SceneBuilder b;

        TransformNode root_node;
        root_node.local = translation(1.f);
        root_node.motion_type = TransformNode::MotionType::Static;

        TransformNode child_a_node;
        child_a_node.local = translation(2.f);
        child_a_node.motion_type = TransformNode::MotionType::Static;

        TransformNode grandchild_node;
        grandchild_node.local = translation(3.f);
        grandchild_node.motion_type = TransformNode::MotionType::Static;

        TransformNode child_b_node;
        child_b_node.local = translation(4.f);
        child_b_node.motion_type = TransformNode::MotionType::Animated;

        auto root = add_node(b, root_node);       // 0
        auto child_a = add_node(b, child_a_node);     // 1
        auto grandchild = add_node(b, grandchild_node);  // 2
        auto child_b = add_node(b, child_b_node);     // 3

        add_edge(b, root, child_a);
        add_edge(b, root, child_b);
        add_edge(b, child_a, grandchild);

        auto result = build(std::move(b));
        assert(result.has_value());
        return std::move(*result);
    }

} // namespace


// ─── Propagation ──────────────────────────────────────────────────────────────

TEST(SceneGraphSpec, PropagateAllRootWorldEqualsLocal)
{
    auto storage = make_scene();
    propagate_all(storage.polytree);

    EXPECT_FLOAT_EQ(world_x(storage.polytree, 0u), 1.f);
}

TEST(SceneGraphSpec, PropagateAllChildAccumulatesParent)
{
    auto storage = make_scene();
    propagate_all(storage.polytree);

    EXPECT_FLOAT_EQ(world_x(storage.polytree, 1u), 3.f); // 1+2
}

TEST(SceneGraphSpec, PropagateAllGrandchildAccumulatesChain)
{
    auto storage = make_scene();
    propagate_all(storage.polytree);

    EXPECT_FLOAT_EQ(world_x(storage.polytree, 2u), 6.f); // 1+2+3
}

TEST(SceneGraphSpec, PropagateAllAnimatedChildAccumulatesParent)
{
    auto storage = make_scene();
    propagate_all(storage.polytree);

    EXPECT_FLOAT_EQ(world_x(storage.polytree, 3u), 5.f); // 1+4
}


// ─── Dirty tracking ───────────────────────────────────────────────────────────

TEST(SceneGraphSpec, AllNodesDirtyAfterBuild)
{
    auto storage = make_scene();
    auto& g = storage.polytree;

    for (uint32_t i = 0; i < node_count(g); ++i)
        EXPECT_TRUE(is_dirty(node_data(g, i)));
}

TEST(SceneGraphSpec, SetLocalMarksDirty)
{
    auto storage = make_scene();
    auto& g = storage.polytree;

    // Propagate to clean all nodes
    propagate_all(g);
    const uint32_t frame = 1u;
    for (uint32_t i = 0; i < node_count(g); ++i)
        const_cast<TransformNode&>(node_data(g, i)).last_updated_frame = frame;

    // Now dirty one node
    set_local(g, 1u, translation(10.f));

    EXPECT_TRUE(is_dirty(node_data(g, 1u)));
    EXPECT_FALSE(is_dirty(node_data(g, 0u))); // root still clean
}


// ─── Collect dirty roots ──────────────────────────────────────────────────────

TEST(SceneGraphSpec, AllRootsCollectedWhenAllDirty)
{
    auto storage = make_scene();
    auto& g = storage.polytree;
    const uint32_t frame = 1u;

    NodeHandle scratch[16];
    auto roots = collect_dirty_roots(g, frame, scratch);

    // Only the scene root (0) is a dirty root —
    // children are dirty too but their parent is also dirty
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0], 0u);
}

TEST(SceneGraphSpec, DirtyRootCollectedAfterSetLocal)
{
    auto storage = make_scene();
    auto& g = storage.polytree;
    const uint32_t frame = 1u;

    // Clean everything
    for (uint32_t i = 0; i < node_count(g); ++i)
        const_cast<TransformNode&>(node_data(g, i)).last_updated_frame = frame;

    // Dirty child_a (1) only
    set_local(g, 1u, translation(5.f));

    NodeHandle scratch[16];
    auto roots = collect_dirty_roots(g, frame, scratch);

    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0], 1u); // child_a is dirty root, root(0) is clean
}


// ─── Static update ────────────────────────────────────────────────────────────

TEST(SceneGraphSpec, StaticUpdatePropagatesFromDirtyRoot)
{
    auto storage = make_scene();
    auto& g = storage.polytree;
    const uint32_t frame = 1u;

    NodeHandle scratch[16];
    auto roots = collect_dirty_roots(g, frame, scratch);
    update_static(g, roots, frame);

    EXPECT_FLOAT_EQ(world_x(g, 0u), 1.f);
    EXPECT_FLOAT_EQ(world_x(g, 1u), 3.f);
    EXPECT_FLOAT_EQ(world_x(g, 2u), 6.f);
    EXPECT_FLOAT_EQ(world_x(g, 3u), 5.f);
}

TEST(SceneGraphSpec, StaticUpdateMarksNodesClean)
{
    auto storage = make_scene();
    auto& g = storage.polytree;
    const uint32_t frame = 1u;

    NodeHandle scratch[16];
    auto roots = collect_dirty_roots(g, frame, scratch);
    update_static(g, roots, frame);

    for (uint32_t i = 0; i < node_count(g); ++i)
        EXPECT_FALSE(is_dirty(node_data(g, i)));
}

TEST(SceneGraphSpec, StaticUpdatePartialDirtyOnlyUpdatesSubtree)
{
    auto storage = make_scene();
    auto& g = storage.polytree;
    const uint32_t frame = 1u;

    // First full update to clean everything
    NodeHandle scratch[16];
    auto roots = collect_dirty_roots(g, frame, scratch);
    update_static(g, roots, frame);

    // Dirty child_a (1) with a new local
    const uint32_t frame2 = 2u;
    set_local(g, 1u, translation(10.f));

    auto roots2 = collect_dirty_roots(g, frame2, scratch);
    ASSERT_EQ(roots2.size(), 1u);
    EXPECT_EQ(roots2[0], 1u);

    update_static(g, roots2, frame2);

    // root unchanged
    EXPECT_FLOAT_EQ(world_x(g, 0u), 1.f);
    // child_a updated: 1+10=11
    EXPECT_FLOAT_EQ(world_x(g, 1u), 11.f);
    // grandchild updated: 1+10+3=14
    EXPECT_FLOAT_EQ(world_x(g, 2u), 14.f);
    // child_b untouched — different subtree
    EXPECT_FLOAT_EQ(world_x(g, 3u), 5.f);
}


// ─── Animated update ──────────────────────────────────────────────────────────

TEST(SceneGraphSpec, AnimatedListContainsOnlyAnimatedNodes)
{
    auto storage = make_scene();
    auto& g = storage.polytree;

    NodeHandle scratch[16];
    auto list = build_animated_list(g, scratch);

    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0], 3u); // child_b is the only animated node
}

TEST(SceneGraphSpec, AnimatedUpdatePropagatesCorrectly)
{
    auto storage = make_scene();
    auto& g = storage.polytree;
    const uint32_t frame = 1u;

    // First propagate all so parent world transforms are valid
    propagate_all(g);

    // Change child_b local
    set_local(g, 3u, translation(6.f));

    NodeHandle scratch[16];
    auto list = build_animated_list(g, scratch);
    update_animated(g, list, frame);

    // root world unchanged
    EXPECT_FLOAT_EQ(world_x(g, 0u), 1.f);
    // child_b world: 1+6=7
    EXPECT_FLOAT_EQ(world_x(g, 3u), 7.f);
}

TEST(SceneGraphSpec, AnimatedUpdateMarksAnimatedNodesClean)
{
    auto storage = make_scene();
    auto& g = storage.polytree;
    const uint32_t frame = 1u;

    propagate_all(g);

    NodeHandle scratch[16];
    auto list = build_animated_list(g, scratch);
    update_animated(g, list, frame);

    EXPECT_FALSE(is_dirty(node_data(g, 3u)));
}