#pragma once
// behavior/agent_tank_config.h
//
// Every tunable "knob" for the spawnable enemy tank in one place -- the numbers
// that shape how it drives, aims, maps world state to cognition goals, and paces
// its re-thinking. Kept out of the logic so this reads as a tuning sheet, and so
// these are the obvious candidates to promote to authorable scene config later.
// (Shared player+agent drive tuning -- move speed, turn speed, turret arc, body
// forward -- lives in tank_drive.h, since both tanks use it.)

namespace agent_tank_config
{
    // --- Steering / drive ---
    inline constexpr float kSteerGain = 2.0f;         // rad/s per rad of heading error
    inline constexpr float kStandoff = 40.0f;         // orbit radius + posture pivot (world u)
    inline constexpr float kTerrainAlignRate = 0.618f;  // rad/s hull-to-surface align

    // --- Speed ---
    inline constexpr float kBaseSpeed = 1.0f;         // engaged forward-speed scale
    inline constexpr float kWaveringSpeed = 0.35f;    // creep factor while deliberating

    // --- Pursue-goal mapping (distance / flee -> engage bias) ---
    inline constexpr float kPursueBaseGoal = 0.6f;
    inline constexpr float kPursueFleeGain = 0.08f;   // per (u/s) of target speed
    inline constexpr float kPursueFleeMax = 0.4f;

    // --- Posture-goal mapping (range error -> close | circle) ---
    inline constexpr float kPostureGoalGain = 0.8f;   // scales the normalized range error

    // --- Meta-qubit "reconsider": how volatile is the situation -> re-think rate.
    // volatility = target_speed*kVolSpeedGain + |closing_rate|*kVolCloseGain - kVolBias.
    // > 0 pushes the reconsider qubit toward VOLATILE (re-think soon); < 0 STABLE. ---
    inline constexpr float kVolSpeedGain = 0.30f;
    inline constexpr float kVolCloseGain = 0.20f;
    inline constexpr float kVolBias = 0.20f;          // calm situations sit below 0
    inline constexpr float kReconsiderGoalGain = 1.0f;
    inline constexpr double kReannealVolatile = 2.0;    // seconds when the meta says VOLATILE
    inline constexpr double kReannealStable = 8.0;      // ...                        STABLE
    inline constexpr double kReannealUntilDecided = 4.0;  // ... before it has committed

    // --- Command structure: the hidden "command" node decides a GROUP order that
    // every enemy tank reads and obeys. One shared encoding so the commander (which
    // decides it) and the tanks (which read it) agree, and it's changed once. ---
    enum class Command : int8_t {
        Undecided = -1,  // commander still deliberating
        Press = 0,       // |0>: close the gap as a squad, aggressive
        Harass = 1,      // |1>: keep distance, cautious
    };
    inline Command command_from(int8_t committed)
    {
        return static_cast<Command>(committed);
    }

    // Commander: it PRESSES when the player is slow/passive, HARASSES when the
    // player is fast/aggressive. goal > 0 favors PRESS (|0>).
    inline constexpr float kCommandSpeedGain = 0.6f;   // per (u/s) of player speed
    inline constexpr float kCommandBias = 0.3f;        // subtracted: passive -> press
    inline constexpr double kCommandReanneal = 6.0;    // commander re-think interval (s)

    // --- SQUAD REINFORCEMENT: the command node brings its squad up to strength
    // from its OWN position (HQ), on its OWN clock -- this REPLACES the player's
    // spacebar spawner. The commander is the spawner entity, so enemy tanks appear
    // AT the command node and roll out from there. Bounded on purpose: a spawn is
    // O(scene) and the roster is grow-only (dead tanks RESPAWN at HQ, they never
    // leave), so member_count IS the live squad size -- once it reaches the target
    // the commander holds. kSquadTargetSize stays within the group agent's member
    // budget (hub + up to kQuantumAgentMaxDecisions-1 stance qubits = 3) so every
    // reinforced tank still gets its own stance qubit in the group order.
    inline constexpr int    kSquadTargetSize = 3;         // tanks the commander brings up
    inline constexpr double kReinforceCooldown = 4.0;     // min seconds between spawns
    inline constexpr float  kReinforceSpawnAhead = 12.0f; // +Z from HQ (spawner frame)
    inline constexpr float  kReinforceSpread = 8.0f;      // lateral gap so they don't stack

