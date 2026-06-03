#include "behavior_test_support.h"

TEST(BehaviorCommands, ApplyLocalTranslationCommandsUpdatesSceneGraph)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_apply_scene";

    wz::engine::assets::SceneNodeAsset root{};
    root.id = "root";
    root.local.translation[0] = 10.0f;
    asset.nodes.push_back(std::move(root));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "root";
    child.local.translation[1] = 2.0f;
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    BehaviorCommandBuffer commands{};
    commands.add_local_translation(child_id, 1.0f, 2.0f, 3.0f);
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
    EXPECT_FLOAT_EQ(child_node.local.m[12], 1.0f);
    EXPECT_FLOAT_EQ(child_node.local.m[13], 4.0f);
    EXPECT_FLOAT_EQ(child_node.local.m[14], 3.0f);
    EXPECT_FLOAT_EQ(child_node.world.m[12], 11.0f);
    EXPECT_FLOAT_EQ(child_node.world.m[13], 4.0f);
    EXPECT_FLOAT_EQ(child_node.world.m[14], 3.0f);
}

TEST(BehaviorCommands, ApplySetLocalTranslationIgnoresInvalidEntities)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_apply_invalid_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.set_local_translation(actor, 4.0f, 5.0f, 6.0f);
    commands.add_local_translation(actor, 1.0f, 1.0f, 1.0f);
    commands.set_local_translation(1000u, 1.0f, 1.0f, 1.0f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 2u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 5.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[13], 6.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[14], 7.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 5.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 6.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 7.0f);
}

TEST(BehaviorCommands, MultipleAddCommandsAccumulateInOrder)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_apply_accumulate_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.local.translation[0] = 1.0f;
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.add_local_translation(actor, 1.0f, 0.0f, 0.0f);
    commands.add_local_translation(actor, 0.0f, 2.0f, 0.0f);
    commands.add_local_translation(actor, 0.0f, 0.0f, 3.0f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 3u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[13], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[14], 3.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 3.0f);
}

TEST(BehaviorCommands, SetLinearVelocityCreatesMotionStateAndIntegrates)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_velocity_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.set_linear_velocity(actor, 2.0f, 4.0f, 6.0f);
    commands.set_angular_velocity(actor, 0.25f, 0.5f, 0.75f);
    commands.set_motion_space(
        actor,
        wz::engine::assets::SceneMotionSpace::Local);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 3u);
    EXPECT_TRUE(changed.empty())
        << "setting velocity updates motion state, not transform state";
    ASSERT_EQ(result.instance.motions.size(), 1u);
    EXPECT_EQ(result.instance.motions[0].node, actor);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.linear_velocity[0],
        2.0f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.linear_velocity[1],
        4.0f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.linear_velocity[2],
        6.0f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.angular_velocity[0],
        0.25f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.angular_velocity[1],
        0.5f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.angular_velocity[2],
        0.75f);
    EXPECT_EQ(
        result.instance.motions[0].component.space,
        wz::engine::assets::SceneMotionSpace::Local);

    const uint32_t integrated =
        integrate_motion(result.instance, 0.5f, &changed);

    EXPECT_EQ(integrated, 2u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 1.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[13], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[14], 3.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 1.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 3.0f);
}

