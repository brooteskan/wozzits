#include <cognition/commit.h>

#include <algorithm>

namespace wz::engine::cognition
{
    bool confident(
        const wz::engine::cognition::qstate::Register& reg, uint32_t q, double confidence)
    {
        const double p1 = wz::engine::cognition::qstate::marginal(reg, q);
        const double leading = std::max(p1, 1.0 - p1);
        return leading >= confidence;
    }

    std::optional<bool> try_commit(
        wz::engine::cognition::qstate::Register& reg,
        uint32_t q,
        const CommitPolicy& policy,
        double dt,
        wz::engine::cognition::qstate::Rng& rng)
    {
        // Threshold: the agent has made up its mind.
        if (confident(reg, q, policy.confidence)) {
            return wz::engine::cognition::qstate::measure(reg, q, rng);
        }

        // Decoherence: environmental pressure forces a snap decision. Modeled as
        // a measurement firing with probability decoherence_rate * dt.
        if (policy.decoherence_rate > 0.0 && dt > 0.0) {
            const double p = std::min(1.0, policy.decoherence_rate * dt);
            if (rng.next_unit() < p) {
                return wz::engine::cognition::qstate::measure(reg, q, rng);
            }
        }

        return std::nullopt;  // keep deliberating
    }

    bool confident_marginal(double z, double confidence)
    {
        const double p1 = std::clamp((1.0 - z) * 0.5, 0.0, 1.0);
        const double leading = std::max(p1, 1.0 - p1);
        return leading >= confidence;
    }

    std::optional<bool> try_commit_marginal(
        double z,
        const CommitPolicy& policy,
        double dt,
        wz::engine::cognition::qstate::Rng& rng)
    {
        const double p1 = std::clamp((1.0 - z) * 0.5, 0.0, 1.0);

        // Threshold: the agent has made up its mind -> the leading disposition.
        if (confident_marginal(z, policy.confidence)) {
            return p1 >= 0.5;  // true = |1>
        }

        // Decoherence: environmental pressure forces a snap decision, Born-sampled
        // from the still-undecided marginal.
        if (policy.decoherence_rate > 0.0 && dt > 0.0) {
            const double p = std::min(1.0, policy.decoherence_rate * dt);
            if (rng.next_unit() < p) {
                return rng.next_unit() < p1;
            }
        }

        return std::nullopt;  // keep deliberating
    }
}
