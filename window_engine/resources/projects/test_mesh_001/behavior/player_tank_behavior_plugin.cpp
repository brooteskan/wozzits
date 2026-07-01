#include "player_tank.h"
#include "tank_drive.h"

namespace
{
    static const char* kTankEvents[] = {
    "input.*","self.start"
    };


    static constexpr float movement_factor = 0.1;

    void tank_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId,
        void*)
    {
        PlayerTankState* state = wz_instance_state<PlayerTankState>(facts);

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
        PlayerTankState* state)
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

    void on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {

        if (!facts || !event) {
            return;
        }

        auto* state = static_cast<PlayerTankState*>(
            wz_get_instance_state(facts));
        if (!state) {
            return;
        }

        // Decider/actuator split: if THIS node also hosts a quantum_agent (add the
        // "quantum_agent" behavior to the tank node and give it a `goal`), it
        // deliberates a binary disposition on its OWN clock. Here we just READ its
        // committed decision -- a cheap cached read, no cognition runs here -- and
        // react when the wave function collapses. The player still drives; the
        // quantum sub-mind governs an auxiliary call (here, whether to engage). The
        // read no-ops cleanly when no quantum_agent is authored on the node.
        WzAgentDecision decision{};
        if (wz_self_agent_decision(facts, event, &decision)
            && decision.committed != state->last_decision)
        {
            state->last_decision = decision.committed;
            if (decision.committed == 0) {
                wz_log_infof(
                    facts,
                    "[tank] quantum mind committed: ENGAGE (z=%.2f)",
                    decision.marginal);
                // React audibly so the collapse is observable in play.
                if (state->canon_audio != WZ_INVALID_BEHAVIOR_ENTITY) {
                    wz_write_play_sound_named(
                        facts, state->canon_audio, "Canon_a");
                }
            } else if (decision.committed == 1) {
                wz_log_infof(
                    facts,
                    "[tank] quantum mind committed: HOLD (z=%.2f)",
                    decision.marginal);
            } else {
                wz_log_infof(
                    facts,
                    "[tank] quantum mind deliberating (z=%.2f)",
                    decision.marginal);
            }
        }

        uint64_t frame_index =  wz_frame_index(facts);
        switch (wz_event_kind(event)) {

        case WZ_EVENT_SELF_START:
        {
            constexpr float kAlignRate = 2.094f;  // 120 deg/s
            wz_self_set_terrain_alignment_rate(facts, event, kAlignRate);
            return;   // set once; skip the motion code below on this event
        }

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

        tank_drive::drive_treads(facts, event, state->left_tread_speed, state->right_tread_speed);

    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "tank_controller",
    tank_init,
    on_event,
    kTankEvents)
