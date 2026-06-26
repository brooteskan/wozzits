#include <engine/behavior/behavior_module_api.h>

#include <cstdio>

namespace
{
    void on_template_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }

        switch (wz_event_kind(event)) {
        case WZ_EVENT_COLLISION_ENTER: {
            WzVec3 self_world{};
            const uint8_t has_self_world =
                wz_self_world_position(facts, event, &self_world);
            char message[192]{};
            if (has_self_world) {
                std::snprintf(
                    message,
                    sizeof(message),
                    "[behavior/template] %s entity=%u other=%u "
                    "self_world=(%.2f, %.2f, %.2f)",
                    wz_event_name(wz_event_kind(event)),
                    wz_self(event),
                    wz_other(event),
                    self_world.x,
                    self_world.y,
                    self_world.z);
            }
            else {
                std::snprintf(
                    message,
                    sizeof(message),
                    "[behavior/template] %s entity=%u other=%u",
                    wz_event_name(wz_event_kind(event)),
                    wz_self(event),
                    wz_other(event));
            }
            wz_log_info(facts, message);
            wz_self_add_local_translation(
                facts,
                event,
                0.0f,
                1.0f,
                0.0f);
            break;
        }
        case WZ_EVENT_COLLISION_STAY:
        case WZ_EVENT_COLLISION_EXIT:
        default:
            break;
        }
    }
}

WZ_BEHAVIOR_MODULE("template", on_template_event)
