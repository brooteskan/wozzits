#include <cognition/graph_bp.h>

#include <cognition/tensor_leg.h>

#include <cmath>
#include <complex>
#include <cstddef>

namespace wz::engine::cognition
{
    namespace
    {
        using wz::engine::cognition::qstate::Complex;
        using wz::core::graph::NodeHandle;
        using GraphType =
            wz::core::graph::SharedEdgeGraph<GraphSite, GraphBond>;

        // The full mode-dimension list [2, bond_0, bond_1, ...] for a site tensor.
        // (graph_tn.cpp keeps its own copy in an anonymous namespace; this readout
        // layer is a separate TU, so it carries its own -- both are trivial and the
        // GraphSite layout is the fixed contract, not a shared implementation
        // detail.)
        std::vector<uint32_t> site_dims(const GraphSite& s)
        {
            std::vector<uint32_t> dims;
            dims.reserve(1 + s.bond_dim.size());
            dims.push_back(2);
            for (uint32_t d : s.bond_dim) {
                dims.push_back(d);
            }
            return dims;
        }

        // The ORDERED incidences of node u: neighbor handle, edge id, and this
        // node's leg position for each incident edge, in for_each_neighbor
        // iteration order (which IS u's canonical bond-leg order, matching
        // graph_tn's GraphSite bond_dim[k]). We need the edge id to fetch that
        // edge's message from BpMessages, and the leg position to know which mode
        // to absorb it into / leave open.
        struct Incidence
        {
            NodeHandle neighbor;
            uint32_t edge;  // edge_store index (BpMessages index)
        };

        std::vector<Incidence> incidences_of(const GraphType& graph, NodeHandle u)
        {
            std::vector<Incidence> inc;
            // for_each_neighbor is non-const on the shared-edge graph (BP mutates
            // edges through it); this readout only reads, so cast away const on the
            // graph handle -- nothing here writes edge_store.
            GraphType& mg = const_cast<GraphType&>(graph);
            for (uint32_t i = mg.adj_offsets[u]; i < mg.adj_offsets[u + 1]; ++i) {
                inc.push_back(Incidence{ mg.adj_neighbor[i], mg.adj_edge[i] });
            }
            return inc;
        }

        // The message flowing INTO node u along its incident edge `edge`: the
        // forward slot when u is the HIGH endpoint b (a -> b arrives at b), the
        // backward slot when u is the low endpoint a (b -> a arrives at a). Uses
        // the stored low endpoint to disambiguate.
        const std::vector<Complex>& message_into(
            const BpMessages& msgs, const std::vector<NodeHandle>& edge_low,
            uint32_t edge, NodeHandle u)
        {
            return (edge_low[edge] == u) ? msgs.backward[edge]
                                         : msgs.forward[edge];
        }

        // Weight an incoming message by the SHARED-BOND lambda before it is folded
        // into a node leg: m'[a*D + a'] = lambda[a] * lambda[a'] * m[a*D + a'].
        //
        // WHY this is here and not baked into the message: graph_tn stores the BARE
        // Vidal Gammas -- the shared Schmidt spectrum lambda lives ON the cut, in
        // neither endpoint's tensor. The norm network contracts the two endpoints'
        // Gammas ACROSS that cut with diag(lambda) between them (ket) and diag
        // (lambda) again (bra), so a message crossing edge e carries the factor
        // lambda_a * lambda_{a'} on its (ket a, bra a') indices. The subtree
        // ENVIRONMENT a message represents is, for a canonical Gamma, the IDENTITY
        // (the Gammas are isometries w.r.t. their lambda-weighted environment), so
        // the message stored per edge converges to ~I and this bond weight supplies
        // the diag(lambda^2) that graph_tn's lambda-gauge marginal_z applies on
        // every incident leg. Applying it here (at fold-in time) makes the BP
        // readout reproduce marginal_z exactly on a tree, and keeps the stored
        // messages the pure environment (independent of the shared bond).
        std::vector<Complex> bond_weighted(
            const std::vector<Complex>& m, const std::vector<double>& lambda)
        {
            const std::size_t D = lambda.size();
            std::vector<Complex> w(m);
            for (std::size_t a = 0; a < D; ++a) {
                for (std::size_t ap = 0; ap < D; ++ap) {
                    w[a * D + ap] *= lambda[a] * lambda[ap];
                }
            }
            return w;
        }

