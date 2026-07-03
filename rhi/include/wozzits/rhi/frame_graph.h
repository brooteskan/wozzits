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

#include <wozzits/rhi/draw_item.h>
#include <wozzits/rhi/geometry_view.h>
#include <wozzits/rhi/gpu_resource.h>
#include <wozzits/rhi/gpu_resource_registry.h>
#include <wozzits/rhi/shader_resource_group.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace wz::rhi
{
    struct DispatchArgs
    {
        uint32_t group_count[3] = { 1, 1, 1 };
    };

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

        // True when this barrier is a transient's FIRST access and its alias
        // group already had a previous occupant — i.e. the shared backing is
        // switching occupants here. The default recorder path treats it like
        // any transition; a placed-heap backend consumes it to issue the
        // aliasing/activation barrier the shared memory needs. Trailing with a
        // default so existing `Barrier{r, from, to}` aggregate inits still hold.
        bool alias_activation = false;
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

        // The FrameGraph::revision_ this plan was compiled from. execute()
        // refuses a plan whose source revision no longer matches the graph, so a
        // stale plan can never record against a mutated graph.
        uint64_t source_revision = 0;

        [[nodiscard]] size_t pass_count() const noexcept { return order.size(); }
    };

    // What the frame graph and its passes record GPU work into during
    // execution. Backend-agnostic: a real backend implements this over a
    // command list; a test implements a recording fake. The frame graph itself
    // only issues barriers; passes issue their own work via PassContext.
    class CommandRecorder
    {
    public:
        virtual ~CommandRecorder() = default;
        virtual void barrier(GpuResourceHandle resource,
                            ResourceState from,
                            ResourceState to) = 0;
        virtual void set_pipeline(Tag) {}
        virtual void set_root_constants(std::span<const uint8_t>) {}
        virtual void bind_resource_group(uint32_t,
                                         const ShaderResourceGroup&) {}
        virtual void set_geometry(const GeometryView&,
                                  const StreamBufferIndices&) {}
        virtual void draw(const DrawArgs&) {}
        virtual void dispatch(const DispatchArgs&) {}
    };

    // Handed to each pass's execute callback. Resolves graph resources to their
    // realized GPU handles and exposes the recorder.
    struct PassContext
    {
        const std::vector<GpuResourceHandle>* realized = nullptr;
        CommandRecorder*                      recorder = nullptr;

        [[nodiscard]] GpuResourceHandle resolve(FrameGraphResource r) const
        {
            return (r.valid() && realized && r.index < realized->size())
                ? (*realized)[r.index]
                : GpuResourceHandle{};
        }

        [[nodiscard]] CommandRecorder& commands() const { return *recorder; }
    };

    using PassExecuteFn = std::function<void(const PassContext&)>;

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
            ++revision_;
            return FrameGraphResource{ index };
        }

        // Register a resource created and destroyed within this frame.
        FrameGraphResource create_transient(std::string name, GpuResourceDesc desc)
        {
            // A transient is anonymous by definition: it exists only for this
            // frame and is never content-addressed. Scrub any identity the
            // caller left on the desc — a non-anonymous identity would dedup in
            // GpuResourceRegistry::acquire and silently hand two overlapping-
            // lifetime alias groups the SAME backing. Force residency Transient
            // so the desc states what it is. (Anonymity also lets desc equality
            // drive alias grouping without identity spoiling the comparison.)
            desc.identity = {};
            desc.residency = ResourceResidency::Transient;
            const uint32_t index = static_cast<uint32_t>(resources_.size());
            resources_.push_back(ResourceNode{
                std::move(name), ResourceKind::Transient, {}, std::move(desc),
                ResourceState::Undefined, /*is_output*/ false });
            ++revision_;
            return FrameGraphResource{ index };
        }

        // Mark a resource as an external output that must be produced. Passes
        // that do not contribute to any output are culled.
        void mark_output(FrameGraphResource resource)
        {
            if (resource.index < resources_.size()) {
                resources_[resource.index].is_output = true;
                ++revision_;
            }
        }

        uint32_t add_pass(std::string name)
        {
            const uint32_t index = static_cast<uint32_t>(passes_.size());
            passes_.push_back(PassNode{ std::move(name), {}, {} });
            ++revision_;
            return index;
        }

        // Declare a read of `resource` by `pass` in `state`. Returns true when
        // the access was recorded; false (and records nothing) when `pass` or
        // `resource` is out of range — a checkable rejection, never a silent
        // out-of-bounds. (Not [[nodiscard]]: engine call sites ignore the
        // return; tightening those is a separate pass.)
        bool read(uint32_t pass, FrameGraphResource resource, ResourceState state)
        {
            if (pass >= passes_.size()
                || !resource.valid() || resource.index >= resources_.size()) {
                return false;
            }
            passes_[pass].accesses.push_back(Access{ resource, state, /*write*/ false });
            ++revision_;
            return true;
        }

        // Declare a write of `resource` by `pass` in `state`. Same checkable
        // rejection contract as read().
        bool write(uint32_t pass, FrameGraphResource resource, ResourceState state)
        {
            if (pass >= passes_.size()
                || !resource.valid() || resource.index >= resources_.size()) {
                return false;
            }
            passes_[pass].accesses.push_back(Access{ resource, state, /*write*/ true });
            ++revision_;
            return true;
        }

        // Attach the GPU work a pass records when executed. Optional — a pass
        // with no callback contributes only its resource declarations (useful
        // for ordering/barrier purposes, e.g. a present pass). Same checkable
        // rejection contract as read()/write(): false for an out-of-range
        // pass, and nothing is recorded.
        bool set_execute(uint32_t pass, PassExecuteFn fn)
        {
            if (pass >= passes_.size()) {
                return false;
            }
            passes_[pass].execute = std::move(fn);
            ++revision_;
            return true;
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
                        // resource_ref counts each reader pass ONCE per distinct
                        // resource it reads (the bucket dedup above), so release
                        // it the same way: decrement once per DISTINCT read
                        // resource of the culled pass. Decrementing per raw read
                        // access would double-count a pass that reads one
                        // resource twice and could transitively cull a producer
                        // whose output another pass still consumes.
                        std::vector<uint32_t> seen;
                        for (const Access& a : passes_[producer].accesses) {
                            if (a.is_write) {
                                continue;
                            }
                            if (std::find(seen.begin(), seen.end(), a.resource.index)
                                != seen.end()) {
                                continue;
                            }
                            seen.push_back(a.resource.index);
                            if (--resource_ref[a.resource.index] == 0) {
                                ready_resources.push_back(a.resource.index);
                            }
                        }
                    }
                }
            }

            // ── topological order of survivors (resource-versioning edges) ────
            // Declaration (pass-index) order defines each resource's version
            // timeline. Per resource we walk its accessing passes in that order,
            // emitting only the hazards that actually serialize:
            //   RAW  last_writer -> reader
            //   WAW  last_writer -> new writer
            //   WAR  each pending reader -> new writer
            // A pass that both reads and writes a resource is reader-then-writer
            // for it. This does NOT force all writers before all readers (the
            // old writers×readers product did, and it had no WAR/WAW edges at
            // all): passes touching disjoint resources stay unordered, and their
            // relative order is decided independently by the min-index tie-break.
            std::vector<int> indegree(pass_count, 0);
            std::vector<std::vector<uint32_t>> edges(pass_count);
            for (uint32_t r = 0; r < res_count; ++r) {
                // Passes accessing r, in declaration order (readers ∪ writers,
                // deduped). Walk this timeline and version the resource.
                std::vector<uint32_t> timeline;
                for (uint32_t p = 0; p < pass_count; ++p) {
                    if (culled[p]) {
                        continue;
                    }
                    bool reads = false, writes = false;
                    for (const Access& a : passes_[p].accesses) {
                        if (a.resource.index != r) {
                            continue;
                        }
                        if (a.is_write) writes = true; else reads = true;
                    }
                    if (reads || writes) {
                        timeline.push_back(p);
                    }
                }

                bool has_last_writer = false;
                uint32_t last_writer = 0;
                std::vector<uint32_t> readers_since_write;
                // Add an edge from -> to unless it self-loops or already exists
                // in THIS resource walk (a per-pair seen-check bounded to the
                // walk keeps indegree consistent without a global dedup).
                std::vector<std::pair<uint32_t, uint32_t>> added;
                auto add_edge = [&](uint32_t from, uint32_t to) {
                    if (from == to) {
                        return;
                    }
                    for (const auto& e : added) {
                        if (e.first == from && e.second == to) {
                            return;
                        }
                    }
                    added.push_back({ from, to });
                    edges[from].push_back(to);
                    ++indegree[to];
                };
                for (const uint32_t p : timeline) {
                    bool reads = false, writes = false;
                    for (const Access& a : passes_[p].accesses) {
                        if (a.resource.index != r) {
                            continue;
                        }
                        if (a.is_write) writes = true; else reads = true;
                    }
                    // Reader side first (a read-write pass consumes the current
                    // version before it produces the next).
                    if (reads) {
                        if (has_last_writer) {
                            add_edge(last_writer, p);   // RAW
                        }
                        readers_since_write.push_back(p);
                    }
                    if (writes) {
                        if (has_last_writer) {
                            add_edge(last_writer, p);   // WAW
                        }
                        for (const uint32_t rd : readers_since_write) {
                            add_edge(rd, p);            // WAR
                        }
                        has_last_writer = true;
                        last_writer = p;
                        readers_since_write.clear();
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

            std::vector<uint32_t> position(pass_count, 0);
            for (uint32_t pos = 0; pos < exec_order.size(); ++pos) {
                position[exec_order[pos]] = pos;
            }

            // ── transient lifetimes + greedy alias grouping ───────────────────
            // Grouping needs only position[], so it runs BEFORE barrier
            // derivation: a barrier's from-state must be read from the physical
            // backing (shared across an alias group), and that requires knowing
            // each transient's group first.
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
            // Two transients may share a group only if their lifetimes are
            // disjoint AND their descs are EQUAL. Equal descs make the single
            // backing correctly sized/typed for every occupant (conservative
            // but correct v1); a future backend can relax this to "fits within".
            // Transient descs are anonymous (scrubbed in create_transient), so
            // identity never spoils the desc comparison.
            std::vector<uint32_t> group_free_at;   // last_pass of each group's occupant
            std::vector<uint32_t> group_desc_res;  // resource index defining each group's desc
            std::vector<uint32_t> group_occupants; // how many transients landed in each group
            std::vector<uint32_t> alias_group_of(res_count, 0);
            std::vector<bool>     transient_reuses_group(res_count, false);
            for (TransientAllocation& t : transients) {
                const GpuResourceDesc& cand_desc = resources_[t.resource.index].desc;
                uint32_t assigned = static_cast<uint32_t>(group_free_at.size());
                for (uint32_t g = 0; g < group_free_at.size(); ++g) {
                    if (group_free_at[g] < t.first_pass          // lifetimes disjoint
                        && resources_[group_desc_res[g]].desc == cand_desc) {
                        assigned = g;
                        break;
                    }
                }
                if (assigned == group_free_at.size()) {
                    group_free_at.push_back(t.last_pass);
                    group_desc_res.push_back(t.resource.index);
                    group_occupants.push_back(1);
                }
                else {
                    group_free_at[assigned] = t.last_pass;
                    // A later occupant of an existing group: its first barrier
                    // switches the backing to a new occupant (alias activation).
                    transient_reuses_group[t.resource.index] =
                        (group_occupants[assigned] > 0);
                    ++group_occupants[assigned];
                }
                t.alias_group = assigned;
                alias_group_of[t.resource.index] = assigned;
            }

            // ── barrier derivation from declared usage ────────────────────────
            // State is tracked per PHYSICAL backing: an imported resource keeps
            // its own current[r] (seeded from initial_state); a transient reads
            // and updates its ALIAS GROUP's state, because group members share
            // one backing. Tracking a transient's from-state per logical
            // resource would claim Undefined for a group's second occupant while
            // the backing actually holds the first occupant's final state — a
            // debug-layer error / driver UB.
            std::vector<ResourceState> current(res_count, ResourceState::Undefined);
            std::vector<ResourceState> group_state(
                group_free_at.size(), ResourceState::Undefined);
            for (uint32_t r = 0; r < res_count; ++r) {
                if (resources_[r].kind == ResourceKind::Imported) {
                    current[r] = resources_[r].initial_state;
                }
            }
            auto state_of = [&](uint32_t r) -> ResourceState& {
                return resources_[r].kind == ResourceKind::Transient
                    ? group_state[alias_group_of[r]]
                    : current[r];
            };
            // UAV hazards are evaluated at PASS granularity: a read-modify-write
            // pass (write UA then read UA) must leave the flag set for the NEXT
            // pass, and must not emit a spurious same-pass UA->UA on its own
            // first use. We snapshot the pending flags BEFORE any of this pass's
            // updates, decide barriers against the snapshot, then update.
            std::vector<bool> uav_write_pending(res_count, false);
            std::vector<bool> first_access_seen(res_count, false);
            for (uint32_t pos = 0; pos < exec_order.size(); ++pos) {
                const uint32_t p = exec_order[pos];
                PassExecution exec;
                exec.pass_index = p;

                // (1) State transitions, unchanged in behavior. A full
                //     transition synchronizes, so it clears any pending UAV
                //     hazard for that resource. Record which resources this pass
                //     transitioned so the UAV pass below skips them.
                std::vector<uint32_t> transitioned;
                for (const Access& a : passes_[p].accesses) {
                    const uint32_t r = a.resource.index;
                    ResourceState& st = state_of(r);
                    if (st != a.state) {
                        Barrier bar{ a.resource, st, a.state };
                        // First access of a transient whose group had a prior
                        // occupant: the backing is switching hands.
                        if (!first_access_seen[r]
                            && resources_[r].kind == ResourceKind::Transient
                            && transient_reuses_group[r]) {
                            bar.alias_activation = true;
                        }
                        exec.barriers.push_back(bar);
                        st = a.state;
                        uav_write_pending[r] = false;
                        transitioned.push_back(r);
                    }
                    first_access_seen[r] = true;
                }

                // (2) UAV read-after-write ordering WITHIN the same state, at
                //     pass granularity: for each DISTINCT resource this pass
                //     touches in UnorderedAccess with no transition emitted this
                //     pass and a hazard pending from a PRIOR pass, emit exactly
                //     one UA->UA barrier.
                std::vector<uint32_t> uav_done;
                for (const Access& a : passes_[p].accesses) {
                    const uint32_t r = a.resource.index;
                    if (a.state != ResourceState::UnorderedAccess) {
                        continue;
                    }
                    if (std::find(transitioned.begin(), transitioned.end(), r)
                        != transitioned.end()) {
                        continue;   // a transition already synchronized it
                    }
                    if (!uav_write_pending[r]) {
                        continue;
                    }
                    if (std::find(uav_done.begin(), uav_done.end(), r)
                        != uav_done.end()) {
                        continue;   // one barrier per distinct resource
                    }
                    uav_done.push_back(r);
                    exec.barriers.push_back(
                        Barrier{ a.resource, a.state, a.state });
                }

                // (3) Update pending flags from THIS pass's UA accesses. A write
                //     arms the hazard for the next pass; a read-only touch of a
                //     UA resource consumes the barrier without re-arming.
                std::vector<uint32_t> uav_touched;
                for (const Access& a : passes_[p].accesses) {
                    const uint32_t r = a.resource.index;
                    if (a.state != ResourceState::UnorderedAccess) {
                        continue;
                    }
                    if (std::find(uav_touched.begin(), uav_touched.end(), r)
                        != uav_touched.end()) {
                        uav_write_pending[r] = uav_write_pending[r] || a.is_write;
                    }
                    else {
                        uav_touched.push_back(r);
                        uav_write_pending[r] = a.is_write;
                    }
                }

                compiled.order.push_back(std::move(exec));
            }

            compiled.transients = std::move(transients);
            compiled.source_revision = revision_;

            return compiled;
        }

        // Execute a compiled plan: realize transients against the registry
        // (one backing per alias group, shared by its transients), issue the
        // derived barriers, run pass callbacks in execution order, then release
        // the transient backings. Backend work goes through the recorder; the
        // registry owns the resources, so device-loss / deferred-release policy
        // all apply uniformly.
        //
        // `frame_timeline` is the GPU timeline value the command work recorded
        // by this call will be signaled with (the value the caller's frame
        // submission passes to its fence). Every transient backing is touched
        // with it before release, so the registry keeps it resident until the
        // GPU has passed that value. It is REQUIRED — there is no default: a
        // wrong-by-omission zero would tag a still-in-flight transient with
        // last-use 0, making it collectable while the frame that uses it is
        // still executing. Callers that drive frames through the engine device
        // loop pass wz::gpu::frame_timeline_value(device); a fake/offline loop
        // passes its own monotonic frame counter.
        //
        // Returns false and records NOTHING (no barriers, no callbacks, no
        // dangling acquires) when the plan can't be safely run: it has a
        // dependency cycle; it was compiled from a different revision of this
        // graph (mutated since); it references a pass/resource index this graph
        // no longer has; or a transient backing fails to acquire. An acquire
        // failure releases any backings already taken before returning — the
        // frame is a clean no-op, never a half-recorded one.
        bool execute(const CompiledFrameGraph& plan,
                     GpuResourceRegistry& registry,
                     CommandRecorder& recorder,
                     uint64_t frame_timeline) const
        {
            // ── refuse an unsafe plan up front (before any side effect) ───────
            if (!plan.acyclic || plan.source_revision != revision_) {
                return false;
            }
            for (const PassExecution& pe : plan.order) {
                if (pe.pass_index >= passes_.size()) {
                    return false;
                }
                for (const Barrier& b : pe.barriers) {
                    if (!b.resource.valid()
                        || b.resource.index >= resources_.size()) {
                        return false;
                    }
                }
            }
            for (const TransientAllocation& t : plan.transients) {
                if (!t.resource.valid()
                    || t.resource.index >= resources_.size()) {
                    return false;
                }
            }

            std::vector<GpuResourceHandle> realized(resources_.size());
            for (uint32_t i = 0; i < resources_.size(); ++i) {
                if (resources_[i].kind == ResourceKind::Imported) {
                    realized[i] = resources_[i].imported;
                }
            }

            // One backing resource per alias group. A group's members all share
            // this backing; grouping requires equal descs (see compile()), so it
            // is correctly sized/typed for every member and there is no
            // first-vs-largest ambiguity. Acquire every group BEFORE issuing any
            // barrier or callback: if one fails, release what we took and bail
            // so the frame records nothing.
            std::vector<GpuResourceHandle> group_handle;
            std::vector<bool>              group_realized;
            for (const TransientAllocation& t : plan.transients) {
                if (t.alias_group >= group_handle.size()) {
                    group_handle.resize(t.alias_group + 1);
                    group_realized.resize(t.alias_group + 1, false);
                }
                if (!group_realized[t.alias_group]) {
                    const GpuResourceHandle h =
                        registry.acquire(resources_[t.resource.index].desc);
                    if (!h.valid()) {
                        for (uint32_t g = 0; g < group_handle.size(); ++g) {
                            if (group_realized[g]) {
                                registry.release(group_handle[g]);
                            }
                        }
                        return false;
                    }
                    group_handle[t.alias_group] = h;
                    group_realized[t.alias_group] = true;
                }
                realized[t.resource.index] = group_handle[t.alias_group];
            }

            for (const PassExecution& pe : plan.order) {
                for (const Barrier& b : pe.barriers) {
                    recorder.barrier(realized[b.resource.index], b.from, b.to);
                }
                const PassNode& pass = passes_[pe.pass_index];
                if (pass.execute) {
                    const PassContext ctx{ &realized, &recorder };
                    pass.execute(ctx);
                }
            }

            // Transients are frame-scoped: record this frame's timeline on each
            // backing so the registry won't reclaim it until the GPU has passed
            // that value, then hand it back for deferred release.
            for (uint32_t g = 0; g < group_handle.size(); ++g) {
                if (group_realized[g]) {
                    registry.touch(group_handle[g], frame_timeline);
                    registry.release(group_handle[g]);
                }
            }
            return true;
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
            PassExecuteFn       execute;
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
        // Bumped by every mutator. compile() stamps it onto the plan; execute()
        // rejects a plan whose stamp no longer matches — a stale plan can never
        // record against a graph that has since changed shape.
        uint64_t                  revision_ = 0;
    };
}
