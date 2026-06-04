#include <engine/behavior/behavior_module_api.h>

namespace
{
    static const char* kTankEvents[] = {
    "input.*",
    "frame.update"
    };

    struct TankState {
        float throttle = 0.0f;
        float turn = 0.0f;
        WzBehaviorEntityId terrain = WZ_INVALID_BEHAVIOR_ENTITY;
    };

    static constexpr float movement_factor = 0.1;

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
            uint8_t result = wz_find_entity_by_authored_id(facts, "empty_2", &state->terrain);
            wz_log_infof(state, "find empty_2: %u", result);
            // wz_find_entity_by_name(facts, "terrain", &state->terrain);
            // First load gives zeroed memory. Re-init/hot reload may preserve it.
        }
    }

    static void stick_to_terrain(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        const TankState* state)
    {
        if (!state || state->terrain == WZ_INVALID_BEHAVIOR_ENTITY) {
            return;
        }

        WzSurfaceSample sample{};

        WzVec3 p{};
        if (!wz_self_world_position(facts, event, &p)) {
            return;
        }

        const float start_y = p.y + 20.0f;
        const float max_distance = 80.0f;

        const WzVec3 origin{ p.x, start_y, p.z };
        const WzVec3 down{ 0.0f, -1.0f, 0.0f };

        
        const WzVec3 self_world{ p.x, start_y, p.z };

        if (!wz_query_collision_surface_ray(
            facts,
            state->terrain,
            origin,
            down,
            max_distance,
            &sample)
            || !sample.hit)
        {
            return;
        }

        const float ride_height = 0.6f;
        wz_self_set_world_translation(
            facts,
            event,
            p.x,
            sample.position.y + ride_height,
            p.z);

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
            state->throttle * kMoveSpeed,
            0.0f,
            0.0f
            );

        //wz_self_set_angular_velocity(
        //    facts,
        //    event,
        //    0.0f,
        //    state->turn * kTurnSpeed,
        //    0.0f);


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
                state->throttle += movement_factor * value;
            }

            if (axis == 0) {
                state->turn += movement_factor * value;
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
        
        case WZ_EVENT_FRAME_UPDATE:
            stick_to_terrain(facts, event, state);
            break;
        
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