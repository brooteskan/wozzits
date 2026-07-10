#include "agent_tank.h"
#include "agent_tank_config.h"
#include "squad_deploy.h"
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
        // Ensure the command -> pool deploy queue exists (get-or-create, init
        // context -- where wz_create_shared_state lives). The commander posts
        // reinforce orders here; the co-located pool_manager consumes them.
        (void)wz_create_shared_state(
            facts, squad_deploy::kKey,
            sizeof(squad_deploy::Queue), alignof(squad_deploy::Queue));
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

        using namespace agent_tank_config;

        // Squad shared state: the ROSTER (live membership + doctrine tallies) and the
        // DEPLOY QUEUE the commander posts reinforce orders to (the pool consumes it).
        const SquadRoster* roster = static_cast<const SquadRoster*>(
            wz_find_shared_state(facts, kSquadRosterKey));
        squad_deploy::Queue* deploy_q = static_cast<squad_deploy::Queue*>(
            wz_find_shared_state(facts, squad_deploy::kKey));

        // FIXED GROUP: hub (PRESS/HARASS order) + 3 tank stance slots + a REINFORCE
        // qubit (the top member, index kReinforceQubit), star-bonded into one wave
        // function. Reshaped ONCE to a fixed size rather than per membership change:
        // a tank always finds its stance slot, the reinforce qubit keeps a stable
        // index, and -- crucially -- the order no longer re-anneals (and risks
        // flipping) every time a tank deploys or recycles. Live membership is tracked
        // by the roster, not by resizing the agent.
        if (state->squad_size != static_cast<int>(kReinforceGroupMembers)) {
            wz_self_reshape_group(
                facts, event, kReinforceGroupMembers, kSquadStarCoupling);
            state->squad_size = static_cast<int>(kReinforceGroupMembers);
            wz_log_infof(
                facts, "[commander] group = hub + %d (%d stance + reinforce)",
                (int)kReinforceGroupMembers, (int)kReinforceGroupMembers - 1);
        }

        sense_world(facts, event, state);   // gets the player's speed

        const double now = wz_sim_time(facts);
        const int live = roster ? roster->active_members : 0;

        // REINFORCE ORDER: bringing a tank up is the commander's decision. When its
        // reinforce qubit is committed |0> (reinforce), there is ROOM under the squad
        // target, and the request cooldown has elapsed, post ONE deploy to the queue;
        // the pool unparks a reserve. Checked every frame (paced by the cooldown) so an
        // understrength squad fills promptly even though the order re-anneals slower.
        // Gating on (live + pending) keeps outstanding requests from ever exceeding the
        // deficit, so it can never over-commit past kSquadTargetSize.
        if (deploy_q) {
            WzAgentDecision reinforce{};
            const uint8_t have = wz_self_agent_decision_at(
                facts, event, kReinforceQubit, &reinforce);
            const bool wants_reinforce =
                have && (reinforce.committed == 0
                    || (reinforce.committed == -1 && reinforce.marginal > 0.0f));
            if (wants_reinforce
                && (live + deploy_q->pending) < kSquadTargetSize
                && now >= state->next_spawn_time)
            {
                deploy_q->pending += 1;
                state->next_spawn_time = now + kReinforceCooldown;
                wz_log_infof(
                    facts, "[commander] REINFORCE order (%d live +%d queued < %d)",
                    live, deploy_q->pending, (int)kSquadTargetSize);
            }
        }

        if (now < state->next_reanneal_time) {
            return;
        }

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

        // ORDER STABILITY (kills the flip-flop): (1) EMA the player speed the order
        // reads, so a momentary twitch near the crossover doesn't swing the stance;
        // (2) bias the goal toward the CURRENTLY committed side (hysteresis / Schmitt
        // trigger), so the order holds unless the battlefield clearly argues otherwise.
        state->order_speed_ema +=
            kOrderSpeedEmaAlpha * (state->target_speed - state->order_speed_ema);

        // Sample the order the group SETTLED on over the window that just ended --
        // the cache holds the last rearm's committed outcome -- BEFORE computing the
        // hysteresis. Sampling it AFTER the rearm below (as we used to) reads a stale
        // value: the rearm doesn't update the cache until the next cognition tick, so
        // `hold` would lag TWO windows, and near the crossover that stale bias pushes
        // toward the side from two windows ago -- reinforcing the very period-2
        // flip-flop the hysteresis exists to kill. Reading here makes it a proper
        // one-window latch: hold(d_n) biases the anneal that produces d_{n+1}.
        WzAgentDecision order{};
        (void)wz_self_agent_decision(facts, event, &order);
        if (order.committed != state->last_decision) {
            state->last_decision = order.committed;
            wz_log_infof(
                facts,
                "[commander] order=%s (player_spd=%.1f doctrine=%.2f)",
                order.committed == 0 ? "PRESS"
                    : (order.committed == 1 ? "HARASS" : "deliberating"),
                (double)state->order_speed_ema,
                (double)doctrine);
        }

        const float hold =
            state->last_decision == 0 ? kOrderHysteresis
            : (state->last_decision == 1 ? -kOrderHysteresis : 0.0f);
        const float order_goal = tank_drive::clampf(
            kCommandBias
                - state->order_speed_ema * kCommandSpeedGain
                + doctrine * kDoctrineGain
                + hold,
            -1.0f, 1.0f);
        wz_self_set_agent_goal(facts, event, 0u, order_goal);

        // REINFORCE goal (qubit kReinforceQubit): UNDERSTRENGTH pressure. The wider the
        // gap between the live squad and its target, the harder it leans |0>
        // (reinforce); at full strength the bias holds reserves. Star-bonded to the
        // hub, so a PRESS order pulls it toward committing and HARASS toward holding.
        const int deficit = kSquadTargetSize - live;
        const float reinforce_goal = tank_drive::clampf(
            kReinforceBias + kReinforceDeficitGain * static_cast<float>(deficit),
            -1.0f, 1.0f);
        wz_self_set_agent_goal(facts, event, kReinforceQubit, reinforce_goal);

        wz_self_rearm_agent(facts, event);
        state->next_reanneal_time = now + kCommandReanneal;
    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "tank_commander",
    commander_init,
    commander_on_event,
    kCommanderEvents)
