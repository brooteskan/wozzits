#include "behavior_test_support.h"

// Self-paced cognition.tick scheduler: dispatch_cognition_tick fires
// WZ_EVENT_COGNITION_TICK only for bindings whose own scheduled wake is due at the
// current sim-time, a binding reschedules itself via wz_set_next_wake, and a
// binding that does not reschedule is parked (does not busy-fire). The probe module
// records its tick count + the sim-time it saw, and reschedules on demand.

namespace
{
    int g_tick_count = 0;
    double g_last_sim_time = -1.0;
    double g_reschedule_delay = 1.0;
    bool g_reschedule = true;

    void reset_probe(double delay, bool reschedule)
    {
        g_tick_count = 0;
        g_last_sim_time = -1.0;
        g_reschedule_delay = delay;
        g_reschedule = reschedule;
    }

    void cognition_probe_on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event || event->kind != WZ_EVENT_COGNITION_TICK) {
            return;
        }
        ++g_tick_count;
        g_last_sim_time = wz_sim_time(facts);
        if (g_reschedule) {
            wz_set_next_wake(facts, g_reschedule_delay);
        }
    }

    uint8_t register_cognition_probe(WzBehaviorPluginApi* api)
    {
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
            || !api->register_module_desc)
        {
            return 0;
        }
        static const char* channels[] = { "cognition.tick" };
        const WzBehaviorModuleDesc desc{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "cognition_probe",
            .on_event = cognition_probe_on_event,
            .event_channels = channels,
            .event_channel_count = 1u,
            .module_user_data = nullptr,
        };
        return api->register_module_desc(api->user, &desc);
    }

    // A 1-node scene whose node carries the cognition_probe module, subscribed to
    // the given channel, with a stable binding_id the scheduler keys on.
    SceneInstance scene_with_probe(const char* channel)
    {
        SceneInstance scene{};
        scene.runtime_names = { "actor" };
        scene.runtime_to_authored = { "actor" };
        BehaviorComponent component{
            .binding_id = "actor/behavior/0",
            .module = "cognition_probe",
            .name = "cognition_probe",
            .enabled = true,
            .events = { channel },
            .channel_mask =
                wz::engine::behavior::channel_mask_for_token(channel),
        };
        scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
            .node = RuntimeEntityId{ 0u },
            .component = std::move(component),
        });
        return scene;
    }

    // Run one cognition.tick pass at the given sim-time.
    void run_tick(
        SceneInstance& scene,
        BehaviorRegistry& registry,
        double sim_time)
    {
        wz::engine::FrameStorage frame_storage{};
        BehaviorFrameContext context{
            .scene = &scene,
            .behavior_state = &scene.behavior_state,
            .commands = &frame_storage.behavior_commands,
            .sim_time = sim_time,
        };
        wz::engine::behavior::dispatch_cognition_tick(
            scene, registry, context);
    }
}

// A fresh subscriber is due immediately (its first think), then wakes on its own
// cadence: it fires only when sim-time crosses the delay it asked for.
TEST(CognitionTickDispatch, DueImmediatelyThenSelfPaces)
{
    reset_probe(/*delay=*/1.0, /*reschedule=*/true);
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_cognition_probe));
    SceneInstance scene = scene_with_probe("cognition.tick");

    run_tick(scene, registry, 0.0);   // no entry -> due -> fire, wake at 1.0
    EXPECT_EQ(g_tick_count, 1);
    EXPECT_EQ(g_last_sim_time, 0.0);

    run_tick(scene, registry, 0.5);   // 1.0 not reached -> no fire
    EXPECT_EQ(g_tick_count, 1);

    run_tick(scene, registry, 1.0);   // due -> fire, wake at 2.0
    EXPECT_EQ(g_tick_count, 2);
    EXPECT_EQ(g_last_sim_time, 1.0);

    run_tick(scene, registry, 1.9);   // not due
    EXPECT_EQ(g_tick_count, 2);

    run_tick(scene, registry, 2.0);   // due -> fire
    EXPECT_EQ(g_tick_count, 3);
}

// A binding that does not reschedule is parked after one tick and never busy-fires,
// no matter how far sim-time advances.
TEST(CognitionTickDispatch, ParksWhenNotRescheduled)
{
    reset_probe(/*delay=*/1.0, /*reschedule=*/false);
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_cognition_probe));
    SceneInstance scene = scene_with_probe("cognition.tick");

    run_tick(scene, registry, 0.0);   // fire once, then parked at +inf
    EXPECT_EQ(g_tick_count, 1);

    run_tick(scene, registry, 100.0);
    run_tick(scene, registry, 1.0e9);
    EXPECT_EQ(g_tick_count, 1);       // still parked
}

// A binding not subscribed to cognition.tick is never woken by the scheduler.
TEST(CognitionTickDispatch, UnsubscribedNeverFires)
{
    reset_probe(/*delay=*/1.0, /*reschedule=*/true);
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_cognition_probe));
    SceneInstance scene = scene_with_probe("frame.update");  // wrong channel

    run_tick(scene, registry, 0.0);
    run_tick(scene, registry, 5.0);
    EXPECT_EQ(g_tick_count, 0);
}