    // --- DOCTRINE LEARNING: the commander's own quantum_agent carries a memory
    // qubit (|0> = pressing pays off vs THIS player, |1> = caution does). Each
    // re-anneal it rewards the doctrine from the squad's net shot-exchange delta
    // (shots_landed - fire_taken since last time) and folds the learned preference
    // into its PRESS/HARASS order -- the design's "director node learns", squad-
    // level credit assignment on top of the tactical rule above. ---
    inline constexpr uint32_t kDoctrineMemoryQubit = 0u;
    inline constexpr float kDoctrineReward = 0.3f;  // per winning re-anneal window
    inline constexpr float kDoctrineGain = 0.6f;    // learned doctrine -> order bias

    // --- ORDER STABILITY: the PRESS/HARASS order is a low-frequency STANCE, not a
    // per-frame reaction. Undamped it flip-flops each re-anneal when the player's
    // speed sits near the press/harass crossover, so damp it two ways: an EMA on the
    // player speed the order reads (a momentary twitch won't swing the stance) and a
    // HYSTERESIS bias toward the CURRENTLY committed side (a Schmitt trigger -- it
    // takes a clear contrary signal to flip). Together they turn a jittery order into
    // a deliberate one that holds until the battlefield genuinely shifts. ---
    inline constexpr float kOrderSpeedEmaAlpha = 0.25f;  // EMA weight of each new speed sample
    inline constexpr float kOrderHysteresis = 0.8f;      // goal bias toward the held order

    // --- REINFORCE DECISION: bringing a tank up is a COMMANDER decision now, not a
    // pool timer. The command node's group agent carries a REINFORCE qubit -- the TOP
    // member (index kReinforceQubit) star-bonded to the PRESS/HARASS hub, so an
    // aggressive (PRESS) commander commits reserves and a cautious (HARASS) one holds
    // them (the entanglement the design wants). Its own goal is UNDERSTRENGTH
    // pressure: the wider the gap between the live squad and kSquadTargetSize, the
    // harder it leans |0> (reinforce). On a commit, with room + kReinforceCooldown
    // elapsed, the commander posts a deploy to the squad_deploy queue and the pool
    // unparks a reserve. The group is reshaped ONCE to kReinforceGroupMembers (hub +
    // 3 tank stance slots + reinforce = 5 qubits, the agent cap) so the reinforce
    // qubit keeps a stable index and the order never re-anneals on a membership
    // change. Since members are indexed 1..member_count, the reinforce qubit (the
    // last member) sits at index == kReinforceGroupMembers. ---
    inline constexpr uint32_t kReinforceQubit = 4u;         // top member (kQuantumAgentMaxDecisions-1)
    inline constexpr uint32_t kReinforceGroupMembers = 4u;  // 3 tank stance slots + reinforce
    inline constexpr float kReinforceBias = -0.3f;          // at full strength -> hold reserves
    inline constexpr float kReinforceDeficitGain = 0.5f;    // per missing tank -> lean reinforce

    // How a tank folds the group command into its own goals.
    inline constexpr float kObeyPressPursue = 0.3f;    // PRESS: engage harder
    inline constexpr float kObeyPressClose = 0.5f;     //        + bias toward CLOSE
    inline constexpr float kObeyHarassCircle = 0.6f;   // HARASS: bias toward CIRCLE

