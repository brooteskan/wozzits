#include <engine/cognition/sequential_goals.h>

#include <complex>

namespace wz::cognition
{
    wz::qstate::Register make_phase_register(uint32_t qubits)
    {
        return wz::qstate::basis_zero(qubits);  // phase 0
    }

    uint64_t current_phase(const wz::qstate::Register& phase_register)
    {
        uint64_t best = 0;
        double best_prob = -1.0;
        const uint64_t dim = phase_register.dim();
        for (uint64_t k = 0; k < dim; ++k) {
            const double p = std::norm(phase_register.amp[k]);
            if (p > best_prob) {
                best_prob = p;
                best = k;
            }
        }
        return best;
    }

    void advance_phase(wz::qstate::Register& phase_register)
    {
        const uint64_t dim = phase_register.dim();
        if (dim == 0) {
            return;
        }
        const uint64_t next = (current_phase(phase_register) + 1) % dim;
        for (uint64_t k = 0; k < dim; ++k) {
            phase_register.amp[k] = wz::qstate::Complex{ 0, 0 };
        }
        phase_register.amp[next] = wz::qstate::Complex{ 1, 0 };
    }

    std::vector<Goal> active_goals(
        const std::vector<Stage>& stages, uint64_t phase)
    {
        std::vector<Goal> out;
        for (const Stage& s : stages) {
            if (s.phase == phase) {
                out.push_back(Goal{ .agent = s.qubit, .field = s.field });
            }
        }
        return out;
    }
}
