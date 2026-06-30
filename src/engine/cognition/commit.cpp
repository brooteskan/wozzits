#include <engine/cognition/commit.h>

#include <algorithm>

namespace wz::cognition
{
    bool confident(
        const wz::qstate::Register& reg, uint32_t q, double confidence)
    {
        const double p1 = wz::qstate::marginal(reg, q);
        const double leading = std::max(p1, 1.0 - p1);
        return leading >= confidence;
    }

    std::optional<bool> try_commit(
        wz::qstate::Register& reg,
        uint32_t q,
        const CommitPolicy& policy,
        double dt,
        wz::qstate::Rng& rng)
    {
        // Threshold: the agent has made up its mind.
        if (confident(reg, q, policy.confidence)) {
            return wz::qstate::measure(reg, q, rng);
        }

        // Decoherence: environmental pressure forces a snap decision. Modeled as
        // a measurement firing with probability decoherence_rate * dt.
        if (policy.decoherence_rate > 0.0 && dt > 0.0) {
            const double p = std::min(1.0, policy.decoherence_rate * dt);
            if (rng.next_unit() < p) {
                return wz::qstate::measure(reg, q, rng);
            }
        }

        return std::nullopt;  // keep deliberating
    }
}
