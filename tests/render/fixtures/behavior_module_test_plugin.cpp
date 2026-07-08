// tests/render/fixtures/behavior_module_test_plugin.cpp
//
// A minimal project behavior-module DLL for the WozzitsApp_v1 behavior dispatch
// tests. It registers two modules, both subscribed to frame.update by default:
//
//   - "move_up_on_frame": writes an add-local-translation command of (0, +1, 0)
//     every frame. The dispatch test loads this DLL through
//     WozzitsApp_v1::load_scene (from the fixture project's
//     behavior_module_folder), then asserts the bound node moves up after a
//     simulation_tick — proving load -> register -> dispatch frame.update ->
//     apply command runs inside the shared runtime.
//
//   - "spawn_child_on_frame": calls wz_spawn_child for its own entity on every
//     frame.update. The behavior-driven add_child test asserts a child node
//     appears under the bound node after the runtime services the frame boundary
//     — proving a behavior can issue the same deferred authoring op the host's
//     add_child uses, through the shared WozzitsApp_v1 apply path (#204). One
//     dispatch queues one deferred spawn, so one tick adds exactly one child.
//
//   - "remove_node_on_frame": calls wz_self_remove_node for its own entity on
//     every frame.update. The behavior-driven remove test binds it to a child
//     under "blank" and asserts the child disappears after one tick — proving a
//     behavior can issue the same deferred remove op the host uses (#204).
//
//   - "set_renderable_on_frame": calls wz_self_set_renderable_asset(42) for its
//     own entity on every frame.update. The behavior-driven renderable test
//     asserts the bound node's preferred asset-graph renderable becomes 42 after
//     one tick — proving a behavior can issue the same deferred
//     set_node_renderable_asset op the host uses (#204).
//
//   - "pulse_tint_on_frame": calls wz_write_set_renderable_param_named(.., "tint",
//     0.9, 0.1, 0.4) for its own entity on every frame.update. The renderable-
//     param test binds it to a node carrying an authored "tint" constant and
//     asserts the per-instance override becomes that color after one tick (alpha
//     preserved), surviving a structural rebuild — proving a behavior can animate
//     an authored look constant through the SET_RENDERABLE_PARAM host verb (#232).
//
//   - "reparent_self_to_top_on_frame": calls wz_self_detach_to_top_level for its
//     own entity on every frame.update (reparent to WZ_INVALID_BEHAVIOR_ENTITY =
//     top level). The behavior-driven reparent test binds it to a child under
//     "blank" and asserts child_node_count("blank") drops to 0 after one tick —
//     proving a behavior can issue the same deferred reparent op the host uses
//     (#204).
//
//   - "add_proximity_on_frame": calls wz_self_add_node_component(.., "proximity")
//     for its own entity on every frame.update. The behavior-driven add-component
//     test asserts node_has_component("blank","proximity") becomes true after one
//     tick — proving a behavior can issue the same deferred add_node_component op
//     the host uses (#204).
//
//   - "remove_proximity_on_frame": calls
//     wz_self_remove_node_component(.., "proximity") for its own entity on every
//     frame.update. The behavior-driven remove-component test pre-adds the
//     component, then asserts node_has_component("blank","proximity") becomes
//     false after one tick — proving a behavior can issue the same deferred
//     remove_node_component op the host uses (#204).

#include <engine/behavior/behavior_module_api.h>
#include <engine/behavior/behavior_plugin_abi.h>

#if defined(_WIN32)
#define WZ_TEST_EXPORT __declspec(dllexport)
#else
#define WZ_TEST_EXPORT __attribute__((visibility("default")))
#endif

namespace
{
    void move_up_on_frame(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event || !facts->write_command) {
            return;
        }
        if (event->kind != WZ_EVENT_FRAME_UPDATE) {
            return;
        }

