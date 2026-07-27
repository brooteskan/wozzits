#include "behavior_test_support.h"

// Self-paced cognition.tick scheduler: dispatch_cognition_tick fires
// WZ_EVENT_COGNITION_TICK only for bindings whose own scheduled wake is due at the
// current sim-time, a binding reschedules itself via wz_set_next_wake, and a
// binding that does not reschedule is parked (does not busy-fire). The probe module
// records its tick count + the sim-time it saw, and reschedules on demand.

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace
{
    int g_tick_count = 0;
    double g_last_sim_time = -1.0;
    double g_reschedule_delay = 1.0;
    bool g_reschedule = true;
    // Wall-clock the probe burns per tick, so a budget can actually be spent, plus
    // the order entities were served in (for the oldest-overdue-first check).
    double g_burn_ms = 0.0;
    std::vector<uint32_t> g_fired;

    void reset_probe(double delay, bool reschedule)
    {
        g_tick_count = 0;
        g_last_sim_time = -1.0;
        g_reschedule_delay = delay;
        g_reschedule = reschedule;
        g_burn_ms = 0.0;
        g_fired.clear();
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
        g_fired.push_back(static_cast<uint32_t>(wz_self(event)));
        if (g_burn_ms > 0.0) {
            const auto until = std::chrono::steady_clock::now()
                + std::chrono::duration<double, std::milli>(g_burn_ms);
            while (std::chrono::steady_clock::now() < until) {
                // spin -- a real think() is CPU-bound on the sim thread too
            }
        }
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

    // An N-node scene, each node carrying a cognition_probe with its own stable
    // binding id -- the shape a squad spawn produces.
    SceneInstance scene_with_probes(uint32_t count)
    {
        SceneInstance scene{};
        for (uint32_t i = 0; i < count; ++i) {
            const std::string name = "actor" + std::to_string(i);
            scene.runtime_names.push_back(name);
            scene.runtime_to_authored.push_back(name);
            BehaviorComponent component{
                .binding_id = name + "/behavior/0",
                .module = "cognition_probe",
                .name = "cognition_probe",
                .enabled = true,
                .events = { "cognition.tick" },
                .channel_mask = wz::engine::behavior::channel_mask_for_token(
                    "cognition.tick"),
            };
            scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
                .node = RuntimeEntityId{ i },
                .component = std::move(component),
            });
        }
        return scene;
    }

    std::string binding_of(uint32_t i)
    {
        return "actor" + std::to_string(i) + "/behavior/0";
    }

    // Run one cognition.tick pass at the given sim-time. A budget <= 0 fires
    // everything due (the pre-budget behavior).
    void run_tick(
        SceneInstance& scene,
        BehaviorRegistry& registry,
        double sim_time,
        double budget_ms = 0.0)
    {
        wz::engine::FrameStorage frame_storage{};
        BehaviorFrameContext context{
            .scene = &scene,
            .behavior_state = &scene.behavior_state,
            .commands = &frame_storage.behavior_commands,
            .sim_time = sim_time,
            .cognition_tick_budget_ms = budget_ms,
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

// The per-frame budget caps how much wall-clock one tick pass spends. Bindings
// past the cap keep their wakes (still DUE), so they run on a later pass rather
// than being skipped -- the spike is spread, not dropped. Each probe burns far
// more than the budget, so exactly one fires per pass.
TEST(CognitionTickDispatch, BudgetDefersTheRestToALaterFrame)
{
    reset_probe(/*delay=*/1.0, /*reschedule=*/true);
    g_burn_ms = 5.0;
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_cognition_probe));
    SceneInstance scene = scene_with_probes(3);

    // All three are due at t = 0 (no wake entry). The first fires, then the pass
    // is already over budget.
    run_tick(scene, registry, 0.0, /*budget_ms=*/1.0);
    EXPECT_EQ(g_tick_count, 1);

    // Same sim-time: the one that fired rescheduled to 1.0 so it is NOT due, and
    // the two deferred ones still are. Nothing starves -- each later pass serves
    // one more.
    run_tick(scene, registry, 0.0, 1.0);
    EXPECT_EQ(g_tick_count, 2);
    run_tick(scene, registry, 0.0, 1.0);
    EXPECT_EQ(g_tick_count, 3);

    // Every agent was served exactly once, none twice.
    std::vector<uint32_t> served = g_fired;
    std::sort(served.begin(), served.end());
    EXPECT_EQ(served, (std::vector<uint32_t>{ 0u, 1u, 2u }));

    // And now they are all parked on their own cadence, so a fourth pass at the
    // same sim-time does nothing.
    run_tick(scene, registry, 0.0, 1.0);
    EXPECT_EQ(g_tick_count, 3);
}

