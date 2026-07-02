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

    double conditional_preference(
        const wz::engine::cognition::qstate::Register& reg,
        uint32_t ctx_qubit,
        uint8_t ctx_value,
        uint32_t dec_qubit)
    {
        namespace qs = wz::engine::cognition::qstate;
        const double z_ctx = qs::expectation_z(reg, ctx_qubit);
        const double z_dec = qs::expectation_z(reg, dec_qubit);
        const double zz = qs::expectation_zz(reg, ctx_qubit, dec_qubit);
        const double s = (ctx_value & 1u) ? -1.0 : 1.0;   // +1 for |0>, -1 for |1>
        const double denom = 1.0 + s * z_ctx;             // = 2 * P(ctx == ctx_value)
        if (denom <= 1e-9) {
            return 0.0;   // this context ~never occurs in the memory
        }
        return (z_dec + s * zz) / denom;
    }
}
