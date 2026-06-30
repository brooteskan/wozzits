#include <engine/qstate/qstate.h>

#include <gtest/gtest.h>

#include <cmath>

using namespace wz::qstate;

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
