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
        "self.activated",   // fires on unpark -- a pool DEPLOY (claim lease + reset)
        "frame.update"
    };


    // Flip to true + rebuild this plugin to hand hull actuation (drive + turret aim) to
    // an attached tank_combat statechart -- the C++ mind still senses / decides / learns
    // / fires / recycles, only the physical APPLY is skipped. Also settable per-instance
    // via a "chart_driven" config key on this behavior (overrides this default when set).
    constexpr bool kChartDrivenDefault = false;

    void quantum_tank_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId self,
        void*)
    {
        QuantumTankState* state = wz_instance_state<QuantumTankState>(facts);
        if (!state) {
            return;
        }
        // The squad slot is a LEASE now: NOT claimed here (init runs once at prewarm,
        // while the pooled instance is parked), but on DEPLOY (self.activated /
        // self.start) in on_event, and released on death/recycle. So a parked reserve
        // holds no slot and can't inflate the commander's group agent past its cap.
        // We only ENSURE the shared roster exists here (get-or-create, init context --
        // where wz_create_shared_state lives); the lease is claimed frame-side via
        // wz_find_shared_state.
        (void)wz_create_shared_state(
            facts, kSquadRosterKey, sizeof(SquadRoster), alignof(SquadRoster));

        // Authorable knob: how fast the tank advances when it commits to ENGAGE.
        (void)wz_config_float(facts, "drive_speed", &state->drive_speed);

        // Hand hull actuation to an attached statechart: default from the compile-time
        // switch above, overridden per-instance by the "chart_driven" config (a declared
        // BOOL param -> editable as a checkbox in the inspector). The mind computes +
        // learns as always; only the physical drive/aim APPLY is skipped so the chart's
        // actuators own the hull.
        uint8_t chart_driven = kChartDrivenDefault ? 1u : 0u;
        (void)wz_config_bool(facts, "chart_driven", &chart_driven);
        state->chart_driven = (chart_driven != 0u);

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

        // THE "blink" teleport effect (teleport.h): expands a cyan bubble around
        // the tank, hides the tank-body subtree, jumps the root to a new spot on
        // the landscape, and shrinks the bubble to reveal it. Self-triggers on a
        // timer for now.
        teleport::init(facts, self, state->terrain, &state->teleport);

        // The squad commander (hidden top-level node). Optional -- the tank fights
        // solo if it isn't present.
        (void)wz_find_entity_by_authored_id(facts, "2:command", &state->command);

        // Life/death: a pooled enemy RECYCLES on death (release its squad lease +
        // park itself; the pool redeploys a spare) rather than respawning, so the
        // lifecycle machine runs IMMORTAL here -- it captures the spawn + reports
        // alive, but never triggers its own hide/respawn beat. The death edge is
        // polled directly in on_event against kEnemyMaxHealth.
        wz_find_descendant_by_name(facts, self, "hitbox", &state->hitbox);
        tank_lifecycle::init(
            state->hitbox, /*immortal*/ 0.0f, &state->lifecycle, state->command);
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

        // BLINK disposition (qubit 4): tactical pressure -> teleport out. Taking
        // fire pushes hard toward a jump, a terrain-blocked shot pushes mildly
        // (reposition for a clean line), and the bias keeps it put otherwise.
        // under_fire / has_los are last frame's sensed values (set in the learning
        // block below) -- a frame's lag is fine for a goal.
        const float blink_goal = tank_drive::clampf(
            (state->under_fire ? kBlinkUnderFireWeight : 0.0f)
                + (state->has_los ? 0.0f : kBlinkBlockedWeight)
                - kBlinkBias,
            -1.0f, 1.0f);

        wz_self_set_agent_goal(facts, event, 0u, pursue_goal);      // pursue vs hold
        wz_self_set_agent_goal(facts, event, 1u, posture_goal);     // close vs circle
        wz_self_set_agent_goal(facts, event, 2u, reconsider_goal);  // volatile vs stable
        wz_self_set_agent_goal(facts, event, 3u, fire_goal);        // fire vs conserve
        wz_self_set_agent_goal(
            facts, event, kBlinkDecisionQubit, blink_goal);         // blink vs stay
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

    // Can the gun actually POINT at the target's aim point, or is the required
    // elevation past its travel (so a committed shot flies over/under)? Uses the
    // SAME lifted aim point the gun is elevated toward, so this agrees with where
    // the barrel is actually driven.
    bool gun_can_reach(
        const WzBehaviorFrameFacts* facts, const QuantumTankState* state)
    {
        if (state->barrel == WZ_INVALID_BEHAVIOR_ENTITY
            || state->target == WZ_INVALID_BEHAVIOR_ENTITY)
        {
            return true;
        }
        const float need = tank_drive::local_elevation_to(
            facts, state->chassis.turret, state->barrel,
            state->target, agent_tank_config::kAimHeight);
        constexpr float kEps = 0.02f;   // ~1 deg slack at the travel limits
        return need >= kGunElevationMin - kEps
            && need <= kGunElevationMax + kEps;
    }

    // Would the round ACTUALLY reach the target, or does terrain intercept it
    // first? Casts the exact ray the launch will cast -- same muzzle anchor +
    // firing axis via cannon_fire::read_muzzle_anchor, same terrain -- so this is
    // the real shot line, not the higher eye-height LOS proxy. A gun snapped onto
    // the target that skims the ground, or one clamped below a low target, puts the
    // terrain hit SHORT of the target and is held. (read_muzzle_anchor reads the
    // same pre-propagate transform cannon_fire::tick reads this frame, so the test
    // matches the shot that fires.)
    bool shot_reaches_target(
        const WzBehaviorFrameFacts* facts, const QuantumTankState* state)
    {
        if (state->terrain == WZ_INVALID_BEHAVIOR_ENTITY
            || state->target == WZ_INVALID_BEHAVIOR_ENTITY)
        {
            return true;   // nothing to test against -> don't suppress the shot
        }
        float ax, ay, az, nx, ny, nz;
        if (!cannon_fire::read_muzzle_anchor(
                facts, state->cannon.data, ax, ay, az, nx, ny, nz))
        {
            return true;
        }
        WzVec3 tp{};
        if (!wz_read_world_position(facts, state->target, &tp)) {
            return true;
        }
        const float tdx = tp.x - ax, tdy = tp.y - ay, tdz = tp.z - az;
        const float target_dist = sqrtf(tdx * tdx + tdy * tdy + tdz * tdz);

        WzSurfaceSample surf{};
        if (wz_query_collision_surface_ray(
                facts, state->terrain,
                WzVec3{ ax, ay, az }, WzVec3{ nx, ny, nz },
                target_dist + agent_tank_config::kShotClearMargin, &surf)
            && surf.hit)
        {
            const float hx = surf.position.x - ax;
            const float hy = surf.position.y - ay;
            const float hz = surf.position.z - az;
            const float hit_dist = sqrtf(hx * hx + hy * hy + hz * hz);
            if (hit_dist < target_dist - agent_tank_config::kShotClearMargin) {
                return false;   // terrain intercepts the round short of the target
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

        const uint32_t kind = wz_event_kind(event);

        // self.start fires at MATERIALIZATION -- which for a pooled instance is at
        // PREWARM, while parked: the active mask isn't current during the spawn's own
        // rebuild, so the self.start gate lets it through even though the node is
        // parked. A parked reserve must NOT claim a lease or run combat, so just
        // return here. The real DEPLOY is the self.activated unpark edge below (which
        // IS driven by the current mask, so it only fires on an actual 0->1 unpark).
        if (kind == WZ_EVENT_SELF_START) {
            return;
        }

        // DEPLOY: self.activated fires on the unpark edge -- the pool's initial deploy
        // AND each redeploy after a recycle. Claim a squad LEASE (lowest free slot)
        // and reset combat state so a recycled instance re-enters fresh -- healed,
        // full ammo, stuck to the terrain. The pool has already positioned us; the
        // tank_id < 0 guard prevents a double-claim within one deployment.
        if (kind == WZ_EVENT_SELF_ACTIVATED) {
            if (state->tank_id < 0) {
                SquadRoster* roster = static_cast<SquadRoster*>(
                    wz_find_shared_state(facts, kSquadRosterKey));
                state->tank_id = squad_roster_claim(roster);
            }
            state->ammo = agent_tank_config::kAmmoMax;
            if (auto* tally = wz_instance_state_of<tank_damage::Tally>(
                    facts, state->hitbox, tank_damage::kModule)) {
                tally->total = 0.0f;   // heal on (re)deploy
            }
            wz_self_set_terrain_alignment_rate(
                facts, event, agent_tank_config::kTerrainAlignRate);
            // Reset the blink so a recycle mid-teleport can't leave a redeployed
            // tank's body hidden (or its bubble stuck on).
            state->teleport.phase = teleport::Phase::Idle;
            teleport::show_body(facts, &state->teleport, 1u);
            teleport::show_bubble(facts, &state->teleport, 0u);
            wz_log_infof(facts, "[qtank:%d] deployed (squad lease)", state->tank_id);
            return;
        }

        // Life/death: capture the spawn + report alive. Runs IMMORTAL (see init) --
        // the machine never triggers its own death beat; death is a pool-recycle,
        // handled just below.
        tank_lifecycle::tick(facts, event, &state->lifecycle);
        const uint8_t alive = tank_lifecycle::is_alive(&state->lifecycle);

        // DEATH -> RECYCLE: while deployed, poll the hitbox damage tally; on death,
        // release the squad lease + PARK ourselves (back to the pool) instead of
        // respawning. The pool sees the vacancy and redeploys a spare. Parking stops
        // dispatch, so this fires exactly once; the next DEPLOY heals + re-leases us.
        //
        // NOT gated on tank_id >= 0: frame.update only fires for an ACTIVE (deployed)
        // tank, and a tank that deployed SOLO (roster full -> tank_id == -1) is just as
        // deployed and must be killable -- gating on the lease would leave it unkillable,
        // pinning its pool slot forever. squad_roster_release is a no-op for -1, and the
        // tally reset below guards against a re-trigger before the park takes effect.
        if (kind == WZ_EVENT_FRAME_UPDATE
            && state->hitbox != WZ_INVALID_BEHAVIOR_ENTITY)
        {
            if (auto* tally = wz_instance_state_of<tank_damage::Tally>(
                    facts, state->hitbox, tank_damage::kModule)) {
                if (tally->total >= kEnemyMaxHealth) {
                    SquadRoster* roster = static_cast<SquadRoster*>(
                        wz_find_shared_state(facts, kSquadRosterKey));
                    squad_roster_release(roster, state->tank_id);
                    wz_log_infof(
                        facts, "[qtank:%d] destroyed (%.0f dmg) -> recycled to pool",
                        state->tank_id, (double)tally->total);
                    state->tank_id = -1;
                    tally->total = 0.0f;
                    wz_self_set_active(facts, event, 0u);   // park -> pool reserve
                    wz_self_set_visible(facts, event, 0u);  // hide
                    return;
                }
            }
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
        // Compute turret_yaw always (it feeds aim_error + learning below); APPLY only
        // when the mind owns actuation -- a chart's aim_at owns the turret otherwise.
        if (!state->chart_driven) {
            tank_drive::aim_turret(facts, state->chassis.turret, state->chassis.turret_yaw);
        }

        // Elevate the gun to hold the target's pitch for direct fire, clamped to
        // the gun's travel -- the barrel (and thus its shot ray, which rides the
        // barrel marker) tracks the target's HEIGHT, the AI counterpart to the
        // player's manual raise/lower.
        if (state->barrel != WZ_INVALID_BEHAVIOR_ENTITY && !state->chart_driven) {
            const float elev = tank_drive::clampf(
                tank_drive::local_elevation_to(
                    facts, state->chassis.turret, state->barrel,
                    state->target, agent_tank_config::kAimHeight),
                kGunElevationMin, kGunElevationMax);
            tank_drive::elevate_gun(facts, state->barrel, elev);
        }

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
            // Sensed shot-readiness for the fire_cannon actuator (chart-driven fire):
            // the aim/range/LOS gate plus gun-reach + terrain-clear, minus the tactical
            // decision / ammo / cooldown. The exact sense the C++ discharge uses below.
            state->can_hit = shot
                && gun_can_reach(facts, state)
                && shot_reaches_target(facts, state);
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
                // Ready to pull the trigger on the coarse conditions? Only then pay
                // for the aim-quality checks: the gun must be able to POINT at the
                // target (elevation not clamped) AND the round's real muzzle ray
                // must reach it without digging into terrain short. This is what
                // stops the shots-into-the-landscape -- the coarse `shot` gate above
                // (yaw + eye-height LOS) still drives learning, but the DISCHARGE is
                // held until the actual shot solution is good.
                const bool ready = alive && weapons_free && state->ammo > 0
                    && now >= state->next_fire_time
                    && state->canon_audio != WZ_INVALID_BEHAVIOR_ENTITY;
                // chart_driven hands the DISCHARGE to the chart's `call fire_cannon`
                // (which re-checks can_hit + cooldown); the mind still senses + learns.
                if (!state->chart_driven && ready && state->can_hit)
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

            // BLINK: when the deliberated blink disposition (qubit 4) commits --
            // or, for an unobserved tank that may never fully collapse, LEANS --
            // toward teleporting (|0>), and the bubble effect is idle + off
            // cooldown, discharge a jump. teleport.h runs the bubble + relocation.
            WzAgentDecision blink_dec{};
            const uint8_t has_blink = wz_self_agent_decision_at(
                facts, event, kBlinkDecisionQubit, &blink_dec);
            const bool wants_blink =
                has_blink
                && (blink_dec.committed == 0
                    || (blink_dec.committed == -1 && blink_dec.marginal > 0.0f));
            const double blink_now = wz_sim_time(facts);
            if (alive && wants_blink
                && state->teleport.phase == teleport::Phase::Idle
                && blink_now >= state->next_blink_time)
            {
                teleport::trigger(facts, &state->teleport);
                state->next_blink_time = blink_now + kBlinkCooldown;
                wz_log_infof(
                    facts, "[qtank:%d] BLINK  ctx=%s dist=%.1f",
                    state->tank_id, ctx ? "braced" : "fleeing",
                    (double)state->distance_to_target);
            }
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

        // Terrain alignment is set on DEPLOY (self.start / self.activated) above.

        // Frozen while destroyed; the lifecycle teleports us to HQ on respawn.
        // Also frozen mid-blink (velocity ZEROED, not just left un-set -- the
        // motion component retains the last velocity, which would coast the tank
        // and fight the teleport's world-translation set) so the jump lands clean.
        // When chart_driven, the chart's pursue/halt actuators own the hull velocity;
        // the mind skips its own drive so the two don't fight (last-writer-wins).
        if (alive && !state->chart_driven) {
            const bool blinking = teleport::is_blinking(&state->teleport);
            tank_drive::drive_facing(
                facts, event,
                blinking ? 0.0f : state->heading,
                blinking ? 0.0f : state->speed);
        }

        // Advance the cannon shot (shared with the player). Fired above on a shot;
        // runs even while destroyed so an in-flight shot still finishes.
        cannon_fire::tick(facts, event, &state->cannon);

        // Advance the blink teleport (bubble expand/shrink + the jump). Runs every
        // frame; self-triggers on its own timer.
        teleport::tick(facts, &state->teleport);
    }
}

namespace
{
    // Declared config tunables, so the editor renders typed fields for them (a checkbox
    // for chart_driven, a number for drive_speed) instead of nothing. The runtime read
    // is unchanged (wz_config_bool / wz_config_float above); this just makes them
    // discoverable + gives authoring defaults.
    const WzBehaviorParamDesc kQuantumTankParams[] = {
        { "chart_driven", "Chart-driven (statechart owns the hull)",
            WZ_BEHAVIOR_PARAM_BOOL, 0.0, nullptr },
        { "drive_speed", "Engage drive speed (units/s)",
            WZ_BEHAVIOR_PARAM_FLOAT, 6.0, nullptr },
    };
}

WZ_BEHAVIOR_MODULE_INIT_PARAMS(
    "quantum_tank_agent",
    quantum_tank_init,
    quantum_tank_on_event,
    kQuantumTankEvents,
    kQuantumTankParams)
