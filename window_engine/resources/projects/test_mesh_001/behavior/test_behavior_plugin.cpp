#include <engine/behavior/behavior_module_api.h>

namespace
{
    static const char* kTankEvents[] = {
    "input.*"
    };

    struct TankState {
        float throttle = 0.0f;
        float turn = 0.0f;
        float left_tread_speed = 0.0f;
        float right_tread_speed = 0.0f;

        uint8_t ammo = 10;
        WzBehaviorEntityId terrain = WZ_INVALID_BEHAVIOR_ENTITY;
        WzBehaviorEntityId canon_audio = WZ_INVALID_BEHAVIOR_ENTITY;

    };

    static constexpr float movement_factor = 0.1;

    static void drive_heading_speed(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        float heading,
        float speed)
    {
        wz_self_set_motion_space(
            facts,
            event,
            WZ_BEHAVIOR_MOTION_SPACE_LOCAL);

        wz_self_set_linear_velocity(
            facts,
            event,
            speed,
            0.0f,
            0.0f);

        wz_self_set_angular_velocity(
            facts,
            event,
            0.0f,
            heading,
            0.0f);
    }

    void tank_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId,
        void*)
    {
        TankState* state = wz_instance_state<TankState>(facts);

        if (state) {
            uint8_t result = wz_find_entity_by_authored_id(facts, "empty_2", &state->terrain);
            wz_log_infof(facts, "[tank init] find terrain: %u", result);

            result = wz_find_entity_by_authored_id(facts, "1", &state->canon_audio);
            wz_log_infof(facts, "[tank init] find audio: %u", result);

            // wz_log_infof(facts, "find empty_2: %u", result);
            // wz_find_entity_by_name(facts, "terrain", &state->terrain);
            // First load gives zeroed memory. Re-init/hot reload may preserve it.
        }
    }


    static void try_fire_canon(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        TankState* state)
    {
        (void)event;
        if (state->ammo > 0) {
            wz_log_infof(facts, "[tank] we have ammo %u", state->ammo);
            state->ammo--;
            if (state->canon_audio != WZ_INVALID_BEHAVIOR_ENTITY) {
                wz_log_info(facts, "[tank] played the canon");
                wz_write_play_sound_named(facts, state->canon_audio, "Canon_a");
            }
        }
    }

    static void apply_tank_motion(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        const TankState* state)
    {
        constexpr float kMoveSpeed = 6.0f;
        constexpr float kTurnSpeed = 1.8f;



        drive_heading_speed(
            facts,
            event,
            state->turn * kTurnSpeed,
            state->throttle * kMoveSpeed);
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
                state->left_tread_speed = value;
            }

            if (axis == 3) {
                state->right_tread_speed = value;
            }

            // wz_log_infof(facts, "frame %u axis %u controller %u value %.2f throttle %.2f turn %.2f",frame_index, axis, controller, value, state->throttle, state->turn);
            break;
        }
        case WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED:
        {
            uint32_t controller = wz_input_event_controller(facts);
            uint32_t button = wz_input_event_controller_button(facts);
            wz_log_infof(facts, "frame %u pressed controller %u button %u",frame_index, controller, button);
            
            if (button == 8) {
                wz_log_info(facts, "[tank] try fire canon");
                try_fire_canon(facts, event, state);
            }
            break;
        }
        case WZ_EVENT_INPUT_CONTROLLER_BUTTON_RELEASED:
        {
            uint32_t controller = wz_input_event_controller(facts);
            uint32_t button = wz_input_event_controller_button(facts);
            // wz_log_infof(facts, "farme %u released controller %u button %u",frame_index, controller, button);
            break;
        }
        
        
        default:
            break;
        }

        state->turn = -0.2f * (state->right_tread_speed - state->left_tread_speed);
        state->throttle = -0.2f * (state->left_tread_speed + state->right_tread_speed);
        apply_tank_motion(facts, event, state);

    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "tank_controller",
    tank_init,
    on_event,
    kTankEvents)
