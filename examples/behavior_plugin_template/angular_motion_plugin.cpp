#include <engine/behavior/behavior_module_api.h>

namespace
{
    void on_angular_motion_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }

        if (wz_event_kind(event) != WZ_EVENT_FRAME_UPDATE) {
            return;
        }

        wz_self_set_motion_space(
            facts,
            event,
            WZ_BEHAVIOR_MOTION_SPACE_LOCAL);
        wz_self_set_angular_velocity(
            facts,
            event,
            0.0f,
            1.5f,
            0.0f);
    }
}

WZ_BEHAVIOR_MODULE("angular_motion", on_angular_motion_event)
