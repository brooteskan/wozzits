#pragma once

// wozzits/rhi/frame_graph.h
//
// The per-frame render graph. This is a DIFFERENT DAG from window-engine's
// asset DAG and shares nothing with it:
//
//   - asset DAG: content-addressed, persistent across frames, invalidated by
//     content change. Answers "what is this and is it current?".
//   - frame graph: ephemeral, rebuilt every frame and discarded, execution-
//     ordered. Answers "in what order, with what barriers, sharing what
//     memory, do I run this frame's passes?".
//
// Conflating them would re-couple this repo to the engine and merge two
// structures with different lifetimes, identity models, and algorithms. The
// frame graph is built only from rhi types (GpuResource / GpuResourceHandle).
//
// It owns the work the old dx12_submit does by hand: ordering passes, culling
// dead ones, deriving barriers from declared resource usage, and planning
// transient-memory aliasing — all as pure logic the backend then executes.

#include <wozzits/rhi/gpu_resource.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace wz::rhi
{
    // The state a resource must be in for a given access. Closed set -> enum
    // (gets exhaustiveness checks; a new state should break switches at compile
    // time, not silently fall through).
    enum class ResourceState : uint8_t
    {
        Undefined,
        RenderTarget,
        DepthWrite,
        ShaderRead,
        UnorderedAccess,
        CopySrc,
        CopyDst,
        Present,
    };

    // Handle to a resource registered in a FrameGraph. Index-based and
    // ephemeral — valid only within the graph that produced it.
    struct FrameGraphResource
    {
        static constexpr uint32_t kInvalid = 0xFFFFFFFFu;
        uint32_t index = kInvalid;

        [[nodiscard]] bool valid() const noexcept { return index != kInvalid; }
        friend bool operator==(FrameGraphResource, FrameGraphResource) = default;
    };

    // A derived state transition the backend must issue before a pass runs.
    struct Barrier
    {
        FrameGraphResource resource{};
        ResourceState from = ResourceState::Undefined;
        ResourceState to   = ResourceState::Undefined;
    };

    // One surviving pass in execution order, with the barriers to issue before
    // it. (The execute callback would live here too; omitted from v0, which is
    // pure planning.)
    struct PassExecution
    {
        uint32_t             pass_index = 0;   // original add_pass() index
        std::vector<Barrier> barriers;
    };

    // A transient's computed lifetime and alias group. Transients sharing an
    // alias_group have non-overlapping lifetimes and may share backing memory.
    struct TransientAllocation
    {
        FrameGraphResource resource{};
        uint32_t alias_group = 0;
        uint32_t first_pass  = 0;   // position in execution order
        uint32_t last_pass   = 0;
    };

    struct CompiledFrameGraph
    {
        std::vector<PassExecution>       order;
        std::vector<TransientAllocation> transients;
        bool acyclic = true;   // false if a dependency cycle was detected

        [[nodiscard]] size_t pass_count() const noexcept { return order.size(); }
    };

    class FrameGraph
    {
    public:
        // Register an externally-owned, persistent resource together with the
        // state it is in on entry to the graph (e.g. a swapchain backbuffer in
        // Present).
        FrameGraphResource import(std::string name,
                                  GpuResourceHandle handle,
                                  ResourceState initial_state)
        {
            const uint32_t index = static_cast<uint32_t>(resources_.size());
            resources_.push_back(ResourceNode{
                std::move(name), ResourceKind::Imported, handle, {},
                initial_state, /*is_output*/ false });
            return FrameGraphResource{ index };
        }

        // Register a resource created and destroyed within this frame.
        FrameGraphResource create_transient(std::string name, GpuResourceDesc desc)
        {
            const uint32_t index = static_cast<uint32_t>(resources_.size());
            resources_.push_back(ResourceNode{
                std::move(name), ResourceKind::Transient, {}, std::move(desc),
                ResourceState::Undefined, /*is_output*/ false });
            return FrameGraphResource{ index };
        }

        // Mark a resource as an external output that must be produced. Passes
        // that do not contribute to any output are culled.
        void mark_output(FrameGraphResource resource)
        {
            if (resource.index < resources_.size()) {
                resources_[resource.index].is_output = true;
            }
        }

        uint32_t add_pass(std::string name)
        {
            const uint32_t index = static_cast<uint32_t>(passes_.size());
            passes_.push_back(PassNode{ std::move(name), {} });
            return index;
        }

        void read(uint32_t pass, FrameGraphResource resource, ResourceState state)
        {
            passes_[pass].accesses.push_back(Access{ resource, state, /*write*/ false });
        }

        void write(uint32_t pass, FrameGraphResource resource, ResourceState state)
        {
            passes_[pass].accesses.push_back(Access{ resource, state, /*write*/ true });
        }

        // Plan the frame: cull dead passes, order survivors, derive barriers,
        // and compute transient lifetimes + alias groups.
        [[nodiscard]] CompiledFrameGraph compile() const
        {
            const size_t pass_count = passes_.size();
            const size_t res_count = resources_.size();

            // ── readers / writers per resource ────────────────────────────────
            std::vector<std::vector<uint32_t>> readers(res_count);
            std::vector<std::vector<uint32_t>> writers(res_count);
            for (uint32_t p = 0; p < pass_count; ++p) {
                for (const Access& a : passes_[p].accesses) {
                    auto& bucket = a.is_write ? writers[a.resource.index]
                                              : readers[a.resource.index];
                    if (bucket.empty() || bucket.back() != p) {
                        bucket.push_back(p);
                    }
                }
            }

            // ── dead-pass culling (ref-count, RDG-style) ──────────────────────
            std::vector<int> resource_ref(res_count, 0);
            for (uint32_t r = 0; r < res_count; ++r) {
                resource_ref[r] = static_cast<int>(readers[r].size())
                    + (resources_[r].is_output ? 1 : 0);
            }
            std::vector<int> pass_ref(pass_count, 0);
            for (uint32_t p = 0; p < pass_count; ++p) {
                pass_ref[p] = static_cast<int>(distinct_writes(p));
            }
            std::vector<bool> culled(pass_count, false);

            std::vector<uint32_t> ready_resources;
            for (uint32_t r = 0; r < res_count; ++r) {
                if (resource_ref[r] == 0) {
                    ready_resources.push_back(r);
                }
            }
            while (!ready_resources.empty()) {
                const uint32_t r = ready_resources.back();
                ready_resources.pop_back();
                for (const uint32_t producer : writers[r]) {
                    if (culled[producer]) {
                        continue;
                    }
                    if (--pass_ref[producer] == 0) {
                        culled[producer] = true;
                        for (const Access& a : passes_[producer].accesses) {
                            if (!a.is_write) {
                                if (--resource_ref[a.resource.index] == 0) {
                                    ready_resources.push_back(a.resource.index);
                                }
                            }
                        }
                    }
                }
            }

            // ── topological order of survivors (writer -> reader edges) ───────
            std::vector<int> indegree(pass_count, 0);
            std::vector<std::vector<uint32_t>> edges(pass_count);
            for (uint32_t r = 0; r < res_count; ++r) {
                for (const uint32_t w : writers[r]) {
                    if (culled[w]) continue;
                    for (const uint32_t rd : readers[r]) {
                        if (culled[rd] || rd == w) continue;
                        edges[w].push_back(rd);
                        ++indegree[rd];
                    }
                }
            }

            CompiledFrameGraph compiled;
            std::vector<uint32_t> ready_passes;
            for (uint32_t p = 0; p < pass_count; ++p) {
                if (!culled[p] && indegree[p] == 0) {
                    ready_passes.push_back(p);
                }
            }
            // Deterministic: always take the lowest-index ready pass next.
            std::vector<uint32_t> exec_order;
            while (!ready_passes.empty()) {
                auto it = std::min_element(ready_passes.begin(), ready_passes.end());
                const uint32_t p = *it;
                ready_passes.erase(it);
                exec_order.push_back(p);
                for (const uint32_t next : edges[p]) {
                    if (--indegree[next] == 0) {
                        ready_passes.push_back(next);
                    }
                }
            }

            size_t surviving = 0;
            for (uint32_t p = 0; p < pass_count; ++p) {
                if (!culled[p]) ++surviving;
            }
            compiled.acyclic = (exec_order.size() == surviving);

            // ── barrier derivation from declared usage ────────────────────────
            std::vector<ResourceState> current(res_count, ResourceState::Undefined);
            for (uint32_t r = 0; r < res_count; ++r) {
                if (resources_[r].kind == ResourceKind::Imported) {
                    current[r] = resources_[r].initial_state;
                }
            }
            std::vector<uint32_t> position(pass_count, 0);
            for (uint32_t pos = 0; pos < exec_order.size(); ++pos) {
                const uint32_t p = exec_order[pos];
                position[p] = pos;
                PassExecution exec;
                exec.pass_index = p;
                for (const Access& a : passes_[p].accesses) {
                    const uint32_t r = a.resource.index;
                    if (current[r] != a.state) {
                        exec.barriers.push_back(Barrier{ a.resource, current[r], a.state });
                        current[r] = a.state;
                    }
                }
                compiled.order.push_back(std::move(exec));
            }

            // ── transient lifetimes + greedy alias grouping ───────────────────
            std::vector<TransientAllocation> transients;
            for (uint32_t r = 0; r < res_count; ++r) {
                if (resources_[r].kind != ResourceKind::Transient) continue;
                bool used = false;
                uint32_t first = 0, last = 0;
                auto consider = [&](uint32_t p) {
                    if (culled[p]) return;
                    const uint32_t pos = position[p];
                    if (!used) { first = last = pos; used = true; }
                    else { first = std::min(first, pos); last = std::max(last, pos); }
                };
                for (const uint32_t w : writers[r]) consider(w);
                for (const uint32_t rd : readers[r]) consider(rd);
                if (used) {
                    transients.push_back(TransientAllocation{
                        FrameGraphResource{ r }, /*alias_group*/ 0, first, last });
                }
            }
            std::sort(transients.begin(), transients.end(),
                [](const TransientAllocation& a, const TransientAllocation& b) {
                    return a.first_pass < b.first_pass;
                });
            std::vector<uint32_t> group_free_at;   // last_pass of each group's occupant
            for (TransientAllocation& t : transients) {
                uint32_t assigned = static_cast<uint32_t>(group_free_at.size());
                for (uint32_t g = 0; g < group_free_at.size(); ++g) {
                    if (group_free_at[g] < t.first_pass) {   // lifetimes disjoint
                        assigned = g;
                        break;
                    }
                }
                if (assigned == group_free_at.size()) {
                    group_free_at.push_back(t.last_pass);
                }
                else {
                    group_free_at[assigned] = t.last_pass;
                }
                t.alias_group = assigned;
            }
            compiled.transients = std::move(transients);

            return compiled;
        }

    private:
        enum class ResourceKind : uint8_t { Imported, Transient };

        struct ResourceNode
        {
            std::string       name;
            ResourceKind      kind = ResourceKind::Transient;
            GpuResourceHandle imported{};
            GpuResourceDesc   desc{};
            ResourceState     initial_state = ResourceState::Undefined;
            bool              is_output = false;
        };

        struct Access
        {
            FrameGraphResource resource{};
            ResourceState      state = ResourceState::Undefined;
            bool               is_write = false;
        };

        struct PassNode
        {
            std::string         name;
            std::vector<Access> accesses;
        };

        [[nodiscard]] size_t distinct_writes(uint32_t pass) const
        {
            std::vector<uint32_t> seen;
            for (const Access& a : passes_[pass].accesses) {
                if (a.is_write
                    && std::find(seen.begin(), seen.end(), a.resource.index)
                        == seen.end()) {
                    seen.push_back(a.resource.index);
                }
            }
            return seen.size();
        }

        std::vector<ResourceNode> resources_;
        std::vector<PassNode>     passes_;
    };
}
