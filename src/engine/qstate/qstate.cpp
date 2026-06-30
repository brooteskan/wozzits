#include <engine/qstate/qstate.h>

#include <cmath>

namespace wz::qstate
{
    uint64_t Rng::next_u64() noexcept
    {
        // xorshift64* -- decent statistical quality, fully deterministic.
        uint64_t x = state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        state = x;
        return x * 0x2545F4914F6CDD1Dull;
    }

    Real Rng::next_unit() noexcept
    {
        // Top 53 bits -> a double in [0, 1).
        return static_cast<Real>(next_u64() >> 11) * (1.0 / 9007199254740992.0);
    }

    Register basis_zero(uint32_t qubits)
    {
        Register reg{ .qubits = qubits,
            .amp = std::vector<Complex>(uint64_t{ 1 } << qubits, Complex{ 0, 0 }) };
        reg.amp[0] = Complex{ 1, 0 };
        return reg;
    }

    Register uniform(uint32_t qubits)
    {
        const uint64_t dim = uint64_t{ 1 } << qubits;
        const Real a = 1.0 / std::sqrt(static_cast<Real>(dim));
        return Register{ .qubits = qubits,
            .amp = std::vector<Complex>(dim, Complex{ a, 0 }) };
    }

    void apply_1q(Register& reg, uint32_t q, const Complex m[4])
    {
        const uint64_t stride = uint64_t{ 1 } << q;
        const uint64_t dim = reg.dim();
        for (uint64_t i = 0; i < dim; ++i) {
            if (i & stride) {
                continue;  // visit each pair once, from its low (bit q == 0) index
            }
            const uint64_t i0 = i;
            const uint64_t i1 = i | stride;
            const Complex a0 = reg.amp[i0];
            const Complex a1 = reg.amp[i1];
            reg.amp[i0] = m[0] * a0 + m[1] * a1;
            reg.amp[i1] = m[2] * a0 + m[3] * a1;
        }
    }

    void apply_x_field(Register& reg, uint32_t q, Real theta)
    {
        // e^{-i theta sigma_x} = cos(theta) I - i sin(theta) sigma_x.
        const Real c = std::cos(theta);
        const Real s = std::sin(theta);
        const Complex off{ 0, -s };
        const Complex m[4] = { Complex{ c, 0 }, off, off, Complex{ c, 0 } };
        apply_1q(reg, q, m);
    }

    void apply_z_field(Register& reg, uint32_t q, Real theta)
    {
        // e^{-i theta sigma_z} = diag(e^{-i theta}, e^{+i theta}). Diagonal gate.
        const uint64_t stride = uint64_t{ 1 } << q;
        const Complex lo = std::polar(Real{ 1 }, -theta);  // bit q == 0
        const Complex hi = std::polar(Real{ 1 }, theta);   // bit q == 1
        const uint64_t dim = reg.dim();
        for (uint64_t i = 0; i < dim; ++i) {
            reg.amp[i] *= (i & stride) ? hi : lo;
        }
    }

    void apply_hadamard(Register& reg, uint32_t q)
    {
        const Real r = 1.0 / std::sqrt(Real{ 2 });
        const Complex m[4] = {
            Complex{ r, 0 }, Complex{ r, 0 }, Complex{ r, 0 }, Complex{ -r, 0 }
        };
        apply_1q(reg, q, m);
    }

    void apply_zz(Register& reg, uint32_t a, uint32_t b, Real theta)
    {
        // e^{-i theta sigma_z^a sigma_z^b}: eigenvalue +1 when bits a,b agree
        // (phase e^{-i theta}), -1 when they differ (phase e^{+i theta}).
        const Complex agree = std::polar(Real{ 1 }, -theta);
        const Complex differ = std::polar(Real{ 1 }, theta);
        const uint64_t dim = reg.dim();
        for (uint64_t k = 0; k < dim; ++k) {
            const uint64_t ba = (k >> a) & 1u;
            const uint64_t bb = (k >> b) & 1u;
            reg.amp[k] *= (ba == bb) ? agree : differ;
        }
    }

    void apply_phase_mask(
        Register& reg, uint64_t mask, uint64_t match, Real phase)
    {
        const Complex p = std::polar(Real{ 1 }, phase);
        const uint64_t dim = reg.dim();
        for (uint64_t k = 0; k < dim; ++k) {
            if ((k & mask) == (match & mask)) {
                reg.amp[k] *= p;
            }
        }
    }

    Real norm(const Register& reg)
    {
        Real sum = 0;
        for (const Complex& a : reg.amp) {
            sum += std::norm(a);  // |a|^2
        }
        return std::sqrt(sum);
    }

    void normalize(Register& reg)
    {
        const Real n = norm(reg);
        if (n <= 0) {
            return;
        }
        const Real inv = 1.0 / n;
        for (Complex& a : reg.amp) {
            a *= inv;
        }
    }

