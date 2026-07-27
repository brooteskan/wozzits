#include <cognition/learning.h>

#include <algorithm>
#include <cmath>

namespace wz::engine::cognition
{
    namespace
    {
        // Weights are unnormalized log-weights, so every read subtracts the max
        // before exp: the standard logsumexp guard, and the reason an arbitrarily
        // large reward saturates rather than overflowing. (The old amplitude form
        // had to clamp strength to +/-50 to avoid exactly that overflow, which
        // produced inf -> NaN and permanently bricked the agent.)
        double max_weight(const LearnedTable& t)
        {
            return *std::max_element(t.log_w.begin(), t.log_w.end());
        }

        // One pass over the table: every bit's <sigma_z>.
        void refresh_z(const LearnedTable& t)
        {
            const uint32_t n = t.bits;
            t.z_cache.assign(n, 0.0);
            t.z_valid = true;
            if (n == 0 || t.log_w.empty()) {
                return;
            }
            const double max_w = max_weight(t);
            const std::size_t dim = t.log_w.size();
            double total = 0.0;
            for (std::size_t k = 0; k < dim; ++k) {
                const double p = std::exp(t.log_w[k] - max_w);
                total += p;
                for (uint32_t a = 0; a < n; ++a) {
                    t.z_cache[a] += ((k >> a) & 1u) ? -p : p;
                }
            }
            if (total <= 0.0) {
                return;   // degenerate; leave the marginals at 0
            }
            const double inv = 1.0 / total;
            for (uint32_t a = 0; a < n; ++a) {
                t.z_cache[a] *= inv;
            }
        }

        // One pass for ONE pair, on demand. Filling the whole bits x bits matrix
        // eagerly would be O(2^n * n^2) -- at the 16-bit cap that measures 21 ms,
        // worse than the three targeted passes this caching replaces. Callers ask
        // about one or two pairs.
        void refresh_zz(const LearnedTable& t, uint32_t a, uint32_t b)
        {
            const uint32_t n = t.bits;
            if (t.zz_cache.size() != static_cast<std::size_t>(n) * n) {
                t.zz_cache.assign(static_cast<std::size_t>(n) * n, 0.0);
                t.zz_valid.assign(static_cast<std::size_t>(n) * n, 0u);
            }
            const double max_w = max_weight(t);
            const std::size_t dim = t.log_w.size();
            double total = 0.0;
            double corr = 0.0;
            for (std::size_t k = 0; k < dim; ++k) {
                const double p = std::exp(t.log_w[k] - max_w);
                total += p;
                corr += (((k >> a) & 1u) == ((k >> b) & 1u)) ? p : -p;
            }
            const double v = total > 0.0 ? corr / total : 0.0;
            t.zz_cache[static_cast<std::size_t>(a) * n + b] = v;
            t.zz_cache[static_cast<std::size_t>(b) * n + a] = v;   // symmetric
            t.zz_valid[static_cast<std::size_t>(a) * n + b] = 1u;
            t.zz_valid[static_cast<std::size_t>(b) * n + a] = 1u;
        }
    }

    LearnedTable make_learned_table(uint32_t bits)
    {
        LearnedTable t;
        t.bits = bits;
        if (bits > 0) {
            t.log_w.assign(std::size_t{ 1 } << bits, 0.0);  // uniform
        }
        return t;
    }

    void reward(
        LearnedTable& memory, uint64_t mask, uint64_t match, double strength)
    {
        // No clamp, and none needed. The old amplitude form multiplied by
        // exp(strength) and renormalized, so a strength past ~709 overflowed to
        // inf and normalize() turned that into NaN -- permanently poisoning the
        // register, since the NaN then propagated through memory_preference into
        // the goal field and every marginal, and the agent never committed again.
        // Adding in log space cannot overflow at any plausible magnitude, and the
        // read's max-subtraction makes a huge weight saturate P -> 1 smoothly,
        // which is what "monotonic and saturating" always claimed to mean.
        const uint64_t target = match & mask;
        const std::size_t dim = memory.log_w.size();
        const double delta = 2.0 * strength;   // probability = amplitude squared
        for (std::size_t k = 0; k < dim; ++k) {
            if ((static_cast<uint64_t>(k) & mask) == target) {
                memory.log_w[k] += delta;
            }
        }
        memory.z_valid = false;
        std::fill(memory.zz_valid.begin(), memory.zz_valid.end(), uint8_t{ 0 });
    }

    double memory_preference(const LearnedTable& memory, uint32_t q)
    {
        if (q >= memory.bits) {
            return 0.0;
        }
        if (!memory.z_valid) {
            refresh_z(memory);
        }
        return memory.z_cache[q];
    }

    double memory_correlation_zz(
        const LearnedTable& memory, uint32_t a, uint32_t b)
    {
        if (a >= memory.bits || b >= memory.bits) {
            return 0.0;
        }
        const std::size_t at = static_cast<std::size_t>(a) * memory.bits + b;
        if (at >= memory.zz_valid.size() || !memory.zz_valid[at]) {
            refresh_zz(memory, a, b);
        }
        return memory.zz_cache[at];
    }
}
