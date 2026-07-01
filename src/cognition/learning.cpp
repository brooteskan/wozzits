#include <cognition/learning.h>

#include <cmath>

namespace wz::engine::cognition
{
    void reward(
        wz::engine::cognition::qstate::Register& memory,
        uint64_t mask,
        uint64_t match,
        double strength)
    {
        const double boost = std::exp(strength);
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