        const WzBehaviorCommand command{
            .entity = event->entity,
            .kind = WZ_BEHAVIOR_COMMAND_ADD_LOCAL_TRANSLATION,
            .values = { 0.0f, 1.0f, 0.0f, 0.0f },
        };
        facts->write_command(facts->command_writer_user, &command);
    }

    void spawn_child_on_frame(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        if (event->kind != WZ_EVENT_FRAME_UPDATE) {
            return;
        }

        // Deferred, fire-and-forget: queued during dispatch, applied at the
        // frame boundary via the shared WozzitsApp_v1 add-child apply path.
        wz_self_spawn_child(facts, event);
    }

    void remove_node_on_frame(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        if (event->kind != WZ_EVENT_FRAME_UPDATE) {
            return;
        }

        // Deferred, fire-and-forget: queued during dispatch, applied at the
        // frame boundary via the shared WozzitsApp_v1 remove apply path.
        wz_self_remove_node(facts, event);
    }

    void set_renderable_on_frame(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        if (event->kind != WZ_EVENT_FRAME_UPDATE) {
            return;
        }

        // Deferred, fire-and-forget: queued during dispatch, applied at the
        // frame boundary via the shared WozzitsApp_v1 set_node_renderable_asset
        // apply path. An arbitrary asset-graph node id is fine — the apply only
        // sets the field.
        wz_self_set_renderable_asset(facts, event, 42u);
    }

    // Pulses the "tint" renderable constant to a fixed distinctive color on
    // every frame.update via SET_RENDERABLE_PARAM (issue #232). The host
    // resolves the "tint" name hash against the node's declared/overridden
    // constants and writes the per-instance override (the #229 seam). The
    // renderable-param test binds it to a node carrying an authored "tint" and
    // asserts the override becomes this color after one tick — proving a
    // behavior can animate an authored look constant through the host, and that
    // the override survives a structural rebuild. Only x/y/z (r/g/b) are
    // carried; the host preserves the fourth component (alpha).
    void pulse_tint_on_frame(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        if (event->kind != WZ_EVENT_FRAME_UPDATE) {
            return;
        }
        wz_write_set_renderable_param_named(
            facts, event->entity, "tint", 0.9f, 0.1f, 0.4f);
    }

    void reparent_self_to_top_on_frame(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        if (event->kind != WZ_EVENT_FRAME_UPDATE) {
            return;
        }

        // Deferred, fire-and-forget: queued during dispatch, applied at the
        // frame boundary via the shared WozzitsApp_v1 reparent apply path.
        // Detach to the top level (WZ_INVALID_BEHAVIOR_ENTITY parent).
        wz_self_detach_to_top_level(facts, event);
    }

    void add_proximity_on_frame(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        if (event->kind != WZ_EVENT_FRAME_UPDATE) {
            return;
        }

        // Deferred, fire-and-forget: queued during dispatch, applied at the
        // frame boundary via the shared WozzitsApp_v1 add_node_component apply
        // path. The "proximity" token is one of the editor-managed components.
        wz_self_add_node_component(facts, event, "proximity");
    }

    void remove_proximity_on_frame(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        if (event->kind != WZ_EVENT_FRAME_UPDATE) {
            return;
        }

        // Deferred, fire-and-forget: queued during dispatch, applied at the
        // frame boundary via the shared WozzitsApp_v1 remove_node_component
        // apply path.
        wz_self_remove_node_component(facts, event, "proximity");
    }

    // Holds a per-node frame counter in INSTANCE STATE: on_init constructs it at
    // 0 (only on first allocation — wz_instance_state constructs T{} once, then
    // returns the preserved block AS-IS on a later init, e.g. after a structural
    // rebuild). Each frame it increments the counter and writes the value into the
    // node's local Y. So the node's Y == the number of frames this binding has
    // dispatched IF its instance state survived every rebuild in between; if a
    // rebuild had RESET the block, on_init would reconstruct counter=0 and Y would
    // drop. The state-preserving-rebuild test reads node_local_translation().y to
    // tell the two apart, and confirms a freshly-added binding starts at 1.
    struct AccumulateState
    {
        float counter = 0.0f;
    };

    void accumulate_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId,
        void*)
    {
        // Construct (first init) or fetch (preserved across a rebuild) the block.
        (void)wz_instance_state<AccumulateState>(facts);
    }

    void accumulate_on_frame(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event || !facts->write_command) {
            return;
        }
        if (event->kind != WZ_EVENT_FRAME_UPDATE) {
            return;
        }
        AccumulateState* state = wz_instance_state<AccumulateState>(facts);
        if (!state) {
            return;
        }
        state->counter += 1.0f;
        const WzBehaviorCommand command{
            .entity = event->entity,
            .kind = WZ_BEHAVIOR_COMMAND_SET_LOCAL_TRANSLATION,
            .values = { 0.0f, state->counter, 0.0f, 0.0f },
        };
        facts->write_command(facts->command_writer_user, &command);
    }

    // Spawns the prefab named "spawnling" on every frame.update at an offset of
    // (0, 0, 5) in the spawner's frame. The spawn end-to-end test registers that
    // prefab, ticks once, and asserts the spawned subtree appears in the behavior
    // runtime while the spawner's own state is untouched. Host-handled command,
    // applied at the frame boundary (see WZ_BEHAVIOR_COMMAND_SPAWN_PREFAB).
    void spawn_prefab_on_frame(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        if (event->kind != WZ_EVENT_FRAME_UPDATE) {
            return;
        }
        wz_write_spawn_prefab(
            facts, event->entity, "spawnling", 0.0f, 0.0f, 5.0f);
    }

    // Pool prewarm probe (#252 spawn-with-identity). On self.start it submits ONE
    // spawn of "spawnling" PARKED, with request_tag 7. On spawn.completed it writes
    // the completion payload into its OWN local translation so the test can read it
    // back: x = 1 if a stable root_authored_id came back, y = the echoed
    // request_tag, z = 1 if the live root handle is valid. Proves the whole submit
    // -> frame-boundary drain -> SPAWN_COMPLETED-to-the-spawner loop end to end.
    void pool_prewarm_probe(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        if (event->kind == WZ_EVENT_SELF_START) {
            WzSpawnPrefabRequest req{};
            req.spawner = event->entity;
            req.prefab_name_hash = wz_prefab_hash("spawnling");
            req.offset[0] = 0.0f;
            req.offset[1] = 0.0f;
            req.offset[2] = 5.0f;
            req.request_tag = 7u;
            req.spawn_parked = 1u;
            WzSpawnTicket ticket{};
            (void)wz_submit_spawn_prefab(facts, &req, &ticket);
            return;
        }
        if (event->kind == WZ_EVENT_SPAWN_COMPLETED && facts->write_command) {
            const float has_root =
                wz_spawn_event_root_authored_id(facts) ? 1.0f : 0.0f;
            const float tag =
                static_cast<float>(wz_spawn_event_request_tag(facts));
            const float root_valid =
                wz_spawn_event_root(facts)
                        != (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY
                    ? 1.0f
                    : 0.0f;
            const WzBehaviorCommand command{
                .entity = event->entity,
                .kind = WZ_BEHAVIOR_COMMAND_SET_LOCAL_TRANSLATION,
                .values = { has_root, tag, root_valid, 0.0f },
            };
            facts->write_command(facts->command_writer_user, &command);
        }
    }

    // Prewarm-and-DEPLOY probe (#252 pooling): like the prewarm probe, but it also
    // UNPARKS the instance (root only, as a real pool manager does) two frames
    // after recording its stable id. The parked-instance regression test spawns a
    // MULTI-node prefab through it and checks that unparking the ROOT revives a
    // CHILD behavior -- the exact case the "park only the root" fix restores (the
    // bug set active=0 on every node, so a child stayed inert after the unpark).
    struct PoolDeployState
    {
        char    root_id[48];
        uint8_t have_root;
        uint8_t deployed;
        int     frames;
    };

    void pool_deploy_init(
        const WzBehaviorInitFacts* facts, WzBehaviorEntityId, void*)
    {
        (void)wz_instance_state<PoolDeployState>(facts);
    }

    void pool_deploy_probe(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        PoolDeployState* s = wz_instance_state<PoolDeployState>(facts);
        if (!s) {
            return;
        }
        switch (wz_event_kind(event)) {
        case WZ_EVENT_SELF_START:
        {
            WzSpawnPrefabRequest req{};
            req.spawner = wz_self(event);
            req.prefab_name_hash = wz_prefab_hash("spawnling");
            req.request_tag = 0u;
            req.spawn_parked = 1u;
            WzSpawnTicket ticket{};
            (void)wz_submit_spawn_prefab(facts, &req, &ticket);
            return;
        }
        case WZ_EVENT_SPAWN_COMPLETED:
        {
            const char* id = wz_spawn_event_root_authored_id(facts);
            if (!id) {
                return;
            }
            unsigned n = 0u;
            for (; n + 1u < sizeof(s->root_id) && id[n]; ++n) {
                s->root_id[n] = id[n];
            }
            s->root_id[n] = '\0';
            s->have_root = 1u;
            return;
        }
        case WZ_EVENT_FRAME_UPDATE:
        {
            s->frames++;
            if (s->have_root && !s->deployed && s->frames >= 2) {
                WzBehaviorEntityId handle =
                    (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY;
                if (wz_find_entity_by_authored_id(facts, s->root_id, &handle)
                    && handle
                        != (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY)
                {
                    // Unpark the ROOT only -- the whole subtree must revive.
                    wz_write_set_active(facts, handle, 1u);
                    wz_write_set_visible(facts, handle, 1u);
                    s->deployed = 1u;
                }
            }
            return;
        }
        default:
            return;
        }
    }

    // Subscribed to "input.*" (NOT frame.update): writes an add-local-translation
    // of (+1, 0, 0) for each input event the runtime routes to it. The dispatch
    // test ticks with a controller-axis InputState and asserts the bound node
    // moves — proving WozzitsApp_v1 now builds + routes input events so an
    // input-driven behavior (like the tank controller) actually fires. With no
    // input, no event reaches it, so it does not move.
    void move_on_input(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event || !facts->write_command) {
            return;
        }
        const WzBehaviorCommand command{
            .entity = event->entity,
            .kind = WZ_BEHAVIOR_COMMAND_ADD_LOCAL_TRANSLATION,
            .values = { 1.0f, 0.0f, 0.0f, 0.0f },
        };
        facts->write_command(facts->command_writer_user, &command);
    }
}

extern "C" WZ_TEST_EXPORT uint8_t wz_register_behaviors(
    WzBehaviorPluginApi* api)
{
    if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
        || !api->register_module_desc)
    {
        return 0;
    }

    static const char* events[] = { "frame.update" };
    const WzBehaviorModuleDesc move_desc{
        .size = sizeof(WzBehaviorModuleDesc),
        .module = "move_up_on_frame",
        .on_event = move_up_on_frame,
        .event_channels = events,
        .event_channel_count = 1u,
        .module_user_data = nullptr,
    };
    const WzBehaviorModuleDesc spawn_desc{
        .size = sizeof(WzBehaviorModuleDesc),
        .module = "spawn_child_on_frame",
        .on_event = spawn_child_on_frame,
        .event_channels = events,
        .event_channel_count = 1u,
        .module_user_data = nullptr,
    };
    const WzBehaviorModuleDesc remove_desc{
        .size = sizeof(WzBehaviorModuleDesc),
        .module = "remove_node_on_frame",
        .on_event = remove_node_on_frame,
        .event_channels = events,
        .event_channel_count = 1u,
        .module_user_data = nullptr,
    };
    const WzBehaviorModuleDesc set_renderable_desc{
        .size = sizeof(WzBehaviorModuleDesc),
        .module = "set_renderable_on_frame",
        .on_event = set_renderable_on_frame,
        .event_channels = events,
        .event_channel_count = 1u,
        .module_user_data = nullptr,
    };
    const WzBehaviorModuleDesc pulse_tint_desc{
        .size = sizeof(WzBehaviorModuleDesc),
        .module = "pulse_tint_on_frame",
        .on_event = pulse_tint_on_frame,
        .event_channels = events,
        .event_channel_count = 1u,
        .module_user_data = nullptr,
    };
    const WzBehaviorModuleDesc reparent_desc{
        .size = sizeof(WzBehaviorModuleDesc),
        .module = "reparent_self_to_top_on_frame",
        .on_event = reparent_self_to_top_on_frame,
        .event_channels = events,
        .event_channel_count = 1u,
        .module_user_data = nullptr,
    };
    const WzBehaviorModuleDesc add_proximity_desc{
        .size = sizeof(WzBehaviorModuleDesc),
        .module = "add_proximity_on_frame",
        .on_event = add_proximity_on_frame,
        .event_channels = events,
        .event_channel_count = 1u,
        .module_user_data = nullptr,
    };
    const WzBehaviorModuleDesc remove_proximity_desc{
        .size = sizeof(WzBehaviorModuleDesc),
        .module = "remove_proximity_on_frame",
        .on_event = remove_proximity_on_frame,
        .event_channels = events,
        .event_channel_count = 1u,
        .module_user_data = nullptr,
    };
    const WzBehaviorModuleDesc accumulate_desc{
        .size = sizeof(WzBehaviorModuleDesc),
        .module = "accumulate_on_frame",
        .on_event = accumulate_on_frame,
        .on_init = accumulate_init,
        .event_channels = events,
        .event_channel_count = 1u,
        .module_user_data = nullptr,
    };
    const WzBehaviorModuleDesc spawn_prefab_desc{
        .size = sizeof(WzBehaviorModuleDesc),
        .module = "spawn_prefab_on_frame",
        .on_event = spawn_prefab_on_frame,
        .event_channels = events,
        .event_channel_count = 1u,
        .module_user_data = nullptr,
    };
    static const char* pool_channels[] = { "self.start", "spawn.completed" };
    const WzBehaviorModuleDesc pool_prewarm_desc{
        .size = sizeof(WzBehaviorModuleDesc),
        .module = "pool_prewarm_probe",
        .on_event = pool_prewarm_probe,
        .event_channels = pool_channels,
        .event_channel_count = 2u,
        .module_user_data = nullptr,
    };
    static const char* pool_deploy_channels[] = {
        "self.start", "spawn.completed", "frame.update" };
    const WzBehaviorModuleDesc pool_deploy_desc{
        .size = sizeof(WzBehaviorModuleDesc),
        .module = "pool_deploy_probe",
        .on_event = pool_deploy_probe,
        .on_init = pool_deploy_init,
        .event_channels = pool_deploy_channels,
        .event_channel_count = 3u,
        .module_user_data = nullptr,
    };
    static const char* input_events[] = { "input.*" };
    const WzBehaviorModuleDesc move_on_input_desc{
        .size = sizeof(WzBehaviorModuleDesc),
        .module = "move_on_input",
        .on_event = move_on_input,
        .event_channels = input_events,
        .event_channel_count = 1u,
        .module_user_data = nullptr,
    };

    const uint8_t move_ok =
        api->register_module_desc(api->user, &move_desc);
    const uint8_t spawn_ok =
        api->register_module_desc(api->user, &spawn_desc);
    const uint8_t remove_ok =
        api->register_module_desc(api->user, &remove_desc);
    const uint8_t set_renderable_ok =
        api->register_module_desc(api->user, &set_renderable_desc);
    const uint8_t pulse_tint_ok =
        api->register_module_desc(api->user, &pulse_tint_desc);
    const uint8_t reparent_ok =
        api->register_module_desc(api->user, &reparent_desc);
    const uint8_t add_proximity_ok =
        api->register_module_desc(api->user, &add_proximity_desc);
    const uint8_t remove_proximity_ok =
        api->register_module_desc(api->user, &remove_proximity_desc);
    const uint8_t move_on_input_ok =
        api->register_module_desc(api->user, &move_on_input_desc);
    const uint8_t accumulate_ok =
        api->register_module_desc(api->user, &accumulate_desc);
    const uint8_t spawn_prefab_ok =
        api->register_module_desc(api->user, &spawn_prefab_desc);
    const uint8_t pool_prewarm_ok =
        api->register_module_desc(api->user, &pool_prewarm_desc);
    const uint8_t pool_deploy_ok =
        api->register_module_desc(api->user, &pool_deploy_desc);
    return (move_ok && spawn_ok && remove_ok && set_renderable_ok
            && pulse_tint_ok && reparent_ok && add_proximity_ok
            && remove_proximity_ok && move_on_input_ok && accumulate_ok
            && spawn_prefab_ok && pool_prewarm_ok && pool_deploy_ok)
        ? uint8_t{ 1 }
        : uint8_t{ 0 };
}
