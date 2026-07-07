#include "behavior_test_support.h"

// Spawn-with-identity + self.activated dispatch (#252 pooling). The host drains a
// wz_submit_spawn_prefab into an actual spawn at the frame boundary and fires
// WZ_EVENT_SPAWN_COMPLETED / _FAILED to the SPAWNER via dispatch_spawn_event, with
// the ticket + request_tag + spawned-root identity readable through the
// wz_spawn_event_* helpers. Separately, an external unpark fires
// WZ_EVENT_SELF_ACTIVATED to the reused node. This covers the dispatch mechanisms
// (routing, the active_spawn_event facts wiring, the new channels, and the
// subscriber gate) at the engine level; the app wires the buffer + edge scan.

namespace
{
    struct SpawnProbe
    {
        int completed = 0;
        int failed = 0;
        int activated = 0;
        uint64_t ticket = 0u;
        uint64_t request_tag = 0u;
        WzBehaviorEntityId root_entity = WZ_INVALID_BEHAVIOR_ENTITY;
        std::string root_authored_id;
        WzBehaviorEntityId activated_entity = WZ_INVALID_BEHAVIOR_ENTITY;
    };
    SpawnProbe g_spawn_probe;

    void reset_spawn_probe() { g_spawn_probe = SpawnProbe{}; }

    void spawn_probe_on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        if (event->kind == WZ_EVENT_SELF_ACTIVATED) {
            ++g_spawn_probe.activated;
            g_spawn_probe.activated_entity = event->entity;
            return;
        }
        if (event->kind != WZ_EVENT_SPAWN_COMPLETED
            && event->kind != WZ_EVENT_SPAWN_FAILED)
        {
            return;
        }
        // Read the completion through the ABI helpers (proves active_spawn_event is
        // wired into the facts and populated during this dispatch).
        if (wz_spawn_event_status(facts) == WZ_SPAWN_STATUS_FAILED) {
            ++g_spawn_probe.failed;
        } else {
            ++g_spawn_probe.completed;
        }
        g_spawn_probe.ticket = wz_spawn_event_ticket(facts).value;
        g_spawn_probe.request_tag = wz_spawn_event_request_tag(facts);
        g_spawn_probe.root_entity = wz_spawn_event_root(facts);
        const char* id = wz_spawn_event_root_authored_id(facts);
        g_spawn_probe.root_authored_id = id ? id : "";  // COPY (valid only now)
    }

    uint8_t register_spawn_probe(WzBehaviorPluginApi* api)
    {
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
            || !api->register_module_desc)
        {
            return 0;
        }
        static const char* channels[] = { "spawn.*", "self.activated" };
        const WzBehaviorModuleDesc desc{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "spawn_probe",
            .on_event = spawn_probe_on_event,
            .event_channels = channels,
            .event_channel_count = 2u,
            .module_user_data = nullptr,
        };
        return api->register_module_desc(api->user, &desc);
    }

    // A 1-node scene whose node carries the spawn_probe, subscribed to the given
    // channels (comma-free list). Node id "spawner" == runtime entity 0.
    SceneInstance scene_with_spawn_probe(std::vector<std::string> channels)
    {
        SceneInstance scene{};
        scene.runtime_names = { "spawner" };
        scene.runtime_to_authored = { "spawner" };
        scene.authored_to_runtime = { { "spawner", RuntimeEntityId{ 0u } } };
        wz::engine::behavior::EventChannelMask mask = 0u;
        for (const std::string& channel : channels) {
            mask |= wz::engine::behavior::channel_mask_for_token(channel);
        }
        BehaviorComponent component{
            .binding_id = "spawner/behavior/0",
            .module = "spawn_probe",
            .name = "spawn_probe",
            .enabled = true,
            .events = channels,
            .channel_mask = mask,
        };
        scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
            .node = RuntimeEntityId{ 0u },
            .component = std::move(component),
        });
        return scene;
    }
}

// A SPAWN_COMPLETED dispatched to the spawner reaches its subscriber with the
// ticket + request_tag + spawned-root identity intact.
TEST(SpawnIdentityDispatch, CompletedDeliversIdentityToSpawner)
{
    reset_spawn_probe();
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(registry, register_spawn_probe));
    SceneInstance scene = scene_with_spawn_probe({ "spawn.*" });

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .scene = &scene,
        .behavior_state = &scene.behavior_state,
        .commands = &frame_storage.behavior_commands,
    };

    const std::string root_id = "spawn:1:enemy_root";
    WzSpawnEventPayload payload{};
    payload.ticket.value = 42u;
    payload.status = WZ_SPAWN_STATUS_COMPLETED;
    payload.request_tag = 7u;
    payload.root_entity = 3u;
    payload.root_authored_id = root_id.c_str();

    wz::engine::behavior::dispatch_spawn_event(
        scene, registry, context, RuntimeEntityId{ 0u }, payload);

    EXPECT_EQ(g_spawn_probe.completed, 1);
    EXPECT_EQ(g_spawn_probe.failed, 0);
    EXPECT_EQ(g_spawn_probe.ticket, 42u);
    EXPECT_EQ(g_spawn_probe.request_tag, 7u);
    EXPECT_EQ(g_spawn_probe.root_entity, 3u);
    EXPECT_EQ(g_spawn_probe.root_authored_id, "spawn:1:enemy_root");
}

