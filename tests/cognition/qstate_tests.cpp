#include <cognition/qstate/qstate.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

using namespace wz::engine::cognition::qstate;

namespace
{
    constexpr Real kPi = 3.14159265358979323846;
    constexpr Real kTight = 1e-12;
}

// --- construction --------------------------------------------------------

TEST(QState, BasisZeroIsComputationalZero)
{
    const Register reg = basis_zero(3);
    EXPECT_EQ(reg.dim(), 8u);
    EXPECT_NEAR(std::abs(reg.amp[0]), 1.0, kTight);
    for (uint64_t k = 1; k < reg.dim(); ++k) {
        EXPECT_NEAR(std::abs(reg.amp[k]), 0.0, kTight);
    }
    EXPECT_NEAR(norm(reg), 1.0, kTight);
    for (uint32_t q = 0; q < reg.qubits; ++q) {
        EXPECT_NEAR(marginal(reg, q), 0.0, kTight);
    }
}

TEST(QState, UniformIsEqualSuperposition)
{
    const Register reg = uniform(3);
    EXPECT_NEAR(norm(reg), 1.0, kTight);
    const Real expect = 1.0 / std::sqrt(8.0);
    for (uint64_t k = 0; k < reg.dim(); ++k) {
        EXPECT_NEAR(std::abs(reg.amp[k]), expect, kTight);
    }
    for (uint32_t q = 0; q < reg.qubits; ++q) {
        EXPECT_NEAR(marginal(reg, q), 0.5, kTight);
    }
}

// --- single-qubit gates --------------------------------------------------

TEST(QState, HadamardIsSelfInverse)
{
    Register reg = basis_zero(1);
    apply_hadamard(reg, 0);
    EXPECT_NEAR(marginal(reg, 0), 0.5, kTight);  // superposed
    apply_hadamard(reg, 0);
    EXPECT_NEAR(marginal(reg, 0), 0.0, kTight);  // back to |0>
    EXPECT_NEAR(norm(reg), 1.0, kTight);
}

TEST(QState, XFieldHalfPiFlipsTheQubit)
{
    Register reg = basis_zero(1);
    apply_x_field(reg, 0, kPi / 2.0);  // -> -i|1>
    EXPECT_NEAR(marginal(reg, 0), 1.0, kTight);
    EXPECT_NEAR(norm(reg), 1.0, kTight);
}

TEST(QState, XFieldQuarterPiIsHalfProbability)
{
    Register reg = basis_zero(1);
    apply_x_field(reg, 0, kPi / 4.0);  // sin^2(pi/4) = 0.5
    EXPECT_NEAR(marginal(reg, 0), 0.5, kTight);
    EXPECT_NEAR(norm(reg), 1.0, kTight);
}

TEST(QState, ZFieldPreservesMagnitudesButRotatesPhase)
{
    Register reg = uniform(1);  // (|0> + |1>)/sqrt2
    apply_z_field(reg, 0, 0.7);
    EXPECT_NEAR(marginal(reg, 0), 0.5, kTight);   // magnitudes unchanged
    // Relative phase between |1> and |0> is e^{+0.7i}/e^{-0.7i} = e^{1.4i}.
    const Complex rel = reg.amp[1] / reg.amp[0];
    EXPECT_NEAR(std::arg(rel), 1.4, 1e-9);
    EXPECT_NEAR(norm(reg), 1.0, kTight);
}

// --- two-qubit coupling --------------------------------------------------

TEST(QState, ZZAppliesParityDependentPhases)
{
    Register reg = uniform(2);  // all amps = 1/2
    const Real theta = 0.3;
    apply_zz(reg, 0, 1, theta);

    const Real half = 0.5;
    // bits agree (00, 11) -> e^{-i theta}; differ (01, 10) -> e^{+i theta}
    EXPECT_NEAR(std::arg(reg.amp[0]), -theta, 1e-9);
    EXPECT_NEAR(std::arg(reg.amp[3]), -theta, 1e-9);
    EXPECT_NEAR(std::arg(reg.amp[1]), theta, 1e-9);
    EXPECT_NEAR(std::arg(reg.amp[2]), theta, 1e-9);
    for (uint64_t k = 0; k < 4; ++k) {
        EXPECT_NEAR(std::abs(reg.amp[k]), half, kTight);
    }
    EXPECT_NEAR(norm(reg), 1.0, kTight);
}

