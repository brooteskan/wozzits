#include "behavior_test_support.h"

#include <limits>

TEST(BehaviorCommands, ApplyLocalScaleCommandsUpdatesSceneGraph)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_apply_scale_scene";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.scale[0] = 2.0f;
    actor.local.scale[1] = 2.0f;
    actor.local.scale[2] = 2.0f;
    asset.nodes.push_back(std::move(actor));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "actor";
    child.local.translation[0] = 1.0f;
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor_id =
        result.instance.authored_to_runtime["actor"];
    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    BehaviorCommandBuffer commands{};
    commands.set_local_scale(actor_id, 3.0f, 4.0f, 5.0f);
    commands.add_local_scale(actor_id, 1.0f, 1.0f, 1.0f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 2u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor_id);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor_id);
    EXPECT_FLOAT_EQ(actor_node.local.m[0], 4.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[5], 5.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[10], 6.0f);

    const auto& child_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id);
    EXPECT_FLOAT_EQ(child_node.world.m[12], 4.0f);
}

TEST(BehaviorCommands, ApplySetLocalRotationPreservesTranslationAndScale)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_apply_rotation_scene";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 10.0f;
    actor.local.translation[1] = 20.0f;
    actor.local.translation[2] = 30.0f;
    actor.local.scale[0] = 2.0f;
    actor.local.scale[1] = 3.0f;
    actor.local.scale[2] = 4.0f;
    asset.nodes.push_back(std::move(actor));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "actor";
    child.local.translation[0] = 1.0f;
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor_id =
        result.instance.authored_to_runtime["actor"];
    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    BehaviorCommandBuffer commands{};
    commands.set_local_rotation(
        actor_id,
        0.0f,
        0.0f,
        0.70710677f,
        0.70710677f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor_id);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor_id);

    EXPECT_NEAR(actor_node.local.m[0], 0.0f, 1e-5f);
    EXPECT_NEAR(actor_node.local.m[1], 2.0f, 1e-5f);
    EXPECT_NEAR(actor_node.local.m[4], -3.0f, 1e-5f);
    EXPECT_NEAR(actor_node.local.m[5], 0.0f, 1e-5f);
    EXPECT_NEAR(actor_node.local.m[10], 4.0f, 1e-5f);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 10.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[13], 20.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[14], 30.0f);

    const auto& child_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id);
    EXPECT_NEAR(child_node.world.m[12], 10.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[13], 22.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[14], 30.0f, 1e-5f);
}

TEST(BehaviorCommands, ApplySetWorldTranslationConvertsThroughParentTransform)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_apply_world_translation_scene";

    wz::engine::assets::SceneNodeAsset parent{};
    parent.id = "parent";
    parent.local.translation[0] = 10.0f;
    parent.local.translation[1] = 2.0f;
    parent.local.scale[0] = 2.0f;
    parent.local.scale[1] = 3.0f;
    parent.local.scale[2] = 4.0f;
    asset.nodes.push_back(std::move(parent));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "parent";
    child.local.translation[0] = 1.0f;
    child.local.translation[1] = 1.0f;
    child.local.translation[2] = 1.0f;
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    BehaviorCommandBuffer commands{};
    commands.set_world_translation(child_id, 14.0f, 11.0f, 20.0f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], child_id);

    const auto& child_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id);
    EXPECT_NEAR(child_node.local.m[12], 2.0f, 1e-5f);
    EXPECT_NEAR(child_node.local.m[13], 3.0f, 1e-5f);
    EXPECT_NEAR(child_node.local.m[14], 5.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[12], 14.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[13], 11.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[14], 20.0f, 1e-5f);
}

