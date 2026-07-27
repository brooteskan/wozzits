#pragma once

// cognition/mean_field.h
//
// First-cut cognition coupling: chi = 1 MEAN-FIELD belief propagation. Each node
// is an agent's product-state qstate register, and agents coordinate through
// effective sigma_z fields derived from their neighbors' current <sigma_z>. It is
// SVD-free and linear in the number of agents.
//
// IMPORTANT: this is a product-state approximation -- it carries NO genuine
// entanglement between agents (the chi = 1 corner of the design's coordination
// dial). The entangled chi > 1 tree tensor network (two-site U_ZZ + a complex
// SVD truncation, contracted exactly via the bp_collect/bp_distribute scaffold)
// is a REQUIRED follow-up, not an optional upgrade -- see the design doc.
//
// This first slice uses SINGLE-QUBIT nodes (each agent's coupled qubit is qubit
// 0). Multi-qubit nodes come later.

#include <cognition/qstate/qstate.h>
#include <graph/shared_edge_polytree.h>

#include <cstdint>

namespace wz::engine::cognition
{
    using Node = wz::engine::cognition::qstate::Register;

    // A bond between a child and its parent. Carries the coupling and the
    // belief messages (each endpoint's current <sigma_z>) in both directions.
    struct MeanFieldBond
    {
        uint32_t child_qubit = 0;   // coupled qubit on the child endpoint
        uint32_t parent_qubit = 0;  // coupled qubit on the parent endpoint
        double j = 0.0;             // coupling: > 0 ferromagnetic (align), < 0 anti
        double up = 0.0;            // child's <sigma_z>  (message child -> parent)
        double down = 0.0;          // parent's <sigma_z> (message parent -> child)
    };

    using MeanFieldNetwork =
        wz::core::graph::SharedEdgePolytree<Node, MeanFieldBond>;

    // Refresh every bond's up/down messages from the current node states.
    void refresh_messages(MeanFieldNetwork& net);

    // One imaginary-time mean-field relaxation step: each node feels an effective
    // longitudinal field h_eff = sum over its bonds of j * (neighbor's <sigma_z>),
    // applied alongside the transverse field `gamma` via an imaginary-time step of
    // size `dtau`; messages are then refreshed. Jacobi-style -- every node uses
    // the messages from the previous step.
    void relax_step(MeanFieldNetwork& net, double gamma, double dtau);

    // Run `iterations` relax steps (refreshing messages once up front).
    void relax(
        MeanFieldNetwork& net, double gamma, double dtau, uint32_t iterations);

    // Agent's decision marginal <sigma_z> in [-1, 1] (its coupled qubit 0). Same
    // read surface as the exact backend's decision_z, so the two coordination
    // backends are interchangeable behind the contract.
    double decision_z(
        const MeanFieldNetwork& net, wz::core::graph::NodeHandle node);

    // Every node's decision marginal (seam parity with the exact/TTN bulk read).
    std::vector<double> decisions(const MeanFieldNetwork& net);

    // Pin node `node`'s qubit to `bit` (project its product-state register). NB:
    // mean-field has no goal storage, so a subsequent relax_step re-relaxes it --
    // holding a commit needs a re-pin each step (the store does this). Mean-field
    // is not a LIVE coordination backend anyway (chi == 1 routes to loopy BP,
    // and MeanFieldNetwork is no longer a Coordination alternative at all).
    void collapse(
        MeanFieldNetwork& net, wz::core::graph::NodeHandle node, bool bit);

    // Measure node `node` along axis theta -- a local rotated measurement of the
    // node's own single-qubit register (a product-state backend carries no
    // entanglement). Present for coordination-seam parity; mean-field is not a
    // live backend -- chi == 1 routes to loopy BP. Returns the outcome bit.
    bool measure_in_basis(
        MeanFieldNetwork& net, wz::core::graph::NodeHandle node, double theta,
        wz::engine::cognition::qstate::Rng& rng);
}
