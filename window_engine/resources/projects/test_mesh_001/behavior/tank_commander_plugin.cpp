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
        // Hand the ORDER phase machine to an attached statechart (config "chart_driven"):
        // the chart drives deliberating/holding + the reanneal cadence; the reward/goal/
        // rearm math + reinforce deploys stay here. The 0 is a throwaway -- the read
        // lands the authored value or the default declared in kCommanderParams (off).
        uint8_t chart_driven = 0;
        (void)wz_config_bool(facts, "chart_driven", &chart_driven);
        state->chart_driven = (chart_driven != 0u);

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

    // Re-anneal the commander's group agent: reward doctrine from the squad's recent
    // exchange, set the order + reinforce goals, and rearm. This is the ONLY place the
    // order re-opens, so it is structurally separated from every decision READ (which
    // live in the Holding branch below). `desired` is the order side to commit toward
    // -- the held side on a heartbeat/reinforce rearm, or the new side on a warranted
    // flip. Leaves the machine in Deliberating.
    void commander_rearm(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        QuantumTankState* state,
        const SquadRoster* roster,
        int desired,
        int deficit,
        double now)
    {
        using namespace agent_tank_config;

        // DOCTRINE LEARNING: reward from the squad's net shot-exchange since the last
        // rearm (shots_landed - fire_taken delta) -- pressing that wins exchanges
        // reinforces |0>, losing them reinforces |1> -- then fold what it has learned
        // into the order below. The "director node learns" whether pressing pays off.
        if (roster) {
            const int d_landed = roster->shots_landed - state->prev_shots_landed;
            const int d_taken = roster->fire_taken - state->prev_fire_taken;
            state->prev_shots_landed = roster->shots_landed;
            state->prev_fire_taken = roster->fire_taken;
            const int net = d_landed - d_taken;
            if (net != 0) {
                // v38 VALUE semantics: the flag names the branch being
                // reinforced, so winning exchanges (net > 0) reinforces the 0
                // branch and losing them reinforces 1. This used to read the
                // other way round for the same meaning.
                wz_self_agent_reward(
                    facts, event, kDoctrineMemoryQubit,
                    /*value=*/ net > 0 ? 0u : 1u, kDoctrineReward);
            }
        }
        const float doctrine =
            wz_self_agent_memory(facts, event, kDoctrineMemoryQubit);

        // Order goal: reactive base (press slow players + learned doctrine) plus a
        // decisive bias toward `desired` so the fresh anneal commits to the intended
        // side -- holding the current order across a reinforce/heartbeat rearm, or
        // carrying a warranted flip through.
        const float bias =
            desired == 0 ? kOrderHysteresis
            : (desired == 1 ? -kOrderHysteresis : 0.0f);
        const float order_goal = tank_drive::clampf(
            kCommandBias
                - state->order_speed_ema * kCommandSpeedGain
                + doctrine * kDoctrineGain
                + bias,
            -1.0f, 1.0f);
        wz_self_set_agent_goal(facts, event, 0u, order_goal);

        // Reinforce goal (top qubit): understrength pressure, star-bonded to the order.
        const float reinforce_goal = tank_drive::clampf(
            kReinforceBias + kReinforceDeficitGain * static_cast<float>(deficit),
            -1.0f, 1.0f);
        wz_self_set_agent_goal(facts, event, kReinforceQubit, reinforce_goal);

        wz_self_rearm_agent(facts, event);
        state->order_deficit_at_rearm = deficit;
        state->next_reanneal_time = now + kOrderHeartbeat;
        state->order_phase = OrderPhase::Deliberating;
    }

    // The order side the reactive base favors: PRESS (0) / HARASS (1) when the base clears
    // the Schmitt deadband either way, else hold the current decision. Shared by the C++
    // re-open and the chart-driven reanneal, so both bias the fresh anneal the same way.
    int desired_order(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        const QuantumTankState* state)
    {
        using namespace agent_tank_config;
        const float doctrine =
            wz_self_agent_memory(facts, event, kDoctrineMemoryQubit);
        const float base =
            kCommandBias
                - state->order_speed_ema * kCommandSpeedGain
                + doctrine * kDoctrineGain;
        if (base > kOrderHysteresis) {
            return 0;   // PRESS
        }
        if (base < -kOrderHysteresis) {
            return 1;   // HARASS
        }
        return state->last_decision;
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
        //
        // Latch squad_size ONLY on a SUCCESSFUL reshape. The reshape fails while the
        // co-located quantum_agent hasn't created its agent yet (handle == 0 before its
        // self.start) -- so it must RETRY on later frames, not latch and silently
        // disable the whole squad (no stance reads, no reinforce, no deploys). A
        // distinct "failed" sentinel keeps retrying but logs the warning just once
        // rather than every frame.
        constexpr int kReshapeFailed = -2;
        if (state->squad_size != static_cast<int>(kReinforceGroupMembers)) {
            if (!wz_self_reshape_group(
                    facts, event, kReinforceGroupMembers, kSquadStarCoupling)) {
                if (state->squad_size != kReshapeFailed) {
                    state->squad_size = kReshapeFailed;
                    // WARN, not error: this one retries every frame, so it is a
                    // "not yet" that only matters if it never clears.
                    wz_log_warn(
                        facts,
                        "[commander] group reshape FAILED (agent not ready?) -- retrying");
                }
                return;   // group still size 1 -- retry next frame, don't run on it
            }
            state->squad_size = static_cast<int>(kReinforceGroupMembers);
            wz_log_infof(
                facts, "[commander] group = hub + %d (%d stance + reinforce)",
                (int)kReinforceGroupMembers, (int)kReinforceGroupMembers - 1);
        }

        sense_world(facts, event, state);   // gets the player's speed

        const double now = wz_sim_time(facts);
        // Live-or-in-flight count from the POOL (mirrored in the queue), NOT the
        // roster's lease count. The pool moves a unit pending->live atomically at
        // deploy, so (live + pending) has no one-frame undercount -- the roster's lease
        // is only claimed on the tank's self.activated, a frame after the deploy, and
        // reading it here would let the commander post an extra order in that gap.
        const int live = deploy_q ? deploy_q->live : 0;
        const int deficit = kSquadTargetSize - live;

        // Smooth the player speed the order reads with a frame-rate-INVARIANT EMA (a
        // per-second time constant, not a per-frame weight). Updated EVERY frame -- the
        // order is event-driven now (no fixed re-anneal window), so the smoothing must
        // sample every frame too, or it would ALIAS the speed rather than average it (a
        // sprint between sparse samples would be invisible; a boundary twitch would get
        // full weight). A sustained change registers over ~kOrderSpeedTau seconds; a
        // sub-second twitch is averaged away before it can cross the flip deadband.
        const float ema_dt = wz_delta_seconds(facts);
        const float ema_alpha =
            ema_dt > 0.0f ? 1.0f - expf(-ema_dt / kOrderSpeedTau) : 0.0f;
        state->order_speed_ema +=
            ema_alpha * (state->target_speed - state->order_speed_ema);

        // REINFORCE: post ONE deploy to the queue when the reinforce qubit is committed
        // |0>, there's room under the target, and the cooldown has elapsed. Runs EVERY
        // frame, independent of the order PHASE -- the qubit reads -1 while the group
        // re-deliberates, so it only posts on a settled group -- so reinforce works the
        // same whether the C++ order machine or an attached chart drives the phase.
        // (live + pending) is the pool's consistent total, capped at the deficit.
        // When chart_driven, the chart's reinforce region owns this (call
        // request_reinforcement, which self-gates on the same deficit + cooldown).
        if (deploy_q && !state->chart_driven) {
            WzAgentDecision reinforce{};
            const uint8_t have = wz_self_agent_decision_at(
                facts, event, kReinforceQubit, &reinforce);
            if (have && reinforce.committed == 0
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

        // chart_driven: an attached statechart drives the ORDER phase (deliberating /
        // holding) + the reanneal cadence. Its `call reanneal` on the loop sets the flag;
        // run the C++ reanneal (reward doctrine + set goals + rearm) here so the cognition
        // stays in one place, biased toward the reactive desired_order just like below.
        if (state->chart_driven) {
            if (state->reanneal_requested) {
                state->reanneal_requested = 0;
                commander_rearm(facts, event, state, roster,
                    desired_order(facts, event, state), deficit, now);
            }
            return;
        }

        // ORDER STATE MACHINE (OrderPhase). Reads live in one state, the rearm on the
        // transition -- so a decision is never read in the block it is rearmed (the
        // async-commit trap), and the order re-deliberates only on a material change
        // (never on a bare timer -- the flip-flop).
        if (state->order_phase == OrderPhase::Deliberating) {
            // Waiting for the freshly-rearmed group to settle: read, and latch on the
            // first commit. Don't act on an unsettled group.
            WzAgentDecision order{};
            (void)wz_self_agent_decision(facts, event, &order);
            if (order.committed != -1) {
                if (order.committed != state->last_decision) {
                    state->last_decision = order.committed;
                    wz_log_infof(
                        facts, "[commander] order=%s (player_spd=%.1f)",
                        order.committed == 0 ? "PRESS" : "HARASS",
                        (double)state->order_speed_ema);
                }
                state->order_phase = OrderPhase::Holding;
            }
            return;
        }

        // HOLDING: re-open the order ONLY on a material change -- the reactive base
        // decisively favors the OTHER side (a Schmitt flip past kOrderHysteresis), the
        // squad size changed, or the slow heartbeat elapsed. Otherwise the latch holds.
        const int desired = desired_order(facts, event, state);
        const bool flip =
            state->last_decision >= 0 && desired != state->last_decision;
        const bool squad_changed = deficit != state->order_deficit_at_rearm;
        const bool heartbeat = now >= state->next_reanneal_time;

        if (flip || squad_changed || heartbeat) {
            commander_rearm(facts, event, state, roster, desired, deficit, now);
        }
    }
}

namespace
{
    // Declared config tunable, so the editor renders a checkbox for it.
    const WzBehaviorParamDesc kCommanderParams[] = {
        { "chart_driven", "Chart-driven (statechart owns the order phase)",
            WZ_BEHAVIOR_PARAM_BOOL, 0.0, nullptr },
    };
}

WZ_BEHAVIOR_MODULE_INIT_PARAMS(
    "tank_commander",
    commander_init,
    commander_on_event,
    kCommanderEvents,
    kCommanderParams)
