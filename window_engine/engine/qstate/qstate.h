#pragma once

// engine/qstate/qstate.h
//
// wz::qstate -- the per-node quantum register for the wavefunction-based agent
// model (Tindall-et-al-inspired tensor-network / belief-propagation cognition).
//
// THIS HEADER IS A LEAF: it depends only on the standard library (no engine,
// math, or graph types). A cognition node carries a Register (its wavefunction
// over a few decision + memory qubits); the gates below evolve it (the paper's
// transverse-field-Ising pieces), and measurement/marginals read it out. Inter-
// node bonds, chi-truncation, and belief propagation live in a separate layer
// on top of the graph types -- NOT here. This slice is the single-node math.
//
// Conventions:
//   * Amplitudes are complex; the state is a dense vector of 2^qubits entries.
//   * Basis index is LITTLE-ENDIAN: bit q of an index is qubit q's value, so
//     index = sum_q (bit_q << q). "qubit 0" is the least significant bit.
//   * Real is double: the register is tiny (n <= ~10 -> <= 1024 entries) and we
//     want norm/unitarity to hold tightly across many gates. Swap the typedef if
//     a consumer ever needs float at the boundary.

#include <complex>
#include <cstdint>
#include <vector>

namespace wz::qstate
{
    using Real = double;
    using Complex = std::complex<Real>;

    // Deterministic PRNG (xorshift64*) so measurement is reproducible given a
    // seed -- important for tests and for replayable agent behavior. Kept here so
    // the leaf needs no <random> at call sites.
    struct Rng
    {
        uint64_t state;

        explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) noexcept
            : state(seed ? seed : 0x9E3779B97F4A7C15ull)
        {
        }

        uint64_t next_u64() noexcept;
        Real next_unit() noexcept;  // uniform in [0, 1)
    };

    // A pure-state register over `qubits` qubits. amp.size() == (1 << qubits).
    struct Register
    {
        uint32_t qubits = 0;
        std::vector<Complex> amp;

        uint64_t dim() const noexcept { return amp.size(); }
    };

    // ---- construction ----
    Register basis_zero(uint32_t qubits);  // |0...0>
    Register uniform(uint32_t qubits);     // equal superposition (all amps equal)

    // ---- single-qubit gates (q in [0, qubits)) ----
    // Arbitrary 2x2 unitary, row-major {m00, m01, m10, m11}. The named gates are
    // conveniences over this; unitarity of `m` is the caller's responsibility.
    void apply_1q(Register& reg, uint32_t q, const Complex m[4]);
    // e^{-i theta sigma_x}: transverse-field MIXING (the Gamma exploration term).
    void apply_x_field(Register& reg, uint32_t q, Real theta);
    // e^{-i theta sigma_z}: longitudinal field -- a disposition/goal BIAS.
    void apply_z_field(Register& reg, uint32_t q, Real theta);
    void apply_hadamard(Register& reg, uint32_t q);

    // ---- two-qubit gate (a != b, both in range) ----
    // e^{-i theta sigma_z^a sigma_z^b}: the COUPLING term (consistency / one-hot
    // within a node; member<->collective when applied across a bond's projection).
    // Diagonal in the computational basis.
    void apply_zz(Register& reg, uint32_t a, uint32_t b, Real theta);

    // ---- reward / phase ----
    // Multiply every basis state whose index, masked by `mask`, equals `match` by
    // e^{i phase}. This is the reward phase-kick: it marks a (memory, action)
    // branch. It changes NO magnitudes (so marginals are unchanged immediately);
    // the written phase reshapes amplitudes only through SUBSEQUENT gates'
    // interference -- which is how learning rides the memory subspace.
    void apply_phase_mask(Register& reg, uint64_t mask, uint64_t match, Real phase);

    // ---- readout ----
    Real norm(const Register& reg);            // sqrt(sum |amp|^2)
    void normalize(Register& reg);
    Real marginal(const Register& reg, uint32_t q);  // P(qubit q == 1)
    Real expectation_z(const Register& reg, uint32_t q);  // <sigma_z> = P0 - P1, in [-1,1]

    // ---- imaginary-time relaxation (NON-unitary; ground-state finding) ----
    // Apply e^{-H dtau} with H = -h*sigma_z - gamma*sigma_x on qubit q, then
    // renormalize. Unlike the unitary gates above this is dissipative: it nudges
    // the register toward that qubit's ground state under a longitudinal field h
    // and transverse field gamma. The cognition layer relaxes an agent toward its
    // decision this way, with h the effective field from its neighbors. (Mirrors
    // the paper's imaginary-time ground-state preparation; unitary evolution only
    // oscillates and would never settle.)
    void apply_imag_time_field(
        Register& reg, uint32_t q, Real gamma, Real h, Real dtau);

    // Imaginary-time coupling step for H = -j*sigma_z^a*sigma_z^b: applies the
    // NON-unitary e^{-H dtau} and renormalizes. j > 0 relaxes the pair toward
    // agreement (ferromagnetic), j < 0 toward disagreement. This is the two-body
    // partner of apply_imag_time_field; together they relax a joint register
    // toward the entangled ground state of a transverse-field Ising group.
    void apply_imag_time_zz(
        Register& reg, uint32_t a, uint32_t b, Real j, Real dtau);

    // <sigma_z^a sigma_z^b>: +1 weight on basis states where qubits a,b agree,
    // -1 where they differ. The connected correlation
    // <sz_a sz_b> - <sz_a><sz_b> is nonzero exactly when a,b are correlated
    // beyond a product state -- the entanglement witness mean-field cannot show.
    Real expectation_zz(const Register& reg, uint32_t a, uint32_t b);

    // ---- complex SVD (for tensor-network chi-truncation) ----
    // Thin singular value decomposition of a complex m x n matrix A (row-major,
    // A[i*n + j]) via one-sided Jacobi: A = U * diag(s) * Vh, with
    //   U  : m x rank, orthonormal columns,
    //   s  : rank real singular values, descending, >= 0,
    //   Vh : rank x n, orthonormal rows (= V^dagger),
    // where rank = min(m, n), truncated to at most max_rank (0 = no truncation --
    // keep all). Truncating to chi keeps the chi largest singular values, the
    // best rank-chi approximation (Eckart-Young) -- the bond-dimension cap.
    struct Svd
    {
        uint32_t rows = 0;
        uint32_t cols = 0;
        uint32_t rank = 0;
        std::vector<Complex> u;   // rows x rank
        std::vector<Real> s;      // rank
        std::vector<Complex> vh;  // rank x cols
    };

    Svd svd(
        const std::vector<Complex>& a,
        uint32_t rows,
        uint32_t cols,
        uint32_t max_rank = 0);

    // ---- measurement (PARTIAL; Born rule; collapses + renormalizes) ----
    // Measures only qubit q: samples its value by the Born rule, projects the
    // state onto that outcome, and renormalizes. Other qubits stay coherent --
    // committing one action does not collapse the rest of the deliberation.
    bool measure(Register& reg, uint32_t q, Rng& rng);
    // Measures every qubit, returning the collapsed basis index.
    uint64_t measure_all(Register& reg, Rng& rng);
}