    // The command node's GROUP agent is qubit 0 (command) + one member "stance"
    // qubit per live tank, star-bonded. A tank claims slot = tank_id + 1 and reads
    // its OWN stance -- entangled with the command and its siblings -- instead of
    // the command bit directly. The commander reshapes the group to the live squad
    // size (dynamic membership) with this bond strength.
    inline constexpr float kSquadStarCoupling = 1.5f;  // hub<->member ferro bond

    // --- CONTEXTUAL LEARNING: a per-tank 2-qubit memory holding an ENTANGLED
    // conditional policy, so aggression depends on the SITUATION instead of being
    // one global mood. Qubit 0 = CONTEXT (is the player engaging us?), qubit 1 =
    // ACTION (|0> aggressive, |1> cautious). The tank reinforces the joint
    // (context, action) branch from OUTCOMES each frame:
    //   * LANDING shots (our gun on the target + in range) -> reward (this context,
    //     AGGRESSIVE): pressing paid off HERE.
    //   * TAKING fire (the target's hull pointed at us + in range) -> reward (this
    //     context, CAUTIOUS): getting shot at teaches restraint HERE.
    // Then it reads conditional_preference(action | CURRENT context) WITHOUT
    // measuring and folds it into pursue/posture -- so it can learn e.g. aggressive
    // when the player is fleeing but cautious when the player is braced. ---
    inline constexpr uint32_t kContextMemoryQubit = 0u;     // player-engaging bucket
    inline constexpr uint32_t kAggressionMemoryQubit = 1u;  // aggressive|0> / cautious|1>

    // Context bucket boundary: the player is "engaging/braced" (context 1) when its
    // hull points within this arc of us, else "fleeing/passive" (context 0). Wider
    // than kFireArc -- context is a coarse situation, not a precise shot.
    inline constexpr float kContextArc = 0.8f;    // rad, ~46 deg hull-on-us

    // "Shot" geometry: a hit is credibly landed / taken when the gun (or hull) is
    // within this arc of the line to the other tank AND within this range AND has
    // clear LINE OF SIGHT (no terrain between us -- see kEyeHeight/kLosMargin).
    inline constexpr float kFireArc = 0.12f;      // rad, ~7 deg gun-on-target
    inline constexpr float kFireRange = 90.0f;    // world u, effective gun range

    // Line-of-sight test: march kLosSamples points along the gun->player segment
    // and sample terrain HEIGHT at each; if the ground pokes above the straight
    // sightline (by more than kLosClearance) a hill/ridge blocks the shot. Uses
    // height sampling (works for ANY terrain collision representation, unlike the
    // mesh-only ray query), with both ends lifted to gun height so the line clears
    // the hull/ground.
    // Kept low so the sightline hugs the terrain -- on gentle ground a high ray
    // clears every modest rise and LOS never blocks. Lower kEyeHeight = more
    // sensitive to hills; raise it if tanks start "seeing" through small bumps.
    inline constexpr float kEyeHeight = 1.5f;     // world u above tank origin
    inline constexpr int   kLosSamples = 12;      // points along the sightline
    inline constexpr float kLosClearance = 0.3f;  // world u the ground may graze

    // Aim the gun at a point kAimHeight above the target's ROOT (its hull/centre,
    // not its base): a direct shot then strikes the body and its trajectory rides
    // above the ground instead of skimming to the target's feet and digging into a
    // rise short of it. Matches kEyeHeight so the shot and the LOS test agree.
    inline constexpr float kAimHeight = 1.5f;     // world u above target origin

    // The tank only discharges when the round's ACTUAL muzzle ray reaches the
    // target: if terrain intercepts it more than kShotClearMargin short of the
    // target it would bury itself in the landscape, so hold fire. The margin (~a
    // hull length) keeps legitimate near-target shots (grazing the base) from being
    // suppressed.
    inline constexpr float kShotClearMargin = 4.0f;  // world u

    // Cannon reload: the tank fires at most once per this interval (seconds), and
    // only while its FIRE disposition is weapons-free (see below) and it has ammo.
    inline constexpr double kFireCooldown = 1.1;

