#include <engine/behavior/behavior_module_api.h>

namespace
{
    void on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        

        if (!facts || !event) {
            return;
        }

        
        switch (wz_event_kind(event)) {
        case WZ_EVENT_FRAME_UPDATE:
            break;

        case WZ_EVENT_COLLISION_STAY:
            wz_self_add_local_translation(facts, event, 0.0f, 1.0f, 0.0f);

            break;


            
        
        default:
            break;
        }
    }
}

WZ_BEHAVIOR_MODULE("test_behavior", on_event)