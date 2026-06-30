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

namespace
{
    wz::engine::assets::CollisionAssetData flat_height_field_surface(
        float height,
        float size = 10.0f,
        bool queryable = false,
        float origin_x = 0.0f,
        float origin_z = 0.0f)
    {
        wz::engine::assets::CollisionAssetData surface{};
        surface.shape_kind =
            wz::engine::assets::CollisionShapeKind::TerrainHeightField;
        surface.occupancy.kind =
            wz::engine::assets::CollisionOccupancyKind::WalkableSurface;
        surface.occupancy.queryable = queryable;
        surface.origin[0] = origin_x;
        surface.origin[1] = origin_z;
        surface.size[0] = size;
        surface.size[1] = size;
        surface.resolution_x = 2u;
        surface.resolution_y = 2u;
        surface.base_height = height;
        surface.height_samples = { 0.0f, 0.0f, 0.0f, 0.0f };
        surface.min_height = height;
        surface.max_height = height;
        surface.supports_height_query = true;
        return surface;
    }

    wz::engine::assets::CollisionAssetData sparse_mesh_surface()
    {
        wz::engine::assets::CollisionAssetData surface{};
        surface.shape_kind =
            wz::engine::assets::CollisionShapeKind::TerrainMeshSurface;
        surface.occupancy.kind =
            wz::engine::assets::CollisionOccupancyKind::WalkableSurface;
        surface.bounds_min[0] = 0.0f;
        surface.bounds_min[1] = 5.0f;
        surface.bounds_min[2] = 0.0f;
        surface.bounds_max[0] = 2.0f;
        surface.bounds_max[1] = 5.0f;
        surface.bounds_max[2] = 2.0f;
        surface.points = {
            wz::engine::assets::CollisionPoint{
                .position = { 0.25f, 5.0f, 0.25f },
            },
            wz::engine::assets::CollisionPoint{
                .position = { 1.75f, 5.0f, 0.25f },
            },
            wz::engine::assets::CollisionPoint{
                .position = { 0.25f, 5.0f, 1.75f },
            },
        };
        surface.indices = { 0u, 1u, 2u };
        surface.triangle_bounds.resize(1u);
        surface.triangle_bounds[0].min[0] = 0.25f;
        surface.triangle_bounds[0].min[1] = 5.0f;
        surface.triangle_bounds[0].min[2] = 0.25f;
        surface.triangle_bounds[0].max[0] = 1.75f;
        surface.triangle_bounds[0].max[1] = 5.0f;
        surface.triangle_bounds[0].max[2] = 1.75f;

        auto& grid = surface.surface_grid;
        grid.origin_x = 0.0f;
        grid.origin_z = 0.0f;
        grid.cell_size_x = 1.0f;
        grid.cell_size_z = 1.0f;
        grid.cells_x = 2u;
        grid.cells_z = 2u;
        grid.cell_bounds = surface.triangle_bounds;
        grid.cell_bounds.resize(4u);
        grid.cell_offsets = { 0u, 1u, 1u, 1u, 1u };
        grid.cell_triangle_indices = { 0u };
        return surface;
    }

    wz::engine::assets::CollisionAssetData sloped_mesh_surface()
    {
        auto surface = sparse_mesh_surface();
        surface.points[0].position[1] = 4.0f;
        surface.points[1].position[1] = 6.0f;
        surface.points[2].position[1] = 4.0f;
        surface.bounds_min[1] = 4.0f;
        surface.bounds_max[1] = 6.0f;
        surface.triangle_bounds[0].min[1] = 4.0f;
        surface.triangle_bounds[0].max[1] = 6.0f;
        surface.surface_grid.cell_bounds[0] = surface.triangle_bounds[0];
        return surface;
    }

