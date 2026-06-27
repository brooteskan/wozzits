// tests/render/scene_node_world_tests.cpp
//
// Unit coverage for compute_scene_node_world_transforms (the RHI render path's
// hierarchical transform composition). Pure CPU: no device. Verifies children
// inherit parent translation + scale, the result is independent of node order,
// chains compose fully, and dangling / cyclic parents resolve safely.

#include <engine/rendering/rhi_scene_renderer.h>

#include <engine/assets/scene/scene_asset_data.h>

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace
{
    namespace ea = wz::engine::assets;
    namespace er = wz::engine::rendering;

    ea::SceneNodeAsset node(
        std::string id,
        std::optional<std::string> parent,
        float tx,
        float ty,
        float tz,
        float scale = 1.0f)
    {
        ea::SceneNodeAsset n{};
        n.id = std::move(id);
        n.parent_id = std::move(parent);
        n.local.translation[0] = tx;
        n.local.translation[1] = ty;
        n.local.translation[2] = tz;
        n.local.scale[0] = scale;
        n.local.scale[1] = scale;
        n.local.scale[2] = scale;
        return n;  // identity rotation (AuthoredTransform default quat)
    }
}

TEST(SceneNodeWorld, RootUsesItsOwnLocal)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("a", std::nullopt, 5.0f, 6.0f, 7.0f),
    };
    const auto world = er::compute_scene_node_world_transforms(nodes);
    ASSERT_EQ(world.size(), 1u);
    EXPECT_FLOAT_EQ(world[0].m[12], 5.0f);
    EXPECT_FLOAT_EQ(world[0].m[13], 6.0f);
    EXPECT_FLOAT_EQ(world[0].m[14], 7.0f);
}

TEST(SceneNodeWorld, ChildInheritsParentTranslation)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("p", std::nullopt, 10.0f, 0.0f, 0.0f),
        node("c", "p", 1.0f, 0.0f, 0.0f),
    };
    const auto world = er::compute_scene_node_world_transforms(nodes);
    EXPECT_FLOAT_EQ(world[1].m[12], 11.0f);  // 10 (parent) + 1 (child)
}

TEST(SceneNodeWorld, ChildInheritsParentScale)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("p", std::nullopt, 0.0f, 0.0f, 0.0f, 2.0f),  // 2x scale
        node("c", "p", 1.0f, 0.0f, 0.0f),                 // local +1 in x
    };
    const auto world = er::compute_scene_node_world_transforms(nodes);
    EXPECT_FLOAT_EQ(world[1].m[0], 2.0f);    // world scale = parent scale
    EXPECT_FLOAT_EQ(world[1].m[12], 2.0f);   // child offset scaled by parent: 2*1
}

TEST(SceneNodeWorld, OrderIndependentChildBeforeParent)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("c", "p", 1.0f, 0.0f, 0.0f),
        node("p", std::nullopt, 10.0f, 0.0f, 0.0f),
    };
    const auto world = er::compute_scene_node_world_transforms(nodes);
    EXPECT_FLOAT_EQ(world[0].m[12], 11.0f);  // child (index 0) still composes
}

TEST(SceneNodeWorld, GrandchildComposesWholeChain)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("a", std::nullopt, 100.0f, 0.0f, 0.0f),
        node("b", "a", 10.0f, 0.0f, 0.0f),
        node("c", "b", 1.0f, 0.0f, 0.0f),
    };
    const auto world = er::compute_scene_node_world_transforms(nodes);
    EXPECT_FLOAT_EQ(world[2].m[12], 111.0f);
}

TEST(SceneNodeWorld, DanglingParentFallsBackToLocal)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("c", "missing", 3.0f, 0.0f, 0.0f),
    };
    const auto world = er::compute_scene_node_world_transforms(nodes);
    ASSERT_EQ(world.size(), 1u);
    EXPECT_FLOAT_EQ(world[0].m[12], 3.0f);
}

