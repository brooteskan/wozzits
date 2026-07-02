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
    inline constexpr float kBaseSpeed = 0.5f;         // engaged forward-speed scale
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

    // --- DOCTRINE LEARNING: the commander's own quantum_agent carries a memory
    // qubit (|0> = pressing pays off vs THIS player, |1> = caution does). Each
    // re-anneal it rewards the doctrine from the squad's net shot-exchange delta
    // (shots_landed - fire_taken since last time) and folds the learned preference
    // into its PRESS/HARASS order -- the design's "director node learns", squad-
    // level credit assignment on top of the tactical rule above. ---
    inline constexpr uint32_t kDoctrineMemoryQubit = 0u;
    inline constexpr float kDoctrineReward = 0.3f;  // per winning re-anneal window
    inline constexpr float kDoctrineGain = 0.6f;    // learned doctrine -> order bias

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
    // within this arc of the line to the other tank AND within this range.
    inline constexpr float kFireArc = 0.12f;      // rad, ~7 deg gun-on-target
    inline constexpr float kFireRange = 90.0f;    // world u, effective gun range

    // Cannon reload: the tank fires whenever it has a shot lined up, at most once
    // per this interval (seconds). Unlimited ammo.
    inline constexpr double kFireCooldown = 1.1;

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
