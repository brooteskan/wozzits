#include <engine/qstate/qstate.h>

#include <algorithm>
#include <cmath>
#include <limits>

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

    Svd svd(
        const std::vector<Complex>& a, uint32_t m, uint32_t n, uint32_t max_rank)
    {
        Svd out;
        if (m == 0 || n == 0) {
            out.rows = m;
            out.cols = n;
            return out;
        }

        // Orthogonalize the columns of the taller orientation (rows >= cols): work
        // on B = A when m >= n, else B = A^dagger and swap U/V at the end.
        const bool tr = (m < n);
        const uint32_t rows = tr ? n : m;
        const uint32_t cols = tr ? m : n;

        std::vector<Complex> B(static_cast<std::size_t>(rows) * cols);
        if (!tr) {
            B = a;  // m x n
        } else {
            for (uint32_t i = 0; i < m; ++i) {
                for (uint32_t j = 0; j < n; ++j) {
                    B[static_cast<std::size_t>(j) * cols + i] =
                        std::conj(a[static_cast<std::size_t>(i) * n + j]);
                }
            }
        }

        // V accumulates the right rotations (cols x cols, identity to start).
        std::vector<Complex> V(static_cast<std::size_t>(cols) * cols,
            Complex{ 0, 0 });
        for (uint32_t k = 0; k < cols; ++k) {
            V[static_cast<std::size_t>(k) * cols + k] = Complex{ 1, 0 };
        }

        const Real eps = std::numeric_limits<Real>::epsilon();
        for (uint32_t sweep = 0; sweep < 100u; ++sweep) {
            bool rotated = false;
            for (uint32_t p = 0; p < cols; ++p) {
                for (uint32_t q = p + 1; q < cols; ++q) {
                    Real alpha = 0;
                    Real beta = 0;
                    Complex gamma{ 0, 0 };
                    for (uint32_t i = 0; i < rows; ++i) {
                        const Complex bp = B[static_cast<std::size_t>(i) * cols + p];
                        const Complex bq = B[static_cast<std::size_t>(i) * cols + q];
                        alpha += std::norm(bp);
                        beta += std::norm(bq);
                        gamma += std::conj(bp) * bq;
                    }
                    const Real absg = std::abs(gamma);
                    if (absg <= eps * std::sqrt(alpha * beta) || absg == 0) {
                        continue;  // columns p, q already orthogonal
                    }
                    rotated = true;

                    // Real Jacobi angle + complex phase that diagonalize the 2x2
                    // Hermitian Gram matrix [[alpha, conj(gamma)], [gamma, beta]].
                    const Real zeta = (beta - alpha) / (2.0 * absg);
                    const Real t = (zeta >= 0 ? 1.0 : -1.0)
                        / (std::abs(zeta) + std::sqrt(zeta * zeta + 1.0));
                    const Real c = 1.0 / std::sqrt(t * t + 1.0);
                    const Real s = c * t;
                    const Complex xi = gamma / absg;  // unit phase
                    const Complex xs = xi * s;
                    const Complex xcs = std::conj(xi) * s;

                    for (uint32_t i = 0; i < rows; ++i) {
                        const Complex bp = B[static_cast<std::size_t>(i) * cols + p];
                        const Complex bq = B[static_cast<std::size_t>(i) * cols + q];
                        B[static_cast<std::size_t>(i) * cols + p] = c * bp - xcs * bq;
                        B[static_cast<std::size_t>(i) * cols + q] = xs * bp + c * bq;
                    }
                    for (uint32_t i = 0; i < cols; ++i) {
                        const Complex vp = V[static_cast<std::size_t>(i) * cols + p];
                        const Complex vq = V[static_cast<std::size_t>(i) * cols + q];
                        V[static_cast<std::size_t>(i) * cols + p] = c * vp - xcs * vq;
                        V[static_cast<std::size_t>(i) * cols + q] = xs * vp + c * vq;
                    }
                }
            }
            if (!rotated) {
                break;
            }
        }

        // Singular values = column norms of B; sort the columns descending.
        std::vector<Real> sigma(cols);
        for (uint32_t k = 0; k < cols; ++k) {
            Real nrm = 0;
            for (uint32_t i = 0; i < rows; ++i) {
                nrm += std::norm(B[static_cast<std::size_t>(i) * cols + k]);
            }
            sigma[k] = std::sqrt(nrm);
        }
        std::vector<uint32_t> idx(cols);
        for (uint32_t k = 0; k < cols; ++k) {
            idx[k] = k;
        }
        std::sort(idx.begin(), idx.end(),
            [&](uint32_t x, uint32_t y) { return sigma[x] > sigma[y]; });

        const uint32_t full_rank = cols;  // min(m, n)
        const uint32_t rank =
            (max_rank > 0 && max_rank < full_rank) ? max_rank : full_rank;

        // U_B = normalized selected columns of B (rows x rank);
        // V_B = selected columns of the accumulated V (cols x rank).
        std::vector<Complex> ub(static_cast<std::size_t>(rows) * rank);
        std::vector<Complex> vb(static_cast<std::size_t>(cols) * rank);
        std::vector<Real> s_out(rank);
        for (uint32_t r = 0; r < rank; ++r) {
            const uint32_t col = idx[r];
            s_out[r] = sigma[col];
            const Real inv = sigma[col] > 0 ? 1.0 / sigma[col] : 0.0;
            for (uint32_t i = 0; i < rows; ++i) {
                ub[static_cast<std::size_t>(i) * rank + r] =
                    B[static_cast<std::size_t>(i) * cols + col] * inv;
            }
            for (uint32_t i = 0; i < cols; ++i) {
                vb[static_cast<std::size_t>(i) * rank + r] =
                    V[static_cast<std::size_t>(i) * cols + col];
            }
        }

        out.rows = m;
        out.cols = n;
        out.rank = rank;
        out.s = std::move(s_out);
        out.vh.assign(static_cast<std::size_t>(rank) * n, Complex{ 0, 0 });
        if (!tr) {
            // A = U_B diag(s) V_B^dagger.
            out.u = std::move(ub);  // m x rank
            for (uint32_t r = 0; r < rank; ++r) {
                for (uint32_t j = 0; j < n; ++j) {  // n == cols
                    out.vh[static_cast<std::size_t>(r) * n + j] =
                        std::conj(vb[static_cast<std::size_t>(j) * rank + r]);
                }
            }
        } else {
            // B = A^dagger = U_B diag(s) V_B^dagger  =>  A = V_B diag(s) U_B^dagger.
            out.u = std::move(vb);  // m x rank (cols == m)
            for (uint32_t r = 0; r < rank; ++r) {
                for (uint32_t j = 0; j < n; ++j) {  // n == rows
                    out.vh[static_cast<std::size_t>(r) * n + j] =
                        std::conj(ub[static_cast<std::size_t>(j) * rank + r]);
                }
            }
        }
        return out;
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
