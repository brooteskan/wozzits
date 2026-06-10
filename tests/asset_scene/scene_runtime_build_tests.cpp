// tests/asset_scene/scene_runtime_build_tests.cpp
//
// build_scene_runtime_from_asset_snapshot composition: the pieces
// (fingerprint, instantiate, propagate) are tested individually elsewhere;
// these tests verify the composed authored-to-runtime boundary.

#include <gtest/gtest.h>

#include <engine/assets/scene/scene_runtime_build.h>

#include <scene/scene_graph.h>

namespace
{
    wz::engine::assets::SceneAssetData make_parent_child_scene()
    {
        using namespace wz::engine::assets;

        SceneAssetData scene{};
        scene.name = "runtime_build_scene";

        SceneNodeAsset parent{};
        parent.id = "parent";
        parent.local.translation[0] = 2.0f;
        parent.local.translation[2] = 5.0f;
        scene.nodes.push_back(std::move(parent));

        SceneNodeAsset child{};
        child.id = "child";
        child.parent_id = "parent";
        child.local.translation[1] = 3.0f;
        scene.nodes.push_back(std::move(child));

        return scene;
    }
}

TEST(SceneRuntimeBuild, BuildsValidRuntimeFromAuthoredSnapshot)
{
    using namespace wz::engine::assets;

    const SceneAssetData authored = make_parent_child_scene();
    const SceneAssetRuntimeBuild build =
        build_scene_runtime_from_asset_snapshot(authored);

    ASSERT_TRUE(build.valid) << build.status;
    EXPECT_EQ(build.snapshot.name, authored.name);
    EXPECT_EQ(build.snapshot.nodes.size(), authored.nodes.size());
    EXPECT_NE(build.status.find("runtime scene ready"), std::string::npos);

    ASSERT_TRUE(build.instance.authored_to_runtime.contains("parent"));
    ASSERT_TRUE(build.instance.authored_to_runtime.contains("child"));
}

TEST(SceneRuntimeBuild, FingerprintMatchesSnapshot)
{
    using namespace wz::engine::assets;

    const SceneAssetData authored = make_parent_child_scene();
    const SceneAssetRuntimeBuild build =
        build_scene_runtime_from_asset_snapshot(authored);

    ASSERT_TRUE(build.valid) << build.status;
    EXPECT_EQ(build.scene_hash, scene_asset_fingerprint(build.snapshot));
    EXPECT_EQ(
        build.scene_hash_text,
        scene_asset_fingerprint_string(build.snapshot));
    EXPECT_NE(build.status.find(build.scene_hash_text), std::string::npos);
}

TEST(SceneRuntimeBuild, PropagatesWorldTransforms)
{
    using namespace wz::engine::assets;

    const SceneAssetData authored = make_parent_child_scene();
    const SceneAssetRuntimeBuild build =
        build_scene_runtime_from_asset_snapshot(authored);

    ASSERT_TRUE(build.valid) << build.status;

    const auto child_handle =
        build.instance.authored_to_runtime.at("child");
    const auto& child_world = wz::core::graph::node_data(
        build.instance.storage.polytree, child_handle).world;

    // Child local (0,3,0) under parent (2,0,5): the composed world
    // translation proves propagate_all() ran as part of the build.
    EXPECT_FLOAT_EQ(child_world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(child_world.m[13], 3.0f);
    EXPECT_FLOAT_EQ(child_world.m[14], 5.0f);
}

TEST(SceneRuntimeBuild, ReportsInstantiateFailure)
{
    using namespace wz::engine::assets;

    SceneAssetData authored{};
    authored.name = "runtime_build_failure";

    // A renderable reference without a resolver in the context makes
    // instantiate_scene fail; the build must surface that instead of
    // returning a half-initialized runtime.
    wz::asset::AssetKey missing_renderable{};
    missing_renderable.content_hash = { 0xABCDu, 0u };

    SceneNodeAsset node{};
    node.id = "obj";
    node.renderable_asset = missing_renderable;
    authored.nodes.push_back(std::move(node));

    const SceneAssetRuntimeBuild build =
        build_scene_runtime_from_asset_snapshot(authored);

    EXPECT_FALSE(build.valid);
    EXPECT_NE(build.status.find("instantiate failed"), std::string::npos);
}
