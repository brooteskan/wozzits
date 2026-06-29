// tests/asset_scene/scene_audio_source_roundtrip_tests.cpp
//
// Tests for the authored AudioSource scene component (audio-track item 6):
// parse <-> export round-trip, fingerprint sensitivity, and instantiation into
// the SceneInstance runtime table. The component references an audio-renderable
// terminal by AssetKey and carries per-entity play policy.

#include "scene_asset_module_test_support.h"

namespace wz::engine::assets::test {

    using namespace wz::engine::assets;

    namespace {
        // Build a real audio-renderable asset and return its output key, so the
        // AudioSource reference round-trips a genuine asset-key.
        wz::asset::AssetKey make_renderable_key(EngineAssetLibrary& assets)
        {
            const AudioClipAsset clip =
                assets.audio_clips().create_procedural_tone_audio_clip({
                    .name = "audio_source/src",
                    .duration_seconds = 0.01f,
                    });
            const AudioRenderableAsset rend =
                assets.audio_renderables().create_audio_renderable({
                    .clip = clip,
                    .gain = 0.8f,
                    .looping = true,
                    });
            return rend.output;
        }
    }

    TEST(SceneAssetModule, AudioSourceComponentRoundTripsThroughSceneJSON)
    {
        const wz::fs::Path root =
            wz::fs::join(wz::fs::temp_directory_path(),
                         "wozzits_scene_audio_source_test");
        ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

        wz::Logger logger;
        wz::gpu::Device device{};
        EngineAssetLibrary assets{ device, logger, root };

        const wz::asset::AssetKey renderable_key = make_renderable_key(assets);

        SceneAssetData authored{};
        authored.name = "audio_source_scene";
        SceneNodeAsset node{};
        node.id = "speaker";
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable_key,
            .auto_play = false,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(node));

        // Export contains the component + its fields.
        const std::string exported =
            wz::json::serialize_json(export_scene_to_json_document(authored));
        EXPECT_NE(exported.find("\"audio_source\""), std::string::npos);
        EXPECT_NE(exported.find("\"audio_renderable\""), std::string::npos);
        EXPECT_NE(exported.find("\"auto_play\""), std::string::npos);
        EXPECT_NE(exported.find("asset-key:"), std::string::npos);

        // Re-parse through the scene asset path.
        auto rel_path =
            write_scene_json(root, "audio_source.scene.json", exported);
        const auto scene_asset =
            assets.scenes().create_scene_from_json({
                .name = "audio_source",
                .path = rel_path,
                });
        ASSERT_TRUE(scene_asset.valid());
        ASSERT_TRUE(assets.commit());
        ASSERT_TRUE(assets.resolve_all().ok());

