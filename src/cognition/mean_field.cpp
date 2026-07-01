#include <cognition/mean_field.h>

#include <vector>

namespace wz::engine::cognition
{
    using wz::core::graph::for_each_neighbor;
    using wz::core::graph::INVALID_NODE;
    using wz::core::graph::node_count;
    using wz::core::graph::node_data;
    using wz::core::graph::NodeHandle;
    using wz::core::graph::parent;

    void refresh_messages(MeanFieldNetwork& net)
    {
        const uint32_t n = node_count(net);
        for (NodeHandle c = 0; c < n; ++c) {
            const NodeHandle p = parent(net, c);
            if (p == INVALID_NODE) {
                continue;  // root has no parent-edge
            }
            MeanFieldBond& b = wz::core::graph::edge_to_parent(net, c);
            b.up = wz::engine::cognition::qstate::expectation_z(node_data(net, c), b.child_qubit);
            b.down = wz::engine::cognition::qstate::expectation_z(node_data(net, p), b.parent_qubit);
        }
    }

    void relax_step(MeanFieldNetwork& net, double gamma, double dtau)
    {
        const uint32_t n = node_count(net);

        // Effective longitudinal field on each node from the CURRENT messages
        // (Jacobi: this step reads the previous step's messages for every node).
        std::vector<double> h_eff(n, 0.0);
        for (NodeHandle u = 0; u < n; ++u) {
            const NodeHandle p = parent(net, u);
            for_each_neighbor(net, u, [&](NodeHandle nb, MeanFieldBond& b) {
                const double neighbor_z = (nb == p) ? b.down : b.up;
                h_eff[u] += b.j * neighbor_z;
            });
        }

        // Relax each node toward its ground state under (gamma, h_eff). Single-
        // qubit nodes: the coupled qubit is qubit 0.
        for (NodeHandle u = 0; u < n; ++u) {
            wz::engine::cognition::qstate::apply_imag_time_field(
                node_data(net, u), 0u, gamma, h_eff[u], dtau);
        }

        refresh_messages(net);
    }

    void relax(
        MeanFieldNetwork& net, double gamma, double dtau, uint32_t iterations)
    {
        refresh_messages(net);
        for (uint32_t i = 0; i < iterations; ++i) {
            relax_step(net, gamma, dtau);
        }
    }

    double decision_z(const MeanFieldNetwork& net, NodeHandle node)
    {
        return wz::engine::cognition::qstate::expectation_z(node_data(net, node), 0u);
    }
}
