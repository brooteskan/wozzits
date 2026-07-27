#include <cognition/agent_cognition.h>

#include <cognition/conditional_policy.h>
#include <cognition/exclusivity.h>
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
        // Sane upper bounds on qubit counts. The exact backend (chi = 0) and the
        // learning-memory register both allocate a 2^n qstate vector, so n must stay
        // WELL under 64 (uint64_t{1} << n is UB at n >= 64, qstate.cpp) and small enough
        // not to OOM (2^30 ~ 16 GB, and it only gets worse). The linear backends
        // (chi = 1 loopy, chi >= 2 TTN) scale linearly, so they get a far higher ceiling
        // that just guards against an absurd count rather than the exponential blow-up.
        constexpr uint32_t kMaxExactQubits = 24;   // 2^24 complex ~ 256 MB -- the hard cap
        constexpr uint32_t kMaxAgentCount = 4096;  // linear-backend sanity ceiling

        // Build the exact joint-state backend (chi = 0): arbitrary pairwise bonds
        // + summed per-agent goals. Refuses a count whose 2^n state vector would
        // overflow the shift (n >= 64) or exhaust memory -- callers get nullopt, not UB.
        //
        // NOTE this is the MEMORY guard only. The TIME guard is
        // kMaxExactQubitsRuntime in the header, applied by build_coordination --
        // 24 qubits is roughly ten past the point where a think() fits in a frame.
        std::optional<Coordination> build_exact(const AgentSpec& spec)
        {
            if (spec.agent_count > kMaxExactQubits) {
                return std::nullopt;
            }
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
        //
        // It is also where chi = 0 is PROMOTED off the exact backend when the group
        // has outgrown the frame budget (see kMaxExactQubitsRuntime). Promotion is
        // recomputed here on every build rather than baked into the agent, so it is
        // a pure function of (authored chi, agent_count, topology) -- a reshape that
        // grows past the budget degrades in fidelity, and one that shrinks back
        // under it recovers the exact backend the author actually asked for.
        // `chi_used` reports the backend that was built.
        // Every agent's soft one-hot expanded into bonds + goals, appended to the
        // authored ones. Derived on each build rather than stored, so a live
        // set_goal() (which OVERWRITES a goal slot) cannot wipe the exclusivity
        // bias, and so a rebuild always reproduces exactly the same terms.
        void append_one_hot_terms(AgentSpec& spec, const AgentLayout& layout)
        {
            for (uint32_t i = 0; i < spec.one_hot_strength.size(); ++i) {
                add_one_hot(
                    spec.bonds, spec.goals, layout, i, spec.one_hot_strength[i],
                    spec.agent_count);
            }
        }

        std::optional<Coordination> build_coordination(
            const AgentSpec& spec, const AgentLayout& layout, uint32_t& chi_used)
        {
            AgentSpec canon = spec;
            // One-hot FIRST, so its antiferro bonds go through the canonicalization
            // below with the authored ones -- same self-bond/parallel-edge hygiene,
            // same girth guarantee the loopy-tier BP math needs. Appending them
            // afterwards is the bug the #297 safety fix was about.
            append_one_hot_terms(canon, layout);
            // Canonicalize to girth>=3 (drop self-bonds/zeros, sum parallels) so every
            // backend gets clean input; the loopy-tier BP math will require it, and it
            // is a no-op for already-clean specs. Idempotent, so rearm/reshape are safe.
            CanonicalBonds canon_bonds =
                canonicalize_bonds(canon.agent_count, canon.bonds);
            canon.bonds = std::move(canon_bonds.bonds);

            chi_used = canon.chi;
            if (canon.chi == 0) {
                if (canon.agent_count <= kMaxExactQubitsRuntime) {
                    return build_exact(canon);
                }
                // Over the time budget. Degrade in FIDELITY, not in frame time --
                // and never by silently refusing to build, which shows up as an NPC
                // that simply never decides. A nearest-neighbour chain keeps some
                // entanglement on the TTN; anything else (star, ring, arbitrary
                // graph) goes to loopy BP, which handles any topology.
                canon.chi = kPromotedChainChi;
                if (std::optional<Coordination> ttn = build_ttn(canon)) {
                    chi_used = kPromotedChainChi;
                    return ttn;
                }
                canon.chi = 1u;
                chi_used = 1u;
                return build_loopy(canon);
            }
            if (canon.chi == 1) return build_loopy(canon);
            return build_ttn(canon);  // chi >= 2 (chi is uint32_t, so 0/1/>=2 total)
        }
    }

    AgentHandle AgentCognitionStore::create(const AgentSpec& spec)
    {
        // Reject an empty agent, an absurd count (guards the linear backends), and a
        // memory register too large for its 2^n qstate (same 1<<n UB / OOM as the exact
        // backend). The exact backend's tighter 2^agent_count cap is enforced in
        // build_exact, so only chi = 0 is bounded that low.
        if (spec.agent_count == 0
            || spec.agent_count > kMaxAgentCount
            || spec.memory_qubits > kMaxExactQubits) {
            return kInvalidAgent;
        }

        // Multi-disposition layout, if the mind declares one. Fail CLOSED on a
        // layout that disagrees with agent_count rather than picking a winner: the
        // two would silently address different qubits, and a layout wider than the
        // group is exactly the case that produces out-of-range bonds.
        const AgentLayout layout =
            make_agent_layout(spec.dispositions_per_agent);
        if (!spec.dispositions_per_agent.empty()
            && layout.total_qubits != spec.agent_count) {
            return kInvalidAgent;
        }
        if (spec.one_hot_strength.size() > spec.dispositions_per_agent.size()) {
            return kInvalidAgent;   // exclusivity for an agent that is not laid out
        }

        uint32_t chi_used = spec.chi;
        std::optional<Coordination> coordination =
            build_coordination(spec, layout, chi_used);
        if (!coordination) {
            return kInvalidAgent;
        }

        const AgentHandle h = next_++;
        Agent agent;
        agent.coordination = std::move(*coordination);
        agent.effective_chi = chi_used;
        agent.clock = spec.clock;
        agent.commit = spec.commit;
        // Structure kept so rearm can rebuild a fresh register (goals come from
        // goal_fields, so we deliberately do NOT store spec.goals).
        agent.bonds = spec.bonds;
        // Only the AUTHORED bonds/goals are stored -- the one-hot terms are
        // re-derived on every build from the layout below.
        agent.layout = layout;
        agent.one_hot_strength = spec.one_hot_strength;
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

        // NOTE there is no re-projection pass here any more. A committed decision
        // is CLAMPED in the backend by collapse(), and every relaxation step
        // re-applies the clamp itself -- so the latch is held for the whole of
        // tick() above, and the still-undecided decisions relax against it
        // throughout rather than only after the fact. Re-projecting here would be
        // a no-op that also hid which layer owns the constraint.

        // One bulk BP read; re-read after any NEW commit so a later decision this
        // tick sees the conditioned state.
        std::vector<double> z = decisions(a->coordination);
        for (uint32_t i = 0; i < a->agent_count && i < z.size(); ++i) {
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

        // Cache AFTER the commit pass, from the final state -- so every slot is
        // conditioned on the same thing. Writing each slot inside the loop meant
        // slot i reflected conditioning on commits 0..i-1 only: every entry
        // conditioned on a different set, and the whole cache dependent on agent
        // ordering, which is not what marginal() claims to return. `z` was already
        // refreshed after the last commit, so this costs nothing extra -- it is
        // strictly cheaper than the per-slot write it replaces.
        for (uint32_t i = 0; i < a->agent_count && i < z.size(); ++i) {
            a->marginal_cache[i] = z[i];
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
        // vector, so a per-index push must carry the others) -- INCLUDING the
        // derived one-hot bias, which lives nowhere else. Without it, the first
        // live re-bias of any qubit would quietly dissolve the exclusivity on
        // every one-hot agent in the mind.
        std::vector<Goal> goals;
        goals.reserve(a->agent_count);
        for (uint32_t i = 0; i < a->agent_count; ++i) {
            goals.push_back(Goal{ .agent = i, .field = a->goal_fields[i] });
        }
        std::vector<ExactBond> ignored_bonds;   // bonds are already in the backend
        for (uint32_t i = 0; i < a->one_hot_strength.size(); ++i) {
            add_one_hot(
                ignored_bonds, goals, a->layout, i, a->one_hot_strength[i],
                a->agent_count);
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

    std::optional<bool> AgentCognitionStore::measure_in_basis(
        AgentHandle h, uint32_t agent, double theta)
    {
        Agent* a = find(h);
        if (!a || agent >= a->agent_count) {
            return std::nullopt;
        }
        // Fully qualify so the member name does not hide the free Coordination
        // overload during unqualified lookup.
        const bool bit = wz::engine::cognition::measure_in_basis(
            a->coordination, agent, theta, a->rng);
        // Latch as the committed outcome so committed() / get_agent_decision read it.
        a->latched[agent] = bit;
        // Refresh EVERY slot, not just the measured one. The measurement's
        // back-action conditions the partners in the coordination immediately, but
        // their cached marginals would keep the pre-measurement value until the
        // next think() -- up to a whole think_interval. Verified on a cat pair:
        // measuring agent 0 to |1> left agent 1's cache at +0.0000 when its true
        // conditioned value was -1.0. Any "measure the leader, then read the
        // follower" protocol silently read the wrong thing; the shipped CHSH
        // witness only escaped it by measuring both qubits.
        const std::vector<double> z = decisions(a->coordination);
        for (uint32_t i = 0; i < a->agent_count && i < z.size(); ++i) {
            a->marginal_cache[i] = z[i];
        }
        return bit;
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
        spec.one_hot_strength = a->one_hot_strength;
        spec.goals.reserve(a->agent_count);
        for (uint32_t i = 0; i < a->agent_count; ++i) {
            spec.goals.push_back(Goal{ .agent = i, .field = a->goal_fields[i] });
        }
        uint32_t chi_used = spec.chi;
        std::optional<Coordination> coordination =
            build_coordination(spec, a->layout, chi_used);
        if (!coordination) {
            return false;
        }
        a->coordination = std::move(*coordination);
        a->effective_chi = chi_used;

        std::fill(a->latched.begin(), a->latched.end(), std::nullopt);
        // Reset the marginal cache too (as reshape does): rearm re-opens every decision
        // to a fresh equal superposition (<sigma_z> = 0). Leaving the pre-rearm +/-1 in
        // the cache would make marginal() report a maximally-wrong "live" value until the
        // next think() -- while committed() already reads "deliberating" off the cleared
        // latches. That inconsistency is the read-after-rearm asymmetry from the module
        // cache, one level down.
        std::fill(a->marginal_cache.begin(), a->marginal_cache.end(), 0.0);
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
        if (!a || agent_count == 0 || agent_count > kMaxAgentCount) {
            return false;
        }
        // reshape() is expressed in FLAT qubits -- a count and a bond list -- so it
        // has no way to say what the new disposition layout should be. Silently
        // keeping the old layout would leave it describing a different number of
        // qubits than the group has (the out-of-range-bond case), and silently
        // dropping it would dissolve every one-hot the mind declared. Refuse, and
        // let a laid-out mind be rebuilt rather than reshaped.
        if (!a->layout.dispositions.empty()) {
            return false;
        }

        // Build + VALIDATE the new shape WITHOUT mutating the agent yet: read the
        // surviving goal fields straight from the (still-unchanged) vector -- existing
        // fields survive where the count overlaps, new qubits start unbiased. Validating
        // BEFORE mutating is essential: if we resized the bookkeeping first and
        // build_coordination then failed (e.g. build_ttn rejecting star bonds at
        // chi>=2), a shrink would leave agent_count LONGER than the shortened vectors
        // and the next think() would write past them -- heap corruption -- while a grow
        // would strand the rejected bonds for every later rearm(). So a rejected reshape
        // must leave the agent exactly as it was.
        AgentSpec spec;
        spec.agent_count = agent_count;
        spec.bonds = bonds;
        spec.chi = a->chi;
        spec.seed = a->seed;
        spec.goals.reserve(agent_count);
        for (uint32_t i = 0; i < agent_count; ++i) {
            const double field =
                i < a->goal_fields.size() ? a->goal_fields[i] : 0.0;
            spec.goals.push_back(Goal{ .agent = i, .field = field });
        }
        uint32_t chi_used = spec.chi;
        std::optional<Coordination> coordination =
            build_coordination(spec, a->layout, chi_used);
        if (!coordination) {
            return false;   // agent left untouched
        }

        // Validated -- NOW commit the new shape. Latches + marginals reset (the
        // reshaped group re-deliberates).
        a->goal_fields.resize(agent_count, 0.0);
        a->bonds = bonds;
        a->latched.assign(agent_count, std::nullopt);
        a->marginal_cache.assign(agent_count, 0.0);
        a->coordination = std::move(*coordination);
        a->effective_chi = chi_used;
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

    std::optional<uint32_t> AgentCognitionStore::qubit_index(
        AgentHandle h, uint32_t agent, uint32_t disposition) const
    {
        const Agent* a = find(h);
        if (!a) {
            return std::nullopt;
        }
        // No layout: one qubit per agent, so the only valid disposition is 0 and
        // the agent index IS the qubit. Saying so here means callers can address
        // by (agent, disposition) uniformly whether or not the mind is laid out.
        if (a->layout.dispositions.empty()) {
            if (disposition != 0 || agent >= a->agent_count) {
                return std::nullopt;
            }
            return agent;
        }
        const uint32_t q = qubit_of(a->layout, agent, disposition);
        if (q >= a->agent_count) {
            return std::nullopt;   // includes kInvalidQubit
        }
        return q;
    }

    std::optional<uint32_t> AgentCognitionStore::disposition_count(
        AgentHandle h, uint32_t agent) const
    {
        const Agent* a = find(h);
        if (!a) {
            return std::nullopt;
        }
        if (a->layout.dispositions.empty()) {
            return agent < a->agent_count ? std::optional<uint32_t>(1u)
                                          : std::nullopt;
        }
        if (agent >= a->layout.dispositions.size()) {
            return std::nullopt;
        }
        return a->layout.dispositions[agent];
    }

    double AgentCognitionStore::marginal(
        AgentHandle h, uint32_t agent, uint32_t disposition) const
    {
        const std::optional<uint32_t> q = qubit_index(h, agent, disposition);
        return q ? marginal(h, *q) : 0.0;
    }

    std::optional<bool> AgentCognitionStore::committed(
        AgentHandle h, uint32_t agent, uint32_t disposition) const
    {
        const std::optional<uint32_t> q = qubit_index(h, agent, disposition);
        return q ? committed(h, *q) : std::nullopt;
    }

    bool AgentCognitionStore::alive(AgentHandle h) const
    {
        return find(h) != nullptr;
    }

    std::optional<uint32_t> AgentCognitionStore::backend_chi(AgentHandle h) const
    {
        const Agent* a = find(h);
        if (!a) {
            return std::nullopt;
        }
        return a->effective_chi;
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
