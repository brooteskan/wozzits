#include "behavior_test_support.h"

TEST(BehaviorRegistry, RegistersAndFindsStaticBehavior)
{
    BehaviorRegistry registry;
    CallCounter counter{};

    const BehaviorHandle handle = registry.register_behavior(
        "gameplay",
        "count",
        count_behavior,
        &counter);

    ASSERT_TRUE(handle.valid());
    const auto found = registry.find("gameplay", "count");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->index, handle.index);

    const BehaviorRegistration* registration = registry.get(handle);
    ASSERT_NE(registration, nullptr);
    EXPECT_EQ(registration->module, "gameplay");
    EXPECT_EQ(registration->name, "count");
    EXPECT_EQ(registration->function, count_behavior);
    EXPECT_EQ(registration->user_data, &counter);
}

TEST(BehaviorRegistry, ReRegisteringBehaviorUpdatesFunctionSlot)
{
    BehaviorRegistry registry;
    CallCounter first{};
    CallCounter second{};

    const BehaviorHandle a = registry.register_behavior(
        "gameplay",
        "count",
        count_behavior,
        &first);
    const BehaviorHandle b = registry.register_behavior(
        "gameplay",
        "count",
        count_behavior,
        &second);

    EXPECT_EQ(a.index, b.index);
    ASSERT_EQ(registry.registrations().size(), 1u);
    EXPECT_EQ(registry.get(a)->user_data, &second);
}

TEST(BehaviorRegistry, RegistersBuiltinDebugBehaviorPack)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    wz::Logger logger{};

    register_builtin_behaviors(registry, plugins, logger);

    const auto found_log = registry.find(
        kDebugBehaviorModule,
        kLogCollisionEventsBehavior);
    ASSERT_TRUE(found_log.has_value());

    const BehaviorRegistration* registration = registry.get(*found_log);
    ASSERT_NE(registration, nullptr);
    EXPECT_EQ(registration->module, "debug");
    EXPECT_EQ(registration->name, "log_collision_events");
    EXPECT_NE(registration->function, nullptr);

    const auto found_bounce = registry.find(
        kSampleBehaviorModule,
        kBounceOnCollisionEnterBehavior);
    ASSERT_TRUE(found_bounce.has_value());
}

TEST(BehaviorRegistry, RegistersGpuKernelContract)
{
    BehaviorRegistry registry;

    BehaviorGpuKernelContract contract{};
    contract.kernel_id = "debug/multiply_u32";
    contract.ports.push_back({
        .name = "input",
        .kind = WZ_GPU_PORT_STRUCTURED_BUFFER,
        .direction = WZ_GPU_PORT_INPUT,
        .stride_bytes = sizeof(uint32_t),
    });
    contract.ports.push_back({
        .name = "factor",
        .kind = WZ_GPU_PORT_U32,
        .direction = WZ_GPU_PORT_INPUT,
    });

    ASSERT_TRUE(registry.register_gpu_kernel_contract(std::move(contract)));
    ASSERT_EQ(registry.gpu_kernel_contracts().size(), 1u);
    EXPECT_EQ(
        registry.gpu_kernel_contracts()[0].kernel_id,
        "debug/multiply_u32");
    ASSERT_EQ(registry.gpu_kernel_contracts()[0].ports.size(), 2u);
    EXPECT_EQ(
        registry.gpu_kernel_contracts()[0].ports[0].stride_bytes,
        sizeof(uint32_t));
}

