// tests/asset_scene/scene_audio_authoring_tests.cpp
//
// Unit tests for the AudioSource / AudioListener authoring verbs (audio-track
// item 10, engine side): add/remove the optional components by kind, point the
// AudioSource's renderable reference at a stable asset-graph node id (the editor
// picker target), and set its play policy. These are the engine-thread applies
// the editor ABI calls.

#include <gtest/gtest.h>

#include <engine/assets/scene/scene_asset_data.h>

#include <vector>

namespace wz::engine::assets::test {

    namespace {
        std::vector<SceneNodeAsset> one_node(const char* id)
        {
            std::vector<SceneNodeAsset> nodes;
            SceneNodeAsset n{};
            n.id = id;
            nodes.push_back(std::move(n));
            return nodes;
        }
    }

    TEST(SceneAudioAuthoring, AddRemoveAudioSourceComponentByKind)
    {
        auto nodes = one_node("speaker");

        EXPECT_FALSE(node_has_optional_component(nodes, "speaker", "audio_source"));
        EXPECT_TRUE(add_node_optional_component(nodes, "speaker", "audio_source"));
        EXPECT_TRUE(node_has_optional_component(nodes, "speaker", "audio_source"));
        EXPECT_TRUE(nodes[0].audio_source.has_value());

        EXPECT_TRUE(
            remove_node_optional_component(nodes, "speaker", "audio_source"));
        EXPECT_FALSE(nodes[0].audio_source.has_value());
    }

    TEST(SceneAudioAuthoring, AddRemoveAudioListenerComponentByKind)
    {
        auto nodes = one_node("ear");

        EXPECT_TRUE(add_node_optional_component(nodes, "ear", "audio_listener"));
        EXPECT_TRUE(node_has_optional_component(nodes, "ear", "audio_listener"));
        EXPECT_TRUE(nodes[0].audio_listener.has_value());

        EXPECT_TRUE(
            remove_node_optional_component(nodes, "ear", "audio_listener"));
        EXPECT_FALSE(nodes[0].audio_listener.has_value());
    }

    TEST(SceneAudioAuthoring, UnknownNodeOrKindFailsClosed)
    {
        auto nodes = one_node("speaker");
        EXPECT_FALSE(add_node_optional_component(nodes, "ghost", "audio_source"));
        EXPECT_FALSE(add_node_optional_component(nodes, "speaker", "bogus"));
        EXPECT_FALSE(
            node_has_optional_component(nodes, "speaker", "audio_source"));
    }

    TEST(SceneAudioAuthoring, SetRenderableNodeIdCreatesComponentAndAnchors)
    {
        auto nodes = one_node("speaker");

        // Picking a renderable on a node without the component creates it.
        EXPECT_TRUE(set_node_audio_renderable(nodes, "speaker", 42));
        ASSERT_TRUE(nodes[0].audio_source.has_value());
        ASSERT_TRUE(
            nodes[0].audio_source->audio_renderable_node_id.has_value());
        EXPECT_EQ(*nodes[0].audio_source->audio_renderable_node_id, 42u);
        // Resolved key stays empty until bind resolves the node id.
        EXPECT_EQ(nodes[0].audio_source->audio_renderable,
                  wz::asset::AssetKey{});
    }

    TEST(SceneAudioAuthoring, SetRenderableNodeIdZeroClearsAnchorKeepsComponent)
    {
        auto nodes = one_node("speaker");
        ASSERT_TRUE(set_node_audio_renderable(nodes, "speaker", 42));

        // Also drop a stale resolved key to prove it is cleared on re-pick.
        nodes[0].audio_source->audio_renderable =
            wz::asset::AssetKey{ .content_hash = { 9, 9 } };

        EXPECT_TRUE(set_node_audio_renderable(nodes, "speaker", 0));
        ASSERT_TRUE(nodes[0].audio_source.has_value()); // component remains
        EXPECT_FALSE(
            nodes[0].audio_source->audio_renderable_node_id.has_value());
        EXPECT_EQ(nodes[0].audio_source->audio_renderable,
                  wz::asset::AssetKey{});
    }

    TEST(SceneAudioAuthoring, SetPlayPolicyRequiresComponent)
    {
        auto nodes = one_node("speaker");

        // No component yet -> fails closed.
        EXPECT_FALSE(set_node_audio_source_play(nodes, "speaker", false, false));

        ASSERT_TRUE(add_node_optional_component(nodes, "speaker", "audio_source"));
        EXPECT_TRUE(set_node_audio_source_play(nodes, "speaker", false, true));
        EXPECT_FALSE(nodes[0].audio_source->auto_play);
        EXPECT_TRUE(nodes[0].audio_source->enabled);

        EXPECT_TRUE(set_node_audio_source_play(nodes, "speaker", true, false));
        EXPECT_TRUE(nodes[0].audio_source->auto_play);
        EXPECT_FALSE(nodes[0].audio_source->enabled);
    }

} // namespace wz::engine::assets::test