        const auto* scene_data = assets.scenes().get_scene_data(
            assets.scenes().get_scene(scene_asset));
        ASSERT_NE(scene_data, nullptr);
        ASSERT_EQ(scene_data->nodes.size(), 1u);
        ASSERT_TRUE(scene_data->nodes[0].audio_source.has_value());
        EXPECT_EQ(scene_data->nodes[0].audio_source->audio_renderable,
                  renderable_key);
        EXPECT_FALSE(scene_data->nodes[0].audio_source->auto_play);
        EXPECT_TRUE(scene_data->nodes[0].audio_source->enabled);
    }

    TEST(SceneAssetModule, AudioSourceNodeIdReferenceRoundTripsAndAnchorsIdentity)
    {
        const wz::fs::Path root =
            wz::fs::join(wz::fs::temp_directory_path(),
                         "wozzits_scene_audio_source_nodeid_test");
        ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

        wz::Logger logger;
        wz::gpu::Device device{};
        EngineAssetLibrary assets{ device, logger, root };

        SceneAssetData authored{};
        authored.name = "audio_source_nodeid_scene";
        SceneNodeAsset node{};
        node.id = "speaker";
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable_node_id = 42u, // stable authored anchor
            .audio_renderable = {},
            .auto_play = true,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(node));

        // Export prefers the stable node id over a resolved key.
        const std::string exported =
            wz::json::serialize_json(export_scene_to_json_document(authored));
        EXPECT_NE(exported.find("\"audio_renderable_node_id\""),
                  std::string::npos);
        EXPECT_EQ(exported.find("asset-key:"), std::string::npos);

        auto rel_path =
            write_scene_json(root, "audio_source_nodeid.scene.json", exported);
        const auto scene_asset =
            assets.scenes().create_scene_from_json({
                .name = "audio_source_nodeid",
                .path = rel_path,
                });
        ASSERT_TRUE(scene_asset.valid());
        ASSERT_TRUE(assets.commit());
        ASSERT_TRUE(assets.resolve_all().ok());

        const auto* scene_data = assets.scenes().get_scene_data(
            assets.scenes().get_scene(scene_asset));
        ASSERT_NE(scene_data, nullptr);
        ASSERT_EQ(scene_data->nodes.size(), 1u);
        ASSERT_TRUE(scene_data->nodes[0].audio_source.has_value());
        ASSERT_TRUE(
            scene_data->nodes[0].audio_source->audio_renderable_node_id
                .has_value());
        EXPECT_EQ(
            *scene_data->nodes[0].audio_source->audio_renderable_node_id, 42u);

        // The stable node id anchors fingerprint identity: changing it shifts the
        // hash, proving the authored reference (not just the transient key) drives
        // identity.
        SceneAssetData fp_scene{};
        SceneNodeAsset fp_node{};
        fp_node.id = "speaker";
        fp_node.audio_source = SceneAudioSourceAsset{
            .audio_renderable_node_id = 42u,
        };
        fp_scene.nodes.push_back(fp_node);
        const uint64_t base = scene_asset_fingerprint(fp_scene);

        fp_scene.nodes[0].audio_source->audio_renderable_node_id = 43u;
        EXPECT_NE(scene_asset_fingerprint(fp_scene), base);
    }

    TEST(SceneAssetModule, AudioSourceInstantiatesIntoRuntimeTable)
    {
        SceneAssetData authored{};
        authored.name = "audio_source_instance";
        SceneNodeAsset node{};
        node.id = "speaker";
        const wz::asset::AssetKey key{
            .content_hash = { 1, 2 },
            .schema_hash = { 3, 4 },
            .compiler_hash = { 5, 6 },
            .deps_hash = { 7, 8 },
        };
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable = key,
            .auto_play = true,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(node));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());
        auto& inst = result.instance;

        ASSERT_TRUE(inst.authored_to_runtime.contains("speaker"));
        const auto h = inst.authored_to_runtime["speaker"];

        ASSERT_EQ(inst.audio_sources.size(), 1u);
        EXPECT_EQ(inst.audio_sources[0].node, h);
        EXPECT_EQ(inst.audio_sources[0].component.audio_renderable, key);
        EXPECT_TRUE(inst.audio_sources[0].component.auto_play);
        EXPECT_TRUE(inst.audio_sources[0].component.enabled);

        const auto summary = summarize_scene_instance_components(inst);
        EXPECT_EQ(summary.audio_sources, 1u);
    }

    TEST(SceneAssetModule, AudioSourceParticipatesInFingerprint)
    {
        SceneAssetData scene{};
        scene.name = "audio_source_fingerprint";
        SceneNodeAsset node{};
        node.id = "speaker";
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable = {},
            .auto_play = true,
            .enabled = true,
        };
        scene.nodes.push_back(std::move(node));

        const uint64_t base = scene_asset_fingerprint(scene);

        scene.nodes[0].audio_source->auto_play = false;
        EXPECT_NE(scene_asset_fingerprint(scene), base);

        scene.nodes[0].audio_source->auto_play = true;
        EXPECT_EQ(scene_asset_fingerprint(scene), base);

        scene.nodes[0].audio_source->audio_renderable =
            wz::asset::AssetKey{ .content_hash = { 99, 0 } };
        EXPECT_NE(scene_asset_fingerprint(scene), base);
    }

} // namespace wz::engine::assets::test
