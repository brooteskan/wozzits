#include <cognition/agent_cognition.h>
#include <cognition/coordination.h>

#include <gtest/gtest.h>

#include <cmath>
#include <optional>

// measure_in_basis -- the non-commuting (rotated-basis) projective measurement.
// The point of these tests is the CHSH separation: an entanglement-capable mind
// read along incompatible axes produces joint correlations that beat the classical
// bound S <= 2 (up to Tsirelson's 2*sqrt(2)), while the SAME authored mind on a
// product-state (chi = 1) backend cannot -- the honest "more than a probabilistic
// FSM" line. Nothing here bakes a "Bell pair" into the engine: the pair is just
// authored test data (a 2-qubit ferromagnetic group, whose ground state is the
// cat/GHZ state (|00>+|11>)/sqrt2), and the primitive measures whatever it is given.

using namespace wz::engine::cognition;

namespace {

constexpr double kPi = 3.14159265358979323846;

// +1 / -1 eigenvalue from a measured bit (false == |0> == +1, per qstate::measure).
double eigenvalue(bool bit) { return bit ? -1.0 : +1.0; }

// One entangled ground state: a two-agent ferromagnetic group relaxed to the cat.
ExactBond cat_bond() { return ExactBond{ .a = 0, .b = 1, .j = 1.0 }; }

// Estimate the CHSH combination
//   S = E(a,b) - E(a,b') + E(a',b) + E(a',b')
// where E(x,y) is the mean product of the two agents' +/-1 outcomes measured along
// axis x (agent 0) and axis y (agent 1). A measurement collapses the state, so each
// shot COPIES `prepared` (the once-relaxed ground state) and measures the copy; the
// rng advances across shots.
double chsh_s(
    const Coordination& prepared,
    double a, double a_prime, double b, double b_prime,
    int shots, uint64_t seed)
{
    qstate::Rng rng{ seed };
    auto correlator = [&](double t0, double t1) {
        double sum = 0.0;
        for (int i = 0; i < shots; ++i) {
            Coordination c = prepared;  // fresh copy of the ground state per shot
            const bool o0 = measure_in_basis(c, 0, t0, rng);
            const bool o1 = measure_in_basis(c, 1, t1, rng);
            sum += eigenvalue(o0) * eigenvalue(o1);
        }
        return sum / shots;
    };
    return correlator(a, b) - correlator(a, b_prime)
        + correlator(a_prime, b) + correlator(a_prime, b_prime);
}

// The optimal CHSH settings for a cat/Bell state (E(x,y) = cos(x - y)): classical
// bound 2, Tsirelson's quantum bound 2*sqrt(2) ~ 2.828.
const double kA = 0.0;
const double kAPrime = kPi / 2.0;
const double kB = kPi / 4.0;
const double kBPrime = 3.0 * kPi / 4.0;

}  // namespace

// ---- primitive correctness -------------------------------------------------

// Measuring |0> along axis theta yields outcome |1> with probability sin^2(theta/2)
// (Born rule for the rotated projector): theta = 0 never flips (a pure z read),
// theta = pi is certain to flip.
TEST(MeasureBasis, SingleQubitBornStatistics)
{
    qstate::Rng rng{ 12345 };
    const double theta = kPi / 3.0;  // 60 deg -> sin^2(30 deg) = 0.25
    const int shots = 20000;
    int ones = 0;
    for (int i = 0; i < shots; ++i) {
        qstate::Register reg = qstate::basis_zero(1);
        if (qstate::measure_in_basis(reg, 0, theta, rng)) {
            ++ones;
        }
    }
    const double p1 = static_cast<double>(ones) / shots;
    const double expected = std::sin(theta / 2.0) * std::sin(theta / 2.0);
    EXPECT_NEAR(p1, expected, 0.02);
}

