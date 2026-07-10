#include <cognition/learning.h>

#include <algorithm>
#include <cmath>

namespace wz::engine::cognition
{
    void reward(
        wz::engine::cognition::qstate::Register& memory,
        uint64_t mask,
        uint64_t match,
        double strength)
    {
        // Clamp before exp(): an unclamped strength >= ~709 overflows to inf, and
        // normalize() then turns that into NaN (finite/inf -> 0, inf/inf -> NaN),
        // PERMANENTLY poisoning the memory register -- the NaN propagates through
        // memory_preference -> goals -> every marginal and the agent never commits
        // again. The learning is monotonic + SATURATING, so a strength this large is
        // already effectively "certain"; a safe finite range changes nothing legitimate
        // (real rewards are < 10) and just caps the pathological input. Clamp both ends
        // so a huge NEGATIVE strength can't zero a branch into a divide-by-zero either.
        const double boost = std::exp(std::clamp(strength, -50.0, 50.0));
        const uint64_t target = match & mask;
        const uint64_t dim = memory.dim();
        for (uint64_t k = 0; k < dim; ++k) {
            if ((k & mask) == target) {
                memory.amp[k] *= boost;
            }
        }
        wz::engine::cognition::qstate::normalize(memory);
    }

    double memory_preference(const wz::engine::cognition::qstate::Register& memory, uint32_t q)
    {
        return wz::engine::cognition::qstate::expectation_z(memory, q);
    }
}
