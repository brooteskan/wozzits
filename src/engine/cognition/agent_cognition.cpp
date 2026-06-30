#include <engine/cognition/agent_cognition.h>

#include <engine/cognition/ttn.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace wz::cognition
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
    }

    AgentHandle AgentCognitionStore::create(const AgentSpec& spec)
    {
        if (spec.agent_count == 0) {
            return kInvalidAgent;
        }

        std::optional<Coordination> coordination;
        if (spec.chi == 0) {
            coordination = build_exact(spec);
        } else if (spec.chi >= 2) {
            coordination = build_ttn(spec);
        } else {
            return kInvalidAgent;  // chi == 1 (mean-field goals) not wired yet
        }
        if (!coordination) {
            return kInvalidAgent;
        }

        const AgentHandle h = next_++;
        Agent agent;
        agent.coordination = std::move(*coordination);
        agent.clock = spec.clock;
        agent.commit = spec.commit;
        agent.rng = wz::qstate::Rng{ spec.seed };
        agent.latched.assign(spec.agent_count, std::nullopt);
        agent.marginal_cache.assign(spec.agent_count, 0.0);
        agent.agent_count = spec.agent_count;
        agents_.emplace(h, std::move(agent));
        return h;
    }

    bool AgentCognitionStore::destroy(AgentHandle h)
    {
        return agents_.erase(h) != 0;
    }

    bool AgentCognitionStore::start(AgentHandle h, double now)
    {
        Agent* a = find(h);
        if (!a) {
            return false;
        }
        wz::cognition::start(a->clock, now);
        return true;
    }

    double AgentCognitionStore::think(AgentHandle h, double now)
    {
        Agent* a = find(h);
        if (!a) {
            return 0.0;
        }

        // Capture the elapsed sim-time BEFORE tick advances the clock -- the
        // decoherence collapse probability is decoherence_rate * elapsed.
        const bool was_started = a->clock.started;
        const double prev = a->clock.last_tick;
        const double dtau = tick(a->coordination, a->clock, now);
        const double dt = was_started ? std::max(0.0, now - prev) : 0.0;

        // One BP read per think (the expensive part) -> cache, so the frame-path
        // readers are O(1). Commit each still-undecided disposition.
        for (uint32_t i = 0; i < a->agent_count; ++i) {
            const double z = decision_z(a->coordination, i);
            a->marginal_cache[i] = z;
            if (a->latched[i].has_value()) {
                continue;
            }
            if (std::optional<bool> bit =
                    try_commit_marginal(z, a->commit, dt, a->rng)) {
                a->latched[i] = bit;
            }
        }
        return dtau;
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
