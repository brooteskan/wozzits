#include <engine/behavior/behavior_module_api.h>

namespace
{
    static const char* kTankEvents[] = {
    "input.*"
    };

    struct TankState {
        float throttle = 0.0f;
        float turn = 0.0f;
    };

    void tank_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId,
        void*)
    {
        auto* state = static_cast<TankState*>(
            wz_alloc_instance_state(
                facts,
                sizeof(TankState),
                alignof(TankState)));

        if (state) {
            // First load gives zeroed memory. Re-init/hot reload may preserve it.
        }
    }

    static void apply_tank_motion(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        const TankState* state)
    {
        constexpr float kMoveSpeed = 6.0f;
        constexpr float kTurnSpeed = 1.8f;

        wz_self_set_motion_space(
            facts,
            event,
            WZ_BEHAVIOR_MOTION_SPACE_LOCAL);

        wz_self_set_linear_velocity(
            facts,
            event,
            0.0f,
            0.0f,
            state->throttle * kMoveSpeed);

        wz_self_set_angular_velocity(
            facts,
            event,
            0.0f,
            state->turn * kTurnSpeed,
            0.0f);
    }

    void on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {

        if (!facts || !event) {
            return;
        }

        auto* state = static_cast<TankState*>(
            wz_get_instance_state(facts));
        if (!state) {
            return;
        }

        uint64_t frame_index =  wz_frame_index(facts);
        switch (wz_event_kind(event)) {

        case WZ_EVENT_INPUT_CONTROLLER_AXIS_CHANGED:
        {
            uint32_t controller = wz_input_event_controller(facts);
            uint32_t axis = wz_input_event_controller_axis(facts);
            float value = wz_input_event_controller_axis_value(facts);

            if (axis == 1) {
                state->throttle += value;
            }

            if (axis == 0) {
                state->turn += value;
            }

            wz_log_infof(facts, "frame %u axis %u controller %u value %.2f throttle %.2f turn %.2f",frame_index, axis, controller, value, state->throttle, state->turn);
            break;
        }
        case WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED:
        {
            uint32_t controller = wz_input_event_controller(facts);
            uint32_t button = wz_input_event_controller_button(facts);
            wz_log_infof(facts, "frame %u pressed controller %u button %u",frame_index, controller, button);
            break;
        }
        case WZ_EVENT_INPUT_CONTROLLER_BUTTON_RELEASED:
        {
            uint32_t controller = wz_input_event_controller(facts);
            uint32_t button = wz_input_event_controller_button(facts);
            wz_log_infof(facts, "farme %u released controller %u button %u",frame_index, controller, button);
            break;
        }
        
        default:
            break;
        }

        apply_tank_motion(facts, event, state);

    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "tank_controller",
    tank_init,
    on_event,
    kTankEvents)