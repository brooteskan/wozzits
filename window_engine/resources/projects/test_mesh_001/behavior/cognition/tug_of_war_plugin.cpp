#include <engine/behavior/behavior_module_api.h>

// tug_of_war -- a demo-control actuator that PINS two of a co-located quantum_agent's
// decisions against each other and re-arms on a period. Each frame it pushes
// goal(slot_a) = +pin and goal(slot_b) = -pin through the write seam (constant, so it
// is independent of behavior-dispatch order relative to the agent's self.start), then
// re-opens the whole deliberation every `period` seconds so it cycles.
//
// On a FERROMAGNETIC chain with the two ENDS pinned OPPOSITE this produces a DOMAIN
// WALL: the ends commit to opposite colors and a single flip boundary forms somewhere
// in the middle -- a different wagon each cycle (the chain pays for exactly one broken
// bond). The pinning attenuates toward the centre, so the middle wagons stay
// undecided longest and the commit runs ends -> middle.
//
// Config (all optional, read live):
//   slot_a  [0]    - decision pinned toward |0> (+pin)
//   slot_b  [4]    - decision pinned toward |1> (-pin)
//   pin     [0.8]  - goal magnitude on each end
//   period  [15]   - seconds between re-arms (0 = latch, never re-open)

namespace
{
    struct TugState
    {
        float timer = 0.0f;  // seconds since the last re-arm
    };

    static const char* kTugEvents[] = { "frame.update" };

    void tug_init(const WzBehaviorInitFacts* facts, WzBehaviorEntityId, void*)
    {
        (void)wz_instance_state<TugState>(facts);
    }

    void tug_on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event
            || wz_event_kind(event) != WZ_EVENT_FRAME_UPDATE) {
            return;
        }
        TugState* s = wz_instance_state<TugState>(facts);
        if (!s) {
            return;
        }
        const WzBehaviorEntityId self = wz_self(event);

        double slot_a = 0.0, slot_b = 4.0, pin = 0.8, period = 15.0;
        wz_config_number(facts, "slot_a", &slot_a);
        wz_config_number(facts, "slot_b", &slot_b);
        wz_config_number(facts, "pin", &pin);
        wz_config_number(facts, "period", &period);

        // Constant opposite pins on the two ends (+pin -> |0>, -pin -> |1>). Set
        // every frame so it does not matter whether the agent existed yet when this
        // behavior first ran; a set on a missing agent / bad slot is a harmless
        // no-op. Takes effect on the agent's next think.
        wz_set_agent_goal(
            facts, self, slot_a < 0.0 ? 0u : (uint32_t)slot_a,
            static_cast<float>(pin));
        wz_set_agent_goal(
            facts, self, slot_b < 0.0 ? 0u : (uint32_t)slot_b,
            -static_cast<float>(pin));

        // Re-open the whole deliberation every `period` seconds so the domain wall
        // re-samples (a different boundary each cycle). Goals survive the re-arm.
        s->timer += wz_delta_seconds(facts);
        if (period > 0.0 && static_cast<double>(s->timer) >= period) {
            wz_rearm_agent(facts, self);
            s->timer = 0.0f;
        }
    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "tug_of_war",
    tug_init,
    tug_on_event,
    kTugEvents)