TEST(BehaviorCommands, MotionIntegratesLocalLinearVelocityInWorldAxes)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_local_velocity_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.local.rotation_quat[1] = 0.70710677f;
    node.local.rotation_quat[3] = 0.70710677f;
    node.local.scale[0] = 2.0f;
    node.local.scale[1] = 3.0f;
    node.local.scale[2] = 4.0f;
    node.motion = wz::engine::assets::SceneMotionAsset{
        .linear_velocity = { 0.0f, 0.0f, 2.0f },
        .space = wz::engine::assets::SceneMotionSpace::Local,
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    std::vector<RuntimeEntityId> changed;

    const uint32_t integrated =
        integrate_motion(result.instance, 0.5f, &changed);

    EXPECT_EQ(integrated, 1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_NEAR(actor_node.world.m[12], 1.0f, 1e-5f);
    EXPECT_NEAR(actor_node.world.m[13], 0.0f, 1e-5f);
    EXPECT_NEAR(actor_node.world.m[14], 0.0f, 1e-5f);
}

TEST(BehaviorCommands, MotionIntegratesLocalVelocityUsingWorldHierarchy)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_local_velocity_hierarchy_scene";

    wz::engine::assets::SceneNodeAsset root{};
    root.id = "root";
    root.local.rotation_quat[2] = 0.70710677f;
    root.local.rotation_quat[3] = 0.70710677f;
    asset.nodes.push_back(std::move(root));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "root";
    child.local.rotation_quat[1] = 0.70710677f;
    child.local.rotation_quat[3] = 0.70710677f;
    child.motion = wz::engine::assets::SceneMotionAsset{
        .linear_velocity = { 0.0f, 0.0f, 2.0f },
        .space = wz::engine::assets::SceneMotionSpace::Local,
    };
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    std::vector<RuntimeEntityId> changed;

    const uint32_t integrated =
        integrate_motion(result.instance, 0.5f, &changed);

    EXPECT_EQ(integrated, 1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], child_id);
    const auto& child_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id);
    EXPECT_NEAR(child_node.world.m[12], 0.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[13], 1.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[14], 0.0f, 1e-5f);
}

TEST(BehaviorCommands, MotionSpaceCanSwitchBetweenWorldAndLocal)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_motion_space_switch_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.local.rotation_quat[1] = 0.70710677f;
    node.local.rotation_quat[3] = 0.70710677f;
    node.motion = wz::engine::assets::SceneMotionAsset{
        .linear_velocity = { 0.0f, 0.0f, 2.0f },
        .space = wz::engine::assets::SceneMotionSpace::World,
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 0.5f, &changed), 1u);
    {
        const auto& actor_node = wz::core::graph::node_data(
            result.instance.storage.polytree,
            actor);
        EXPECT_NEAR(actor_node.world.m[12], 0.0f, 1e-5f);
        EXPECT_NEAR(actor_node.world.m[13], 0.0f, 1e-5f);
        EXPECT_NEAR(actor_node.world.m[14], 1.0f, 1e-5f);
    }

    BehaviorCommandBuffer commands{};
    commands.set_motion_space(
        actor,
        wz::engine::assets::SceneMotionSpace::Local);
    EXPECT_EQ(
        apply_behavior_commands(result.instance, commands.commands, &changed),
        1u);
    EXPECT_TRUE(changed.empty());

    EXPECT_EQ(integrate_motion(result.instance, 0.5f, &changed), 1u);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_NEAR(actor_node.world.m[12], 1.0f, 1e-5f);
    EXPECT_NEAR(actor_node.world.m[13], 0.0f, 1e-5f);
    EXPECT_NEAR(actor_node.world.m[14], 1.0f, 1e-5f);
}

TEST(BehaviorCommands, InvalidMotionSpaceCommandIsIgnored)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_invalid_motion_space_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.motion = wz::engine::assets::SceneMotionAsset{
        .space = wz::engine::assets::SceneMotionSpace::Local,
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    std::vector<RuntimeEntityId> changed;

    const BehaviorCommand commands[] = {
        BehaviorCommand{
            .entity = actor,
            .kind = BehaviorCommandKind::SetMotionSpace,
            .values = { 2.0f, 0.0f, 0.0f, 0.0f },
        },
        BehaviorCommand{
            .entity = actor,
            .kind = BehaviorCommandKind::SetMotionSpace,
            .values = { -1.0f, 0.0f, 0.0f, 0.0f },
        },
        BehaviorCommand{
            .entity = actor,
            .kind = BehaviorCommandKind::SetMotionSpace,
            .values = {
                std::numeric_limits<float>::quiet_NaN(),
                0.0f,
                0.0f,
                0.0f },
        },
    };

    EXPECT_EQ(apply_behavior_commands(result.instance, commands, &changed), 0u);
    EXPECT_TRUE(changed.empty());
    ASSERT_EQ(result.instance.motions.size(), 1u);
    EXPECT_EQ(
        result.instance.motions[0].component.space,
        wz::engine::assets::SceneMotionSpace::Local);
}

