// tests/asset_scene/scene_renderable_binding_roundtrip_tests.cpp
//
// Issue #229: the custom-renderable scene ingredients — semantic resource
// bindings + per-instance constant overrides on a scene node. Locks the
// authored-intent persistence contract: parse -> export -> parse preserves
// the bindings' semantic + asset-graph anchor and the constants' name +
// value, only the authored intent is written (a bridged resolved key never
// exports), and both ingredients feed the scene fingerprint.

#include "scene_asset_module_test_support.h"

namespace wz::engine::assets::test {

    using namespace wz::engine::assets;

    TEST(SceneAssetModule, RenderableBindingsAndConstantsRoundTripThroughSceneJSON)
    {
        const wz::fs::Path root =
            wz::fs::join(wz::fs::temp_directory_path(),
                         "wozzits_scene_renderable_binding_test");
        ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

        wz::Logger logger;
        wz::gpu::Device device{};
        EngineAssetLibrary assets{ device, logger, root };

        SceneAssetData authored{};
        authored.name = "renderable_binding_scene";
        SceneNodeAsset node{};
        node.id = "bound";
        node.geometry_asset_node_id = 7u;
        node.render_program_node_id = 9u;
        node.renderable_bindings.push_back(SceneRenderableSemanticBinding{
            .semantic = "scalar_field_texture",
            .asset_graph_node_id = 17u,
            // A bridged resolved key: authored-vs-resolved identity says it
            // must NOT export (re-derived on every bind).
            .asset = wz::asset::AssetKey{
                .content_hash = { 0x11ull, 0x22ull },
                .schema_hash = { 0x33ull, 0x44ull },
                .compiler_hash = { 0x55ull, 0x66ull },
                .deps_hash = { 0x77ull, 0x88ull },
            },
        });
        node.renderable_bindings.push_back(SceneRenderableSemanticBinding{
            .semantic = "splat_cloud",
            // No anchor: a half-authored row (semantic only) still persists.
        });
        node.renderable_constants.push_back(SceneRenderableConstantOverride{
            .name = "tint",
            .value = { 0.25f, 0.5f, 0.75f, 1.0f },
        });
        authored.nodes.push_back(std::move(node));

        const std::string exported =
            wz::json::serialize_json(export_scene_to_json_document(authored));
        EXPECT_NE(exported.find("\"renderable_bindings\""), std::string::npos);
        EXPECT_NE(exported.find("\"scalar_field_texture\""), std::string::npos);
        EXPECT_NE(exported.find("\"splat_cloud\""), std::string::npos);
        EXPECT_NE(exported.find("\"renderable_constants\""), std::string::npos);
        EXPECT_NE(exported.find("\"tint\""), std::string::npos);
        // Only the authored intent exports — never the bridged resolved key.
        EXPECT_EQ(exported.find("asset-key:"), std::string::npos);

        // Re-parse through the scene asset path.
        auto rel_path = write_scene_json(
            root, "renderable_binding.scene.json", exported);
        const auto scene_asset =
            assets.scenes().create_scene_from_json({
                .name = "renderable_binding",
                .path = rel_path,
            });
        ASSERT_TRUE(scene_asset.valid());
        ASSERT_TRUE(assets.commit());
        ASSERT_TRUE(assets.resolve_all().ok());

        const auto* scene_data = assets.scenes().get_scene_data(
            assets.scenes().get_scene(scene_asset));
        ASSERT_NE(scene_data, nullptr);
        ASSERT_EQ(scene_data->nodes.size(), 1u);
        const SceneNodeAsset& parsed = scene_data->nodes[0];

        ASSERT_EQ(parsed.renderable_bindings.size(), 2u);
        EXPECT_EQ(parsed.renderable_bindings[0].semantic,
                  "scalar_field_texture");
        ASSERT_TRUE(
            parsed.renderable_bindings[0].asset_graph_node_id.has_value());
        EXPECT_EQ(*parsed.renderable_bindings[0].asset_graph_node_id, 17u);
        // The resolved key did not ride through the JSON.
        EXPECT_TRUE(
            parsed.renderable_bindings[0].asset == wz::asset::AssetKey{});
        EXPECT_EQ(parsed.renderable_bindings[1].semantic, "splat_cloud");
        EXPECT_FALSE(
            parsed.renderable_bindings[1].asset_graph_node_id.has_value());

        ASSERT_EQ(parsed.renderable_constants.size(), 1u);
        EXPECT_EQ(parsed.renderable_constants[0].name, "tint");
        EXPECT_FLOAT_EQ(parsed.renderable_constants[0].value[0], 0.25f);
        EXPECT_FLOAT_EQ(parsed.renderable_constants[0].value[1], 0.5f);
        EXPECT_FLOAT_EQ(parsed.renderable_constants[0].value[2], 0.75f);
        EXPECT_FLOAT_EQ(parsed.renderable_constants[0].value[3], 1.0f);
    }

    TEST(SceneAssetModule, RenderableBindingsAndConstantsFeedTheFingerprint)
    {
        SceneAssetData scene{};
        scene.name = "fingerprint_scene";
        SceneNodeAsset node{};
        node.id = "bound";
        scene.nodes.push_back(std::move(node));

        const uint64_t bare = scene_asset_fingerprint(scene);

        scene.nodes[0].renderable_bindings.push_back(
            SceneRenderableSemanticBinding{
                .semantic = "scalar_field_texture",
                .asset_graph_node_id = 17u,
            });
        const uint64_t with_binding = scene_asset_fingerprint(scene);
        EXPECT_NE(bare, with_binding);

        scene.nodes[0].renderable_constants.push_back(
            SceneRenderableConstantOverride{
                .name = "tint",
                .value = { 0.2f, 0.7f, 0.3f, 1.0f },
            });
        const uint64_t with_constant = scene_asset_fingerprint(scene);
        EXPECT_NE(with_binding, with_constant);

        // A value edit alone re-fingerprints (authored look data).
        scene.nodes[0].renderable_constants[0].value[0] = 0.9f;
        EXPECT_NE(with_constant, scene_asset_fingerprint(scene));

        // The bridged resolved key is NOT authored state: it must not move
        // the fingerprint (it is cleared + re-derived on every bind).
        const uint64_t before_key = scene_asset_fingerprint(scene);
        scene.nodes[0].renderable_bindings[0].asset = wz::asset::AssetKey{
            .content_hash = { 0x1ull, 0x2ull },
            .schema_hash = { 0x3ull, 0x4ull },
            .compiler_hash = { 0x5ull, 0x6ull },
            .deps_hash = { 0x7ull, 0x8ull },
        };
        EXPECT_EQ(before_key, scene_asset_fingerprint(scene));
    }

} // namespace wz::engine::assets::test
