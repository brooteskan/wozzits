// tests/asset_scene/scene_runtime_build_tests.cpp
//
// build_scene_runtime_from_asset_snapshot composition: the pieces
// (fingerprint, instantiate, propagate) are tested individually elsewhere;
// these tests verify the composed authored-to-runtime boundary.

#include <gtest/gtest.h>

#include <engine/assets/scene/scene_runtime_build.h>

#include <scene/scene_graph.h>

#include <array>

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
    EXPECT_FALSE(build.ok());
    EXPECT_EQ(
        build.failed_phase,
        SceneRuntimeBuildPhase::Instantiate);
    EXPECT_EQ(build.error.phase, SceneRuntimeBuildPhase::Instantiate);
    EXPECT_EQ(build.error.completed_phase, SceneRuntimeBuildPhase::Snapshot);
    EXPECT_EQ(build.error.message, "instantiate failed");
    EXPECT_TRUE(build.error.any());
    EXPECT_STREQ(
        scene_runtime_build_phase_name(build.error.phase),
        "instantiate");
    EXPECT_EQ(
        build.completed_phase,
        SceneRuntimeBuildPhase::Snapshot);
    EXPECT_NE(build.status.find("instantiate failed"), std::string::npos);
    EXPECT_NE(
        build.error_detail.find("instantiate failed"),
        std::string::npos);
}

TEST(SceneRuntimeBuild, BuildsRenderStorageAndSkyCommands)
{
    using namespace wz::engine::assets;

    SceneRuntimeBuildOptions options{};
    options.initial_view.camera_position = { 1.0f, 2.0f, 3.0f };

    std::array<wz::render::SkyDrawCommand, 1> sky{};
    sky[0].visual_kind = wz::render::SkyVisualKind::SolidColor;
    sky[0].solid_color = { 0.25f, 0.5f, 0.75f };
    options.sky_commands = sky;

    const SceneAssetData authored = make_parent_child_scene();
    const SceneAssetRuntimeBuild build =
        build_scene_runtime_from_asset_snapshot(
            authored,
            {},
            options);

    ASSERT_TRUE(build.ok()) << build.status;
    EXPECT_EQ(
        build.completed_phase,
        SceneRuntimeBuildPhase::BuildRenderFrame);
    EXPECT_EQ(build.failed_phase, SceneRuntimeBuildPhase::None);

    EXPECT_FLOAT_EQ(
        build.compiled_scene.scene.view.camera_position.x,
        1.0f);
    EXPECT_FLOAT_EQ(
        build.render_ir.ir.source.view.camera_position.y,
        2.0f);
    EXPECT_FLOAT_EQ(
        build.render_frame.frame.view.camera_position.z,
        3.0f);

    ASSERT_EQ(build.sky_commands.size(), 1u);
    EXPECT_EQ(
        build.sky_commands[0].visual_kind,
        wz::render::SkyVisualKind::SolidColor);

    ASSERT_EQ(build.render_frame.frame.sky.size(), 1u);
    EXPECT_EQ(
        build.render_frame.frame.sky[0].visual_kind,
        wz::render::SkyVisualKind::SolidColor);
}

TEST(SceneRuntimeBuild, CommitRejectsInvalidCandidateAndPreservesLive)
{
    using namespace wz::engine::assets;

    SceneAssetRuntimeBuild live =
        build_scene_runtime_from_asset_snapshot(make_parent_child_scene());
    ASSERT_TRUE(live.ok()) << live.status;

    const std::string live_name = live.snapshot.name;
    const std::string live_hash_text = live.scene_hash_text;
    const auto live_node_count =
        wz::core::graph::node_count(live.instance.storage.polytree);

    SceneAssetData authored{};
    authored.name = "failed_candidate";

    wz::asset::AssetKey missing_renderable{};
    missing_renderable.content_hash = { 0xABCDu, 0u };

    SceneNodeAsset node{};
    node.id = "obj";
    node.renderable_asset = missing_renderable;
    authored.nodes.push_back(std::move(node));

    SceneAssetRuntimeBuild candidate =
        build_scene_runtime_from_asset_snapshot(authored);
    ASSERT_FALSE(candidate.ok());

    EXPECT_FALSE(
        commit_scene_runtime_build(live, std::move(candidate)));

    EXPECT_TRUE(live.ok());
    EXPECT_EQ(live.snapshot.name, live_name);
    EXPECT_EQ(live.scene_hash_text, live_hash_text);
    EXPECT_EQ(
        wz::core::graph::node_count(live.instance.storage.polytree),
        live_node_count);
    EXPECT_TRUE(live.instance.authored_to_runtime.contains("parent"));
    EXPECT_TRUE(live.instance.authored_to_runtime.contains("child"));
}