// A FAILED status delivers WZ_EVENT_SPAWN_FAILED (null root), not COMPLETED.
TEST(SpawnIdentityDispatch, FailedDeliversFailure)
{
    reset_spawn_probe();
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(registry, register_spawn_probe));
    SceneInstance scene = scene_with_spawn_probe({ "spawn.*" });

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .scene = &scene,
        .behavior_state = &scene.behavior_state,
        .commands = &frame_storage.behavior_commands,
    };

    WzSpawnEventPayload payload{};
    payload.ticket.value = 9u;
    payload.status = WZ_SPAWN_STATUS_FAILED;
    payload.request_tag = 3u;
    payload.root_entity = WZ_INVALID_BEHAVIOR_ENTITY;
    payload.root_authored_id = nullptr;

    wz::engine::behavior::dispatch_spawn_event(
        scene, registry, context, RuntimeEntityId{ 0u }, payload);

    EXPECT_EQ(g_spawn_probe.completed, 0);
    EXPECT_EQ(g_spawn_probe.failed, 1);
    EXPECT_EQ(g_spawn_probe.ticket, 9u);
    EXPECT_TRUE(g_spawn_probe.root_authored_id.empty());
}

// The completion routes ONLY to the addressed spawner: a dispatch aimed at a
// different entity delivers nothing.
TEST(SpawnIdentityDispatch, RoutesOnlyToSpawner)
{
    reset_spawn_probe();
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(registry, register_spawn_probe));
    SceneInstance scene = scene_with_spawn_probe({ "spawn.*" });

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .scene = &scene,
        .behavior_state = &scene.behavior_state,
        .commands = &frame_storage.behavior_commands,
    };

    WzSpawnEventPayload payload{};
    payload.status = WZ_SPAWN_STATUS_COMPLETED;
    payload.root_authored_id = "spawn:1:x";
    // Address a non-existent entity 5 -- the probe on entity 0 must not fire.
    wz::engine::behavior::dispatch_spawn_event(
        scene, registry, context, RuntimeEntityId{ 5u }, payload);

    EXPECT_EQ(g_spawn_probe.completed, 0);
}

// scene_has_event_subscriber (the zero-cost gate for the SELF_ACTIVATED edge scan)
// detects a self.activated subscriber, and reports false when nothing listens.
TEST(SpawnIdentityDispatch, SubscriberGateDetectsSelfActivated)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(registry, register_spawn_probe));

    SceneInstance with = scene_with_spawn_probe({ "spawn.*", "self.activated" });
    EXPECT_TRUE(wz::engine::behavior::scene_has_event_subscriber(
        with, registry, WZ_EVENT_SELF_ACTIVATED));

    SceneInstance without = scene_with_spawn_probe({ "spawn.*" });
    EXPECT_FALSE(wz::engine::behavior::scene_has_event_subscriber(
        without, registry, WZ_EVENT_SELF_ACTIVATED));
}

// A SELF_ACTIVATED dispatched to a node reaches its self.activated subscriber --
// the routing the host's rising-edge scan uses on an external unpark.
TEST(SpawnIdentityDispatch, SelfActivatedReachesSubscriber)
{
    reset_spawn_probe();
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(registry, register_spawn_probe));
    SceneInstance scene = scene_with_spawn_probe({ "self.activated" });

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .scene = &scene,
        .behavior_state = &scene.behavior_state,
        .commands = &frame_storage.behavior_commands,
    };

    wz::engine::behavior::dispatch_behavior_event(
        scene, registry, context,
        BehaviorEvent{
            .kind = WZ_EVENT_SELF_ACTIVATED,
            .entity = RuntimeEntityId{ 0u },
        });

    EXPECT_EQ(g_spawn_probe.activated, 1);
    EXPECT_EQ(g_spawn_probe.activated_entity, 0u);

    // A parked node (mask marks entity 0 inactive) does NOT receive it -- the
    // dispatch gate drops the event, matching dispatch of any other kind.
    reset_spawn_probe();
    scene.entity_active = { 0u };
    wz::engine::behavior::dispatch_behavior_event(
        scene, registry, context,
        BehaviorEvent{
            .kind = WZ_EVENT_SELF_ACTIVATED,
            .entity = RuntimeEntityId{ 0u },
        });
    EXPECT_EQ(g_spawn_probe.activated, 0);
}