// --- invariants ----------------------------------------------------------

TEST(QState, NormPreservedAcrossGateSequence)
{
    Register reg = basis_zero(3);
    // A scramble of every gate type; each is unitary, so norm must stay 1.
    apply_hadamard(reg, 0);
    apply_x_field(reg, 1, 0.9);
    apply_z_field(reg, 2, 0.4);
    apply_zz(reg, 0, 2, 0.7);
    apply_x_field(reg, 2, 1.3);
    apply_hadamard(reg, 1);
    apply_zz(reg, 1, 2, 0.2);
    apply_z_field(reg, 0, 2.1);
    EXPECT_NEAR(norm(reg), 1.0, kTight);
}

// The genuinely-quantum test: two amplitude paths to |1> destructively cancel
// when the state stays COHERENT (H,H -> |0>, marginal 0), but measuring in
// between destroys the interference (H, measure, H -> marginal 0.5). No
// classical probability model reproduces the cancellation.
TEST(QState, InterferenceCancelsWhenCoherentAndNotAfterMeasurement)
{
    Register coherent = basis_zero(1);
    apply_hadamard(coherent, 0);
    apply_hadamard(coherent, 0);
    EXPECT_NEAR(marginal(coherent, 0), 0.0, kTight);  // destructive cancellation

    Register collapsed = basis_zero(1);
    apply_hadamard(collapsed, 0);
    Rng rng{ 12345u };
    (void)measure(collapsed, 0, rng);                 // decohere mid-path
    apply_hadamard(collapsed, 0);
    EXPECT_NEAR(marginal(collapsed, 0), 0.5, kTight); // interference gone
}

// --- reward phase --------------------------------------------------------

// apply_phase_mask writes phase only (magnitudes/marginals unchanged immediately),
// but that phase becomes observable through a SUBSEQUENT gate's interference --
// the mechanism by which a reward reshapes future amplitudes.
TEST(QState, RewardPhaseIsObservableOnlyAfterMixing)
{
    Register reg = basis_zero(1);
    apply_hadamard(reg, 0);                       // (|0> + |1>)/sqrt2
    apply_phase_mask(reg, /*mask=*/1u, /*match=*/1u, kPi);  // |1> -> -|1>
    EXPECT_NEAR(marginal(reg, 0), 0.5, kTight);   // magnitudes untouched
    apply_hadamard(reg, 0);                        // H(|0> - |1>)/sqrt2 = |1>
    EXPECT_NEAR(marginal(reg, 0), 1.0, kTight);   // phase flipped the outcome
}

// --- measurement ---------------------------------------------------------

TEST(QState, MeasurementIsPartialAndLeavesOtherQubitsCoherent)
{
    Register reg = basis_zero(2);
    apply_x_field(reg, 1, kPi / 2.0);  // qubit 1 -> |1>
    apply_hadamard(reg, 0);            // qubit 0 superposed
    EXPECT_NEAR(marginal(reg, 1), 1.0, kTight);
    EXPECT_NEAR(marginal(reg, 0), 0.5, kTight);

    Rng rng{ 777u };
    (void)measure(reg, 0, rng);        // collapse only qubit 0
    EXPECT_NEAR(marginal(reg, 1), 1.0, kTight);  // qubit 1 untouched
    const Real m0 = marginal(reg, 0);
    EXPECT_TRUE(std::abs(m0) < kTight || std::abs(m0 - 1.0) < kTight);  // collapsed
    EXPECT_NEAR(norm(reg), 1.0, kTight);
}

