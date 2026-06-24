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

    const uint8_t move_ok =
        api->register_module_desc(api->user, &move_desc);
    const uint8_t spawn_ok =
        api->register_module_desc(api->user, &spawn_desc);
    return (move_ok && spawn_ok) ? uint8_t{ 1 } : uint8_t{ 0 };
}
