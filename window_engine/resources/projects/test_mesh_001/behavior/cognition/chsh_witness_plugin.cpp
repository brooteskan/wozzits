#include <engine/behavior/behavior_module_api.h>

#include <cmath>
#include <cstdint>

// chsh_witness -- the end-to-end "more than a probabilistic FSM" demonstrator.
//
// It drives a CHSH test on the co-located quantum_agent using the non-commuting
// measurement ABI (wz_measure_agent_in_basis, v36). Each cycle it lets the agent
// anneal, MEASURES its two decision qubits along a chosen pair of axes, records the
// +/-1 correlation, then re-arms to re-prepare -- cycling through the four CHSH
// setting pairs (a,b) (a,b') (a',b) (a',b'). It accumulates
//   S = E(a,b) - E(a,b') + E(a',b) + E(a',b'),
// logs the running S, and once |S| exceeds the classical bound 2 (up to Tsirelson's
// 2*sqrt(2) ~ 2.83) emits a "chsh_violation" behavior event -- a correlation no
// classical / finite-state model, shared randomness included, can reproduce.
//
// REQUIRES an authored mind that (1) is ENTANGLED -- two coupled qubits on an
// entanglement-capable backend: chi 0 (exact) or chi >= 2 (TTN); chi 1 (loopy BP)
// is a product state and stays at the classical floor -- and (2) NEVER SELF-COMMITS
// (set the mind's commit confidence high, e.g. 2.0, so its own policy can't collapse
// it in the z basis before this measures it in the chosen basis). The engine already
// PROVES the physics in tests/cognition/measure_basis_tests.cpp; this shows it live,
// through the authored pipeline: mind editor -> quantum_agent -> ABI -> witness.
//
// Config (optional): shots [64] samples per setting pair before a report;
// settle [1.0] seconds to anneal after each re-arm before measuring (>= the mind's
// anneal time); angle_a [0], angle_ap [pi/2], angle_b [pi/4], angle_bp [3pi/4] the
// four measurement axes in radians (defaults are the CHSH-optimal set).

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    struct ChshState
    {
        double sum[4] = { 0.0, 0.0, 0.0, 0.0 };  // sum of v0*v1 per setting pair
        uint32_t count[4] = { 0u, 0u, 0u, 0u };  // shots per setting pair
        uint32_t pair = 0u;                       // next setting pair to sample (0..3)
        uint32_t total = 0u;                      // total shots taken
        uint32_t next_report = 0u;                // report S once total reaches this
        float timer = 0.0f;                       // seconds annealed since last re-arm
        bool started = false;                     // done the initial re-arm?
        bool emitted = false;                     // chsh_violation emitted?
    };

    static const char* kChshEvents[] = { "self.start", "frame.update" };

    void chsh_reset(ChshState* s)
    {
        *s = ChshState{};
    }

    void chsh_init(const WzBehaviorInitFacts* facts, WzBehaviorEntityId, void*)
    {
        (void)wz_instance_state<ChshState>(facts);
    }

    // The two measurement axes for setting pair p: agent 0 gets a / a', agent 1
    // gets b / b'. Pairs: 0=(a,b) 1=(a,b') 2=(a',b) 3=(a',b').
    void axes_for(
        uint32_t p, double a, double ap, double b, double bp,
        double* t0, double* t1)
    {
        *t0 = (p < 2) ? a : ap;
        *t1 = (p == 0 || p == 2) ? b : bp;
    }

    void chsh_on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        ChshState* s = wz_instance_state<ChshState>(facts);
        if (!s) {
            return;
        }
        const WzBehaviorEventKind kind = wz_event_kind(event);
        if (kind == WZ_EVENT_SELF_START) {
            chsh_reset(s);
            return;
        }
        if (kind != WZ_EVENT_FRAME_UPDATE) {
            return;
        }

        double shots = 64.0, settle = 1.0;
        double a = 0.0, ap = kPi / 2.0, b = kPi / 4.0, bp = 3.0 * kPi / 4.0;
        wz_config_number(facts, "shots", &shots);
        wz_config_number(facts, "settle", &settle);
        wz_config_number(facts, "angle_a", &a);
        wz_config_number(facts, "angle_ap", &ap);
        wz_config_number(facts, "angle_b", &b);
        wz_config_number(facts, "angle_bp", &bp);
        const uint32_t per_pair = shots < 1.0 ? 1u : static_cast<uint32_t>(shots);
        const WzBehaviorEntityId entity = event->entity;

        // Kick off the first anneal so the initial measurement lands on a prepared
        // (not equal-superposition) state.
        if (!s->started) {
            s->started = true;
            s->next_report = 4u * per_pair;
            s->timer = 0.0f;
            wz_self_rearm_agent(facts, event);
            wz_log_infof(
                facts, "[chsh_witness] running: %u shots/pair, settle %.2fs",
                per_pair, settle);
            return;
        }

        // Wait for the agent to anneal to its (entangled) ground state before
        // measuring; re-arm restored an equal superposition, which carries none of
        // the correlation.
        s->timer += wz_delta_seconds(facts);
        if (static_cast<double>(s->timer) < settle) {
            return;
        }

        // One shot on the current setting pair: measure BOTH qubits on the SAME
        // prepared state (the projection of qubit 0 conditions qubit 1 through the
        // shared wavefunction -- the back-action that makes it non-classical).
        double t0 = 0.0, t1 = 0.0;
        axes_for(s->pair, a, ap, b, bp, &t0, &t1);
        int8_t b0 = 0, b1 = 0;
        if (!wz_measure_agent_in_basis(
                facts, entity, nullptr, 0u, static_cast<float>(t0), &b0)) {
            return;  // no co-located quantum_agent yet; try again next frame
        }
        if (!wz_measure_agent_in_basis(
                facts, entity, nullptr, 1u, static_cast<float>(t1), &b1)) {
            // Agent has fewer than two decisions -- not a CHSH-shaped mind.
            wz_self_rearm_agent(facts, event);
            s->timer = 0.0f;
            return;
        }
        const double v0 = b0 ? -1.0 : 1.0;
        const double v1 = b1 ? -1.0 : 1.0;
        s->sum[s->pair] += v0 * v1;
        s->count[s->pair] += 1u;
        s->pair = (s->pair + 1u) & 3u;  // round-robin so all four fill evenly
        s->total += 1u;

        // Report the running S once each pair has `per_pair` more samples.
        if (s->total >= s->next_report
            && s->count[0] && s->count[1] && s->count[2] && s->count[3]) {
            const double e0 = s->sum[0] / s->count[0];
            const double e1 = s->sum[1] / s->count[1];
            const double e2 = s->sum[2] / s->count[2];
            const double e3 = s->sum[3] / s->count[3];
            const double S = e0 - e1 + e2 + e3;
            wz_log_infof(
                facts,
                "[chsh_witness] shots=%u  S=%+.3f  (classical <= 2, Tsirelson ~ 2.83)",
                s->total, S);
            if (!s->emitted && std::fabs(S) > 2.0) {
                s->emitted = true;
                wz_emit_behavior_event(facts, entity, "chsh_violation");
                wz_log_infof(
                    facts,
                    "[chsh_witness] |S| = %.3f > 2 -- beyond any classical/FSM model",
                    std::fabs(S));
            }
            s->next_report = s->total + 4u * per_pair;
        }

        // Re-prepare for the next shot.
        wz_self_rearm_agent(facts, event);
        s->timer = 0.0f;
    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "chsh_witness",
    chsh_init,
    chsh_on_event,
    kChshEvents)