TEST(QState, MeasurementFollowsBornRule)
{
    // Prepare a state with P(1) = sin^2(pi/6) = 0.25 and sample many times.
    Rng rng{ 0xC0FFEEu };
    const uint32_t trials = 40000u;
    uint32_t ones = 0;
    for (uint32_t t = 0; t < trials; ++t) {
        Register reg = basis_zero(1);
        apply_x_field(reg, 0, kPi / 6.0);
        if (measure(reg, 0, rng)) {
            ++ones;
        }
    }
    const Real freq = static_cast<Real>(ones) / trials;
    EXPECT_NEAR(freq, 0.25, 0.02);
}

// --- expectation + imaginary-time relaxation ---------------------------

TEST(QState, ExpectationZMatchesBasisAndSuperposition)
{
    Register zero = basis_zero(1);
    EXPECT_NEAR(expectation_z(zero, 0), 1.0, kTight);   // |0> -> +1

    Register one = basis_zero(1);
    apply_x_field(one, 0, kPi / 2.0);                    // -> |1>
    EXPECT_NEAR(expectation_z(one, 0), -1.0, kTight);    // |1> -> -1

    Register sup = uniform(1);
    EXPECT_NEAR(expectation_z(sup, 0), 0.0, kTight);     // |+> -> 0
}

TEST(QState, ImagTimeFieldRelaxesTowardGroundState)
{
    // Positive longitudinal field, no transverse field: ground state is |0>, so
    // <sigma_z> climbs toward +1 from an unbiased start.
    Register up = uniform(1);
    for (int i = 0; i < 200; ++i) {
        apply_imag_time_field(up, 0, /*gamma=*/0.0, /*h=*/0.5, /*dtau=*/0.05);
    }
    EXPECT_GT(expectation_z(up, 0), 0.99);
    EXPECT_NEAR(norm(up), 1.0, 1e-9);

    // Negative field -> ground state |1> -> <sigma_z> toward -1.
    Register down = uniform(1);
    for (int i = 0; i < 200; ++i) {
        apply_imag_time_field(down, 0, 0.0, -0.5, 0.05);
    }
    EXPECT_LT(expectation_z(down, 0), -0.99);

    // Strong transverse field, no longitudinal: ground state is |+> -> <sz> ~ 0,
    // even starting from a biased state.
    Register para = basis_zero(1);  // biased to |0>
    for (int i = 0; i < 400; ++i) {
        apply_imag_time_field(para, 0, /*gamma=*/1.0, /*h=*/0.0, 0.05);
    }
    EXPECT_NEAR(expectation_z(para, 0), 0.0, 1e-6);
}

TEST(QState, ExpectationZZMatchesBasisStates)
{
    Register agree = basis_zero(2);  // |00>
    EXPECT_NEAR(expectation_zz(agree, 0, 1), 1.0, kTight);

    Register differ = basis_zero(2);
    apply_x_field(differ, 1, kPi / 2.0);  // -> |10> (qubit 1 set)
    EXPECT_NEAR(expectation_zz(differ, 0, 1), -1.0, kTight);
}

// Imaginary-time ZZ relaxes a pair into the ferromagnetic cat state
// (|00> + |11>)/sqrt2: each qubit is unpolarized (<sigma_z> = 0) yet perfectly
// correlated (<sigma_z sigma_z> = 1). That is genuine entanglement -- correlation
// without polarization -- which a product (mean-field) state cannot represent.
TEST(QState, ImagTimeZZBuildsCorrelatedCatState)
{
    Register reg = uniform(2);
    for (int i = 0; i < 200; ++i) {
        apply_imag_time_zz(reg, 0, 1, /*j=*/1.0, /*dtau=*/0.1);
    }
    EXPECT_NEAR(expectation_z(reg, 0), 0.0, 1e-6);
    EXPECT_NEAR(expectation_z(reg, 1), 0.0, 1e-6);
    EXPECT_GT(expectation_zz(reg, 0, 1), 0.999);
    EXPECT_NEAR(norm(reg), 1.0, 1e-9);
}

