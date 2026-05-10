#pragma once

// JobGraphTemplate — persistent static topology + precomputed scheduling cache.
//
// Lifecycle:
//   1. Add nodes with add_job().
//   2. Add edges with add_dependency(prerequisite, dependent).
//   3. Call commit() once — builds the DAG, runs topo sort, caches scheduling
//      metadata (indegrees, initial ready nodes).
//   4. Use the committed view repeatedly; topology does not change.
//
// Topology changes require constructing a new JobGraphTemplate.
// The caches (indegree_, initial_ready_) are the only authoritative source for
// FrameExecution reset; they must never be edited outside commit().

#include <optional>
#include <span>
#include <vector>

#include <graph/static_dag.h>
#include "job_types.h"

namespace wz::jobs
{
    using JobGraph   = wz::core::graph::DAG<JobNode, JobEdge>;
    using JobBuilder = wz::core::graph::DAGBuilder<JobNode, JobEdge>;

    class JobGraphTemplate
    {
    public:
        JobGraphTemplate()  = default;
        ~JobGraphTemplate() = default;

        JobGraphTemplate(const JobGraphTemplate&)            = delete;
        JobGraphTemplate& operator=(const JobGraphTemplate&) = delete;
        JobGraphTemplate(JobGraphTemplate&&)                 = default;
        JobGraphTemplate& operator=(JobGraphTemplate&&)      = default;

        // ── Construction phase (before commit) ──────────────────────────────

        NodeHandle add_job(JobNode node);

        // add_dependency(A, B) means "B depends on A" / "A must complete before B".
        // Returns false if either handle is invalid or equal.
        bool add_dependency(NodeHandle prerequisite, NodeHandle dependent);

        // ── Commit ──────────────────────────────────────────────────────────

        // Finalises topology, runs topological sort, and caches scheduling
        // metadata. Returns false if a cycle is detected.
        // After a successful commit, add_job / add_dependency are no longer valid.
        bool commit();

        bool committed() const { return storage_.has_value(); }

        // ── Scheduling view (only valid after commit) ────────────────────────

        const JobGraph& graph()    const;
        uint32_t        node_count() const;

        // Topological order embedded in the DAG buffer — prerequisites first.
        std::span<const NodeHandle> topo_order()    const;

        // indegree()[n] == number of direct prerequisites of n.
        // Copied into FrameExecution::remaining_inputs on each reset.
        std::span<const uint32_t>   indegree()      const;

        // Nodes with zero prerequisites — the first wave submitted each run.
        std::span<const NodeHandle> initial_ready() const;

        // Direct dependents of n (nodes that become ready when n completes).
        // Backed by the DAG's CSR out-adjacency list — zero extra allocation.
        std::span<const NodeHandle> dependents(NodeHandle n) const;

    private:
        JobBuilder                                                       builder_;
        std::optional<wz::core::graph::DAGStorage<JobNode, JobEdge>>    storage_;
        std::vector<uint32_t>                                            indegree_;
        std::vector<NodeHandle>                                          initial_ready_;
    };

} // namespace wz::jobs