    Real marginal(const Register& reg, uint32_t q)
    {
        const uint64_t stride = uint64_t{ 1 } << q;
        const uint64_t dim = reg.dim();
        Real total = 0;
        Real set = 0;
        for (uint64_t k = 0; k < dim; ++k) {
            const Real p = std::norm(reg.amp[k]);
            total += p;
            if (k & stride) {
                set += p;
            }
        }
        return total > 0 ? set / total : 0;
    }

    Real expectation_z(const Register& reg, uint32_t q)
    {
        // <sigma_z> = P(0) - P(1) = 1 - 2*P(1). sigma_z is +1 on |0>, -1 on |1>.
        return 1.0 - 2.0 * marginal(reg, q);
    }

    void apply_imag_time_field(
        Register& reg, uint32_t q, Real gamma, Real h, Real dtau)
    {
        // H = -h*sigma_z - gamma*sigma_x = [[-h, -gamma], [-gamma, +h]].
        // sigma_z and sigma_x anticommute, so H^2 = (h^2 + gamma^2) I = E^2 I, and
        //   e^{-H dtau} = cosh(E dtau) I - (sinh(E dtau)/E) H.
        const Real e = std::sqrt(h * h + gamma * gamma);
        if (e <= 0) {
            return;  // H == 0: e^{-H dtau} = I, nothing to relax toward
        }
        const Real ch = std::cosh(e * dtau);
        const Real sh_over_e = std::sinh(e * dtau) / e;

        // M = cosh I - (sinh/E) H. Real-valued for this Hamiltonian.
        const Complex m00{ ch + sh_over_e * h, 0 };   // cosh - (sinh/E)(-h)
        const Complex off{ sh_over_e * gamma, 0 };     // -(sinh/E)(-gamma)
        const Complex m11{ ch - sh_over_e * h, 0 };
        const Complex m[4] = { m00, off, off, m11 };
        apply_1q(reg, q, m);
        normalize(reg);  // e^{-H dtau} is not unitary
    }

    void apply_imag_time_zz(
        Register& reg, uint32_t a, uint32_t b, Real j, Real dtau)
    {
        // H = -j sigma_z^a sigma_z^b ; e^{-H dtau} = e^{+j dtau sigma_z sigma_z}.
        // sigma_z^a sigma_z^b is +1 when bits a,b agree, -1 when they differ.
        const Real agree = std::exp(j * dtau);
        const Real differ = std::exp(-j * dtau);
        const uint64_t dim = reg.dim();
        for (uint64_t k = 0; k < dim; ++k) {
            const uint64_t ba = (k >> a) & 1u;
            const uint64_t bb = (k >> b) & 1u;
            reg.amp[k] *= (ba == bb) ? agree : differ;
        }
        normalize(reg);  // e^{-H dtau} is not unitary
    }

    Real expectation_zz(const Register& reg, uint32_t a, uint32_t b)
    {
        const uint64_t dim = reg.dim();
        Real total = 0;
        Real corr = 0;
        for (uint64_t k = 0; k < dim; ++k) {
            const Real p = std::norm(reg.amp[k]);
            total += p;
            const uint64_t ba = (k >> a) & 1u;
            const uint64_t bb = (k >> b) & 1u;
            corr += (ba == bb) ? p : -p;
        }
        return total > 0 ? corr / total : 0;
    }

    bool measure(Register& reg, uint32_t q, Rng& rng)
    {
        const uint64_t stride = uint64_t{ 1 } << q;
        const Real p1 = marginal(reg, q);
        const bool outcome = rng.next_unit() < p1;

        // Project onto the sampled outcome, then renormalize.
        const uint64_t dim = reg.dim();
        for (uint64_t k = 0; k < dim; ++k) {
            const bool bit = (k & stride) != 0;
            if (bit != outcome) {
                reg.amp[k] = Complex{ 0, 0 };
            }
        }
        normalize(reg);
        return outcome;
    }

    uint64_t measure_all(Register& reg, Rng& rng)
    {
        const uint64_t dim = reg.dim();
        Real total = 0;
        for (uint64_t k = 0; k < dim; ++k) {
            total += std::norm(reg.amp[k]);
        }

        const Real target = rng.next_unit() * total;
        Real acc = 0;
        uint64_t chosen = dim - 1;  // guard against FP shortfall on the last bin
        for (uint64_t k = 0; k < dim; ++k) {
            acc += std::norm(reg.amp[k]);
            if (acc >= target) {
                chosen = k;
                break;
            }
        }

        // Collapse to the chosen basis state.
        for (uint64_t k = 0; k < dim; ++k) {
            reg.amp[k] = Complex{ 0, 0 };
        }
        reg.amp[chosen] = Complex{ 1, 0 };
        return chosen;
    }
}