        // Fold node u's tensor with conj(u) into the D_target x D_target
        // environment matrix on edge `target_leg` (u's leg position of the target
        // edge), absorbing every INCOMING message on u's OTHER incident edges. This
        // is tree_tn's down_message_to generalized to a graph node: absorb every
        // incident message except the target's into a ket copy of T_u, then
        // contract s and all bond legs except the target against conj(T_u), leaving
        // the target bond's ket / bra legs free.
        //
        // `in_msgs[k]` is the incoming message on u's leg k (the D_k x D_k matrix
        // m_{neighbor_k -> u}); in_msgs[target_leg] is IGNORED. `leg_lambda[k]` is
        // the shared-bond Schmidt spectrum of u's leg k -- each incoming message is
        // bond-weighted by it before folding (see bond_weighted). Returns the
        // D_target x D_target matrix (row-major [ket*D + bra]).
        std::vector<Complex> fold_environment(
            const GraphSite& s, std::size_t target_leg,
            const std::vector<const std::vector<Complex>*>& in_msgs,
            const std::vector<const std::vector<double>*>& leg_lambda)
        {
            const std::vector<uint32_t> dims = site_dims(s);
            const uint32_t Dc = s.bond_dim[target_leg];  // target bond dim

            std::vector<Complex> W = s.t;  // ket working copy
            for (std::size_t k = 0; k < s.bond_dim.size(); ++k) {
                if (k == target_leg) {
                    continue;  // the target bond stays free
                }
                absorb_leg(W, dims, 1 + k,
                    bond_weighted(*in_msgs[k], *leg_lambda[k]));
            }

            // Iterate: outer legs before the target, the target leg, inner legs
            // after it -- mirroring down_message_to. All modes strictly left of the
            // target (physical + earlier bonds) contract to a single summed index;
            // the target leg is free on both W (ket) and conj(T); inner legs after
            // the target share their bra multi-index.
            std::size_t inner = 1;  // product of bond dims after the target leg
            for (std::size_t k = target_leg + 1; k < s.bond_dim.size(); ++k) {
                inner *= s.bond_dim[k];
            }
            std::size_t left_span = 2;  // physical, then bonds before the target
            for (std::size_t k = 0; k < target_leg; ++k) {
                left_span *= s.bond_dim[k];
            }
            const std::size_t c_block = static_cast<std::size_t>(Dc) * inner;

            std::vector<Complex> m(static_cast<std::size_t>(Dc) * Dc,
                Complex{ 0, 0 });
            for (std::size_t left = 0; left < left_span; ++left) {
                const std::size_t left_base = left * c_block;
                for (uint32_t a = 0; a < Dc; ++a) {           // ket index from W
                    const std::size_t w_base = left_base + a * inner;
                    for (uint32_t ap = 0; ap < Dc; ++ap) {    // bra index from T
                        const std::size_t t_base = left_base + ap * inner;
                        Complex acc{ 0, 0 };
                        for (std::size_t j = 0; j < inner; ++j) {
                            acc += W[w_base + j] * std::conj(s.t[t_base + j]);
                        }
                        m[static_cast<std::size_t>(a) * Dc + ap] += acc;
                    }
                }
            }
            return m;
        }

        // Divide a message by its trace so trace == 1. These environment matrices
        // are PSD with positive trace; trace-normalizing keeps them bounded (the
        // loop-stable analog of tree_bp leaving them unnormalized). A degenerate
        // zero-trace matrix (should not occur for a physical state) is left as-is.
        void trace_normalize(std::vector<Complex>& m, uint32_t D)
        {
            Complex tr{ 0, 0 };
            for (uint32_t a = 0; a < D; ++a) {
                tr += m[static_cast<std::size_t>(a) * D + a];
            }
            const double t = tr.real();
            if (std::abs(t) <= 0.0) {
                return;
            }
            const double inv = 1.0 / t;
            for (Complex& c : m) {
                c *= inv;
            }
        }

        // The normalized identity D x D matrix (I / D) in [ket*D + bra] layout --
        // the message initializer.
        std::vector<Complex> identity_over_d(uint32_t D)
        {
            std::vector<Complex> m(static_cast<std::size_t>(D) * D, Complex{ 0, 0 });
            const double inv = D > 0 ? 1.0 / static_cast<double>(D) : 0.0;
            for (uint32_t a = 0; a < D; ++a) {
                m[static_cast<std::size_t>(a) * D + a] = Complex{ inv, 0 };
            }
            return m;
        }
    }  // namespace