// A z-basis measurement (theta = 0) on the entangled cat CONDITIONS the partner:
// reading agent 0 drags agent 1 to the same branch (ferromagnetic). This
// back-action -- the projection acting on the shared joint register -- is what
// makes the joint statistics non-classical.
TEST(MeasureBasis, BackActionConditionsEntangledPartner)
{
    qstate::Rng rng{ 7 };
    for (int trial = 0; trial < 20; ++trial) {
        ExactGroup g = make_exact_group(2, { cat_bond() });
        relax(g, /*gamma=*/0.1, /*dtau=*/0.05, /*iterations=*/400);
        const bool o0 = measure_in_basis(g, 0, /*theta=*/0.0, rng);
        const double z1 = decision_z(g, 1);
        if (o0) {
            EXPECT_LT(z1, -0.9);  // agent 0 -> |1| forces agent 1 -> |1>
        } else {
            EXPECT_GT(z1, 0.9);   // agent 0 -> |0> forces agent 1 -> |0>
        }
    }
}

// ---- the separation: exact violates, chi = 1 does not, chi >= 2 recovers it ----

// EXACT backend (chi = 0, full 2^N joint state): the entangled cat VIOLATES CHSH,
// |S| > 2 -- no classical shared-randomness / finite-state model reproduces it.
TEST(MeasureBasis, ExactEntangledViolatesChsh)
{
    Coordination c{ make_exact_group(2, { cat_bond() }) };
    relax(c, 0.1, 0.05, 400);
    const double s = std::abs(chsh_s(c, kA, kAPrime, kB, kBPrime, 4000, 0xB0FFu));
    EXPECT_GT(s, 2.2);           // clears the classical bound (2)
    EXPECT_LT(s, 2.828 + 0.05);  // stays under Tsirelson
}

// LOOPY-BP backend (chi = 1, product state): the SAME authored mind CANNOT violate
// -- it carries no entanglement, so the joint statistics factorize and |S| <= 2.
// This is the classical floor / control that proves the violation above is the
// entanglement, not the measurement.
TEST(MeasureBasis, LoopyBpChiOneCannotViolate)
{
    Coordination c{ make_loopy_bp_group(2, { cat_bond() }) };
    relax(c, 0.1, 0.05, 400);
    const double s = std::abs(chsh_s(c, kA, kAPrime, kB, kBPrime, 4000, 0xC0FFEEu));
    EXPECT_LE(s, 2.0 + 0.05);  // never beats the classical bound
}

// TTN backend at chi = 2: a two-qubit maximally entangled state is Schmidt-rank 2,
// so chi = 2 captures it EXACTLY -- the violation returns, matching exact. This is
// why chi >= 2 stays in: GHZ/Mermin nonclassicality at linear cost.
TEST(MeasureBasis, TtnChiTwoRecoversViolation)
{
    Coordination c{ make_ttn_chain(2, { 1.0 }, /*chi=*/2) };
    relax(c, 0.1, 0.05, 400);
    const double s = std::abs(chsh_s(c, kA, kAPrime, kB, kBPrime, 4000, 0x7717u));
    EXPECT_GT(s, 2.2);
}

// ---- store surface ---------------------------------------------------------

// The store's measure_in_basis latches the outcome as the committed decision (so
// committed() / marginal read it back) and returns it; rearm re-opens it.
TEST(MeasureBasis, StoreLatchesOutcomeAndRearmClears)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 2;
    spec.bonds = { cat_bond() };
    // Disable the self-timed commit policy (confidence unreachable, no decoherence)
    // so think() only anneals -- we drive the measurement ourselves.
    spec.commit = CommitPolicy{ .confidence = 2.0, .decoherence_rate = 0.0 };
    spec.chi = 0;  // exact
    const AgentHandle h = store.create(spec);
    ASSERT_NE(h, kInvalidAgent);

    store.start(h, 0.0);
    for (int i = 1; i <= 50; ++i) {
        store.think(h, 0.1 * i);  // anneal toward the entangled ground state
    }
    ASSERT_FALSE(store.committed(h, 0).has_value());  // still deliberating

    const std::optional<bool> out = store.measure_in_basis(h, 0, kPi / 4.0);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(store.committed(h, 0), out);  // latched as the committed bit
    EXPECT_NEAR(store.marginal(h, 0), *out ? -1.0 : 1.0, 1e-9);

    store.rearm(h, 10.0);
    EXPECT_FALSE(store.committed(h, 0).has_value());  // re-opened

    // Bad handle / out-of-range agent -> nullopt.
    EXPECT_FALSE(store.measure_in_basis(h, 5, 0.0).has_value());
}
