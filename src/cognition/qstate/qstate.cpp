#include <cognition/qstate/qstate.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace wz::engine::cognition::qstate
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

    bool usable(const Register& reg)
    {
        Real total = 0;
        for (const Complex& a : reg.amp) {
            total += std::norm(a);
        }
        return total > 0 && std::isfinite(total);
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
        // NaN, NOT 0, when there is no answer -- and that distinction is the whole
        // of finding C2-C2. The old `total > 0 ? set/total : 0` returned a
        // PLAUSIBLE NUMBER for a destroyed register: an all-zero or all-NaN state
        // reported marginal 0, so expectation_z read +1.0 and confident() returned
        // TRUE. Every numeric failure in the subsystem was laundered into a
        // maximally confident decision for |0> instead of being detectable, which
        // is also why the shipping long-tick test passed over an all-NaN register
        // -- it asserted isfinite on a value this guard had already made finite.
        //
        // NaN propagates the right way through the commit policy: confident_marginal
        // compares false, try_commit_marginal returns nullopt, and the agent stops
        // deciding rather than deciding wrongly. The store turns that into a report
        // (see AgentCognitionStore::wrecked).
        if (!(total > 0) || !std::isfinite(total)) {
            return std::numeric_limits<Real>::quiet_NaN();
        }
        return set / total;
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
        //
        // WRITTEN IN THE TANH FORM, and that is what makes it total. The literal
        // cosh/sinh reading of the formula above overflows: cosh(E dtau) leaves
        // double range at E dtau ~ 710, and the amplitudes it produces leave range
        // at ~355 because norm() SQUARES them. Past the first the matrix is
        // inf + (-inf) = NaN; past the second normalize() multiplies by 1/inf = 0.
        // Either way the register is destroyed, and marginal() then reported the
        // wreck as <sigma_z> = +1.0 with full confidence -- an agent committing to
        // |0> exactly when its goal said |1>. Reachable from an authored field of
        // ~1.4e4, from a hub whose neighbours sum to ~7.2e3, or from any inf/NaN a
        // behavior computes.
        //
        // The gate is applied and then RENORMALIZED, so any positive scalar
        // multiple of M gives an identical state. Dividing through by cosh(E dtau):
        //
        //   M / cosh = I - tanh(E dtau) * H/E
        //
        // tanh saturates to +/-1 instead of overflowing and |h|,|gamma| <= E, so
        // every entry lies in [-2, 2] for EVERY finite input. The limit is also the
        // physically right one: at large E dtau this is the projector onto H's
        // ground state, which is what infinite imaginary time means.
        // sqrt(h^2 + gamma^2), computed so the SQUARE cannot overflow either.
        // hypot is the correct primitive but measurably slower, and this is a
        // per-qubit per-substep hot path -- so guard the INPUTS rather than
        // testing the result, which would raise a spurious FE_OVERFLOW on the way
        // to deciding not to trust it.
        constexpr Real kSquareSafe = 1e150;   // |x|^2 leaves double range past ~1.3e154
        const Real e =
            (std::abs(h) < kSquareSafe && std::abs(gamma) < kSquareSafe)
                ? std::sqrt(h * h + gamma * gamma)
                : std::hypot(h, gamma);
        // Reject direction, so a NaN field or step is rejected rather than
        // propagated: `e <= 0` would have let NaN straight through.
        if (!(e > 0) || !std::isfinite(e) || !std::isfinite(dtau)) {
            return;  // H == 0 (nothing to relax toward), or no defined evolution
        }

        const Real th = std::tanh(e * dtau);
        const Complex m00{ 1.0 + th * (h / e), 0 };
        const Complex off{ th * (gamma / e), 0 };
        const Complex m11{ 1.0 - th * (h / e), 0 };
        const Complex m[4] = { m00, off, off, m11 };
        apply_1q(reg, q, m);
        normalize(reg);  // e^{-H dtau} is not unitary
    }

    void apply_imag_time_zz(
        Register& reg, uint32_t a, uint32_t b, Real j, Real dtau)
    {
        // H = -j sigma_z^a sigma_z^b ; e^{-H dtau} = e^{+j dtau sigma_z sigma_z}.
        // sigma_z^a sigma_z^b is +1 when bits a,b agree, -1 when they differ.
        //
        // Same total form as apply_imag_time_field, for the same reason: the
        // literal e^{+j dtau} overflows at j dtau ~ 709 (and its square at ~355),
        // which a bond of |j| >= 1e4 reaches at the shipped step size. The gate is
        // renormalized after, so scaling both eigenvalues by e^{-|j dtau|} leaves
        // the state identical and puts both in (0, 1] for every finite input --
        // exp of a negative cannot overflow, and underflowing to 0 is the correct
        // limit (an infinitely strong bond).
        const Real x = j * dtau;
        if (!std::isfinite(x)) {
            return;   // no defined evolution; leave the register alone
        }
        const Real decay = std::exp(-2.0 * std::abs(x));
        const Real agree = x >= 0 ? Real{ 1 } : decay;
        const Real differ = x >= 0 ? decay : Real{ 1 };
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
        // NaN for an unusable register, as marginal() does: returning 0 here would
        // report "these two agents are uncorrelated", which is a real and
        // believable answer, for a state that has no answer at all.
        if (!(total > 0) || !std::isfinite(total)) {
            return std::numeric_limits<Real>::quiet_NaN();
        }
        return corr / total;
    }

    void svd(
        const std::vector<Complex>& a, uint32_t m, uint32_t n, uint32_t max_rank,
        Svd& out, SvdScratch& scratch)
    {
        out.rows = m;
        out.cols = n;
        out.rank = 0;
        out.discarded_weight = 0;
        out.u.clear();
        out.s.clear();
        out.vh.clear();
        if (m == 0 || n == 0) {
            return;
        }

        // Orthogonalize the columns of the taller orientation (rows >= cols): work
        // on B = A when m >= n, else B = A^dagger and swap U/V at the end.
        const bool tr = (m < n);
        const uint32_t rows = tr ? n : m;
        const uint32_t cols = tr ? m : n;

        std::vector<Complex>& B = scratch.b;
        B.resize(static_cast<std::size_t>(rows) * cols);
        if (!tr) {
            std::copy(a.begin(), a.end(), B.begin());  // m x n
        } else {
            for (uint32_t i = 0; i < m; ++i) {
                for (uint32_t j = 0; j < n; ++j) {
                    B[static_cast<std::size_t>(j) * cols + i] =
                        std::conj(a[static_cast<std::size_t>(i) * n + j]);
                }
            }
        }

        // V accumulates the right rotations (cols x cols, identity to start).
        std::vector<Complex>& V = scratch.v;
        V.assign(static_cast<std::size_t>(cols) * cols, Complex{ 0, 0 });
        for (uint32_t k = 0; k < cols; ++k) {
            V[static_cast<std::size_t>(k) * cols + k] = Complex{ 1, 0 };
        }

        // SQUARED column norms, maintained incrementally. The Jacobi rotation is
        // norm-preserving on the pair, so after rotating (p, q) the two new norms
        // follow in closed form from the old ones -- which removes two of the three
        // O(rows) accumulations the pair scan used to do. Only gamma, the column
        // inner product, still needs the full pass.
        std::vector<Real>& norms = scratch.norms;
        norms.assign(cols, 0);
        for (uint32_t k = 0; k < cols; ++k) {
            Real nrm = 0;
            for (uint32_t i = 0; i < rows; ++i) {
                nrm += std::norm(B[static_cast<std::size_t>(i) * cols + k]);
            }
            norms[k] = nrm;
        }

        // Converge on the RELATIVE off-diagonal weight rather than a per-pair
        // machine-epsilon test. The old form kept sweeping while any single pair
        // was not orthogonal to within DBL_EPSILON, which buys precision far below
        // what a chi-truncated bond can carry -- the tail singular values are about
        // to be discarded outright. kTol is still ~1e-13 relative, i.e. well inside
        // the truncation error; svd_tests pins the result against the exact
        // reconstruction so a bad threshold shows up as a failure, not as drift.
        constexpr Real kTol = 1e-13;
        constexpr uint32_t kMaxSweeps = 60u;
        const Real eps = std::numeric_limits<Real>::epsilon();
        for (uint32_t sweep = 0; sweep < kMaxSweeps; ++sweep) {
            Real off = 0;      // summed |gamma|^2 over the pairs this sweep
            Real diag = 0;     // summed alpha*beta, the scale to compare it against
            for (uint32_t p = 0; p < cols; ++p) {
                for (uint32_t q = p + 1; q < cols; ++q) {
                    const Real alpha = norms[p];
                    const Real beta = norms[q];
                    Complex gamma{ 0, 0 };
                    for (uint32_t i = 0; i < rows; ++i) {
                        const std::size_t base =
                            static_cast<std::size_t>(i) * cols;
                        gamma += std::conj(B[base + p]) * B[base + q];
                    }
                    const Real absg = std::abs(gamma);
                    const Real scale = alpha * beta;
                    off += absg * absg;
                    diag += scale;
                    // `scale <= 0` means one of the two columns carries no weight
                    // at all, and a zero column is orthogonal to everything -- so
                    // there is nothing to rotate. It has to be said explicitly,
                    // because the RELATIVE test beside it degenerates exactly
                    // there: with scale 0 the threshold `eps * sqrt(scale)` is 0,
                    // and the rounding noise left in absg (measured at 2.2e-157
                    // against a mathematically zero inner product) fails it. A
                    // fully committed group reaches this on every bond -- each
                    // clamp zeroes a physical component, so half of every two-site
                    // theta's columns go to zero.
                    if (absg == 0 || scale <= 0 || absg <= eps * std::sqrt(scale)) {
                        continue;  // columns p, q already orthogonal
                    }

                    // Real Jacobi angle + complex phase that diagonalize the 2x2
                    // Hermitian Gram matrix [[alpha, conj(gamma)], [gamma, beta]].
                    const Real zeta = (beta - alpha) / (2.0 * absg);
                    // A large |zeta| is an almost-diagonal pair. The closed form
                    // below SQUARES zeta, which leaves double range past ~1.3e154 --
                    // and the guard above is not enough on its own, since a tiny
                    // but nonzero `scale` gets there too. Past the crossover,
                    // sqrt(1 + zeta^2) IS |zeta| in double precision, so the whole
                    // expression collapses to 1/(2 zeta): not an approximation, the
                    // same number.
                    //
                    // The crossover is where 1 stops changing zeta^2 at all --
                    // relative weight 1/(2 zeta^2), which drops below half an ulp
                    // at |zeta| = 1e8. Anchored there rather than just under the
                    // overflow, so the branch is taken while both forms still agree.
                    constexpr Real kZetaAsymptote = 1e8;
                    const Real t = std::abs(zeta) > kZetaAsymptote
                        ? 1.0 / (2.0 * zeta)
                        : (zeta >= 0 ? 1.0 : -1.0)
                            / (std::abs(zeta) + std::sqrt(zeta * zeta + 1.0));
                    const Real c = 1.0 / std::sqrt(t * t + 1.0);
                    const Real s = c * t;
                    const Complex xi = gamma / absg;  // unit phase
                    const Complex xs = xi * s;
                    const Complex xcs = std::conj(xi) * s;

                    for (uint32_t i = 0; i < rows; ++i) {
                        const std::size_t base =
                            static_cast<std::size_t>(i) * cols;
                        const Complex bp = B[base + p];
                        const Complex bq = B[base + q];
                        B[base + p] = c * bp - xcs * bq;
                        B[base + q] = xs * bp + c * bq;
                    }
                    for (uint32_t i = 0; i < cols; ++i) {
                        const std::size_t base =
                            static_cast<std::size_t>(i) * cols;
                        const Complex vp = V[base + p];
                        const Complex vq = V[base + q];
                        V[base + p] = c * vp - xcs * vq;
                        V[base + q] = xs * vp + c * vq;
                    }
                    // The rotation diagonalizes the pair's 2x2 Gram matrix, so the
                    // new squared norms are its eigenvalues. Both are eigenvalues
                    // of a POSITIVE SEMI-DEFINITE matrix, so both are >= 0
                    // mathematically -- but on a near-rank-deficient pair (a column
                    // that is nearly a multiple of another, which is exactly what a
                    // chi-truncated bond produces) the subtraction rounds to a small
                    // NEGATIVE value. That fed `scale = alpha * beta` above, whose
                    // `std::sqrt(scale)` then raised FE_INVALID and returned NaN, so
                    // the "already orthogonal" shortcut compared false and the pair
                    // was rotated needlessly. Harmless to the RESULT -- the rotation
                    // is norm-preserving and the reconstruction error stays at 1e-15
                    // -- but it manufactured a NaN on an ordinary healthy input,
                    // which is noise in a channel that should only fire on real
                    // trouble. Clamping here rather than at the sqrt fixes it where
                    // the invariant lives: a squared norm is never negative.
                    //
                    // Argument order is deliberate: std::max(0.0, NaN) returns 0.0
                    // while std::max(NaN, 0.0) returns NaN, so the constant goes
                    // first. (Neither operand can be NaN here; the order is a habit
                    // this codebase has been bitten by -- see the C1 audit.)
                    const Real shift = t * absg;
                    norms[p] = std::max(Real{ 0 }, alpha - shift);
                    norms[q] = std::max(Real{ 0 }, beta + shift);
                }
            }
            if (off <= kTol * kTol * diag) {
                break;
            }
        }

        // Singular values = column norms of B (maintained above), sorted descending.
        std::vector<Real>& sigma = scratch.sigma;
        sigma.resize(cols);
        for (uint32_t k = 0; k < cols; ++k) {
            sigma[k] = std::sqrt(norms[k] > 0 ? norms[k] : 0);
        }
        std::vector<uint32_t>& idx = scratch.idx;
        idx.resize(cols);
        for (uint32_t k = 0; k < cols; ++k) {
            idx[k] = k;
        }
        std::sort(idx.begin(), idx.end(),
            [&](uint32_t x, uint32_t y) { return sigma[x] > sigma[y]; });

        const uint32_t full_rank = cols;  // min(m, n)
        const uint32_t rank =
            (max_rank > 0 && max_rank < full_rank) ? max_rank : full_rank;

        // Gate truncation error (the paper's eps_g), orientation-independent: the
        // L2 mass in the singular values the chi-cap drops. 0 when untruncated.
        for (uint32_t r = rank; r < full_rank; ++r) {
            const Real dropped = sigma[idx[r]];
            out.discarded_weight += dropped * dropped;
        }

        // U_B = normalized selected columns of B (rows x rank);
        // V_B = selected columns of the accumulated V (cols x rank).
        std::vector<Complex>& ub = scratch.ub;
        std::vector<Complex>& vb = scratch.vb;
        ub.resize(static_cast<std::size_t>(rows) * rank);
        vb.resize(static_cast<std::size_t>(cols) * rank);
        out.s.resize(rank);
        for (uint32_t r = 0; r < rank; ++r) {
            const uint32_t col = idx[r];
            out.s[r] = sigma[col];
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

        out.rank = rank;
        out.vh.assign(static_cast<std::size_t>(rank) * n, Complex{ 0, 0 });
        // u is `rows x rank` in the untransposed case and `cols x rank` in the
        // transposed one -- both equal m x rank, since m is whichever of the two
        // the orientation put first.
        out.u.resize(static_cast<std::size_t>(m) * rank);
        const std::vector<Complex>& u_src = tr ? vb : ub;
        const std::vector<Complex>& vh_src = tr ? ub : vb;
        std::copy(
            u_src.begin(),
            u_src.begin() + static_cast<std::ptrdiff_t>(
                static_cast<std::size_t>(m) * rank),
            out.u.begin());
        for (uint32_t r = 0; r < rank; ++r) {
            for (uint32_t j = 0; j < n; ++j) {
                out.vh[static_cast<std::size_t>(r) * n + j] =
                    std::conj(vh_src[static_cast<std::size_t>(j) * rank + r]);
            }
        }
    }

    Svd svd(
        const std::vector<Complex>& a, uint32_t m, uint32_t n, uint32_t max_rank)
    {
        Svd out;
        SvdScratch scratch;
        svd(a, m, n, max_rank, out, scratch);
        return out;
    }

    bool measure(Register& reg, uint32_t q, Rng& rng)
    {
        const Real p1 = marginal(reg, q);
        const bool outcome = rng.next_unit() < p1;
        project(reg, q, outcome);
        return outcome;
    }

    bool measure_in_basis(Register& reg, uint32_t q, Real theta, Rng& rng)
    {
        // Rotate the measurement axis n̂(theta) onto z with R_y(-theta), row-major
        // {cos(t/2), sin(t/2); -sin(t/2), cos(t/2)}: a z-measurement of the rotated
        // register is a n̂-measurement of the original. No rotate-back -- the
        // leftover unitary on q leaves every OTHER qubit's reduced state (and the
        // joint statistics) unchanged, and the caller latches + rearms before reuse.
        const Real c = std::cos(theta * Real{ 0.5 });
        const Real s = std::sin(theta * Real{ 0.5 });
        const Complex u[4] = {
            Complex{ c, 0 }, Complex{ s, 0 },
            Complex{ -s, 0 }, Complex{ c, 0 },
        };
        apply_1q(reg, q, u);
        return measure(reg, q, rng);
    }

    void project(Register& reg, uint32_t q, bool value)
    {
        const uint64_t stride = uint64_t{ 1 } << q;
        const uint64_t dim = reg.dim();
        for (uint64_t k = 0; k < dim; ++k) {
            const bool bit = (k & stride) != 0;
            if (bit != value) {
                reg.amp[k] = Complex{ 0, 0 };
            }
        }
        normalize(reg);
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
