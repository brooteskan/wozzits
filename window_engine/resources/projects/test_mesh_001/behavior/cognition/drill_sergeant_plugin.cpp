#include <engine/behavior/behavior_module_api.h>

// drill_sergeant -- flips a co-located quantum_agent's HUB goal sign on a period and
// re-arms, so a STAR group (hub bonded to every member) visibly re-commits the whole
// squad in unison each time the command changes. The members carry no goals of their
// own -- their polarization arrives entirely through the star bond from the hub -- so
// this is the "commander orders, squad obeys" idiom (the tank commander's pattern),
// made watchable. Sets the hub goal every frame (constant to the current sign, so it
// is independent of dispatch order vs the agent's self.start) and flips + re-arms
// every `period` seconds.
//
// Config (optional, read live): slot [0] the hub qubit, mag [0.6] goal magnitude,
// period [8] seconds between command flips.

namespace
{
    struct SgtState
    {
        float timer = 0.0f;  // seconds since the last flip
        int8_t sign = 1;     // current command sign (+ -> |0>, - -> |1>)
    };

    static const char* kSgtEvents[] = { "frame.update" };

    void sgt_init(const WzBehaviorInitFacts* facts, WzBehaviorEntityId, void*)
    {
        (void)wz_instance_state<SgtState>(facts);
    }

    void sgt_on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event
            || wz_event_kind(event) != WZ_EVENT_FRAME_UPDATE) {
            return;
        }
        SgtState* s = wz_instance_state<SgtState>(facts);
        if (!s) {
            return;
        }
        const WzBehaviorEntityId self = wz_self(event);

        double slot = 0.0, mag = 0.6, period = 8.0;
        wz_config_number(facts, "slot", &slot);
        wz_config_number(facts, "mag", &mag);
        wz_config_number(facts, "period", &period);

        s->timer += wz_delta_seconds(facts);
        const bool flip = period > 0.0
            && static_cast<double>(s->timer) >= period;
        if (flip) {
            s->sign = static_cast<int8_t>(-s->sign);
            s->timer = 0.0f;
        }

        // Hold the current command on the hub every frame; flip + re-open the whole
        // group's deliberation on the beat so the squad re-commits in unison.
        wz_set_agent_goal(
            facts, self, slot < 0.0 ? 0u : static_cast<uint32_t>(slot),
            static_cast<float>(s->sign * mag));
        if (flip) {
            wz_rearm_agent(facts, self);
        }
    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "drill_sergeant",
    sgt_init,
    sgt_on_event,
    kSgtEvents)