TEST(BehaviorCommands, ApplyAddWorldTranslationPreservesParentedWorldDelta)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_add_world_translation_scene";

    wz::engine::assets::SceneNodeAsset parent{};
    parent.id = "parent";
    parent.local.translation[0] = 10.0f;
    parent.local.scale[0] = 2.0f;
    parent.local.scale[1] = 2.0f;
    parent.local.scale[2] = 2.0f;
    asset.nodes.push_back(std::move(parent));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "parent";
    child.local.translation[0] = 1.0f;
    child.local.translation[1] = 2.0f;
    child.local.translation[2] = 3.0f;
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    BehaviorCommandBuffer commands{};
    commands.add_world_translation(child_id, 0.0f, 6.0f, 0.0f);

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        nullptr);

    EXPECT_EQ(applied, 1u);
    const auto& child_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id);
    EXPECT_NEAR(child_node.local.m[12], 1.0f, 1e-5f);
    EXPECT_NEAR(child_node.local.m[13], 5.0f, 1e-5f);
    EXPECT_NEAR(child_node.local.m[14], 3.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[12], 12.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[13], 10.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[14], 6.0f, 1e-5f);
}

TEST(BehaviorCommands, ApplySetWorldTranslationUpdatesRootLocalTranslation)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_root_world_translation_scene";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 1.0f;
    actor.local.translation[1] = 2.0f;
    actor.local.translation[2] = 3.0f;
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor_id =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.set_world_translation(actor_id, 7.0f, 8.0f, 9.0f);

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        nullptr);

    EXPECT_EQ(applied, 1u);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor_id);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 7.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[13], 8.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[14], 9.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 7.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 8.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 9.0f);
}

TEST(BehaviorCommands, ApplySetWorldTranslationHandlesRotatedParent)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_rotated_parent_world_translation_scene";

    wz::engine::assets::SceneNodeAsset parent{};
    parent.id = "parent";
    parent.local.translation[0] = 10.0f;
    parent.local.rotation_quat[2] = 0.70710677f;
    parent.local.rotation_quat[3] = 0.70710677f;
    asset.nodes.push_back(std::move(parent));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "parent";
    child.local.translation[0] = 1.0f;
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    BehaviorCommandBuffer commands{};
    commands.set_world_translation(child_id, 8.0f, 3.0f, 4.0f);

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        nullptr);

    EXPECT_EQ(applied, 1u);
    const auto& child_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id);
    EXPECT_NEAR(child_node.local.m[12], 3.0f, 1e-5f);
    EXPECT_NEAR(child_node.local.m[13], 2.0f, 1e-5f);
    EXPECT_NEAR(child_node.local.m[14], 4.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[12], 8.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[13], 3.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[14], 4.0f, 1e-5f);
}

TEST(BehaviorCommands, SetLocalRotationOnIdentityScaleEntity)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_rotation_identity_scale";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[1] = 5.0f;
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor_id =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.set_local_rotation(actor_id, 0.0f, 0.0f, 0.70710677f, 0.70710677f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 1u);
    const auto& node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor_id);
    EXPECT_NEAR(node.local.m[0], 0.0f, 1e-5f);
    EXPECT_NEAR(node.local.m[1], 1.0f, 1e-5f);
    EXPECT_NEAR(node.local.m[4], -1.0f, 1e-5f);
    EXPECT_NEAR(node.local.m[5], 0.0f, 1e-5f);
    EXPECT_NEAR(node.local.m[10], 1.0f, 1e-5f);
    EXPECT_FLOAT_EQ(node.local.m[13], 5.0f)
        << "translation must be preserved";
    float col0_len = std::sqrt(
        node.local.m[0] * node.local.m[0]
        + node.local.m[1] * node.local.m[1]
        + node.local.m[2] * node.local.m[2]);
    EXPECT_NEAR(col0_len, 1.0f, 1e-5f)
        << "unit scale must be preserved";
}

TEST(BehaviorCommands, SequentialSetLocalRotationReplacesNotComposes)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_rotation_replace";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor_id =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.set_local_rotation(actor_id, 0.0f, 0.0f, 0.70710677f, 0.70710677f);
    commands.set_local_rotation(actor_id, 0.0f, 0.0f, 0.0f, 1.0f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 2u);
    const auto& node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor_id);
    EXPECT_NEAR(node.local.m[0], 1.0f, 1e-5f)
        << "second set_local_rotation(identity) must replace, not compose";
    EXPECT_NEAR(node.local.m[1], 0.0f, 1e-5f);
    EXPECT_NEAR(node.local.m[4], 0.0f, 1e-5f);
    EXPECT_NEAR(node.local.m[5], 1.0f, 1e-5f);
    EXPECT_NEAR(node.local.m[10], 1.0f, 1e-5f);
}