TEST(BehaviorCommands, AngularVelocityCreatesMotionAndIntegrates)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_angular_velocity_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.local.translation[0] = 3.0f;
    node.local.translation[1] = -2.0f;
    node.local.translation[2] = 5.0f;
    node.local.scale[0] = 2.0f;
    node.local.scale[1] = 3.0f;
    node.local.scale[2] = 4.0f;
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.set_angular_velocity(actor, 0.0f, 1.5f, 0.0f);
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(
        apply_behavior_commands(result.instance, commands.commands, &changed),
        1u);
    EXPECT_TRUE(changed.empty());
    ASSERT_EQ(result.instance.motions.size(), 1u);
    EXPECT_TRUE(result.instance.motions[0].component.enabled);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.angular_velocity[1],
        1.5f);

    EXPECT_EQ(integrate_motion(result.instance, 1.0f, &changed), 1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 3.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], -2.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 5.0f);

    wz::math::Transform trs{};
    ASSERT_TRUE(wz::math::decompose_trs(actor_node.local, trs));
    EXPECT_FLOAT_EQ(trs.scale.x, 2.0f);
    EXPECT_FLOAT_EQ(trs.scale.y, 3.0f);
    EXPECT_FLOAT_EQ(trs.scale.z, 4.0f);
    expect_same_rotation(
        trs.rotation,
        wz::math::from_axis_angle({ 0.0f, 1.0f, 0.0f }, 1.5f));
}

TEST(BehaviorCommands, AngularVelocityLocalAndWorldComposeDifferently)
{
    wz::engine::assets::SceneAssetData world_asset{};
    world_asset.name = "behavior_angular_world_scene";

    wz::engine::assets::SceneNodeAsset world_node{};
    world_node.id = "actor";
    world_node.local.rotation_quat[1] = 0.70710677f;
    world_node.local.rotation_quat[3] = 0.70710677f;
    world_node.motion = wz::engine::assets::SceneMotionAsset{
        .angular_velocity = { 0.0f, 0.0f, kPi * 0.5f },
        .space = wz::engine::assets::SceneMotionSpace::World,
    };
    world_asset.nodes.push_back(std::move(world_node));

    wz::engine::assets::SceneAssetData local_asset = world_asset;
    local_asset.name = "behavior_angular_local_scene";
    local_asset.nodes[0].motion->space =
        wz::engine::assets::SceneMotionSpace::Local;

    auto world_result = wz::engine::assets::instantiate_scene(world_asset);
    ASSERT_TRUE(world_result.ok()) << world_result.error_detail;
    auto local_result = wz::engine::assets::instantiate_scene(local_asset);
    ASSERT_TRUE(local_result.ok()) << local_result.error_detail;

    const RuntimeEntityId world_actor =
        world_result.instance.authored_to_runtime["actor"];
    const RuntimeEntityId local_actor =
        local_result.instance.authored_to_runtime["actor"];
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(world_result.instance, 1.0f, &changed), 1u);
    EXPECT_EQ(integrate_motion(local_result.instance, 1.0f, &changed), 1u);

    const wz::math::Quaternion initial =
        wz::math::from_axis_angle({ 0.0f, 1.0f, 0.0f }, kPi * 0.5f);
    const wz::math::Quaternion delta =
        wz::math::from_axis_angle({ 0.0f, 0.0f, 1.0f }, kPi * 0.5f);
    expect_same_rotation(
        node_local_rotation(world_result.instance, world_actor),
        wz::math::mul(delta, initial));
    expect_same_rotation(
        node_local_rotation(local_result.instance, local_actor),
        wz::math::mul(initial, delta));
    EXPECT_LT(
        std::abs(wz::math::dot(
            node_local_rotation(world_result.instance, world_actor),
            node_local_rotation(local_result.instance, local_actor))),
        0.999f);
}