    wz::engine::assets::SceneAssetData terrain_constraint_scene(
        bool terrain_constrains,
        bool actor_constrained,
        float ride_height = 0.0f,
        float actor_x = 2.0f,
        float actor_y = 12.0f,
        float actor_z = 3.0f)
    {
        wz::engine::assets::SceneAssetData asset{};
        asset.name = "terrain_constraint_scene";

        wz::engine::assets::SceneNodeAsset terrain{};
        terrain.id = "terrain";
        terrain.terrain = wz::engine::assets::SceneTerrainAsset{
            .constrain_movement = terrain_constrains,
        };
        asset.nodes.push_back(std::move(terrain));

        wz::engine::assets::SceneNodeAsset actor{};
        actor.id = "actor";
        actor.local.translation[0] = actor_x;
        actor.local.translation[1] = actor_y;
        actor.local.translation[2] = actor_z;
        actor.motion = wz::engine::assets::SceneMotionAsset{
            .terrain_constrained = actor_constrained,
            .terrain_ride_height = ride_height,
        };
        asset.nodes.push_back(std::move(actor));

        return asset;
    }

    wz::engine::collision::CollisionFrameStorage collision_with_surface(
        wz::scene::RuntimeEntityId terrain,
        const wz::engine::assets::CollisionAssetData& surface)
    {
        wz::engine::collision::CollisionFrameStorage collision{};
        collision.world.push_back(wz::engine::collision::CollisionWorldEntry{
            .entity = terrain,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &surface,
        });
        return collision;
    }
}

TEST(BehaviorCommands, TerrainConstraintSetsActorHeightAndRideOffset)
{
    auto asset = terrain_constraint_scene(true, true, 0.75f);
    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    const RuntimeEntityId terrain =
        result.instance.authored_to_runtime["terrain"];
    const auto surface = flat_height_field_surface(4.0f);
    const auto collision = collision_with_surface(terrain, surface);
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(
        apply_terrain_constraints(result.instance, collision, &changed),
        1u);

    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 4.75f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 3.0f);
}

// A movement-constraint surface authored as a COLLISION component (no terrain
// component, issue #216 — e.g. the clipmap landscape's scalar-field heightfield
// collision) must constrain an actor exactly like a terrain component does.
// Before the entity_has_constraining_terrain fix this returned 0: the surface
// was built but the sample-time filter only recognized terrain components.
TEST(BehaviorCommands, TerrainConstraintAcceptsConstrainMovementCollisionComponent)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "collision_constraint_scene";

    wz::engine::assets::SceneNodeAsset surface_node{};
    surface_node.id = "landscape";
    surface_node.collision = wz::engine::assets::SceneCollisionAsset{
        .constrain_movement = true,
    };
    asset.nodes.push_back(std::move(surface_node));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 2.0f;
    actor.local.translation[1] = 12.0f;
    actor.local.translation[2] = 3.0f;
    actor.motion = wz::engine::assets::SceneMotionAsset{
        .terrain_constrained = true,
        .terrain_ride_height = 0.5f,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor_id =
        result.instance.authored_to_runtime["actor"];
    const RuntimeEntityId surface_id =
        result.instance.authored_to_runtime["landscape"];
    const auto surface = flat_height_field_surface(4.0f);
    const auto collision = collision_with_surface(surface_id, surface);
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(
        apply_terrain_constraints(result.instance, collision, &changed),
        1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor_id);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor_id);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 4.5f);  // surface 4 + ride 0.5
}

TEST(BehaviorCommands, TerrainConstraintUsesNearestMeshSurfaceOnExactMiss)
{
    auto asset = terrain_constraint_scene(
        true,
        true,
        0.25f,
        1.45f,
        12.0f,
        1.45f);
    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    const RuntimeEntityId terrain =
        result.instance.authored_to_runtime["terrain"];
    const auto surface = sparse_mesh_surface();
    const auto collision = collision_with_surface(terrain, surface);
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(
        apply_terrain_constraints(result.instance, collision, &changed),
        1u);

    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 1.45f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 5.25f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 1.45f);
}

