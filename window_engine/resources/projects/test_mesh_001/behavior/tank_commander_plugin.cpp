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
        // The squad watches the player. The player is a runtime-spawned prefab
        // (root node named "tank"), so its authored id is remapped on spawn -- find
        // it by NAME, the same way the tanks do. It usually isn't present yet at the
        // commander's init (the commander is authored into the scene; the player
        // drops in a frame later), so on_event retries this until it resolves.
        if (wz_find_entity_by_name(facts, "tank", &state->player)) {
            state->target = state->player;
        }
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

        // NB: the player is resolved in commander_init (by NAME "tank"), not here.
        // A prefab spawn triggers a full behavior-scene rebuild that re-runs EVERY
        // binding's init, so the player's own spawn re-runs commander_init after it
        // exists -- init resolves it then. No per-frame retry needed.

        // DYNAMIC MEMBERSHIP: grow the group agent to the live squad size (a member
        // stance qubit per registered tank, star-bonded to the command). Only when
        // the roster count changes -- reshape re-anneals the whole group.
        const SquadRoster* roster = static_cast<const SquadRoster*>(
            wz_find_shared_state(facts, kSquadRosterKey));
        // Live squad size = the number of LEASED slots (tanks currently deployed),
        // which the roster caps at kSquadLeaseSlots -- so a pool larger than the group
        // can't push the group agent past its member cap. Rises on deploy, falls on
        // recycle; reshape re-anneals the group to match.
        const int count = roster ? roster->active_members : 0;
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

        // SQUAD REINFORCEMENT is now owned by the co-located `pool_manager` behavior
        // (the #252 prewarm-and-park pool): it prewarms enemy_tank instances PARKED at
        // load and DEPLOYS them by unpark on a cooldown, instead of a fresh spawn per
        // reinforcement (each of which was an O(scene) rebuild -- the pooling baseline).
        // The commander keeps only its quantum PRESS/HARASS order below. See
        // pool_manager_plugin.cpp.

        if (now < state->next_reanneal_time) {
            return;
        }

        using namespace agent_tank_config;

        // DOCTRINE LEARNING: reward the commander's doctrine memory from the squad's
        // net shot-exchange since the last order (shots_landed - fire_taken delta),
        // then fold what it has learned into the order. Beyond the reactive rule
        // (press slow players), the commander LEARNS whether pressing actually pays
        // off against THIS player and leans that way -- the "director node learns".
        if (roster) {
            const int d_landed = roster->shots_landed - state->prev_shots_landed;
            const int d_taken = roster->fire_taken - state->prev_fire_taken;
            state->prev_shots_landed = roster->shots_landed;
            state->prev_fire_taken = roster->fire_taken;
            const int net = d_landed - d_taken;
            if (net != 0) {
                // net > 0: squad won the exchange -> pressing paid off (|0>).
                // net < 0: squad got the worse of it -> caution paid off (|1>).
                wz_self_agent_reward(
                    facts, event, kDoctrineMemoryQubit,
                    /*toward=*/ net > 0 ? 1u : 0u, kDoctrineReward);
            }
        }
        // +1 => memory leans |0> (pressing pays); fold into the PRESS/HARASS order.
        const float doctrine =
            wz_self_agent_memory(facts, event, kDoctrineMemoryQubit);

        // Command goal: reactive rule (PRESS when the player is slow/passive) PLUS
        // the learned doctrine bias.
        const float order_goal = tank_drive::clampf(
            kCommandBias
                - state->target_speed * kCommandSpeedGain
                + doctrine * kDoctrineGain,
            -1.0f, 1.0f);
        wz_self_set_agent_goal(facts, event, 0u, order_goal);
        wz_self_rearm_agent(facts, event);
        state->next_reanneal_time = now + kCommandReanneal;

        WzAgentDecision order{};
        (void)wz_self_agent_decision(facts, event, &order);
        if (order.committed != state->last_decision) {
            state->last_decision = order.committed;
            wz_log_infof(
                facts,
                "[commander] order=%s (player_spd=%.1f doctrine=%.2f)",
                order.committed == 0 ? "PRESS"
                    : (order.committed == 1 ? "HARASS" : "deliberating"),
                (double)state->target_speed,
                (double)doctrine);
        }
    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "tank_commander",
    commander_init,
    commander_on_event,
    kCommanderEvents)
