#include "agent_tank.h"
#include "agent_tank_config.h"
#include "tank_drive.h"

// tank_commander -- the DIRECTOR half of a squad's command tree.
//
// It lives on a hidden top-level "command" node (no renderable) and drives that
// node's co-located quantum_agent to decide ONE group order -- PRESS vs HARASS --
// from the battlefield. Every enemy tank READS that decision and folds it into its
// own goals, so the squad shares an intent (the star = the design's group/director
// node). This is the apex of the command structure.
//
// It reuses the tanks' QuantumTankState + sense_world + agent_tank_config, so the
// shared machinery is defined once. The commander only cares about the target's
// SPEED (its own node position is arbitrary), so it ignores distance.

namespace
{
    static const char* kCommanderEvents[] = {
        "self.start",
        "frame.update"
    };

    void commander_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId,
        void*)
    {
        QuantumTankState* state = wz_instance_state<QuantumTankState>(facts);
        if (!state) {
            return;
        }
        // The squad watches the player.
        (void)wz_find_entity_by_authored_id(facts, "empty_1", &state->player);
        state->target = state->player;
    }

    void commander_on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        QuantumTankState* state = wz_instance_state<QuantumTankState>(facts);
        if (!state) {
            return;
        }
        if (wz_event_kind(event) != WZ_EVENT_FRAME_UPDATE) {
            return;
        }

        // DYNAMIC MEMBERSHIP: grow the group agent to the live squad size (a member
        // stance qubit per registered tank, star-bonded to the command). Only when
        // the roster count changes -- reshape re-anneals the whole group.
        const SquadRoster* roster = static_cast<const SquadRoster*>(
            wz_find_shared_state(facts, kSquadRosterKey));
        const int count = roster ? roster->member_count : 0;
        if (count != state->squad_size) {
            wz_self_reshape_group(
                facts, event,
                static_cast<uint32_t>(count),
                agent_tank_config::kSquadStarCoupling);
            state->squad_size = count;
            wz_log_infof(facts, "[commander] reshape squad -> %d members", count);
        }

        sense_world(facts, event, state);   // gets the player's speed

        const double now = wz_sim_time(facts);
        if (now < state->next_reanneal_time) {
            return;
        }

        // Command goal: PRESS (|0>, goal > 0) when the player is slow/passive,
        // HARASS (|1>) when the player is fast/aggressive.
        const float order_goal = tank_drive::clampf(
            agent_tank_config::kCommandBias
                - state->target_speed * agent_tank_config::kCommandSpeedGain,
            -1.0f, 1.0f);
        wz_self_set_agent_goal(facts, event, 0u, order_goal);
        wz_self_rearm_agent(facts, event);
        state->next_reanneal_time = now + agent_tank_config::kCommandReanneal;

        WzAgentDecision order{};
        (void)wz_self_agent_decision(facts, event, &order);
        if (order.committed != state->last_decision) {
            state->last_decision = order.committed;
            wz_log_infof(
                facts,
                "[commander] order=%s (player_spd=%.1f)",
                order.committed == 0 ? "PRESS"
                    : (order.committed == 1 ? "HARASS" : "deliberating"),
                (double)state->target_speed);
        }
    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "tank_commander",
    commander_init,
    commander_on_event,
    kCommanderEvents)