TEST(SceneRuntimeBuild, CommitMovesSuccessfulCandidate)
{
    using namespace wz::engine::assets;

    SceneAssetRuntimeBuild live =
        build_scene_runtime_from_asset_snapshot(make_parent_child_scene());
    ASSERT_TRUE(live.ok()) << live.status;

    SceneAssetData replacement = make_parent_child_scene();
    replacement.name = "replacement_scene";
    replacement.nodes[0].id = "replacement_parent";
    replacement.nodes[1].id = "replacement_child";
    replacement.nodes[1].parent_id = "replacement_parent";

    SceneAssetRuntimeBuild candidate =
        build_scene_runtime_from_asset_snapshot(replacement);
    ASSERT_TRUE(candidate.ok()) << candidate.status;

    EXPECT_TRUE(
        commit_scene_runtime_build(live, std::move(candidate)));

    EXPECT_TRUE(live.ok());
    EXPECT_EQ(live.snapshot.name, "replacement_scene");
    EXPECT_FALSE(live.instance.authored_to_runtime.contains("parent"));
    EXPECT_TRUE(
        live.instance.authored_to_runtime.contains("replacement_parent"));
    EXPECT_EQ(
        live.completed_phase,
        SceneRuntimeBuildPhase::BuildRenderFrame);
}

TEST(SceneRuntimeBundle, BuildsCommittedBundle)
{
    using namespace wz::engine::assets;

    wz::gpu::DeferredReleaseQueue release_queue{};
    SceneRuntimeBundleBuildResult candidate =
        build_scene_runtime_bundle(
            release_queue,
            make_parent_child_scene());

    ASSERT_TRUE(candidate.ok()) << candidate.status;
    ASSERT_TRUE(candidate.bundle);
    EXPECT_TRUE(candidate.bundle->valid);
    EXPECT_EQ(candidate.bundle->authored_scene.name, "runtime_build_scene");
    EXPECT_TRUE(
        candidate.bundle->scene_instance.authored_to_runtime.contains(
            "parent"));

    SceneRuntimeBundle live{ release_queue };
    EXPECT_TRUE(commit_scene_runtime_bundle(live, std::move(candidate)));

    EXPECT_TRUE(live.valid);
    EXPECT_EQ(live.authored_scene.name, "runtime_build_scene");
    EXPECT_TRUE(live.scene_instance.authored_to_runtime.contains("child"));
    EXPECT_EQ(live.status.find("runtime scene ready"), 0u);
}

TEST(SceneRuntimeBundle, CommitRejectsFailedCandidateAndPreservesLive)
{
    using namespace wz::engine::assets;

    wz::gpu::DeferredReleaseQueue release_queue{};
    SceneRuntimeBundle live{ release_queue };

    SceneRuntimeBundleBuildResult valid_candidate =
        build_scene_runtime_bundle(
            release_queue,
            make_parent_child_scene());
    ASSERT_TRUE(valid_candidate.ok()) << valid_candidate.status;
    ASSERT_TRUE(commit_scene_runtime_bundle(
        live,
        std::move(valid_candidate)));

    const std::string live_hash_text = live.scene_hash_text;
    const auto live_node_count =
        wz::core::graph::node_count(live.scene_instance.storage.polytree);

    SceneAssetData authored{};
    authored.name = "failed_bundle_candidate";

    wz::asset::AssetKey missing_renderable{};
    missing_renderable.content_hash = { 0xABCDu, 0u };

    SceneNodeAsset node{};
    node.id = "obj";
    node.renderable_asset = missing_renderable;
    authored.nodes.push_back(std::move(node));

    SceneRuntimeBundleBuildResult failed_candidate =
        build_scene_runtime_bundle(release_queue, authored);

    ASSERT_FALSE(failed_candidate.ok());
    EXPECT_EQ(
        failed_candidate.failed_phase,
        SceneRuntimeBuildPhase::Instantiate);
    EXPECT_EQ(
        failed_candidate.error.phase,
        SceneRuntimeBuildPhase::Instantiate);

    EXPECT_FALSE(commit_scene_runtime_bundle(
        live,
        std::move(failed_candidate)));

    EXPECT_TRUE(live.valid);
    EXPECT_EQ(live.scene_hash_text, live_hash_text);
    EXPECT_EQ(
        wz::core::graph::node_count(live.scene_instance.storage.polytree),
        live_node_count);
    EXPECT_TRUE(live.scene_instance.authored_to_runtime.contains("parent"));
}