TEST(BehaviorCommands, TerrainConstraintFootprintUsesHighestSupportSample)
{
    auto asset = terrain_constraint_scene(
        true,
        true,
        0.0f,
        2.0f,
        12.0f,
        3.0f);
    asset.nodes[1].motion->terrain_footprint_radius = 1.0f;
    wz::engine::assets::SceneNodeAsset high_patch{};
    high_patch.id = "high_patch";
    high_patch.terrain = wz::engine::assets::SceneTerrainAsset{
        .constrain_movement = true,
    };
    asset.nodes.push_back(std::move(high_patch));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    const RuntimeEntityId terrain =
        result.instance.authored_to_runtime["terrain"];
    const RuntimeEntityId high_patch_id =
        result.instance.authored_to_runtime["high_patch"];
    const auto low_surface = flat_height_field_surface(1.0f);
    const auto high_surface = flat_height_field_surface(
        5.0f,
        0.5f,
        false,
        2.75f,
        2.75f);
    wz::engine::collision::CollisionFrameStorage collision{};
    collision.world.push_back(wz::engine::collision::CollisionWorldEntry{
        .entity = terrain,
        .world_from_local = wz::math::Mat4::identity(),
        .enabled = true,
        .resolved = &low_surface,
    });
    collision.world.push_back(wz::engine::collision::CollisionWorldEntry{
        .entity = high_patch_id,
        .world_from_local = wz::math::Mat4::identity(),
        .enabled = true,
        .resolved = &high_surface,
    });
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(
        apply_terrain_constraints(result.instance, collision, &changed),
        1u);

    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 5.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 3.0f);
}

TEST(BehaviorCommands, TerrainConstraintPrefersConstraintSurfaceOverride)
{
    auto asset = terrain_constraint_scene(true, true, 0.0f);
    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    const RuntimeEntityId terrain =
        result.instance.authored_to_runtime["terrain"];
    const auto regular_surface = flat_height_field_surface(1.0f);
    const auto constraint_surface = flat_height_field_surface(6.0f);
    wz::engine::collision::CollisionFrameStorage collision{};
    collision.world.push_back(wz::engine::collision::CollisionWorldEntry{
        .entity = terrain,
        .world_from_local = wz::math::Mat4::identity(),
        .enabled = true,
        .resolved = &regular_surface,
    });
    collision.terrain_constraint_surfaces.push_back(
        wz::engine::collision::TerrainConstraintSurfaceEntry{
            .entity = terrain,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &constraint_surface,
        });
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(
        apply_terrain_constraints(result.instance, collision, &changed),
        1u);

    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 6.0f);
}

TEST(BehaviorCommands, TerrainConstraintUsesConstraintSurfaceWithoutWorldEntry)
{
    auto asset = terrain_constraint_scene(true, true, 0.5f);
    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    const RuntimeEntityId terrain =
        result.instance.authored_to_runtime["terrain"];
    const auto constraint_surface = flat_height_field_surface(3.0f);
    wz::engine::collision::CollisionFrameStorage collision{};
    collision.terrain_constraint_surfaces.push_back(
        wz::engine::collision::TerrainConstraintSurfaceEntry{
            .entity = terrain,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &constraint_surface,
        });
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(
        apply_terrain_constraints(result.instance, collision, &changed),
        1u);

    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 3.5f);
}

TEST(BehaviorCommands, TerrainConstraintAlignsActorUpToSurfaceNormal)
{
    auto asset = terrain_constraint_scene(
        true,
        true,
        0.0f,
        0.75f,
        12.0f,
        0.75f);
    asset.nodes[1].motion->terrain_align_to_surface = true;
    asset.nodes[1].motion->terrain_alignment_strength = 1.0f;

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    const RuntimeEntityId terrain =
        result.instance.authored_to_runtime["terrain"];
    const auto surface = sloped_mesh_surface();
    const auto collision = collision_with_surface(terrain, surface);
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(
        apply_terrain_constraints(result.instance, collision, &changed),
        1u);

    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    wz::math::Transform world_trs{};
    ASSERT_TRUE(wz::math::decompose_trs(actor_node.world, world_trs));
    const wz::math::Vec3 up =
        wz::math::quat_axis_y(wz::math::normalize(world_trs.rotation));
    const wz::math::Vec3 forward =
        wz::math::quat_axis_z(wz::math::normalize(world_trs.rotation));

    EXPECT_NEAR(up.x, -0.8f, 1e-5f);
    EXPECT_NEAR(up.y, 0.6f, 1e-5f);
    EXPECT_NEAR(up.z, 0.0f, 1e-5f);
    EXPECT_NEAR(
        forward.x * up.x + forward.y * up.y + forward.z * up.z,
        0.0f,
        1e-5f);
    EXPECT_NEAR(forward.x, 0.0f, 1e-5f);
    EXPECT_NEAR(forward.y, 0.0f, 1e-5f);
    EXPECT_NEAR(forward.z, 1.0f, 1e-5f);
}