TEST(QState, ImagTimeZZAntiAlignsWithNegativeCoupling)
{
    Register reg = uniform(2);
    for (int i = 0; i < 200; ++i) {
        apply_imag_time_zz(reg, 0, 1, -1.0, 0.1);
    }
    EXPECT_LT(expectation_zz(reg, 0, 1), -0.999);
}

// --- complex SVD --------------------------------------------------------

namespace
{
    std::vector<Complex> random_matrix(uint32_t m, uint32_t n, Rng& rng)
    {
        std::vector<Complex> a(static_cast<std::size_t>(m) * n);
        for (Complex& c : a) {
            c = Complex{ rng.next_unit() * 2.0 - 1.0, rng.next_unit() * 2.0 - 1.0 };
        }
        return a;
    }

    std::vector<Complex> reconstruct(const Svd& d)
    {
        std::vector<Complex> r(
            static_cast<std::size_t>(d.rows) * d.cols, Complex{ 0, 0 });
        for (uint32_t i = 0; i < d.rows; ++i) {
            for (uint32_t j = 0; j < d.cols; ++j) {
                Complex acc{ 0, 0 };
                for (uint32_t k = 0; k < d.rank; ++k) {
                    acc += d.u[i * d.rank + k] * d.s[k] * d.vh[k * d.cols + j];
                }
                r[static_cast<std::size_t>(i) * d.cols + j] = acc;
            }
        }
        return r;
    }

    double max_diff(const std::vector<Complex>& a, const std::vector<Complex>& b)
    {
        double m = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            m = std::max(m, std::abs(a[i] - b[i]));
        }
        return m;
    }
}

TEST(QState, SvdReconstructsRandomMatrices)
{
    Rng rng{ 0x5D0u };
    for (auto [m, n] : { std::pair{ 4u, 4u }, std::pair{ 5u, 3u },
             std::pair{ 3u, 5u }, std::pair{ 1u, 4u } }) {
        for (int trial = 0; trial < 10; ++trial) {
            const auto a = random_matrix(m, n, rng);
            const Svd d = svd(a, m, n);
            EXPECT_EQ(d.rank, std::min(m, n));
            EXPECT_LT(max_diff(reconstruct(d), a), 1e-9)
                << m << "x" << n << " trial " << trial;
        }
    }
}

TEST(QState, SvdSingularValuesAreDescendingAndNonNegative)
{
    Rng rng{ 0x123u };
    const auto a = random_matrix(5, 4, rng);
    const Svd d = svd(a, 5, 4);
    for (uint32_t k = 0; k < d.rank; ++k) {
        EXPECT_GE(d.s[k], 0.0);
        if (k > 0) {
            EXPECT_GE(d.s[k - 1] + 1e-12, d.s[k]);
        }
    }
}

TEST(QState, SvdFactorsAreOrthonormal)
{
    Rng rng{ 0x777u };
    const auto a = random_matrix(5, 4, rng);
    const Svd d = svd(a, 5, 4);

    // U^dagger U = I (rank x rank).
    for (uint32_t r = 0; r < d.rank; ++r) {
        for (uint32_t rp = 0; rp < d.rank; ++rp) {
            Complex acc{ 0, 0 };
            for (uint32_t i = 0; i < d.rows; ++i) {
                acc += std::conj(d.u[i * d.rank + r]) * d.u[i * d.rank + rp];
            }
            EXPECT_NEAR(acc.real(), r == rp ? 1.0 : 0.0, 1e-9);
            EXPECT_NEAR(acc.imag(), 0.0, 1e-9);
        }
    }
    // Vh Vh^dagger = I (rows of Vh orthonormal).
    for (uint32_t r = 0; r < d.rank; ++r) {
        for (uint32_t rp = 0; rp < d.rank; ++rp) {
            Complex acc{ 0, 0 };
            for (uint32_t j = 0; j < d.cols; ++j) {
                acc += d.vh[r * d.cols + j] * std::conj(d.vh[rp * d.cols + j]);
            }
            EXPECT_NEAR(acc.real(), r == rp ? 1.0 : 0.0, 1e-9);
            EXPECT_NEAR(acc.imag(), 0.0, 1e-9);
        }
    }
}