TEST(SceneNodeWorld, ParentCycleTerminates)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("a", "b", 1.0f, 0.0f, 0.0f),
        node("b", "a", 2.0f, 0.0f, 0.0f),
    };
    // Must not hang or crash; values are cycle-broken but finite.
    const auto world = er::compute_scene_node_world_transforms(nodes);
    ASSERT_EQ(world.size(), 2u);
}

TEST(SceneNodeWorld, EmptySceneYieldsEmpty)
{
    const std::vector<ea::SceneNodeAsset> nodes;
    const auto world = er::compute_scene_node_world_transforms(nodes);
    EXPECT_TRUE(world.empty());
}

// ── Inherited visibility (compute_scene_node_effective_visibility) ──────────

TEST(SceneNodeVisibility, HiddenParentHidesChildSubtree)
{
    std::vector<ea::SceneNodeAsset> nodes{
        node("host", std::nullopt, 0.0f, 0.0f, 0.0f),
        node("host/body", "host", 0.0f, 0.0f, 0.0f),
        node("host/body/turret", "host/body", 0.0f, 0.0f, 0.0f),
    };
    nodes[0].visible = false;  // hide the host
    const auto vis = er::compute_scene_node_effective_visibility(nodes);
    ASSERT_EQ(vis.size(), 3u);
    EXPECT_EQ(vis[0], 0u);  // host hidden
    EXPECT_EQ(vis[1], 0u);  // child inherits hidden
    EXPECT_EQ(vis[2], 0u);  // grandchild inherits hidden
}

TEST(SceneNodeVisibility, VisibleParentLeavesChildrenVisible)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("host", std::nullopt, 0.0f, 0.0f, 0.0f),
        node("host/body", "host", 0.0f, 0.0f, 0.0f),
    };
    const auto vis = er::compute_scene_node_effective_visibility(nodes);
    EXPECT_EQ(vis[0], 1u);
    EXPECT_EQ(vis[1], 1u);
}

TEST(SceneNodeVisibility, OwnHiddenDoesNotHideSiblings)
{
    std::vector<ea::SceneNodeAsset> nodes{
        node("root", std::nullopt, 0.0f, 0.0f, 0.0f),
        node("a", "root", 0.0f, 0.0f, 0.0f),
        node("b", "root", 0.0f, 0.0f, 0.0f),
    };
    nodes[1].visible = false;  // hide 'a' only
    const auto vis = er::compute_scene_node_effective_visibility(nodes);
    EXPECT_EQ(vis[0], 1u);  // root visible
    EXPECT_EQ(vis[1], 0u);  // a hidden
    EXPECT_EQ(vis[2], 1u);  // sibling b unaffected
}

TEST(SceneNodeVisibility, OrderIndependentChildBeforeHiddenParent)
{
    std::vector<ea::SceneNodeAsset> nodes{
        node("c", "p", 0.0f, 0.0f, 0.0f),
        node("p", std::nullopt, 0.0f, 0.0f, 0.0f),
    };
    nodes[1].visible = false;  // hide parent (listed after the child)
    const auto vis = er::compute_scene_node_effective_visibility(nodes);
    EXPECT_EQ(vis[0], 0u);  // child still inherits the hidden parent
    EXPECT_EQ(vis[1], 0u);
}

TEST(SceneNodeVisibility, DanglingParentUsesOwnVisibility)
{
    std::vector<ea::SceneNodeAsset> nodes{
        node("c", "missing", 0.0f, 0.0f, 0.0f),
    };
    nodes[0].visible = true;
    const auto vis = er::compute_scene_node_effective_visibility(nodes);
    ASSERT_EQ(vis.size(), 1u);
    EXPECT_EQ(vis[0], 1u);  // no usable parent -> own visibility wins
}

TEST(SceneNodeVisibility, ParentCycleTerminates)
{
    std::vector<ea::SceneNodeAsset> nodes{
        node("a", "b", 0.0f, 0.0f, 0.0f),
        node("b", "a", 0.0f, 0.0f, 0.0f),
    };
    nodes[0].visible = false;
    const auto vis = er::compute_scene_node_effective_visibility(nodes);
    ASSERT_EQ(vis.size(), 2u);  // must not hang
}