// A budget of 0 (or less) means no ceiling -- the pre-budget behavior, and what
// every non-cognition caller of run_tick above relies on.
TEST(CognitionTickDispatch, NonPositiveBudgetFiresEverythingDue)
{
    reset_probe(/*delay=*/1.0, /*reschedule=*/true);
    g_burn_ms = 2.0;
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_cognition_probe));
    SceneInstance scene = scene_with_probes(4);

    run_tick(scene, registry, 0.0, /*budget_ms=*/0.0);
    EXPECT_EQ(g_tick_count, 4);
}

// One agent that costs more than the WHOLE budget must still make progress. If
// the budget were checked before the first dispatch it would be deferred every
// frame forever and never think at all -- a worse failure than the hitch.
TEST(CognitionTickDispatch, AlwaysFiresAtLeastOneEvenIfOverBudget)
{
    reset_probe(/*delay=*/1.0, /*reschedule=*/true);
    g_burn_ms = 5.0;
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_cognition_probe));
    SceneInstance scene = scene_with_probes(1);

    run_tick(scene, registry, 0.0, /*budget_ms=*/0.001);
    EXPECT_EQ(g_tick_count, 1);
    run_tick(scene, registry, 1.0, 0.001);
    EXPECT_EQ(g_tick_count, 2);   // and it keeps its cadence
}

// Oldest-overdue first. A scene that is persistently over budget must rotate
// through its agents rather than always serving the same prefix of scene order,
// or the tail never thinks. Wakes are seeded so scene order and overdue order
// disagree.
TEST(CognitionTickDispatch, ServesTheMostOverdueFirst)
{
    reset_probe(/*delay=*/1.0, /*reschedule=*/true);
    g_burn_ms = 5.0;
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_cognition_probe));
    SceneInstance scene = scene_with_probes(3);

    // All due at t = 10, but entity 2 has been waiting longest and entity 0 the
    // least -- the reverse of scene order.
    scene.behavior_state.set_next_wake(binding_of(0), 9.0);
    scene.behavior_state.set_next_wake(binding_of(1), 5.0);
    scene.behavior_state.set_next_wake(binding_of(2), 1.0);

    run_tick(scene, registry, 10.0, /*budget_ms=*/1.0);
    run_tick(scene, registry, 10.0, 1.0);
    run_tick(scene, registry, 10.0, 1.0);

    EXPECT_EQ(g_fired, (std::vector<uint32_t>{ 2u, 1u, 0u }));
}

// A parked (inactive) node's cognition does not tick -- and, crucially, is NOT
// stranded asleep by the park-at-infinity step. Regression for the ordering bug
// where an inactive node's due wake was parked at +inf *before* the gate dropped
// the tick, so the handler never reran to reschedule and the node never ticked
// again even after being unparked. The active check must skip BEFORE the park, so
// the wake stays due and the node resumes on unpark.
TEST(CognitionTickDispatch, ParkedNodeResumesTickingAfterUnpark)
{
    reset_probe(/*delay=*/1.0, /*reschedule=*/true);
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_cognition_probe));
    SceneInstance scene = scene_with_probe("cognition.tick");

    run_tick(scene, registry, 0.0);   // active: fire, reschedule wake -> 1.0
    EXPECT_EQ(g_tick_count, 1);

    // Park entity 0. Its wake (1.0) comes due while parked -- it must not tick,
    // and must not be stranded asleep.
    scene.entity_active = { 0u };
    run_tick(scene, registry, 1.0);   // due but parked -> no tick
    run_tick(scene, registry, 5.0);   // still parked -> no tick
    EXPECT_EQ(g_tick_count, 1);

    // Unpark (empty mask = all live). The wake was left due, so it fires again.
    // Before the fix this stayed at 1 forever (parked at +inf).
    scene.entity_active.clear();
    run_tick(scene, registry, 6.0);
    EXPECT_EQ(g_tick_count, 2);
    EXPECT_EQ(g_last_sim_time, 6.0);
}
