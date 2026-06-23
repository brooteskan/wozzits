// tests/render/fixtures/behavior_module_test_plugin.cpp
//
// A minimal project behavior-module DLL for the WozzitsApp_v1 behavior dispatch
// test. It registers one module, "move_up_on_frame", subscribed to frame.update
// by default, that writes an add-local-translation command of (0, +1, 0) every
// frame. The test loads this DLL through WozzitsApp_v1::load_scene (from the
// fixture project's behavior_module_folder), then asserts the bound node moves
// up after a simulation_tick — proving load -> register -> dispatch frame.update
// -> apply command runs inside the shared runtime.

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
    const WzBehaviorModuleDesc desc{
        .size = sizeof(WzBehaviorModuleDesc),
        .module = "move_up_on_frame",
        .on_event = move_up_on_frame,
        .event_channels = events,
        .event_channel_count = 1u,
        .module_user_data = nullptr,
    };
    return api->register_module_desc(api->user, &desc);
}
