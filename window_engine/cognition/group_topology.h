#pragma once

// cognition/group_topology.h
//
// Bond-list canonicalization for a coordination group. An author draws an
// arbitrary relationship graph -- self-loops, parallel edges, either
// orientation, out-of-range endpoints -- and this normalizes it into the
// girth >= 3 form the tensor-network / BP coordination backends are well-posed
// on, then classifies the topology of the cleaned-up graph.
//
// "girth >= 3" means the smallest cycle has length >= 3: no self-loops
// (a 1-cycle) and no doubled edges between the same pair (a 2-cycle). Both are
// removed here -- self-loops dropped (rule 1), parallel edges merged (rule 2) --
// so the only cycles that survive to be flagged Cyclic are genuine loops of
// three or more distinct agents.

#include <cognition/exact_group.h>

#include <cstdint>
#include <vector>

namespace wz::engine::cognition
{
    enum class GroupTopology
    {
        None,    // no bonds remain after canonicalization
        Tree,    // acyclic bond graph (a forest -- one or more disjoint trees)
        Cyclic,  // at least one genuine (girth >= 3) cycle
    };

    struct CanonicalBonds
    {
        // The canonicalized bonds. Each is on an unordered pair stored with
        // a < b, parallel bonds summed, self-bonds and zero couplings dropped,
        // and the whole list sorted ascending by (a, then b) -- so the result
        // is deterministic regardless of the input order.
        std::vector<ExactBond> bonds;

        // Topology of `bonds` over `agent_count` nodes.
        GroupTopology topology = GroupTopology::None;
    };

    // Canonicalize a coordination group's coupling list to the girth >= 3 form
    // the tensor-network / BP backends are well-posed on, and classify its
    // topology. Canonicalization rules:
    //
    //   1. Drop self-bonds (a == b). A self-coupling is
    //      j*sigma_z^a*sigma_z^a = j*(sigma_z^a)^2 = j*I -- a pure constant
    //      energy shift, dynamically INERT: it cannot bias a decision or change
    //      any marginal. So it is DROPPED, not folded into a self-field. A
    //      self-bond is a constant, not a self-field; an author who wants a bias
    //      on an agent uses a Goal (a longitudinal field), not a self-coupling.
    //
    //   2. Sum parallel bonds. All bonds on the same UNORDERED pair {a,b}
    //      (a != b) combine into ONE bond whose j is their sum. (a,b) and (b,a)
    //      are the same pair; the canonical bond is stored with a < b.
    //
    //   3. Drop zero couplings. After summing, any bond with j == 0.0 (e.g. a
    //      ferro + anti pair that exactly cancels) is dropped -- also
    //      dynamically inert. Exact 0.0 only; couplings are NOT epsilon-
    //      thresholded (a tiny-but-nonzero j is a real, if weak, coupling).
    //
    //   4. Bounds. Any bond referencing an endpoint >= agent_count is out of
    //      range and dropped (defensive against malformed authoring).
    //
    //   5. Deterministic order. The output `bonds` are sorted ascending by
    //      (a, then b), so the result is reproducible regardless of input order.
    //
    // Topology classification (of the canonicalized bonds, on `agent_count`
    // nodes), via union-find over the nodes:
    //   - None   if no bonds remain.
    //   - Tree   if the bond graph is acyclic (a forest): every canonical bond
    //            joins two previously-unconnected components.
    //   - Cyclic if any canonical bond's endpoints are ALREADY connected, i.e.
    //            it closes a cycle. Because parallel bonds are merged and
    //            self-bonds dropped first, no 1- or 2-cycle can survive to be
    //            misclassified -- only genuine girth >= 3 cycles reach here.
    CanonicalBonds canonicalize_bonds(
        uint32_t agent_count, const std::vector<ExactBond>& bonds);
}
