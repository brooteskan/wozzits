#include "jobs/job_graph_template.h"

#include <cassert>

namespace wz::jobs
{
    NodeHandle JobGraphTemplate::add_job(JobNode node)
    {
        assert(!committed() && "cannot add jobs after commit");
        return wz::core::graph::add_node(builder_, std::move(node));
    }

    bool JobGraphTemplate::add_dependency(NodeHandle prerequisite, NodeHandle dependent)
    {
        assert(!committed() && "cannot add dependencies after commit");
        return wz::core::graph::add_edge(builder_, prerequisite, dependent);
    }

    bool JobGraphTemplate::commit()
    {
        auto result = wz::core::graph::build(std::move(builder_));
        if (!result.has_value())
            return false;

        storage_ = std::move(*result);
        const JobGraph& g = storage_->dag;
        const uint32_t  nc = wz::core::graph::node_count(g);

        // Cache indegrees — used to initialise FrameExecution::remaining_inputs
        indegree_.resize(nc);
        for (uint32_t i = 0; i < nc; ++i)
            indegree_[i] = static_cast<uint32_t>(wz::core::graph::parents(g, i).size());

        // Cache initial ready nodes — roots of the graph (no prerequisites)
        initial_ready_.clear();
        for (uint32_t i = 0; i < nc; ++i)
            if (wz::core::graph::is_root(g, i))
                initial_ready_.push_back(static_cast<NodeHandle>(i));

        return true;
    }

    const JobGraph& JobGraphTemplate::graph() const
    {
        assert(committed());
        return storage_->dag;
    }

    uint32_t JobGraphTemplate::node_count() const
    {
        if (!committed())
            return static_cast<uint32_t>(builder_.nodes.size());
        return wz::core::graph::node_count(storage_->dag);
    }

    std::span<const NodeHandle> JobGraphTemplate::topo_order() const
    {
        assert(committed());
        return wz::core::graph::topo_order(storage_->dag);
    }

    std::span<const uint32_t> JobGraphTemplate::indegree() const
    {
        assert(committed());
        return indegree_;
    }

    std::span<const NodeHandle> JobGraphTemplate::initial_ready() const
    {
        assert(committed());
        return initial_ready_;
    }

    std::span<const NodeHandle> JobGraphTemplate::dependents(NodeHandle n) const
    {
        assert(committed());
        return wz::core::graph::children(storage_->dag, n);
    }

} // namespace wz::jobs