TEST(BehaviorCommands, SetLocalRotationWithInvalidEntityIgnored)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_rotation_invalid";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    BehaviorCommandBuffer commands{};
    commands.set_local_rotation(
        wz::scene::INVALID_RUNTIME_ENTITY,
        0.0f, 0.0f, 0.70710677f, 0.70710677f);
    commands.set_local_rotation(
        1000u,
        0.0f, 0.0f, 0.70710677f, 0.70710677f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 0u);
    EXPECT_TRUE(changed.empty());
}

TEST(BehaviorCommands, SetLocalRotationThenTranslationBothApply)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_rotation_then_translation";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 1.0f;
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor_id =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.set_local_rotation(actor_id, 0.0f, 0.0f, 0.70710677f, 0.70710677f);
    commands.set_local_translation(actor_id, 7.0f, 8.0f, 9.0f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 2u);
    const auto& node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor_id);
    EXPECT_NEAR(node.local.m[0], 0.0f, 1e-5f);
    EXPECT_NEAR(node.local.m[1], 1.0f, 1e-5f);
    EXPECT_FLOAT_EQ(node.local.m[12], 7.0f);
    EXPECT_FLOAT_EQ(node.local.m[13], 8.0f);
    EXPECT_FLOAT_EQ(node.local.m[14], 9.0f);
}


// C1(v2)-C20 (#314): a behavior that emits a non-finite transform value must not
// be able to write NaN/inf into a node. A NaN reaching the active camera (or any
// ancestor of it) silently disables frustum culling for the whole frame, and
// nothing downstream notices -- the frustum consumer's ordered compare does not
// even raise FE_INVALID for a quiet NaN, so no instrument sees it. Every
// transform/velocity command gates its values (the SetMotionSpace/decompose_trs
// policy); a rejected command leaves the prior transform.
//
// LOAD-BEARING: the point is that the node stays FINITE and UNCHANGED. Revert-
// checked -- with any of the gates neutered the node goes non-finite and the
// isfinite/UNCHANGED assertions fail. Deleting them turns this into decoration.
TEST(BehaviorCommands, NonFiniteTransformCommandsAreRejected)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_nonfinite_reject_scene";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 5.0f;
    actor.local.translation[1] = 6.0f;
    actor.local.translation[2] = 7.0f;
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    const RuntimeEntityId actor_id =
        result.instance.authored_to_runtime["actor"];

    const float qnan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    // One command per gated route, each carrying a non-finite value.
    BehaviorCommandBuffer commands{};
    commands.set_world_translation(actor_id, qnan, 0.0f, 0.0f);  // parentless -> direct write
    commands.set_local_translation(actor_id, 0.0f, inf, 0.0f);
    commands.add_local_translation(actor_id, qnan, 0.0f, 0.0f);
    commands.add_world_translation(actor_id, inf, 0.0f, 0.0f);
    commands.set_local_scale(actor_id, qnan, 1.0f, 1.0f);
    commands.set_local_rotation(actor_id, 0.0f, 0.0f, 0.0f, qnan);
    commands.set_linear_velocity(actor_id, qnan, 0.0f, 0.0f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    // Every command rejected: nothing applied, nothing marked changed.
    EXPECT_EQ(applied, 0u);
    EXPECT_TRUE(changed.empty());

    // The node kept its authored, finite transform.
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor_id);
    for (float v : actor_node.local.m) {
        EXPECT_TRUE(std::isfinite(v));
    }
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 5.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[13], 6.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[14], 7.0f);
}

// A control alongside the rejection test: a FINITE transform command still
// applies. Without this, the gate could reject everything and the test above
// would still pass -- the placebo shape the rotation warns about.
TEST(BehaviorCommands, FiniteTransformCommandStillApplies)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_finite_control_scene";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    const RuntimeEntityId actor_id =
        result.instance.authored_to_runtime["actor"];

    BehaviorCommandBuffer commands{};
    commands.set_world_translation(actor_id, 3.0f, 4.0f, 5.0f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 1u);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor_id);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 3.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[13], 4.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[14], 5.0f);
}
