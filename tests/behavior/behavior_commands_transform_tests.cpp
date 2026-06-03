#include "behavior_test_support.h"

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