    // --- LIMITED AMMO + FIRE DECISION: firing is no longer reflexive. A 4th
    // decision qubit (index 3) is the tank's FIRE DISPOSITION -- |0> weapons-free,
    // |1> conserve -- deliberated on the cognition's own clock like pursue/posture.
    // Its goal is driven by ammo (full -> fire freely, low -> conserve) and shot
    // quality (close targets are worth the round), so a low-ammo tank holds fire on
    // marginal shots and spends its last rounds on good ones. It only discharges
    // (and depletes ammo) when this disposition is committed weapons-free AND a
    // shot is lined up AND ammo remains. ---
    inline constexpr uint32_t kFireDecisionQubit = 3u;
    inline constexpr uint32_t kAmmoMax = 15u;         // rounds per tank (no resupply yet)
    inline constexpr float kFireAmmoWeight = 1.0f;    // full ammo -> fire, empty -> conserve
    inline constexpr float kFireRangeWeight = 0.6f;   // close target -> worth the shot

    // --- BLINK / TELEPORT DECISION: a 5th decision qubit (index 4) is the tank's
    // BLINK disposition -- |0> teleport away, |1> stay -- deliberated on the
    // cognition's own clock like pursue/posture/fire. Its goal is driven by
    // TACTICAL PRESSURE: taking fire pushes hard toward a blink (escape /
    // reposition), a terrain-blocked shot pushes mildly (jump for a clean line),
    // and a baseline bias keeps the tank PUT when nothing is pressing. The blink
    // discharges when this disposition commits (or leans) to |0>, the bubble
    // effect is idle, and the cooldown has elapsed; teleport.h runs the sequence.
    // Requires the co-located quantum_agent's `decisions` >= 5 (enemy prefab). ---
    inline constexpr uint32_t kBlinkDecisionQubit = 4u;
    inline constexpr double kBlinkCooldown = 6.0;        // min seconds between blinks
    inline constexpr float kBlinkUnderFireWeight = 1.2f; // taking fire -> blink out
    inline constexpr float kBlinkBlockedWeight = 0.8f;   // shot blocked -> reposition
    inline constexpr float kBlinkBias = 0.7f;            // baseline lean toward STAY

    // --- OBSERVATION-FORCED DECOHERENCE (quantum Zeno): when the player is looking
    // at the tank, its decisions collapse fast -> it commits early and acts
    // PREDICTABLY; unobserved, near-zero decoherence keeps it in superposition
    // (wavering / creeping) so it surprises you when you turn back. "Observed" is a
    // hull-facing proxy for line-of-sight (the chase camera aligns with the player's
    // hull forward) -- upgradeable to a true view frustum + occlusion later. ---
    inline constexpr float kObservedArc = 1.0f;          // rad (~57 deg) view cone
    inline constexpr float kObservedDecoherence = 2.5f;  // watched -> snap commit
    inline constexpr float kUnobservedDecoherence = 0.02f; // unwatched -> stays coherent

    // Reinforcement RATES, per SECOND of sustained advantage (scaled by frame dt
    // at the call site so learning speed is frame-rate INVARIANT -- same Poisson-
    // style cadence-invariance as the decoherence rate; a 144Hz machine must not
    // learn 5x faster than a 30Hz one). ~3/s matches the old 0.05/frame at 60fps.
    inline constexpr float kRewardLanding = 3.0f;    // toward aggressive when we hit
    inline constexpr float kRewardTakingFire = 3.0f; // toward cautious when shot at

    // How the learned aggression folds back into the tactical goals. A preference
    // of +1 (fully aggressive) adds this much engage/close bias; -1 (cautious)
    // subtracts it (leans hold / circle).
    inline constexpr float kMemoryPursueGain = 0.4f;   // aggression -> pursue harder
    inline constexpr float kMemoryPostureGain = 0.4f;  // aggression -> close in
}
