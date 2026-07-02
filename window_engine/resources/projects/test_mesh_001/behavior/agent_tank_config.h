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

    // --- LEARNING: a per-tank 1-qubit AGGRESSION memory (|0> = aggressive,
    // |1> = cautious), held in the co-located quantum_agent's memory register
    // (outside the coordination, so it accumulates across every re-anneal). The
    // tank reinforces it from OUTCOMES each frame:
    //   * LANDING shots (our gun on the target + in range) -> reward toward
    //     aggressive; being effective pays off.
    //   * TAKING fire (the target's hull pointed at us + in range) -> reward
    //     toward cautious; getting shot at teaches restraint.
    // Then it reads <sigma_z> back and folds it into pursue/posture, so a tank
    // that keeps landing hits presses harder and one that keeps getting shot
    // learns to circle. Memory qubit index is 0. ---
    inline constexpr uint32_t kAggressionMemoryQubit = 0u;

    // "Shot" geometry: a hit is credibly landed / taken when the gun (or hull) is
    // within this arc of the line to the other tank AND within this range.
    inline constexpr float kFireArc = 0.12f;      // rad, ~7 deg gun-on-target
    inline constexpr float kFireRange = 90.0f;    // world u, effective gun range

    // Per-frame reinforcement strengths (small; the memory saturates over many
    // frames of sustained advantage, not one lucky frame).
    inline constexpr float kRewardLanding = 0.05f;   // toward aggressive when we hit
    inline constexpr float kRewardTakingFire = 0.05f; // toward cautious when shot at

    // How the learned aggression folds back into the tactical goals. A preference
    // of +1 (fully aggressive) adds this much engage/close bias; -1 (cautious)
    // subtracts it (leans hold / circle).
    inline constexpr float kMemoryPursueGain = 0.4f;   // aggression -> pursue harder
    inline constexpr float kMemoryPostureGain = 0.4f;  // aggression -> close in
}
