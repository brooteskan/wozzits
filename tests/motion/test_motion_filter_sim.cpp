#include <gtest/gtest.h>

#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene/scene_instance.h>
#include <engine/behavior/behavior_command_apply.h>
#include <engine/frame_storage.h>
#include <engine/motion/motion_filter.h>

#include <math/mat4.h>
#include <scene/scene_graph.h>

#include <unordered_map>

// Seam 2b integration: the Motion Filter sim pass (apply_motion_filters) on a
// live polytree, headless. Verifies the frame cycle -- the tick's propagate sets
// each filtered node's CLEAN target world, the pass eases the persistent state
// toward it and overwrites the world IN PLACE without touching local (so next
// frame's propagate re-derives a clean target -> no cross-frame windup). The
// per-DOF filter math itself is covered by test_motion_filter.cpp.

using wz::engine::assets::instantiate_scene;
using wz::engine::assets::SceneAssetData;
using wz::engine::assets::SceneInstance;
using wz::engine::assets::SceneMotionFilterAsset;
using wz::engine::assets::SceneNodeAsset;
using wz::engine::behavior::apply_motion_filters;
using wz::engine::motion::MotionFilterState;
using wz::scene::AuthoredEntityId;
using wz::scene::RuntimeEntityId;

namespace
{
    float world_y(const SceneInstance& scene, RuntimeEntityId e)
    {
        return wz::core::graph::node_data(scene.storage.polytree, e).world.m[13];
    }
    float local_y(const SceneInstance& scene, RuntimeEntityId e)
    {
        return wz::core::graph::node_data(scene.storage.polytree, e).local.m[13];
    }
    void set_local_y(SceneInstance& scene, RuntimeEntityId e, float y)
    {
        const_cast<wz::scene::TransformNode&>(
            wz::core::graph::node_data(scene.storage.polytree, e)).local.m[13] = y;
    }

    SceneAssetData parent_and_filtered_child(const SceneMotionFilterAsset& f)
    {
        SceneAssetData asset;
        asset.name = "mf_sim";
        SceneNodeAsset parent;
        parent.id = "parent";
        asset.nodes.push_back(parent);
        SceneNodeAsset child;
        child.id = "child";
        child.parent_id = "parent";
        child.motion_filter = f;
        asset.nodes.push_back(child);
        return asset;
    }

    constexpr float kDt = 1.0f / 60.0f;
}

TEST(MotionFilterSim, DampsChildTowardMovingParentWithoutTouchingLocal)
{
    SceneMotionFilterAsset f{};
    f.translation_smoothing[1] = 0.3f;  // Y damped
    auto result = instantiate_scene(parent_and_filtered_child(f));
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance& scene = result.instance;
    ASSERT_EQ(scene.motion_filters.size(), 1u);

    const RuntimeEntityId child = scene.authored_to_runtime["child"];
    const RuntimeEntityId parent = scene.authored_to_runtime["parent"];

    wz::engine::FrameStorage frame{};
    std::unordered_map<AuthoredEntityId, MotionFilterState> states;

    // Frame 1: at origin -> the filter seeds to the target (no lag from nowhere).
    wz::scene::propagate_all(scene.storage.polytree);
    apply_motion_filters(scene, frame.collision, kDt, states);
    EXPECT_NEAR(world_y(scene, child), 0.0f, 1e-4f);

    // Parent jumps to y=10; the child's clean target world becomes 10.
    set_local_y(scene, parent, 10.0f);
    wz::scene::propagate_all(scene.storage.polytree);
    apply_motion_filters(scene, frame.collision, kDt, states);
    const float y1 = world_y(scene, child);
    EXPECT_GT(y1, 0.0f);   // eased toward the parent
    EXPECT_LT(y1, 10.0f);  // but lagging (secondary motion)

    // Converges to the rigid target over time (steady state == rigid).
    for (int i = 0; i < 600; ++i) {
        wz::scene::propagate_all(scene.storage.polytree);
        apply_motion_filters(scene, frame.collision, kDt, states);
    }
    EXPECT_NEAR(world_y(scene, child), 10.0f, 1e-2f);

    // The child's LOCAL was never mutated -- the pass overwrites world only, so
    // the target stays uncontaminated frame to frame.
    EXPECT_NEAR(local_y(scene, child), 0.0f, 1e-5f);
}

TEST(MotionFilterSim, NodeWithoutFilterIsUntouched)
{
    SceneAssetData asset;
    asset.name = "mf_sim_none";
    SceneNodeAsset a;
    a.id = "a";
    a.local.translation[1] = 5.0f;
    asset.nodes.push_back(a);

    auto result = instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance& scene = result.instance;
    EXPECT_TRUE(scene.motion_filters.empty());
    const RuntimeEntityId a_id = scene.authored_to_runtime["a"];

    wz::engine::FrameStorage frame{};
    std::unordered_map<AuthoredEntityId, MotionFilterState> states;
    wz::scene::propagate_all(scene.storage.polytree);
    const uint32_t applied =
        apply_motion_filters(scene, frame.collision, kDt, states);
    EXPECT_EQ(applied, 0u);
    EXPECT_NEAR(world_y(scene, a_id), 5.0f, 1e-5f);
}

TEST(MotionFilterSim, DisabledFilterTracksRigidly)
{
    SceneMotionFilterAsset f{};
    f.enabled = false;
    f.translation_smoothing[1] = 0.5f;  // would lag if enabled
    auto result = instantiate_scene(parent_and_filtered_child(f));
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance& scene = result.instance;

    const RuntimeEntityId child = scene.authored_to_runtime["child"];
    const RuntimeEntityId parent = scene.authored_to_runtime["parent"];

    wz::engine::FrameStorage frame{};
    std::unordered_map<AuthoredEntityId, MotionFilterState> states;
    wz::scene::propagate_all(scene.storage.polytree);
    apply_motion_filters(scene, frame.collision, kDt, states);
    set_local_y(scene, parent, 10.0f);
    wz::scene::propagate_all(scene.storage.polytree);
    apply_motion_filters(scene, frame.collision, kDt, states);

    EXPECT_NEAR(world_y(scene, child), 10.0f, 1e-4f);  // no damping when disabled
}
