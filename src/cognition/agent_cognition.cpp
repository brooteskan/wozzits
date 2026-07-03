#include <cognition/agent_cognition.h>

#include <cognition/conditional_policy.h>
#include <cognition/group_topology.h>
#include <cognition/learning.h>
#include <cognition/loopy_bp.h>
#include <cognition/ttn.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace wz::engine::cognition
{
    namespace
    {
        // Build the exact joint-state backend (chi = 0): arbitrary pairwise bonds
        // + summed per-agent goals.
        std::optional<Coordination> build_exact(const AgentSpec& spec)
        {
            ExactGroup g = make_exact_group(spec.agent_count, spec.bonds);
            set_goals(g, spec.goals);
            return Coordination{ std::move(g) };
        }

        // Build the chi-truncated TTN chain (chi >= 2). The MPS structure is a
        // chain, so every bond must be a nearest-neighbour edge (i, i+1); goals
        // map onto the per-agent goal field. Returns nullopt if a bond is not a
        // chain edge.
        std::optional<Coordination> build_ttn(const AgentSpec& spec)
        {
            const uint32_t n = spec.agent_count;
            std::vector<double> coupling(n > 0 ? n - 1u : 0u, 0.0);
            for (const ExactBond& b : spec.bonds) {
                const uint32_t lo = std::min(b.a, b.b);
                const uint32_t hi = std::max(b.a, b.b);
                if (hi != lo + 1u || hi >= n) {
                    return std::nullopt;  // not a nearest-neighbour chain edge
                }
                coupling[lo] = b.j;
            }

            TtnChain t = make_ttn_chain(n, std::move(coupling), spec.chi);
            for (const Goal& goal : spec.goals) {
                if (goal.agent < n) {
                    t.goal_field[goal.agent] += goal.field;
                }
            }
            return Coordination{ std::move(t) };
        }

        // chi == 1: the LoopyBpNetwork backend (self-consistent mean field with
        // damping), works for ANY topology -- trees and cyclic villages -- and
        // scales linearly, so it is the large-group / cyclic-village path. Uses the
        // default damping (0.5); a per-spec damping knob is a follow-up.
        std::optional<Coordination> build_loopy(const AgentSpec& spec)
        {
            LoopyBpGroup g = make_loopy_bp_group(spec.agent_count, spec.bonds);
            set_goals(g, spec.goals);
            return Coordination{ std::move(g) };
        }

        // Build the backend the spec selects (chi == 0 exact / chi == 1 loopy BP /
        // chi >= 2 TTN). A FRESH register in equal superposition, which is why rearm
        // can reuse this to genuinely re-open a collapsed decision.
        //
        // The bonds an Agent stores are RAW (as authored); this is the single choke
        // point that re-canonicalizes them on every build, so whichever backend we
        // dispatch to always receives clean girth >= 3 input.
        std::optional<Coordination> build_coordination(const AgentSpec& spec)
        {
            // Canonicalize to girth>=3 (drop self-bonds/zeros, sum parallels) so every
            // backend gets clean input; the loopy-tier BP math will require it, and it
            // is a no-op for already-clean specs. Idempotent, so rearm/reshape are safe.
            CanonicalBonds canon_bonds = canonicalize_bonds(spec.agent_count, spec.bonds);
            AgentSpec canon = spec;
            canon.bonds = std::move(canon_bonds.bonds);
            if (canon.chi == 0) return build_exact(canon);
            if (canon.chi == 1) return build_loopy(canon);
            return build_ttn(canon);  // chi >= 2 (chi is uint32_t, so 0/1/>=2 total)
        }
    }

    AgentHandle AgentCognitionStore::create(const AgentSpec& spec)
    {
        if (spec.agent_count == 0) {
            return kInvalidAgent;
        }

        std::optional<Coordination> coordination = build_coordination(spec);
        if (!coordination) {
            return kInvalidAgent;
        }

        const AgentHandle h = next_++;
        Agent agent;
        agent.coordination = std::move(*coordination);
        agent.clock = spec.clock;
        agent.commit = spec.commit;
        // Structure kept so rearm can rebuild a fresh register (goals come from
        // goal_fields, so we deliberately do NOT store spec.goals).
        agent.bonds = spec.bonds;
        agent.chi = spec.chi;
        agent.seed = spec.seed;
        agent.rng = qstate::Rng{ spec.seed };
        agent.latched.assign(spec.agent_count, std::nullopt);
        agent.marginal_cache.assign(spec.agent_count, 0.0);
        // Mirror the goals baked into the backend so set_goal() can re-bias one
        // decision without needing the others re-supplied.
        agent.goal_fields.assign(spec.agent_count, 0.0);
        for (const Goal& goal : spec.goals) {
            if (goal.agent < spec.agent_count) {
                agent.goal_fields[goal.agent] += goal.field;
            }
        }
        agent.agent_count = spec.agent_count;
        // Optional LEARNING memory: a separate register in equal superposition,
        // held OUTSIDE the coordination so it is never measured and its learned
        // bias accumulates across commits / rearms / reshapes.
        if (spec.memory_qubits > 0) {
            agent.memory = qstate::uniform(spec.memory_qubits);
            agent.memory_qubits = spec.memory_qubits;
        }
        agents_.emplace(h, std::move(agent));
        return h;
    }

    bool AgentCognitionStore::destroy(AgentHandle h)
    {
        return agents_.erase(h) != 0;
    }

    std::size_t AgentCognitionStore::retain(const std::vector<AgentHandle>& live)
    {
        const std::unordered_set<AgentHandle> keep(live.begin(), live.end());
        std::size_t dropped = 0;
        for (auto it = agents_.begin(); it != agents_.end();) {
            if (keep.find(it->first) == keep.end()) {
                it = agents_.erase(it);
                ++dropped;
            } else {
                ++it;
            }
        }
        return dropped;
    }

    bool AgentCognitionStore::start(AgentHandle h, double now)
    {
        Agent* a = find(h);
        if (!a) {
            return false;
        }
        wz::engine::cognition::start(a->clock, now);
        return true;
    }

    double AgentCognitionStore::think(AgentHandle h, double now)
    {
        Agent* a = find(h);
        if (!a) {
            return 0.0;
        }

        // Capture the elapsed sim-time BEFORE tick advances the clock -- the
        // decoherence collapse probability is 1 - e^{-rate * elapsed}.
        const bool was_started = a->clock.started;
        const double prev = a->clock.last_tick;
        const double dtau = tick(a->coordination, a->clock, now);
        const double dt = was_started ? std::max(0.0, now - prev) : 0.0;

        // A committed decision is a COLLAPSED branch, but relaxation re-mixes it
        // each step -- re-project every latched qubit so the still-undecided
        // decisions keep deliberating CONDITIONED on the ones already made (a
        // coupled decision respects the bond instead of drifting free).
        for (uint32_t i = 0; i < a->agent_count; ++i) {
            if (a->latched[i].has_value()) {
                collapse(a->coordination, i, *a->latched[i]);
            }
        }

        // One bulk BP read -> cache (frame-path readers are O(1)); re-read after
        // any NEW commit so a later decision this tick sees the conditioned state.
        std::vector<double> z = decisions(a->coordination);
        for (uint32_t i = 0; i < a->agent_count && i < z.size(); ++i) {
            a->marginal_cache[i] = z[i];
            if (a->latched[i].has_value()) {
                continue;
            }
            if (std::optional<bool> bit =
                    try_commit_marginal(z[i], a->commit, dt, a->rng)) {
                a->latched[i] = bit;
                // Collapse NOW: condition the remaining decisions this tick on it,
                // so coupled outcomes stay jointly consistent (no zero-probability
                // joint like 01 from an entangled 00/11 pair).
                collapse(a->coordination, i, *bit);
                z = decisions(a->coordination);
            }
        }
        return dtau;
    }

    bool AgentCognitionStore::set_goal(
        AgentHandle h, uint32_t agent, double field)
    {
        Agent* a = find(h);
        if (!a || agent >= a->agent_count) {
            return false;
        }
        a->goal_fields[agent] = field;

        // Re-apply the FULL goal set to the backend (set_goals resets the field
        // vector, so a per-index push must carry the others).
        std::vector<Goal> goals;
        goals.reserve(a->agent_count);
        for (uint32_t i = 0; i < a->agent_count; ++i) {
            goals.push_back(Goal{ .agent = i, .field = a->goal_fields[i] });
        }
        wz::engine::cognition::set_goals(a->coordination, goals);
        return true;
    }

    bool AgentCognitionStore::reward(
        AgentHandle h, uint32_t memory_qubit, bool toward, double strength)
    {
        Agent* a = find(h);
        if (!a || memory_qubit >= a->memory_qubits) {
            return false;
        }
        // Amplify the branch of this memory qubit selected by `toward`: mask picks
        // the qubit, match sets which basis value (|0> toward == true, |1>
        // otherwise) gets the e^{strength} boost. Monotonic + saturating, so
        // repeated rewards converge toward that branch (a learning curve).
        const uint64_t mask = 1ull << memory_qubit;
        const uint64_t match = toward ? 0ull : mask;
        wz::engine::cognition::reward(a->memory, mask, match, strength);
        return true;
    }

    double AgentCognitionStore::memory_preference(
        AgentHandle h, uint32_t memory_qubit) const
    {
        const Agent* a = find(h);
        if (!a || memory_qubit >= a->memory_qubits) {
            return 0.0;
        }
        return wz::engine::cognition::memory_preference(a->memory, memory_qubit);
    }

    bool AgentCognitionStore::reward_pair(
        AgentHandle h,
        uint32_t ctx_qubit, bool ctx_value,
        uint32_t dec_qubit, bool dec_value,
        double strength)
    {
        Agent* a = find(h);
        if (!a || ctx_qubit >= a->memory_qubits || dec_qubit >= a->memory_qubits) {
            return false;
        }
        wz::engine::cognition::reward_pair(
            a->memory, ctx_qubit, ctx_value ? 1u : 0u,
            dec_qubit, dec_value ? 1u : 0u, strength);
        return true;
    }

    double AgentCognitionStore::conditional_preference(
        AgentHandle h,
        uint32_t ctx_qubit, bool ctx_value,
        uint32_t dec_qubit) const
    {
        const Agent* a = find(h);
        if (!a || ctx_qubit >= a->memory_qubits || dec_qubit >= a->memory_qubits) {
            return 0.0;
        }
        return wz::engine::cognition::conditional_preference(
            a->memory, ctx_qubit, ctx_value ? 1u : 0u, dec_qubit);
    }

    bool AgentCognitionStore::set_decoherence(AgentHandle h, double rate)
    {
        Agent* a = find(h);
        if (!a) {
            return false;
        }
        a->commit.decoherence_rate = rate < 0.0 ? 0.0 : rate;
        return true;
    }

    bool AgentCognitionStore::rearm(AgentHandle h, double now)
    {
        Agent* a = find(h);
        if (!a) {
            return false;
        }

        // Rebuild a FRESH coordination (register back in equal superposition) from
        // the kept structure + the CURRENT goals, so re-deliberation genuinely
        // re-explores instead of nudging the already-collapsed state. Then clear
        // the latches and restart the anneal clock.
        AgentSpec spec;
        spec.agent_count = a->agent_count;
        spec.bonds = a->bonds;
        spec.chi = a->chi;
        spec.seed = a->seed;
        spec.goals.reserve(a->agent_count);
        for (uint32_t i = 0; i < a->agent_count; ++i) {
            spec.goals.push_back(Goal{ .agent = i, .field = a->goal_fields[i] });
        }
        std::optional<Coordination> coordination = build_coordination(spec);
        if (!coordination) {
            return false;
        }
        a->coordination = std::move(*coordination);

        std::fill(a->latched.begin(), a->latched.end(), std::nullopt);
        wz::engine::cognition::start(a->clock, now);
        return true;
    }

    bool AgentCognitionStore::reshape(
        AgentHandle h,
        uint32_t agent_count,
        const std::vector<ExactBond>& bonds,
        double now)
    {
        Agent* a = find(h);
        if (!a || agent_count == 0) {
            return false;
        }

        // Resize the bookkeeping (existing goal fields survive where the count
        // overlaps; new qubits start unbiased). Latches + marginals reset -- the
        // reshaped group re-deliberates.
        a->goal_fields.resize(agent_count, 0.0);
        a->bonds = bonds;
        a->latched.assign(agent_count, std::nullopt);
        a->marginal_cache.assign(agent_count, 0.0);

        AgentSpec spec;
        spec.agent_count = agent_count;
        spec.bonds = bonds;
        spec.chi = a->chi;
        spec.seed = a->seed;
        spec.goals.reserve(agent_count);
        for (uint32_t i = 0; i < agent_count; ++i) {
            spec.goals.push_back(Goal{ .agent = i, .field = a->goal_fields[i] });
        }
        std::optional<Coordination> coordination = build_coordination(spec);
        if (!coordination) {
            return false;
        }
        a->coordination = std::move(*coordination);
        a->agent_count = agent_count;
        wz::engine::cognition::start(a->clock, now);
        return true;
    }

    double AgentCognitionStore::marginal(AgentHandle h, uint32_t agent) const
    {
        const Agent* a = find(h);
        if (!a || agent >= a->agent_count) {
            return 0.0;
        }
        return a->marginal_cache[agent];
    }

    std::optional<bool> AgentCognitionStore::committed(
        AgentHandle h, uint32_t agent) const
    {
        const Agent* a = find(h);
        if (!a || agent >= a->agent_count) {
            return std::nullopt;
        }
        return a->latched[agent];
    }

    bool AgentCognitionStore::alive(AgentHandle h) const
    {
        return find(h) != nullptr;
    }

    uint32_t AgentCognitionStore::agent_count(AgentHandle h) const
    {
        const Agent* a = find(h);
        return a ? a->agent_count : 0u;
    }

    const AgentCognitionStore::Agent* AgentCognitionStore::find(
        AgentHandle h) const
    {
        const auto it = agents_.find(h);
        return it == agents_.end() ? nullptr : &it->second;
    }

    AgentCognitionStore::Agent* AgentCognitionStore::find(AgentHandle h)
    {
        const auto it = agents_.find(h);
        return it == agents_.end() ? nullptr : &it->second;
    }
}