TEST(BehaviorCommands, TerrainConstraintPreservesIntegratedHorizontalMotion)
{
    auto asset = terrain_constraint_scene(true, true, 0.5f);
    asset.nodes[1].motion->linear_velocity[0] = 2.0f;
    asset.nodes[1].motion->linear_velocity[1] = 5.0f;
    asset.nodes[1].motion->linear_velocity[2] = -4.0f;

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    const RuntimeEntityId terrain =
        result.instance.authored_to_runtime["terrain"];
    const auto surface = flat_height_field_surface(1.25f);
    const auto collision = collision_with_surface(terrain, surface);
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 0.5f, &changed), 1u);
    EXPECT_EQ(
        apply_terrain_constraints(result.instance, collision, &changed),
        1u);

    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 3.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 1.75f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 1.0f);
}

TEST(BehaviorCommands, TerrainConstraintRequiresActorAndTerrainOptIn)
{
    for (const auto [terrain_constrains, actor_constrained] :
        { std::pair{ false, true }, std::pair{ true, false } })
    {
        auto asset = terrain_constraint_scene(
            terrain_constrains,
            actor_constrained,
            0.0f);
        auto result = wz::engine::assets::instantiate_scene(asset);
        ASSERT_TRUE(result.ok()) << result.error_detail;

        const RuntimeEntityId actor =
            result.instance.authored_to_runtime["actor"];
        const RuntimeEntityId terrain =
            result.instance.authored_to_runtime["terrain"];
        const auto surface = flat_height_field_surface(4.0f);
        const auto collision = collision_with_surface(terrain, surface);
        std::vector<RuntimeEntityId> changed;

        EXPECT_EQ(
            apply_terrain_constraints(result.instance, collision, &changed),
            0u);
        EXPECT_TRUE(changed.empty());

        const auto& actor_node = wz::core::graph::node_data(
            result.instance.storage.polytree,
            actor);
        EXPECT_FLOAT_EQ(actor_node.world.m[13], 12.0f);
    }
}

TEST(BehaviorCommands, TerrainConstraintClampsOutOfBoundsToEdge)
{
    // An actor whose XZ is off the heightfield used to be left unconstrained (it
    // floated / fell through). The nearest-surface query now clamps an
    // off-terrain probe to the boundary, so the actor snaps to the edge height
    // (4 on this flat field) instead of being ignored.
    auto asset = terrain_constraint_scene(
        true,
        true,
        0.0f,
        20.0f,
        12.0f,
        20.0f);
    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    const RuntimeEntityId terrain =
        result.instance.authored_to_runtime["terrain"];
    const auto surface = flat_height_field_surface(4.0f);
    const auto collision = collision_with_surface(terrain, surface);
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(
        apply_terrain_constraints(result.instance, collision, &changed),
        1u);
    EXPECT_FALSE(changed.empty());

    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 4.0f);
}

TEST(BehaviorCommands, TerrainConstraintUsesHighestSampledSurface)
{
    auto asset = terrain_constraint_scene(true, true, 0.25f);
    wz::engine::assets::SceneNodeAsset upper{};
    upper.id = "upper_terrain";
    upper.terrain = wz::engine::assets::SceneTerrainAsset{
        .constrain_movement = true,
    };
    asset.nodes.push_back(std::move(upper));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    const RuntimeEntityId terrain =
        result.instance.authored_to_runtime["terrain"];
    const RuntimeEntityId upper_terrain =
        result.instance.authored_to_runtime["upper_terrain"];
    const auto low_surface = flat_height_field_surface(2.0f);
    const auto high_surface = flat_height_field_surface(6.0f);
    wz::engine::collision::CollisionFrameStorage collision{};
    collision.world.push_back(wz::engine::collision::CollisionWorldEntry{
        .entity = terrain,
        .world_from_local = wz::math::Mat4::identity(),
        .enabled = true,
        .resolved = &low_surface,
    });
    collision.world.push_back(wz::engine::collision::CollisionWorldEntry{
        .entity = upper_terrain,
        .world_from_local = wz::math::Mat4::identity(),
        .enabled = true,
        .resolved = &high_surface,
    });
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(
        apply_terrain_constraints(result.instance, collision, &changed),
        1u);

    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 6.25f);
}

