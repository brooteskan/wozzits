#include <engine/behavior/behavior_module_api.h>

#include <math.h>

// zeno_observer -- observation-forced decoherence (the quantum Zeno effect). Finds the
// player and, when it is within `radius` (horizontal distance, so terrain height does
// not matter), drives the CO-LOCATED quantum_agent's decoherence HIGH -- a watched
// agent snap-commits fast + predictably. Unwatched, decoherence drops to ~0 and the
// agent stays coherent, deliberating indefinitely (its confidence is authored
// unreachably high, so decoherence is the ONLY commit path). Watch it -> it collapses
// and re-decides rapidly; look away -> it freezes mid-thought.
//
// Config (all optional, read live):
//   radius      [25]     - horizontal distance within which the player "observes" it
//   observed    [2.5]    - decoherence rate while watched (fast, predictable commits)
//   idle        [0.02]   - decoherence rate while unwatched (effectively frozen)
//   player_name ["tank"] - node name of the observer (the player tank)

namespace
{
    struct ZenoState
    {
        uint8_t observed = 2;  // 2 = uninitialized, so the first state logs
    };

    static const char* kZenoEvents[] = { "frame.update" };

    void zeno_init(const WzBehaviorInitFacts* facts, WzBehaviorEntityId, void*)
    {
        (void)wz_instance_state<ZenoState>(facts);
    }

    void zeno_on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event
            || wz_event_kind(event) != WZ_EVENT_FRAME_UPDATE) {
            return;
        }
        ZenoState* s = wz_instance_state<ZenoState>(facts);
        if (!s) {
            return;
        }

        double radius = 25.0, observed_rate = 2.5, idle_rate = 0.02;
        wz_config_number(facts, "radius", &radius);
        wz_config_number(facts, "observed", &observed_rate);
        wz_config_number(facts, "idle", &idle_rate);
        char player_name[32] = "tank";
        uint32_t required = 0;
        wz_config_string(
            facts, "player_name", player_name, sizeof(player_name), &required);

        // Observed if the player is within `radius` horizontally. No player found /
        // no position -> unobserved (the default idle state).
        uint8_t observed = 0;
        WzBehaviorEntityId player = WZ_INVALID_BEHAVIOR_ENTITY;
        WzVec3 me{}, them{};
        if (wz_find_entity_by_name(
                facts, player_name[0] ? player_name : "tank", &player)
            && wz_read_world_position(facts, wz_self(event), &me)
            && wz_read_world_position(facts, player, &them)) {
            const float dx = me.x - them.x;
            const float dz = me.z - them.z;
            const float dist = sqrtf(dx * dx + dz * dz);
            observed = dist <= static_cast<float>(radius) ? 1u : 0u;
        }

        wz_self_set_agent_decoherence(
            facts, event,
            observed ? static_cast<float>(observed_rate)
                     : static_cast<float>(idle_rate));

        if (observed != s->observed) {
            s->observed = observed;
            wz_log_infof(
                facts, "[zeno] %s (decoherence=%.2f)",
                observed ? "OBSERVED -- snapping" : "unwatched -- coherent",
                observed ? observed_rate : idle_rate);
        }
    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "zeno_observer",
    zeno_init,
    zeno_on_event,
    kZenoEvents)
