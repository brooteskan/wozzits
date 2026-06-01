#include <engine/behavior/behavior_plugin_abi.h>

#include <cstdio>

#if defined(_WIN32)
#define WZ_PLUGIN_EXPORT __declspec(dllexport)
#else
#define WZ_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

namespace
{
    const char* collision_kind_name(WzCollisionEventKind kind)
    {
        switch (kind) {
        case WZ_COLLISION_EVENT_ENTER:
            return "enter";
        case WZ_COLLISION_EVENT_STAY:
            return "stay";
        case WZ_COLLISION_EVENT_EXIT:
            return "exit";
        default:
            return "unknown";
        }
    }

    void log_collision_events(
        const WzBehaviorFrameFacts* facts,
        WzBehaviorEntityId entity,
        void*)
    {
        if (!facts || !facts->log_info || !facts->collision_events.read) {
            return;
        }

        for (uint32_t i = 0; i < facts->collision_events.count; ++i) {
            WzCollisionEntityEvent event{};
            if (!facts->collision_events.read(
                    facts->collision_events.user,
                    i,
                    &event)
                || event.entity != entity)
            {
                continue;
            }

            char message[192]{};
            std::snprintf(
                message,
                sizeof(message),
                "[behavior/template] collision.%s entity=%u other=%u",
                collision_kind_name(event.kind),
                event.entity,
                event.other);
            facts->log_info(facts->log_user, message);
        }
    }

    void bounce_on_collision_enter(
        const WzBehaviorFrameFacts* facts,
        WzBehaviorEntityId entity,
        void*)
    {
        if (!facts
            || !facts->write_command
            || !facts->collision_events.read)
        {
            return;
        }

        for (uint32_t i = 0; i < facts->collision_events.count; ++i) {
            WzCollisionEntityEvent event{};
            if (!facts->collision_events.read(
                    facts->collision_events.user,
                    i,
                    &event)
                || event.entity != entity
                || event.kind != WZ_COLLISION_EVENT_ENTER)
            {
                continue;
            }

            const WzBehaviorCommand command{
                .entity = entity,
                .kind = WZ_BEHAVIOR_COMMAND_ADD_LOCAL_TRANSLATION,
                .values = { 0.0f, 1.0f, 0.0f, 0.0f },
            };
            facts->write_command(facts->command_writer_user, &command);
            return;
        }
    }
}

extern "C" WZ_PLUGIN_EXPORT uint8_t wz_register_behaviors(
    WzBehaviorPluginApi* api)
{
    if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
        || !api->register_behavior)
    {
        return 0;
    }

    const uint8_t log_registered = api->register_behavior(
        api->user,
        "template",
        "log_collision_events",
        log_collision_events,
        nullptr);
    const uint8_t bounce_registered = api->register_behavior(
        api->user,
        "template",
        "bounce_on_collision_enter",
        bounce_on_collision_enter,
        nullptr);

    return log_registered && bounce_registered;
}

