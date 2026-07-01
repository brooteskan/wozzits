#include <cognition/commit.h>

#include <algorithm>
#include <cmath>

namespace
{
    // Probability that a Poisson process of rate `rate` fires at least once over
    // an interval `dt`. Cadence-INDEPENDENT: one draw over dt matches the compound
    // of N draws over dt/N (unlike rate*dt), so think_interval stays a pure
    // performance knob. Self-clamps to [0, 1), so no min() needed.
    double poisson_fire_probability(double rate, double dt)
    {
        if (rate <= 0.0 || dt <= 0.0) {
            return 0.0;
        }
        return 1.0 - std::exp(-rate * dt);
    }
}

namespace wz::engine::cognition
{
    bool confident(
        const qstate::Register& reg, uint32_t q, double confidence)
    {
        const double p1 = qstate::marginal(reg, q);
        const double leading = std::max(p1, 1.0 - p1);
        return leading >= confidence;
    }

    std::optional<bool> try_commit(
        qstate::Register& reg,
        uint32_t q,
        const CommitPolicy& policy,
        double dt,
        qstate::Rng& rng)
    {
        // Threshold: the agent has made up its mind.
        if (confident(reg, q, policy.confidence)) {
            return qstate::measure(reg, q, rng);
        }

        // Decoherence: environmental pressure forces a snap decision. Modeled as
        // a measurement firing as a Poisson process of rate decoherence_rate.
        const double p = poisson_fire_probability(policy.decoherence_rate, dt);
        if (p > 0.0 && rng.next_unit() < p) {
            return qstate::measure(reg, q, rng);
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
        qstate::Rng& rng)
    {
        const double p1 = std::clamp((1.0 - z) * 0.5, 0.0, 1.0);

        // Threshold: the agent has made up its mind -> the leading disposition.
        if (confident_marginal(z, policy.confidence)) {
            return p1 >= 0.5;  // true = |1>
        }

        // Decoherence: environmental pressure forces a snap decision, Born-sampled
        // from the still-undecided marginal (Poisson process of rate
        // decoherence_rate).
        const double p = poisson_fire_probability(policy.decoherence_rate, dt);
        if (p > 0.0 && rng.next_unit() < p) {
            return rng.next_unit() < p1;
        }

        return std::nullopt;  // keep deliberating
    }
}