TEST(BehaviorCommands, WorldAngularVelocityUsesTrueWorldFrameForChild)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_parented_world_angular_scene";

    wz::engine::assets::SceneNodeAsset root{};
    root.id = "root";
    root.local.rotation_quat[1] = 0.70710677f;
    root.local.rotation_quat[3] = 0.70710677f;
    asset.nodes.push_back(std::move(root));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "root";
    child.motion = wz::engine::assets::SceneMotionAsset{
        .angular_velocity = { 0.0f, 0.0f, kPi * 0.5f },
        .space = wz::engine::assets::SceneMotionSpace::World,
    };
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 1.0f, &changed), 1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], child_id);

    const wz::math::Quaternion parent_rotation =
        wz::math::from_axis_angle({ 0.0f, 1.0f, 0.0f }, kPi * 0.5f);
    const wz::math::Quaternion delta =
        wz::math::from_axis_angle({ 0.0f, 0.0f, 1.0f }, kPi * 0.5f);
    expect_same_rotation(
        node_world_rotation(result.instance, child_id),
        wz::math::mul(delta, parent_rotation));
}

TEST(BehaviorCommands, AngularVelocitySkipsUnsafeLocalTrs)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_angular_bad_trs_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.local.scale[0] = 0.0f;
    node.motion = wz::engine::assets::SceneMotionAsset{
        .angular_velocity = { 0.0f, kPi, 0.0f },
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    const auto before = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor).local;
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 1.0f, &changed), 0u);
    EXPECT_TRUE(changed.empty());
    const auto& after = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor).local;
    for (uint32_t i = 0; i < 16u; ++i) {
        EXPECT_FLOAT_EQ(after.m[i], before.m[i]) << "index " << i;
        EXPECT_TRUE(std::isfinite(after.m[i])) << "index " << i;
    }
}

TEST(BehaviorCommands, AngularVelocitySkipsShearedLocalTrs)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_angular_sheared_trs_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.motion = wz::engine::assets::SceneMotionAsset{
        .angular_velocity = { 0.0f, kPi, 0.0f },
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    auto& node_data = const_cast<wz::scene::TransformNode&>(
        wz::core::graph::node_data(result.instance.storage.polytree, actor));
    node_data.local.m[4] = 0.25f;
    node_data.world = node_data.local;
    const auto before = node_data.local;
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 1.0f, &changed), 0u);
    EXPECT_TRUE(changed.empty());
    const auto& after = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor).local;
    for (uint32_t i = 0; i < 16u; ++i) {
        EXPECT_FLOAT_EQ(after.m[i], before.m[i]) << "index " << i;
        EXPECT_TRUE(std::isfinite(after.m[i])) << "index " << i;
    }
}

TEST(BehaviorCommands, AngularVelocitySkipsReflectedLocalTrs)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_angular_reflected_trs_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.local.scale[0] = -1.0f;
    node.motion = wz::engine::assets::SceneMotionAsset{
        .angular_velocity = { 0.0f, kPi, 0.0f },
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    const auto before = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor).local;
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 1.0f, &changed), 0u);
    EXPECT_TRUE(changed.empty());
    const auto& after = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor).local;
    for (uint32_t i = 0; i < 16u; ++i) {
        EXPECT_FLOAT_EQ(after.m[i], before.m[i]) << "index " << i;
        EXPECT_TRUE(std::isfinite(after.m[i])) << "index " << i;
    }
}

TEST(BehaviorCommands, WorldAngularVelocitySkipsDegenerateParentTrs)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_angular_bad_parent_trs_scene";

    wz::engine::assets::SceneNodeAsset root{};
    root.id = "root";
    root.local.scale[0] = 0.0f;
    asset.nodes.push_back(std::move(root));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "root";
    child.motion = wz::engine::assets::SceneMotionAsset{
        .angular_velocity = { 0.0f, 0.0f, kPi },
        .space = wz::engine::assets::SceneMotionSpace::World,
    };
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    const auto before = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id).local;
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 1.0f, &changed), 0u);
    EXPECT_TRUE(changed.empty());
    const auto& after = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id).local;
    for (uint32_t i = 0; i < 16u; ++i) {
        EXPECT_FLOAT_EQ(after.m[i], before.m[i]) << "index " << i;
        EXPECT_TRUE(std::isfinite(after.m[i])) << "index " << i;
    }
}

