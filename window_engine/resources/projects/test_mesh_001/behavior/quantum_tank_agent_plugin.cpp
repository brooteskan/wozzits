#include "agent_tank.h"
#include "tank_drive.h"
#include "agent_tank_config.h"

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
        WzBehaviorEntityId self,
        void*)
    {
        QuantumTankState* state = wz_instance_state<QuantumTankState>(facts);
        if (!state) {
            return;
        }
        // Claim a squad slot ONCE (first init only -- re-init returns preserved
        // state whose id is already set). The slot = our index in the shared
        // roster, and roster.member_count is the squad size the commander sizes its
        // group agent to. Shared state is get-or-create by key, so all tanks share
        // one roster.
        if (state->tank_id < 0) {
            SquadRoster* roster = static_cast<SquadRoster*>(
                wz_create_shared_state(
                    facts, kSquadRosterKey,
                    sizeof(SquadRoster), alignof(SquadRoster)));
            state->tank_id = roster ? roster->member_count : 0;
            if (roster) {
                roster->member_count++;
            }
            state->ammo = agent_tank_config::kAmmoMax;  // limited magazine, no resupply yet
        }
        // Authorable knob: how fast the tank advances when it commits to ENGAGE.
        (void)wz_config_float(facts, "drive_speed", &state->drive_speed);

        // The clipmap landscape node -- its Heightfield collision is what we sample
        // for line of sight (and ground height later).
        uint8_t result = wz_find_entity_by_authored_id(
            facts, "clipmap_landscape", &state->terrain);
        wz_log_infof(facts, "[agent tank init] find landscape: %u", result);

        // The cannon audio source lives on the enemy tank's OWN root node (self),
        // so each tank's shot is emitted + spatialized from ITS position -- not the
        // player's camera. The enemy_tank scenelet authors an AudioSource on the
        // root (cannon bank); we play "Canon_a" on it when a shot is lined up.
        state->canon_audio = self;

        // The player is a spawned prefab now (its authored id is prefixed on
        // spawn), so find it by its unique NAME "tank" rather than a scene id.
        result = wz_find_entity_by_name(facts, "tank", &state->player);
        wz_log_infof(facts, "[agent tank init] find player: %u", result);

        // For now the engagement target IS the player tank. Steering, turret aim,
        // and distance sensing all key off `target`, so switching who we engage
        // later is a one-line change.
        state->target = state->player;

        result = wz_find_descendant_by_name(facts, self,"turret", &state->chassis.turret);
        wz_log_infof(facts, "[agent tank init] find turret: %u", result);

        result = wz_find_descendant_by_name(facts, self, "gun", &state->barrel);
        wz_log_infof(facts, "[agent tank init] find barrel: %u", result);

        // THE "fire the cannon" -- shared with the player (cannon_fire.h). Anchors
        // the muzzle flash on the barrel; fired below when the agent commits to a
        // shot.
        cannon_fire::init(
            facts, self, state->chassis.turret, state->barrel,
            state->terrain, &state->cannon);

        // The squad commander (hidden top-level node). Optional -- the tank fights
        // solo if it isn't present.
        (void)wz_find_entity_by_authored_id(facts, "2:command", &state->command);
    }

    // sense_world lives in agent_tank.h now -- SHARED with the commander.

    // Map the world snapshot to the agent's per-decision goal biases and push them
    // -- CONTINUOUS, no thresholds:
    //   qubit 0 PURSUE   -- leans harder when the target flees;
    //   qubit 1 POSTURE  -- slides CLOSE (far) <-> CIRCLE (near) across a standoff;
    //   qubit 2 RECONSIDER (meta) -- how VOLATILE the situation is, which decides
    //     how soon to re-think (see the cadence logic in on_event).
    void push_goals_from_world(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        const QuantumTankState* state)
    {
        using namespace agent_tank_config;

        float pursue_goal = kPursueBaseGoal
            + tank_drive::clampf(
                state->target_speed * kPursueFleeGain, 0.0f, kPursueFleeMax);

        float posture_goal = tank_drive::clampf(
            (state->distance_to_target - kStandoff) / kStandoff, -1.0f, 1.0f)
            * kPostureGoalGain;

        // OBEY the squad's group decision. Rather than read the command bit
        // directly, read our OWN stance qubit in the command node's GROUP agent
        // (slot = tank_id + 1) -- it's ENTANGLED with the command and our siblings
        // through the star bond, so the whole squad's stance is one wave function
        // (if a sibling collapses, ours is conditioned too). PRESS shifts us toward
        // engage + close, HARASS toward keeping distance. Our own cognition still
        // deliberates; this just biases it.
        if (state->command != WZ_INVALID_BEHAVIOR_ENTITY && state->tank_id >= 0) {
            const uint32_t slot = static_cast<uint32_t>(state->tank_id) + 1u;
            WzAgentDecision stance{};
            // Returns 0 if the commander hasn't grown the group to our slot yet
            // (a brief lag after spawn) -> we fight solo until then.
            if (wz_agent_decision_at(facts, state->command, slot, &stance)) {
                const Command c = command_from(stance.committed);
                if (c == Command::Press) {
                    pursue_goal += kObeyPressPursue;
                    posture_goal += kObeyPressClose;    // + = CLOSE (|0>)
                } else if (c == Command::Harass) {
                    posture_goal -= kObeyHarassCircle;  // - = CIRCLE (|1>)
                }
            }
        }

        // LEARNED aggression, CONDITIONED on the current context: read the entangled
        // policy for "the situation we're actually in" (context_engaging) WITHOUT
        // measuring -- +1 = the memory learned aggression pays off in THIS context,
        // -1 = caution does. Fold it into the tactical goals. So a tank can press
        // when the player is fleeing yet circle when the player is braced, instead
        // of one global mood. Survives every re-anneal (memory is outside the
        // coordination).
        const float aggression =
            wz_self_agent_conditional_pref(
                facts, event,
                kContextMemoryQubit, state->context_engaging,
                kAggressionMemoryQubit);
        pursue_goal += aggression * kMemoryPursueGain;
        posture_goal += aggression * kMemoryPostureGain;   // + = CLOSE (|0>)

        // Meta: > 0 when the target is moving / range is changing fast.
        const float volatility =
            state->target_speed * kVolSpeedGain
            + fabsf(state->closing_rate) * kVolCloseGain
            - kVolBias;
        const float reconsider_goal =
            tank_drive::clampf(volatility, -1.0f, 1.0f) * kReconsiderGoalGain;

        // FIRE disposition (qubit 3): weapons-free (|0>, goal > 0) when we have
        // ammo and a worthwhile shot; conserve (|1>) as ammo runs low, especially
        // on distant targets. So a nearly-dry tank saves its rounds for close shots.
        const float ammo_frac = tank_drive::clampf(
            static_cast<float>(state->ammo)
                / static_cast<float>(kAmmoMax), 0.0f, 1.0f);
        const float range_quality =
            0.5f - state->distance_to_target / kFireRange;   // + when close
        const float fire_goal = tank_drive::clampf(
            (2.0f * ammo_frac - 1.0f) * kFireAmmoWeight
                + range_quality * kFireRangeWeight,
            -1.0f, 1.0f);

        wz_self_set_agent_goal(facts, event, 0u, pursue_goal);      // pursue vs hold
        wz_self_set_agent_goal(facts, event, 1u, posture_goal);     // close vs circle
        wz_self_set_agent_goal(facts, event, 2u, reconsider_goal);  // volatile vs stable
        wz_self_set_agent_goal(facts, event, 3u, fire_goal);        // fire vs conserve
    }

    // Clear line of sight from our gun to the player? March points along the
    // gun->player segment and sample terrain height at each; if the ground rises
    // above the straight sightline, a hill or ridge blocks the shot. Height
    // sampling works for any terrain collision representation (a clipmap is a
    // heightmap surface, no overhangs). Assumes clear when there's nothing to test
    // against so a tank without a resolved landscape still fights.
    bool has_line_of_sight(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        const QuantumTankState* state)
    {
        if (state->terrain == WZ_INVALID_BEHAVIOR_ENTITY
            || state->target == WZ_INVALID_BEHAVIOR_ENTITY)
        {
            return true;
        }
        WzMat4 self_w{};
        WzVec3 tpos{};
        if (!wz_self_world_transform(facts, event, &self_w)
            || !wz_read_world_position(facts, state->target, &tpos))
        {
            return true;
        }
        using namespace agent_tank_config;
        const float ox = self_w.m[12], oy = self_w.m[13] + kEyeHeight,
                    oz = self_w.m[14];
        const float px = tpos.x, py = tpos.y + kEyeHeight, pz = tpos.z;
        for (int i = 1; i < kLosSamples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kLosSamples);
            const float sx = ox + (px - ox) * t;
            const float sz = oz + (pz - oz) * t;
            const float sightline_y = oy + (py - oy) * t;
            WzSurfaceSample ground{};
            if (wz_sample_terrain_surface(facts, state->terrain, sx, sz, &ground)
                && ground.hit
                && ground.position.y > sightline_y + kLosClearance)
            {
                return false;   // the ground pokes above the sightline here
            }
        }
        return true;
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

        // Sense the world every frame, then RE-ANNEAL the stance on a cadence the
        // COGNITION picks -- the "reconsider" meta-qubit (2): its committed VOLATILE
        // (|0>) vs STABLE (|1>) verdict, driven by how much the world is changing,
        // sets how soon to re-open the tactical decisions. So the tank re-thinks
        // often when things are dynamic and holds steady when they're calm -- no
        // fixed timer, no dead zone. (The coupling is structural, surviving the
        // re-anneal.)
        if (wz_event_kind(event) == WZ_EVENT_FRAME_UPDATE) {
            sense_world(facts, event, state);

            // Classify the CURRENT context ONCE per frame (both the reward and the
            // conditional read must agree within a frame): the player is
            // "engaging/braced" when its hull points within kContextArc of us.
            // The same hull-facing read drives OBSERVATION-FORCED DECOHERENCE: if
            // the player is looking at us (a wider view cone), crank decoherence so
            // our decisions snap-commit (quantum Zeno -> predictable); unobserved,
            // near-zero decoherence keeps us coherent (wavering) so we surprise the
            // player when they look back.
            if (state->target != WZ_INVALID_BEHAVIOR_ENTITY) {
                using namespace agent_tank_config;
                const float facing = fabsf(tank_drive::hull_aim_error(
                    facts, state->target, wz_self(event)));
                state->context_engaging = facing < kContextArc ? 1u : 0u;

                const uint8_t observed = facing < kObservedArc ? 1u : 0u;
                wz_self_set_agent_decoherence(
                    facts, event,
                    observed ? kObservedDecoherence : kUnobservedDecoherence);
                if (observed != state->observed) {
                    state->observed = observed;
                    wz_log_infof(
                        facts, "[qtank:%d] %s  (decoherence %s)",
                        state->tank_id,
                        observed ? "OBSERVED" : "unobserved",
                        observed ? "high -> snap-commit"
                                 : "low -> stays coherent");
                }
            }

            // Once the meta-qubit commits, it chooses the interval to the next
            // re-anneal, measured from the last one.
            WzAgentDecision reconsider{};
            if (wz_self_agent_decision_at(facts, event, 2u, &reconsider)
                && reconsider.committed != -1)
            {
                state->next_reanneal_time = state->last_reanneal_time
                    + (reconsider.committed == 0
                        ? agent_tank_config::kReannealVolatile   // |0> volatile
                        : agent_tank_config::kReannealStable);   // |1> stable
            }

            const double now = wz_sim_time(facts);
            if (now >= state->next_reanneal_time) {
                push_goals_from_world(facts, event, state);
                wz_self_rearm_agent(facts, event);
                state->last_reanneal_time = now;
                // Provisional cadence until the meta-qubit commits this cycle.
                state->next_reanneal_time =
                    now + agent_tank_config::kReannealUntilDecided;
                wz_log_infof(facts,
                    "[qtank:%d] re-anneal dist=%.1f close=%.1f tgt_spd=%.1f meta=%d",
                    state->tank_id,
                    (double)state->distance_to_target,
                    (double)state->closing_rate,
                    (double)state->target_speed,
                    (int)reconsider.committed);
            }
        }

        // Coupled decisions from the co-located quantum_agent:
        //   decision 0 = PURSUE (|0>) vs HOLD (|1>)   -> gates our SPEED
        //   decision 1 = CLOSE  (|0>) vs CIRCLE (|1>) -> picks our APPROACH
        //   decision 2 = RECONSIDER (meta)            -> sets the re-think cadence
        // If this node has no quantum_agent there is nothing to actuate.
        WzAgentDecision pursue{};
        if (!wz_self_agent_decision(facts, event, &pursue)) {
            return;
        }
        WzAgentDecision posture{};
        const uint8_t has_posture =
            wz_self_agent_decision_at(facts, event, 1u, &posture);

        // SPEED from PURSUE. Committed ENGAGE -> full; HOLD -> stop; still
        // deliberating -> CREEP forward in proportion to how far it leans toward
        // engaging (marginal > 0), so you can SEE the wave function wavering
        // before it collapses.
        float speed_factor = 0.0f;
        if (pursue.committed == 0) {
            speed_factor = 1.0f;                       // ENGAGE
        } else if (pursue.committed == -1) {
            speed_factor = agent_tank_config::kWaveringSpeed
                * (pursue.marginal > 0.0f ? pursue.marginal : 0.0f);  // wavering
        }
        state->speed = agent_tank_config::kBaseSpeed * speed_factor;

        // HEADING from POSTURE. CIRCLE (|1>) orbits the player at a standoff;
        // CLOSE (|0>) or undecided drives the nose straight at the player.
        if (has_posture && posture.committed == 1) {
            state->heading = tank_drive::orbit_yaw_rate(
                facts, event, state->target, agent_tank_config::kStandoff,
                agent_tank_config::kSteerGain, tank_drive::kTurnSpeed);
        } else {
            state->heading = tank_drive::face_yaw_rate(
                facts, event, state->target,
                agent_tank_config::kSteerGain, tank_drive::kTurnSpeed);
        }

        // Turret always tracks the target, within the frontal arc.
        state->chassis.turret_yaw = tank_drive::clampf(
            tank_drive::face_bearing_to(facts, event, state->target),
            -tank_drive::kTurretHalfArc,
            tank_drive::kTurretHalfArc);
        tank_drive::aim_turret(facts, state->chassis.turret, state->chassis.turret_yaw);

        // How far off the gun is from the target (0 = on target; nonzero when the
        // target is beyond the frontal arc so the hull must reposition).
        state->aim_error = tank_drive::aim_error(
            facts, event, state->target, state->chassis.turret_yaw);

        // LEARN a CONTEXT-DEPENDENT policy from outcomes (frame.update only --
        // continuous reinforcement). Two aim proxies, each gated on range, reinforce
        // the JOINT (context, action) branch of the memory:
        //   * OUR gun on the target + in range  -> LANDING shots -> reward
        //     (this context, AGGRESSIVE |0>): pressing paid off in this situation.
        //   * the target's HULL pointed at us + in range -> TAKING fire -> reward
        //     (this context, CAUTIOUS |1>): getting shot at here teaches restraint.
        // Because the reward is bucketed by context_engaging, the tank learns
        // different dispositions per situation (an entangled conditional policy),
        // not one global mood. (Later, landing/taking-fire can key off real
        // projectile geometry; for now the pointed-gun / pointed-hull proxies stand
        // in.)
        if (wz_event_kind(event) == WZ_EVENT_FRAME_UPDATE
            && state->target != WZ_INVALID_BEHAVIOR_ENTITY)
        {
            using namespace agent_tank_config;
            // Scale the per-second reward RATES by frame dt so learning speed is
            // frame-rate invariant (amplifying by e^{rate*dt} each frame integrates
            // to e^{rate} per second regardless of fps).
            const float dt = wz_delta_seconds(facts);
            const uint8_t ctx = state->context_engaging;
            const bool in_range = state->distance_to_target < kFireRange;

            // Shared squad tally the COMMANDER learns its doctrine from: bump it on
            // the acquisition edges below (get-or-create; may be null very early).
            SquadRoster* roster = static_cast<SquadRoster*>(
                wz_find_shared_state(facts, kSquadRosterKey));

            // LINE OF SIGHT: a shot also needs a clear line to the player -- a hill
            // or ridge between us blocks it. Log when sight is lost / regained.
            const bool los = has_line_of_sight(facts, event, state);
            if (!los && state->has_los) {
                wz_log_infof(
                    facts, "[qtank:%d] LOST line of sight (terrain blocks the shot)",
                    state->tank_id);
            } else if (los && !state->has_los) {
                wz_log_infof(
                    facts, "[qtank:%d] regained line of sight", state->tank_id);
            }
            state->has_los = los ? 1u : 0u;

            // The deliberated FIRE disposition (qubit 3): weapons-free (0) vs
            // conserve (1) vs still deciding (-1). Log when it flips.
            WzAgentDecision fire_dec{};
            const uint8_t has_fire = wz_self_agent_decision_at(
                facts, event, kFireDecisionQubit, &fire_dec);
            if (has_fire && fire_dec.committed != state->fire_stance) {
                state->fire_stance = fire_dec.committed;
                wz_log_infof(
                    facts, "[qtank:%d] weapons %s  (ammo=%u)",
                    state->tank_id,
                    fire_dec.committed == 0 ? "FREE"
                        : (fire_dec.committed == 1 ? "HOLD (conserve)" : "deciding"),
                    state->ammo);
            }

            // A shot requires aim + range + LOS (so we never fire through a hill,
            // and never reward aggression for a blocked shot).
            const bool shot =
                in_range && fabsf(state->aim_error) < kFireArc && los;
            if (shot) {
                wz_self_agent_reward_pair(
                    facts, event,
                    kContextMemoryQubit, ctx,
                    kAggressionMemoryQubit, /*dec_value=*/0u,  // |0> aggressive
                    kRewardLanding * dt);

                // DISCHARGE only if the fire disposition is committed weapons-free
                // AND we still have ammo -- so a low-ammo tank that has chosen to
                // conserve holds its shot even with a clean line. Sound spatializes
                // from this tank's own audio source.
                const double now = wz_sim_time(facts);
                // Fire when weapons-free -- OR merely LEANING that way while still
                // deciding: an unobserved tank has near-zero decoherence so its fire
                // qubit may never fully commit, and it should still shoot when it
                // leans toward firing. Fall back to firing if the agent has no fire
                // qubit at all. Only a committed / leaning CONSERVE holds the shot.
                const bool weapons_free =
                    !has_fire
                    || fire_dec.committed == 0
                    || (fire_dec.committed == -1 && fire_dec.marginal > 0.0f);
                if (weapons_free && state->ammo > 0
                    && now >= state->next_fire_time
                    && state->canon_audio != WZ_INVALID_BEHAVIOR_ENTITY)
                {
                    cannon_fire::fire(&state->cannon);
                    state->ammo--;
                    state->next_fire_time = now + kFireCooldown;
                    wz_log_infof(
                        facts, "[qtank:%d] FIRE  ctx=%s dist=%.1f  ammo=%u",
                        state->tank_id, ctx ? "braced" : "fleeing",
                        (double)state->distance_to_target, state->ammo);
                    if (state->ammo == 0) {
                        wz_log_infof(
                            facts, "[qtank:%d] OUT OF AMMO", state->tank_id);
                    }
                }
            }
            // Log only on the RISING edge (acquiring the shot), with the learned
            // aggression lean for THIS context so you can watch the policy form:
            // pref -> +1 aggressive, -1 cautious.
            if (shot && !state->had_shot) {
                if (roster) { roster->shots_landed++; }   // squad exchange tally
                wz_log_infof(
                    facts,
                    "[qtank:%d] SHOT lined up  ctx=%s dist=%.1f aim=%.2f  pref=%.2f",
                    state->tank_id,
                    ctx ? "braced" : "fleeing",
                    (double)state->distance_to_target,
                    (double)state->aim_error,
                    (double)wz_self_agent_conditional_pref(
                        facts, event, kContextMemoryQubit, ctx,
                        kAggressionMemoryQubit));
            }
            state->had_shot = shot ? 1u : 0u;

            const float incoming =
                tank_drive::hull_aim_error(facts, state->target, wz_self(event));
            const bool fire = in_range && fabsf(incoming) < kFireArc;
            if (fire) {
                wz_self_agent_reward_pair(
                    facts, event,
                    kContextMemoryQubit, ctx,
                    kAggressionMemoryQubit, /*dec_value=*/1u,  // |1> cautious
                    kRewardTakingFire * dt);
            }
            if (fire && !state->under_fire) {
                if (roster) { roster->fire_taken++; }   // squad exchange tally
                wz_log_infof(
                    facts,
                    "[qtank:%d] TAKING fire  ctx=%s dist=%.1f  pref=%.2f",
                    state->tank_id,
                    ctx ? "braced" : "fleeing",
                    (double)state->distance_to_target,
                    (double)wz_self_agent_conditional_pref(
                        facts, event, kContextMemoryQubit, ctx,
                        kAggressionMemoryQubit));
            }
            state->under_fire = fire ? 1u : 0u;
        }

        // Announce each collapse of the joint decision (once per change).
        if (pursue.committed != state->last_decision) {
            state->last_decision = pursue.committed;
            wz_log_infof(
                facts,
                "[qtank:%d] pursue=%d (z=%.2f)  posture=%d",
                state->tank_id,
                (int)pursue.committed,
                pursue.marginal,
                (int)(has_posture ? posture.committed : -2));
        }

        switch (wz_event_kind(event)) {

        case WZ_EVENT_SELF_START:
        {
            // Keep it low: tanks suddenly lurching to the surface looks weird.
            wz_self_set_terrain_alignment_rate(
                facts, event, agent_tank_config::kTerrainAlignRate);
            return;   // set once; skip the motion code below on this event
        }

        default:
            break;
        }

        tank_drive::drive_facing(facts, event, state->heading, state->speed);

        // Advance the cannon shot (shared with the player). Fired above on a shot.
        cannon_fire::tick(facts, event, &state->cannon);
    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "quantum_tank_agent",
    quantum_tank_init,
    quantum_tank_on_event,
    kQuantumTankEvents)
