#include <engine/behavior/behavior_module_api.h>

#include "cognition_lamp.h"

// cognition_lamp -- the ACTUATOR that draws a quantum_agent's decision as geometry.
// See cognition_lamp.h for the authoring contract. This plugin only links the C ABI:
// it reads the committed decision + live marginal of a quantum_agent (co-located on
// the same node, or on another node named by the `agent_node` config -- so several
// lamps can share one entangled group agent) and turns it into child scale/visibility.
// It runs no cognition -- the built-in quantum_agent owns the wave function and, once
// started, self-drives its own think loop.

namespace
{
    using cognition_lamp::LampState;

    // self.start: log the wiring once (diagnosability). frame.update: read + draw.
    static const char* kLampEvents[] = { "self.start", "frame.update" };

    float clamp01(float v)
    {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }

    // Set a child body's uniform local scale (a no-op if the child was not found).
    void drive_child(
        const WzBehaviorFrameFacts* facts,
        WzBehaviorEntityId child,
        uint8_t found,
        uint8_t visible,
        float scale)
    {
        if (!found) {
            return;
        }
        wz_write_set_visible(facts, child, visible);
        wz_write_set_local_scale(facts, child, scale, scale, scale);
    }

    void lamp_init(const WzBehaviorInitFacts* facts, WzBehaviorEntityId, void*)
    {
        // Allocate the per-instance state (zeroed on first alloc -> default member
        // inits run: last_committed = -2). Preserved as-is across hot-reloads.
        (void)wz_instance_state<LampState>(facts);
    }

    void lamp_on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }

        const WzBehaviorEventKind kind = wz_event_kind(event);
        const WzBehaviorEntityId self = wz_self(event);

        LampState* s = wz_instance_state<LampState>(facts);
        if (!s) {
            return;
        }

        // --- read config (live; cheap) ------------------------------------------
        double slot_d = 0.0;
        wz_config_number(facts, cognition_lamp::kKeySlot, &slot_d);
        const uint32_t slot = slot_d > 0.0 ? (uint32_t)slot_d : 0u;

        float full_scale = cognition_lamp::kDefaultFullScale;
        wz_config_float(facts, cognition_lamp::kKeyFullScale, &full_scale);

        double metronome = 0.0;
        wz_config_number(facts, cognition_lamp::kKeyRearmMetronome, &metronome);

        // --- resolve which node's agent to read ---------------------------------
        // Default is the co-located agent (self); `agent_node` names another node so
        // several lamps can visualize the qubits of ONE shared/entangled agent. A
        // named-but-missing node leaves `agent` invalid, so the read below no-ops and
        // the self.start line flags it.
        WzBehaviorEntityId agent = self;
        uint8_t agent_named = 0;
        char agent_node[64] = { 0 };
        uint32_t agent_node_req = 0;
        if (wz_config_string(
                facts, cognition_lamp::kKeyAgentNode,
                agent_node, sizeof(agent_node), &agent_node_req)
            && agent_node[0]) {
            agent_named = 1;
            WzBehaviorEntityId found = WZ_INVALID_BEHAVIOR_ENTITY;
            agent = wz_find_entity_by_name(facts, agent_node, &found)
                ? found
                : (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY;
        }

        // --- resolve the two branch bodies in self's subtree --------------------
        WzBehaviorEntityId lamp0 = WZ_INVALID_BEHAVIOR_ENTITY;
        WzBehaviorEntityId lamp1 = WZ_INVALID_BEHAVIOR_ENTITY;
        const uint8_t has0 =
            wz_find_descendant_by_name(facts, self, cognition_lamp::kChild0, &lamp0);
        const uint8_t has1 =
            wz_find_descendant_by_name(facts, self, cognition_lamp::kChild1, &lamp1);

        if (kind == WZ_EVENT_SELF_START) {
            // One line that says whether the lamp is wired: which slot, and whether
            // it found its two bodies. If a body is MISSING or the agent read fails
            // below, this is where you look first.
            wz_log_infof(
                facts,
                "[lamp] start slot=%u agent=%s(%s) full=%.2f metronome=%.1f "
                "%s=%s %s=%s",
                slot,
                agent_named ? agent_node : "self",
                agent != (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY
                    ? "ok" : "MISSING",
                (double)full_scale,
                metronome,
                cognition_lamp::kChild0, has0 ? "ok" : "MISSING",
                cognition_lamp::kChild1, has1 ? "ok" : "MISSING");
            s->last_committed = -2;
            s->time_since_commit = 0.0f;
            return;
        }

        if (kind != WZ_EVENT_FRAME_UPDATE) {
            return;
        }

        // --- read the agent's decision for this slot ----------------------------
        WzAgentDecision d{};
        if (!wz_agent_decision_at(facts, agent, slot, &d)) {
            return;  // no such quantum_agent / bad slot / missing node (start logged)
        }

        const float z = d.marginal;             // <sigma_z> in [-1, 1], > 0 leans |0>
        const float p0 = clamp01(0.5f * (1.0f + z));
        const float p1 = clamp01(0.5f * (1.0f - z));

        if (d.committed < 0) {
            // DELIBERATING: both bodies visible, each sized to its branch probability.
            drive_child(facts, lamp0, has0, 1, full_scale * p0);
            drive_child(facts, lamp1, has1, 1, full_scale * p1);
            s->time_since_commit = 0.0f;
        }
        else {
            // COMMITTED: winner full-size + visible, loser hidden.
            const uint8_t won1 = (d.committed == 1) ? 1u : 0u;
            drive_child(facts, lamp0, has0, won1 ? 0u : 1u, won1 ? 0.0f : full_scale);
            drive_child(facts, lamp1, has1, won1 ? 1u : 0u, won1 ? full_scale : 0.0f);

            // Commit EDGE (was deliberating / uninitialized last frame): click + log.
            if (s->last_committed < 0) {
                char clip[64] = { 0 };
                uint32_t required = 0;
                if (wz_config_string(
                        facts, cognition_lamp::kKeyClickSound,
                        clip, sizeof(clip), &required)
                    && clip[0]) {
                    wz_write_play_sound_named(facts, self, clip);
                }
                wz_log_infof(
                    facts, "[lamp] commit=%d z=%.2f", (int)d.committed, (double)z);
            }

            // Rearm metronome: after `metronome` seconds committed, re-open the
            // decision so the demo cycles (deliberate -> commit -> re-anneal).
            s->time_since_commit += wz_delta_seconds(facts);
            if (metronome > 0.0
                && (double)s->time_since_commit >= metronome) {
                wz_rearm_agent(facts, agent);
                s->time_since_commit = 0.0f;
            }
        }

        s->last_committed = d.committed;
    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "cognition_lamp",
    lamp_init,
    lamp_on_event,
    kLampEvents)
