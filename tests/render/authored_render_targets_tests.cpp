// tests/render/authored_render_targets_tests.cpp
//
// Issue #287: the SELECTION half of authored render-to-texture sources -- which
// nodes fill which target, and which the main pass must then skip. Device-free
// on purpose: this is where the behaviour lives (subtree gathering, disabled
// and unresolved sources, two targets disagreeing about one node), and none of
// it needs a GPU to be wrong.

#include <gtest/gtest.h>

#include <engine/rendering/authored_render_targets.h>

#include <string>
#include <vector>

namespace
{
    namespace ea = wz::engine::assets;
    using wz::engine::rendering::collect_authored_render_targets;

    wz::asset::AssetKey texture_key(std::uint64_t tag)
    {
        return wz::asset::AssetKey{
            .content_hash  = { tag, 0 },
            .schema_hash   = { tag, 1 },
            .compiler_hash = { tag, 2 },
            .deps_hash     = { 0, 0 },
        };
    }

    ea::SceneNodeAsset node(
        std::string id, std::optional<std::string> parent = std::nullopt)
    {
        ea::SceneNodeAsset n{};
        n.id = std::move(id);
        n.parent_id = std::move(parent);
        return n;
    }

    ea::SceneRenderToTextureAsset source(
        wz::asset::AssetKey target, bool descendants = true)
    {
        ea::SceneRenderToTextureAsset rtt{};
        rtt.target = target;
        rtt.target_node_id = wz::asset::AssetGraphDraftNodeId{ 1 };
        rtt.include_descendants = descendants;
        return rtt;
    }
}

TEST(AuthoredRenderTargets, SceneWithNoSourceCollectsNothing)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("root"), node("child", "root") };

    const auto collected = collect_authored_render_targets(nodes);

    EXPECT_TRUE(collected.empty());
    ASSERT_EQ(collected.excluded_from_scene.size(), nodes.size());
    for (const bool excluded : collected.excluded_from_scene) {
        EXPECT_FALSE(excluded);
    }
}

TEST(AuthoredRenderTargets, GathersTheSubtreeAndSkipsItInTheMainPass)
{
    std::vector<ea::SceneNodeAsset> nodes{
        node("world"),
        node("card"),                 // the source
        node("art", "card"),          // child
        node("art_detail", "art"),    // grandchild
        node("other", "world"),
    };
    nodes[1].render_to_texture = source(texture_key(0xA));

    const auto collected = collect_authored_render_targets(nodes);

    ASSERT_EQ(collected.targets.size(), 1u);
    EXPECT_TRUE(collected.targets[0].texture == texture_key(0xA));
    EXPECT_EQ(collected.targets[0].source_index, 1u);
    // Source + child + grandchild, in ARRAY order -- the offscreen pass draws
    // them in the same order the main pass would.
    EXPECT_EQ(
        collected.targets[0].node_indices, (std::vector<std::size_t>{ 1, 2, 3 }));

    // ...and exactly those are dropped from the main pass, so art that lives on
    // a surface does not also appear floating in the scene.
    EXPECT_FALSE(collected.excluded_from_scene[0]);
    EXPECT_TRUE(collected.excluded_from_scene[1]);
    EXPECT_TRUE(collected.excluded_from_scene[2]);
    EXPECT_TRUE(collected.excluded_from_scene[3]);
    EXPECT_FALSE(collected.excluded_from_scene[4]);
}

TEST(AuthoredRenderTargets, WithoutDescendantsOnlyTheSourceNodeIsGathered)
{
    std::vector<ea::SceneNodeAsset> nodes{
        node("card"), node("child", "card") };
    nodes[0].render_to_texture = source(texture_key(0xB), /*descendants*/ false);

    const auto collected = collect_authored_render_targets(nodes);

    ASSERT_EQ(collected.targets.size(), 1u);
    EXPECT_EQ(
        collected.targets[0].node_indices, (std::vector<std::size_t>{ 0 }));
    EXPECT_TRUE(collected.excluded_from_scene[0]);
    EXPECT_FALSE(collected.excluded_from_scene[1])
        << "a child outside the source's selection must still draw normally";
}

// The mirror / security-monitor case: the source is world geometry that must
// keep drawing where it is.
TEST(AuthoredRenderTargets, AlsoDrawInSceneKeepsTheNodesInTheMainPass)
{
    std::vector<ea::SceneNodeAsset> nodes{ node("room"), node("prop", "room") };
    nodes[0].render_to_texture = source(texture_key(0xC));
    nodes[0].render_to_texture->also_draw_in_scene = true;

    const auto collected = collect_authored_render_targets(nodes);

    ASSERT_EQ(collected.targets.size(), 1u);
    EXPECT_EQ(
        collected.targets[0].node_indices, (std::vector<std::size_t>{ 0, 1 }));
    EXPECT_FALSE(collected.excluded_from_scene[0]);
    EXPECT_FALSE(collected.excluded_from_scene[1]);
}

// Two targets disagreeing about one node: an explicit ask to see it beats
// another target's silence, whichever order they are authored in.
TEST(AuthoredRenderTargets, AlsoDrawInSceneWinsOverAnotherTargetsExclusion)
{
    // `shared` is inside the excluding source's subtree AND is itself a source
    // that asked to keep drawing. A tree cannot give it two parents, so the
    // overlap is expressed the way it actually arises: a nested source.
    std::vector<ea::SceneNodeAsset> nodes{
        node("hider"),
        node("shared", "hider"),
    };
    nodes[0].render_to_texture = source(texture_key(0xD));          // excludes
    nodes[1].render_to_texture = source(texture_key(0xE), false);
    nodes[1].render_to_texture->also_draw_in_scene = true;

    const auto collected = collect_authored_render_targets(nodes);

    ASSERT_EQ(collected.targets.size(), 2u);
    EXPECT_TRUE(collected.excluded_from_scene[0]);
    EXPECT_FALSE(collected.excluded_from_scene[1])
        << "the source that asked to also draw in scene must still draw";
}

TEST(AuthoredRenderTargets, DisabledOrUnresolvedSourcesContributeNothing)
{
    std::vector<ea::SceneNodeAsset> nodes{
        node("disabled"), node("unresolved"), node("ok") };
    nodes[0].render_to_texture = source(texture_key(0xF));
    nodes[0].render_to_texture->enabled = false;
    nodes[1].render_to_texture = source(wz::asset::AssetKey{});  // never bridged
    nodes[2].render_to_texture = source(texture_key(0x10));

    const auto collected = collect_authored_render_targets(nodes);

    ASSERT_EQ(collected.targets.size(), 1u);
    EXPECT_EQ(collected.targets[0].source_index, 2u);

    // Neither degenerate source may quietly remove its node from the scene:
    // that would turn "my target is broken" into "my object vanished".
    EXPECT_FALSE(collected.excluded_from_scene[0]);
    EXPECT_FALSE(collected.excluded_from_scene[1]);
    EXPECT_TRUE(collected.excluded_from_scene[2]);
}

// A parent chain that loops must not hang the frame.
TEST(AuthoredRenderTargets, CyclicParentChainTerminates)
{
    std::vector<ea::SceneNodeAsset> nodes{
        node("a", "b"), node("b", "a"), node("target") };
    nodes[2].render_to_texture = source(texture_key(0x11));

    const auto collected = collect_authored_render_targets(nodes);

    ASSERT_EQ(collected.targets.size(), 1u);
    EXPECT_EQ(
        collected.targets[0].node_indices, (std::vector<std::size_t>{ 2 }));
}
