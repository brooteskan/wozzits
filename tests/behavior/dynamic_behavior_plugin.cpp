#include <engine/behavior/behavior_plugin_abi.h>

#if defined(_WIN32)
#define WZ_TEST_EXPORT __declspec(dllexport)
#else
#define WZ_TEST_EXPORT __attribute__((visibility("default")))
#endif

namespace
{
    void always_add_local_y(
        const WzBehaviorFrameFacts* facts,
        WzBehaviorEntityId entity,
        void*)
    {
        if (!facts || !facts->write_command) {
            return;
        }

        const WzBehaviorCommand command{
            .entity = entity,
            .kind = WZ_BEHAVIOR_COMMAND_ADD_LOCAL_TRANSLATION,
            .values = { 0.0f, 2.0f, 0.0f, 0.0f },
        };
        facts->write_command(facts->command_writer_user, &command);
    }
}

extern "C" WZ_TEST_EXPORT uint8_t wz_register_behaviors(
    WzBehaviorPluginApi* api)
{
    if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
        || !api->register_behavior)
    {
        return 0;
    }

    return api->register_behavior(
        api->user,
        "dynamic_test",
        "always_add_local_y",
        always_add_local_y,
        nullptr);
}