// Truncation keeps the largest singular values (the best low-rank approximation).
TEST(QState, SvdTruncationKeepsLargestSingularValues)
{
    // diag(3, 2, 1).
    std::vector<Complex> a(9, Complex{ 0, 0 });
    a[0] = Complex{ 3, 0 };
    a[4] = Complex{ 2, 0 };
    a[8] = Complex{ 1, 0 };

    const Svd full = svd(a, 3, 3);
    ASSERT_EQ(full.rank, 3u);
    EXPECT_NEAR(full.s[0], 3.0, 1e-9);
    EXPECT_NEAR(full.s[1], 2.0, 1e-9);
    EXPECT_NEAR(full.s[2], 1.0, 1e-9);
    EXPECT_LT(max_diff(reconstruct(full), a), 1e-9);

    const Svd trunc = svd(a, 3, 3, /*max_rank=*/2);
    EXPECT_EQ(trunc.rank, 2u);
    EXPECT_NEAR(trunc.s[0], 3.0, 1e-9);
    EXPECT_NEAR(trunc.s[1], 2.0, 1e-9);
    // Dropping the sigma=1 component leaves exactly that error.
    EXPECT_NEAR(max_diff(reconstruct(trunc), a), 1.0, 1e-9);

    // The discarded weight is the dropped singular value squared: sigma=1 -> 1.0.
    EXPECT_NEAR(trunc.discarded_weight, 1.0, 1e-9);
    // Untruncated (full and max_rank=0) discard nothing.
    EXPECT_NEAR(full.discarded_weight, 0.0, 1e-12);
    EXPECT_NEAR(svd(a, 3, 3, /*max_rank=*/0).discarded_weight, 0.0, 1e-12);
}

// discarded_weight equals the summed square of the singular values the chi-cap
// dropped -- computed against an untruncated SVD that knows every sigma.
TEST(QState, SvdDiscardedWeightMatchesDroppedSingularValues)
{
    Rng rng{ 0xD15Cu };
    const auto a = random_matrix(5, 4, rng);

    const Svd full = svd(a, 5, 4);  // untruncated: every sigma
    ASSERT_EQ(full.rank, 4u);
    EXPECT_NEAR(full.discarded_weight, 0.0, 1e-12);

    for (uint32_t keep = 1; keep < full.rank; ++keep) {
        double expected = 0.0;
        for (uint32_t r = keep; r < full.rank; ++r) {
            expected += full.s[r] * full.s[r];
        }
        const Svd trunc = svd(a, 5, 4, keep);
        ASSERT_EQ(trunc.rank, keep);
        EXPECT_NEAR(trunc.discarded_weight, expected, 1e-9) << "keep " << keep;
    }
}

TEST(QState, MeasureAllCollapsesToABasisState)
{
    Register reg = uniform(3);
    Rng rng{ 42u };
    const uint64_t k = measure_all(reg, rng);
    ASSERT_LT(k, reg.dim());
    EXPECT_NEAR(std::abs(reg.amp[k]), 1.0, kTight);
    for (uint64_t j = 0; j < reg.dim(); ++j) {
        if (j != k) {
            EXPECT_NEAR(std::abs(reg.amp[j]), 0.0, kTight);
        }
    }
}

