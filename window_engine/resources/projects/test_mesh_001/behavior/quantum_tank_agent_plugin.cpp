#include "agent_tank.h"
#include "tank_drive.h"

// quantum_tank_agent -- the ACTUATOR half of a quantum NPC tank.
//
// It does NOT think: a project plugin only links the C ABI, so it can't run the
// cognition math (that lives engine-side in the quantum_agent built-in, which owns
// the wave function). Instead this reads the committed decision of a quantum_agent
// CO-LOCATED on the same node (via wz_self_agent_decision) and turns it into
// motion. Author both on an NPC node: quantum_agent decides, quantum_tank_agent
// drives.
//
// Step 1 (this file): the simplest possible mapping off the built-in agent's single
// binary disposition -- ENGAGE (|0>) advances, HOLD (|1>) / deliberating stops.
// Steering and richer decisions come in later steps.

namespace
{
    // Subscribe to self.start (nothing yet -- reserved for target lookup later) and
    // frame.update (poll the decision + drive). Reading the decision is a cheap
    // cached read; no cognition runs here.
    static const char* kQuantumTankEvents[] = {
        "self.start",
        "frame.update"
    };


    void quantum_tank_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId,
        void*)
    {
        QuantumTankState* state = wz_instance_state<QuantumTankState>(facts);
        if (!state) {
            return;
        }
        // Authorable knob: how fast the tank advances when it commits to ENGAGE.
        (void)wz_config_float(facts, "drive_speed", &state->drive_speed);

        uint8_t result = wz_find_entity_by_authored_id(facts, "empty_2", &state->terrain);
        wz_log_infof(facts, "[agent tank init] find terrain: %u", result);

        result = wz_find_entity_by_authored_id(facts, "1", &state->canon_audio);
        wz_log_infof(facts, "[agent tank init] find audio: %u", result);

        result = wz_find_entity_by_authored_id(facts, "empty_1", &state->player);
        wz_log_infof(facts, "[agent tank init] find player: %u", result);
    }

    void quantum_tank_on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event ) return;
        

        QuantumTankState* state = wz_instance_state<QuantumTankState>(facts);
        if (!state) {
            return;
        }

        // Read the co-located quantum_agent's committed disposition. If this node
        // has no quantum_agent, there is nothing to actuate -- leave the tank be.
        WzAgentDecision decision{};
        if (!wz_self_agent_decision(facts, event, &decision)) {
            return;
        }

        // Step-1 mapping: ENGAGE (committed |0>) advances in local +X; HOLD (|1>)
        // and still-deliberating (-1) hold position. Motion is issued in the
        // entity's local frame, like the player tank's drive.
        const float speed =
            (decision.committed == 0) ? 1.0f : 0.0f;

        state->left_tread_speed = speed*0.5;
        state->right_tread_speed = speed*0.5;

        if (decision.committed != state->last_decision) {
            state->last_decision = decision.committed;
            wz_log_infof(
                facts,
                "[qtank] decision=%d z=%.2f speed=%.1f",
                (int)decision.committed,
                decision.marginal,
                speed);
        }

        switch (wz_event_kind(event)) {

        case WZ_EVENT_SELF_START:
        {
            constexpr float kAlignRate = 0.618f;  // keep it low: tanks suddenly lurching looks weird
            wz_self_set_terrain_alignment_rate(facts, event, kAlignRate);
            return;   // set once; skip the motion code below on this event
        }

        default:
            break;
        }

        tank_drive::drive_treads(facts, event, state->left_tread_speed, state->right_tread_speed);

    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "quantum_tank_agent",
    quantum_tank_init,
    quantum_tank_on_event,
    kQuantumTankEvents)