TEST(BehaviorCommands, AngularVelocitySmallStepsStayCloseToSingleStep)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_angular_stability_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.motion = wz::engine::assets::SceneMotionAsset{
        .angular_velocity = { 0.0f, kPi * 0.5f, 0.0f },
    };
    asset.nodes.push_back(std::move(node));

    auto single = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(single.ok()) << single.error_detail;
    auto stepped = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(stepped.ok()) << stepped.error_detail;

    const RuntimeEntityId single_actor =
        single.instance.authored_to_runtime["actor"];
    const RuntimeEntityId stepped_actor =
        stepped.instance.authored_to_runtime["actor"];
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(single.instance, 1.0f, &changed), 1u);
    for (uint32_t i = 0; i < 1000u; ++i) {
        ASSERT_EQ(integrate_motion(stepped.instance, 0.001f, &changed), 1u);
    }

    expect_same_rotation(
        node_local_rotation(stepped.instance, stepped_actor),
        node_local_rotation(single.instance, single_actor),
        1e-4f);
}

TEST(BehaviorCommands, LinearAndAngularVelocityIntegrateInSameFrame)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_linear_angular_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.motion = wz::engine::assets::SceneMotionAsset{
        .linear_velocity = { 2.0f, 0.0f, 0.0f },
        .angular_velocity = { 0.0f, kPi * 0.5f, 0.0f },
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 1.0f, &changed), 2u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 0.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 0.0f);
    expect_same_rotation(
        node_local_rotation(result.instance, actor),
        wz::math::from_axis_angle({ 0.0f, 1.0f, 0.0f }, kPi * 0.5f));
}

TEST(BehaviorCommands, LocalMotionUsesFallbackAxisForDegenerateScale)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_degenerate_local_motion_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.local.scale[0] = 0.0f;
    node.motion = wz::engine::assets::SceneMotionAsset{
        .linear_velocity = { 2.0f, 0.0f, 0.0f },
        .space = wz::engine::assets::SceneMotionSpace::Local,
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 0.5f, &changed), 1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 1.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 0.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 0.0f);
}

TEST(BehaviorCommands, LinearVelocityIntegratesWorldSpaceForParentedNode)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_velocity_parented_scene";

    wz::engine::assets::SceneNodeAsset root{};
    root.id = "root";
    root.local.translation[0] = 10.0f;
    asset.nodes.push_back(std::move(root));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "root";
    child.motion = wz::engine::assets::SceneMotionAsset{
        .linear_velocity = { 0.0f, 2.0f, 0.0f },
    };
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    std::vector<RuntimeEntityId> changed;

    const uint32_t integrated =
        integrate_motion(result.instance, 0.25f, &changed);

    EXPECT_EQ(integrated, 1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], child_id);
    const auto& child_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id);
    EXPECT_FLOAT_EQ(child_node.local.m[12], 0.0f);
    EXPECT_FLOAT_EQ(child_node.local.m[13], 0.5f);
    EXPECT_FLOAT_EQ(child_node.local.m[14], 0.0f);
    EXPECT_FLOAT_EQ(child_node.world.m[12], 10.0f);
    EXPECT_FLOAT_EQ(child_node.world.m[13], 0.5f);
    EXPECT_FLOAT_EQ(child_node.world.m[14], 0.0f);
}

TEST(BehaviorCommands, LinearVelocityIgnoresNonPositiveOrInvalidDelta)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_velocity_delta_guard_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.motion = wz::engine::assets::SceneMotionAsset{
        .linear_velocity = { 2.0f, 3.0f, 4.0f },
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 0.0f, &changed), 0u);
    EXPECT_TRUE(changed.empty());
    EXPECT_EQ(integrate_motion(result.instance, -1.0f, &changed), 0u);
    EXPECT_TRUE(changed.empty());
    EXPECT_EQ(
        integrate_motion(
            result.instance,
            std::numeric_limits<float>::infinity(),
            &changed),
        0u);
    EXPECT_TRUE(changed.empty());

    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 0.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[13], 0.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[14], 0.0f);
}

