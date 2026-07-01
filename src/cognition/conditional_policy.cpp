#include <cognition/conditional_policy.h>

#include <cognition/learning.h>  // reward

namespace wz::engine::cognition
{
    void reward_pair(
        wz::engine::cognition::qstate::Register& reg,
        uint32_t ctx_qubit,
        uint8_t ctx_value,
        uint32_t dec_qubit,
        uint8_t dec_value,
        double strength)
    {
        const uint64_t mask =
            (uint64_t{ 1 } << ctx_qubit) | (uint64_t{ 1 } << dec_qubit);
        const uint64_t match =
            (uint64_t{ ctx_value & 1u } << ctx_qubit)
            | (uint64_t{ dec_value & 1u } << dec_qubit);
        reward(reg, mask, match, strength);
    }

    double policy_correlation(
        const wz::engine::cognition::qstate::Register& reg, uint32_t ctx_qubit, uint32_t dec_qubit)
    {
        return wz::engine::cognition::qstate::expectation_zz(reg, ctx_qubit, dec_qubit)
            - wz::engine::cognition::qstate::expectation_z(reg, ctx_qubit)
                * wz::engine::cognition::qstate::expectation_z(reg, dec_qubit);
    }
}