// The Jacobi sweep now converges on the RELATIVE off-diagonal weight instead of
// sweeping until every single pair is orthogonal to within machine epsilon, which
// bought precision far below what a chi-truncated bond can carry. Pin the thing
// that actually matters: A must still reconstruct from U diag(s) Vh, at the
// matrix sizes the tensor-network hot path uses (rows/cols = 2*chi).
TEST(QState, SvdReconstructsAtTensorNetworkSizes)
{
    using wz::engine::cognition::qstate::Complex;
    for (uint32_t chi : { 2u, 4u, 8u, 16u }) {
        const uint32_t rows = 2 * chi;
        const uint32_t cols = 2 * chi;
        // Deterministic pseudo-random with a decaying spectrum -- what a relaxed
        // MPS bond looks like, and the case where a loose threshold would show.
        std::vector<Complex> a(static_cast<std::size_t>(rows) * cols);
        uint64_t s = 0x9e3779b97f4a7c15ull + chi;
        const auto next = [&]() {
            s ^= s << 13; s ^= s >> 7; s ^= s << 17;
            return static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0) - 0.5;
        };
        for (uint32_t i = 0; i < rows; ++i) {
            for (uint32_t j = 0; j < cols; ++j) {
                const double decay = 1.0 / (1.0 + static_cast<double>(i + j));
                a[static_cast<std::size_t>(i) * cols + j] =
                    Complex{ next() * decay, next() * decay };
            }
        }

        const auto d = wz::engine::cognition::qstate::svd(a, rows, cols);
        ASSERT_EQ(d.rank, cols);

        double worst = 0.0;
        for (uint32_t i = 0; i < rows; ++i) {
            for (uint32_t j = 0; j < cols; ++j) {
                Complex acc{ 0, 0 };
                for (uint32_t r = 0; r < d.rank; ++r) {
                    acc += d.u[static_cast<std::size_t>(i) * d.rank + r]
                        * Complex{ d.s[r], 0 }
                        * d.vh[static_cast<std::size_t>(r) * cols + j];
                }
                worst = std::max(worst,
                    std::abs(acc - a[static_cast<std::size_t>(i) * cols + j]));
            }
        }
        EXPECT_LT(worst, 1e-12) << "chi = " << chi;
    }
}

// The buffer-reusing overload is the one the hot path calls; it must agree with
// the allocating one exactly, including across repeated calls into the SAME
// scratch and result (where stale contents from a bigger previous matrix could
// leak through if a buffer were resized but not fully written).
TEST(QState, SvdScratchOverloadMatchesTheAllocatingOne)
{
    using wz::engine::cognition::qstate::Complex;
    wz::engine::cognition::qstate::Svd out;
    wz::engine::cognition::qstate::SvdScratch scratch;

    // Deliberately descending sizes, so each call reuses buffers grown larger.
    for (uint32_t n : { 8u, 4u, 6u, 2u, 8u }) {
        std::vector<Complex> a(static_cast<std::size_t>(n) * n);
        for (uint32_t i = 0; i < n; ++i) {
            for (uint32_t j = 0; j < n; ++j) {
                a[static_cast<std::size_t>(i) * n + j] = Complex{
                    0.5 + 0.25 * static_cast<double>((i * 7 + j * 3) % 5),
                    0.1 * static_cast<double>((i + 2 * j) % 4) };
            }
        }
        const auto expected = wz::engine::cognition::qstate::svd(a, n, n, 3u);
        wz::engine::cognition::qstate::svd(a, n, n, 3u, out, scratch);

        ASSERT_EQ(out.rank, expected.rank) << "n = " << n;
        EXPECT_EQ(out.rows, expected.rows);
        EXPECT_EQ(out.cols, expected.cols);
        EXPECT_EQ(out.u.size(), expected.u.size());
        EXPECT_EQ(out.vh.size(), expected.vh.size());
        EXPECT_NEAR(out.discarded_weight, expected.discarded_weight, 1e-15);
        for (uint32_t r = 0; r < out.rank; ++r) {
            EXPECT_NEAR(out.s[r], expected.s[r], 1e-15);
        }
        for (std::size_t i = 0; i < out.u.size(); ++i) {
            EXPECT_LT(std::abs(out.u[i] - expected.u[i]), 1e-15);
        }
        for (std::size_t i = 0; i < out.vh.size(); ++i) {
            EXPECT_LT(std::abs(out.vh[i] - expected.vh[i]), 1e-15);
        }
    }
}