    BpMessages converge_bp(
        const GraphTn& g, uint32_t max_iters, double tol, double damping,
        BpConvergence* out)
    {
        const GraphType& graph = g.graph;
        const uint32_t n = node_count(graph);
        const uint32_t ec = edge_count(graph);

        // ── gather each edge's endpoints once. The graph stores an edge id per
        // incidence (adj_edge); we record, per edge id, its LOW and HIGH endpoint
        // (canonicalize_bonds emits a < b, so low == the forward-message source).
        std::vector<NodeHandle> edge_low(ec, 0);
        std::vector<NodeHandle> edge_high(ec, 0);
        // Also cache, per node, its ordered incidences (neighbor + edge id) so the
        // per-edge fold can find the target leg and the other legs' messages.
        std::vector<std::vector<Incidence>> inc(n);
        for (NodeHandle u = 0; u < n; ++u) {
            inc[u] = incidences_of(graph, u);
            for (const Incidence& in : inc[u]) {
                if (u < in.neighbor) {
                    edge_low[in.edge] = u;
                    edge_high[in.edge] = in.neighbor;
                }
            }
        }

        // ── initialize every message to the normalized identity at the bond's
        // current dim (lambda.size()).
        BpMessages msgs;
        msgs.forward.resize(ec);
        msgs.backward.resize(ec);
        msgs.dim.assign(ec, 1);
        // Record each edge's bond dim from either endpoint (both see the same
        // shared bond). inc[u][k] is u's leg k, so its bond_dim[k] IS that edge's
        // current dim.
        for (NodeHandle u = 0; u < n; ++u) {
            const GraphSite& s = node_data(graph, u);
            for (std::size_t k = 0; k < inc[u].size(); ++k) {
                msgs.dim[inc[u][k].edge] = s.bond_dim[k];
            }
        }
        for (uint32_t e = 0; e < ec; ++e) {
            msgs.forward[e] = identity_over_d(msgs.dim[e]);
            msgs.backward[e] = identity_over_d(msgs.dim[e]);
        }

        // inc[u][k] carries (neighbor, edge) at leg k, so a node's leg position of
        // a given edge is found by a small scan of inc[u] in the hot loop below.

        BpConvergence conv;
        for (uint32_t iter = 0; iter < max_iters; ++iter) {
            // Jacobi: compute all new directed messages from the CURRENT msgs, then
            // swap in. A directed message lives in forward[e] (low -> high) or
            // backward[e] (high -> low).
            BpMessages next = msgs;  // copies dims + old values as the damping base
            double residual = 0.0;

            for (uint32_t e = 0; e < ec; ++e) {
                const NodeHandle a = edge_low[e];
                const NodeHandle b = edge_high[e];

                // Recompute BOTH directions of edge e.
                //   forward[e] = message a -> b : folded from node a, target leg =
                //     a's leg of e, absorbing every incoming message on a's OTHER
                //     legs.
                //   backward[e] = message b -> a : folded from node b likewise.
                for (int dir = 0; dir < 2; ++dir) {
                    const NodeHandle src = (dir == 0) ? a : b;
                    const GraphSite& s = node_data(graph, src);
                    const std::vector<Incidence>& si = inc[src];

                    // src's leg position of the target edge e.
                    std::size_t target_leg = 0;
                    for (std::size_t k = 0; k < si.size(); ++k) {
                        if (si[k].edge == e) {
                            target_leg = k;
                            break;
                        }
                    }

                    // The incoming message on each of src's legs (the message
                    // flowing INTO src along that edge), from the CURRENT sweep, and
                    // that leg's shared-bond lambda (for the bond weight).
                    std::vector<const std::vector<Complex>*> in_msgs(si.size(),
                        nullptr);
                    std::vector<const std::vector<double>*> leg_lambda(si.size(),
                        nullptr);
                    for (std::size_t k = 0; k < si.size(); ++k) {
                        in_msgs[k] = &message_into(msgs, edge_low, si[k].edge, src);
                        leg_lambda[k] = &graph.edge_store[si[k].edge].lambda;
                    }

                    std::vector<Complex> computed =
                        fold_environment(s, target_leg, in_msgs, leg_lambda);

                    // DAMPING per element BEFORE normalization: mix with the old
                    // message in the SAME direction.
                    const uint32_t D = msgs.dim[e];
                    std::vector<Complex>& old_dir =
                        (dir == 0) ? msgs.forward[e] : msgs.backward[e];
                    std::vector<Complex>& new_dir =
                        (dir == 0) ? next.forward[e] : next.backward[e];
                    for (std::size_t i = 0; i < computed.size(); ++i) {
                        computed[i] = (1.0 - damping) * old_dir[i]
                            + damping * computed[i];
                    }
                    trace_normalize(computed, D);

                    // Residual = max abs change vs the old (post-normalization)
                    // message in this direction.
                    for (std::size_t i = 0; i < computed.size(); ++i) {
                        const double d = std::abs(computed[i] - old_dir[i]);
                        if (d > residual) {
                            residual = d;
                        }
                    }
                    new_dir = std::move(computed);
                }
            }

            msgs = std::move(next);
            conv.iterations = iter + 1;
            conv.residual = residual;
            if (residual < tol) {
                conv.converged = true;
                break;
            }
        }

        if (out != nullptr) {
            *out = conv;
        }
        return msgs;
    }

    double bp_marginal_z(
        const GraphTn& g, const BpMessages& msgs, uint32_t agent)
    {
        const GraphType& graph = g.graph;
        if (agent >= node_count(graph)) {
            return 0.0;
        }
        const GraphSite& s = node_data(graph, agent);
        const std::vector<Incidence> si = incidences_of(graph, agent);
        const std::vector<uint32_t> dims = site_dims(s);

        // Rebuild edge_low locally so message_into can disambiguate direction (the
        // marginal reads the CONVERGED messages, so we do not thread the collect
        // struct through -- a small per-agent scan of this node's incidences).
        // A neighbor is the low endpoint iff its handle < agent.
        std::vector<Complex> W = s.t;  // ket working copy
        for (std::size_t k = 0; k < si.size(); ++k) {
            const uint32_t e = si[k].edge;
            const bool agent_is_low = (agent < si[k].neighbor);
            // message flowing INTO agent along e: forward if agent is HIGH, else
            // backward.
            const std::vector<Complex>& m =
                agent_is_low ? msgs.backward[e] : msgs.forward[e];
            // bond-weight by the shared lambda (supplies the diag(lambda^2) the
            // lambda-gauge marginal_z applies on every incident leg -- see
            // bond_weighted), then fold into the leg.
            absorb_leg(W, dims, 1 + k,
                bond_weighted(m, graph.edge_store[e].lambda));
        }

        // W now carries every incoming environment on its bond legs; contract
        // everything except the physical leg against conj(s.t), keeping s free ->
        // 2x2 diagonal rho. Exactly marginal_sigma_z.
        std::size_t tail = 1;  // product of bond dims
        for (uint32_t d : s.bond_dim) {
            tail *= d;
        }
        Complex rho[2] = { Complex{ 0, 0 }, Complex{ 0, 0 } };
        for (uint32_t phys = 0; phys < 2; ++phys) {
            const std::size_t base = static_cast<std::size_t>(phys) * tail;
            Complex acc{ 0, 0 };
            for (std::size_t j = 0; j < tail; ++j) {
                acc += W[base + j] * std::conj(s.t[base + j]);
            }
            rho[phys] = acc;
        }
        const double p0 = rho[0].real();
        const double p1 = rho[1].real();
        const double total = p0 + p1;
        return total > 0.0 ? (p0 - p1) / total : 0.0;
    }

    std::vector<double> bp_decisions(const GraphTn& g, const BpMessages& msgs)
    {
        const uint32_t n = node_count(g.graph);
        std::vector<double> z(n, 0.0);
        for (uint32_t i = 0; i < n; ++i) {
            z[i] = bp_marginal_z(g, msgs, i);
        }
        return z;
    }

    std::vector<double> bp_decisions_converged(
        const GraphTn& g, uint32_t max_iters, double tol, double damping)
    {
        const BpMessages msgs = converge_bp(g, max_iters, tol, damping, nullptr);
        return bp_decisions(g, msgs);
    }
}
