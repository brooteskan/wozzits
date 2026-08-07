#include "wz_test.h"

#include <wozzits/rhi/frame_graph.h>

#include <string>
#include <vector>

using namespace wz::rhi;

namespace
{
    struct FakeBackend final : GpuBackend
    {
        uint64_t next_id = 1;
        int creates = 0;
        int destroys = 0;
        // When non-zero, create() returns the invalid resource for descs whose
        // size_bytes matches — a way to force a targeted acquire failure.
        uint64_t fail_size = 0;
        BackendResource create(const GpuResourceDesc& desc) override
        {
            if (fail_size != 0 && desc.size_bytes == fail_size) {
                return BackendResource{};   // 0 == invalid -> acquire fails
            }
            ++creates;
            return BackendResource{ next_id++ };
        }
        void destroy(BackendResource) override { ++destroys; }
        bool write(BackendResource, const void*, uint64_t, uint64_t) override
        {
            return true;
        }
    };

    struct RecordedBarrier
    {
        GpuResourceHandle resource;
        ResourceState from;
        ResourceState to;
    };

    // Stateful recorder: it tracks the state it believes each backing handle is
    // in and asserts every barrier's `from` matches that tracked state before
    // advancing it to `to`. This is the executable form of the DX12 debug
    // layer's before-state check — any from-state drift (e.g. a transient's
    // second occupant claiming Undefined while the backing holds the first
    // occupant's final state) fails loudly here. Imported handles must be seeded
    // with their declared initial state via seed(); every other handle (the
    // transient backings acquired at execute time) starts Undefined.
    struct RecordingRecorder final : CommandRecorder
    {
        std::vector<RecordedBarrier>                                 barriers;
        std::vector<std::pair<GpuResourceHandle, ResourceState>>     tracked;

        void seed(GpuResourceHandle h, ResourceState s)
        {
            set_state(h, s);
        }

        void barrier(GpuResourceHandle r, ResourceState from, ResourceState to) override
        {
            // The recorder's belief about the backing must match the plan's
            // declared from-state. A mismatch is the exact class of bug this
            // fake exists to catch.
            WZ_CHECK_EQ(state_of(r), from);
            barriers.push_back(RecordedBarrier{ r, from, to });
            set_state(r, to);
        }

    private:
        [[nodiscard]] ResourceState state_of(GpuResourceHandle h) const
        {
            for (const auto& e : tracked) {
                if (e.first == h) {
                    return e.second;
                }
            }
            return ResourceState::Undefined;   // unseen backing starts Undefined
        }

        void set_state(GpuResourceHandle h, ResourceState s)
        {
            for (auto& e : tracked) {
                if (e.first == h) {
                    e.second = s;
                    return;
                }
            }
            tracked.push_back({ h, s });
        }
    };

    GpuResourceDesc transient_target()
    {
        GpuResourceDesc desc;
        desc.size_bytes = 4096;
        desc.usage = ResourceUsage_RenderTarget | ResourceUsage_Sampled;
        desc.residency = ResourceResidency::Transient;
        return desc;   // anonymous identity -> never deduped
    }

    // A transient target of a caller-chosen size — used to make two transients'
    // descs DIFFER so they must not share an alias group.
    GpuResourceDesc transient_target_sized(uint64_t size)
    {
        GpuResourceDesc desc = transient_target();
        desc.size_bytes = size;
        return desc;
    }

    // A transient desc carrying a NON-anonymous identity. create_transient must
    // scrub this (identity -> anonymous) so two such transients with overlapping
    // lifetimes never dedup to one backing in the registry.
    GpuResourceDesc transient_target_with_identity(uint64_t asset_id)
    {
        GpuResourceDesc desc = transient_target();
        desc.identity.asset_id = asset_id;
        return desc;
    }
}

// Pass callbacks fire in topological order, and only for surviving passes.
static void executes_passes_in_topological_order()
{
    FrameGraph fg;
    const FrameGraphResource color = fg.create_transient("color", transient_target());
    const FrameGraphResource out = fg.create_transient("out", transient_target());
    fg.mark_output(out);

    std::vector<std::string> ran;

    // producer writes `color`; consumer reads it (a genuine RAW dependency).
    // The RAW edge — not add order — pins the producer first in execution.
    const uint32_t producer = fg.add_pass("producer");
    fg.write(producer, color, ResourceState::RenderTarget);
    fg.set_execute(producer, [&](const PassContext&) { ran.push_back("producer"); });

    const uint32_t consumer = fg.add_pass("consumer");
    fg.read(consumer, color, ResourceState::ShaderRead);
    fg.write(consumer, out, ResourceState::RenderTarget);
    fg.set_execute(consumer, [&](const PassContext&) { ran.push_back("consumer"); });

    FakeBackend backend;
    GpuResourceRegistry registry(backend);
    RecordingRecorder recorder;

    const CompiledFrameGraph plan = fg.compile();
    WZ_CHECK(fg.execute(plan, registry, recorder, /*frame_timeline*/ 1));

    WZ_CHECK_EQ(ran.size(), static_cast<size_t>(2));
    if (ran.size() == 2) {
        WZ_CHECK_EQ(ran[0], std::string{ "producer" });
        WZ_CHECK_EQ(ran[1], std::string{ "consumer" });
    }
}

// The derived barriers are actually issued through the recorder, in order, with
// resolved (non-stale) resource handles.
static void issues_derived_barriers_during_execution()
{
    FrameGraph fg;
    // Imported handles use a high index base so they never collide with the
    // low, sequential slot indices the registry hands transient backings — the
    // stateful recorder keys on handle identity, so a collision would conflate
    // an import's state with a transient's.
    const FrameGraphResource backbuffer =
        fg.import("backbuffer", GpuResourceHandle{ 1000, 0 }, ResourceState::Present);
    const FrameGraphResource color = fg.create_transient("color", transient_target());
    fg.mark_output(backbuffer);

    const uint32_t geometry = fg.add_pass("geometry");
    fg.write(geometry, color, ResourceState::RenderTarget);

    const uint32_t post = fg.add_pass("post");
    fg.read(post, color, ResourceState::ShaderRead);
    fg.write(post, backbuffer, ResourceState::RenderTarget);

    FakeBackend backend;
    GpuResourceRegistry registry(backend);
    RecordingRecorder recorder;
    recorder.seed(GpuResourceHandle{ 1000, 0 }, ResourceState::Present);

    const CompiledFrameGraph plan = fg.compile();
    WZ_CHECK(fg.execute(plan, registry, recorder, /*frame_timeline*/ 1));

    // geometry: color Undefined->RenderTarget.
    // post:     color RenderTarget->ShaderRead, backbuffer Present->RenderTarget.
    WZ_CHECK_EQ(recorder.barriers.size(), static_cast<size_t>(3));
    if (recorder.barriers.size() == 3) {
        WZ_CHECK_EQ(recorder.barriers[0].from, ResourceState::Undefined);
        WZ_CHECK_EQ(recorder.barriers[0].to, ResourceState::RenderTarget);
        WZ_CHECK_EQ(recorder.barriers[1].from, ResourceState::RenderTarget);
        WZ_CHECK_EQ(recorder.barriers[1].to, ResourceState::ShaderRead);
        WZ_CHECK_EQ(recorder.barriers[2].from, ResourceState::Present);
        WZ_CHECK_EQ(recorder.barriers[2].to, ResourceState::RenderTarget);
        // The transient color barrier resolved to a real registry handle.
        WZ_CHECK(recorder.barriers[0].resource.valid());
    }
}

// Transients with strictly disjoint lifetimes resolve to the SAME backing
// handle at execution time — the alias plan honored as shared GPU memory.
static void aliased_transients_share_backing()
{
    FrameGraph fg;
    // Outputs are imported (not transient), so the only transients are A and B.
    // High index base keeps these clear of registry-issued slot handles.
    const FrameGraphResource out0 =
        fg.import("out0", GpuResourceHandle{ 1000, 0 }, ResourceState::RenderTarget);
    const FrameGraphResource out1 =
        fg.import("out1", GpuResourceHandle{ 1001, 0 }, ResourceState::RenderTarget);
    fg.mark_output(out0);
    fg.mark_output(out1);

    const FrameGraphResource a = fg.create_transient("a", transient_target());
    const FrameGraphResource b = fg.create_transient("b", transient_target());

    GpuResourceHandle handle_a;
    GpuResourceHandle handle_b;

    // A lives across passes 0-1; B across passes 2-3 — strictly disjoint.
    const uint32_t p0 = fg.add_pass("p0");
    fg.write(p0, a, ResourceState::RenderTarget);
    fg.set_execute(p0, [&](const PassContext& ctx) { handle_a = ctx.resolve(a); });

    const uint32_t p1 = fg.add_pass("p1");
    fg.read(p1, a, ResourceState::ShaderRead);     // a dies here
    fg.write(p1, out0, ResourceState::RenderTarget);

    const uint32_t p2 = fg.add_pass("p2");
    fg.write(p2, b, ResourceState::RenderTarget);  // b born here, after a died
    fg.set_execute(p2, [&](const PassContext& ctx) { handle_b = ctx.resolve(b); });

    const uint32_t p3 = fg.add_pass("p3");
    fg.read(p3, b, ResourceState::ShaderRead);
    fg.write(p3, out1, ResourceState::RenderTarget);

    FakeBackend backend;
    GpuResourceRegistry registry(backend);
    RecordingRecorder recorder;
    recorder.seed(GpuResourceHandle{ 1000, 0 }, ResourceState::RenderTarget);
    recorder.seed(GpuResourceHandle{ 1001, 0 }, ResourceState::RenderTarget);

    const CompiledFrameGraph plan = fg.compile();
    WZ_CHECK(fg.execute(plan, registry, recorder, /*frame_timeline*/ 1));

    WZ_CHECK(handle_a.valid());
    WZ_CHECK(handle_b.valid());
    WZ_CHECK(handle_a == handle_b);          // shared backing (aliased)
    WZ_CHECK_EQ(backend.creates, 1);          // one allocation for the group
}

// Transient backings are released after the frame and reclaimed on collect.
static void transients_released_after_frame()
{
    FrameGraph fg;
    const FrameGraphResource color = fg.create_transient("color", transient_target());
    const FrameGraphResource out = fg.create_transient("out", transient_target());
    fg.mark_output(out);

    const uint32_t producer = fg.add_pass("producer");
    fg.write(producer, color, ResourceState::RenderTarget);
    const uint32_t consumer = fg.add_pass("consumer");
    fg.read(consumer, color, ResourceState::ShaderRead);
    fg.write(consumer, out, ResourceState::RenderTarget);

    FakeBackend backend;
    GpuResourceRegistry registry(backend);
    RecordingRecorder recorder;

    const CompiledFrameGraph plan = fg.compile();
    WZ_CHECK(fg.execute(plan, registry, recorder, /*frame_timeline*/ 100));

    // Backings were released (pending) but tagged with this frame's timeline.
    WZ_CHECK_EQ(backend.destroys, 0);
    // The GPU has not reached 100 yet: collect must NOT reclaim them.
    registry.collect(/*completed*/ 50);
    WZ_CHECK_EQ(backend.destroys, 0);
    // Once the GPU passes 100, collect reclaims them.
    registry.collect(/*completed*/ 100);
    WZ_CHECK(backend.destroys > 0);
    WZ_CHECK_EQ(registry.resident_count(), static_cast<size_t>(0));
}

// The precise-reclamation boundary at the exact frame_timeline value: a
// transient executed with frame_timeline == V survives collect(V - 1) — the GPU
// has NOT yet passed the submission that referenced it — and is reclaimed by
// collect(V). This is the fine-grained guarantee the timeline wiring exists to
// provide (a fixed-latency queue could not draw the line at V exactly).
static void transient_reclaimed_exactly_at_frame_timeline()
{
    FrameGraph fg;
    const FrameGraphResource color = fg.create_transient("color", transient_target());
    const FrameGraphResource out = fg.create_transient("out", transient_target());
    fg.mark_output(out);

    const uint32_t producer = fg.add_pass("producer");
    fg.write(producer, color, ResourceState::RenderTarget);
    const uint32_t consumer = fg.add_pass("consumer");
    fg.read(consumer, color, ResourceState::ShaderRead);
    fg.write(consumer, out, ResourceState::RenderTarget);

    FakeBackend backend;
    GpuResourceRegistry registry(backend);
    RecordingRecorder recorder;

    constexpr uint64_t kFrame = 42;
    const CompiledFrameGraph plan = fg.compile();
    WZ_CHECK(fg.execute(plan, registry, recorder, /*frame_timeline*/ kFrame));

    // Released (pending) but not yet destroyed; tagged with kFrame's timeline.
    WZ_CHECK_EQ(backend.destroys, 0);
    // One tick short of the frame value: the backings are still in flight.
    registry.collect(/*completed*/ kFrame - 1);
    WZ_CHECK_EQ(backend.destroys, 0);
    WZ_CHECK(registry.resident_count() > 0);
    // Exactly at the frame value (last_use <= completed): reclaimed.
    registry.collect(/*completed*/ kFrame);
    WZ_CHECK(backend.destroys > 0);
    WZ_CHECK_EQ(registry.resident_count(), static_cast<size_t>(0));
}

namespace
{
    // Find the first barrier touching `res` across the plan's execution order.
    // Returns nullptr if none (the resource was never transitioned).
    const Barrier* first_barrier_for(const CompiledFrameGraph& plan,
                                     FrameGraphResource res)
    {
        for (const PassExecution& pe : plan.order) {
            for (const Barrier& b : pe.barriers) {
                if (b.resource == res) {
                    return &b;
                }
            }
        }
        return nullptr;
    }
}

// ── Bug 2: aliased transient's first barrier reads the backing's TRUE state ───
// t1 is written RenderTarget then read ShaderRead; a lifetime-disjoint t2 with
// an IDENTICAL desc is then written RenderTarget. t2 shares t1's backing, so its
// first barrier's from-state must be ShaderRead (t1's final state) — NOT
// Undefined — and it must be flagged as an alias activation. The stateful
// recorder independently verifies the from-state matches the backing.
static void aliased_transient_first_barrier_uses_prior_state()
{
    FrameGraph fg;
    const FrameGraphResource out0 =
        fg.import("out0", GpuResourceHandle{ 1000, 0 }, ResourceState::RenderTarget);
    const FrameGraphResource out1 =
        fg.import("out1", GpuResourceHandle{ 1001, 0 }, ResourceState::RenderTarget);
    fg.mark_output(out0);
    fg.mark_output(out1);

    const FrameGraphResource t1 = fg.create_transient("t1", transient_target());
    const FrameGraphResource t2 = fg.create_transient("t2", transient_target());

    GpuResourceHandle handle_t1, handle_t2;

    const uint32_t p0 = fg.add_pass("p0");
    fg.write(p0, t1, ResourceState::RenderTarget);
    fg.set_execute(p0, [&](const PassContext& ctx) { handle_t1 = ctx.resolve(t1); });

    const uint32_t p1 = fg.add_pass("p1");
    fg.read(p1, t1, ResourceState::ShaderRead);      // t1 dies here (final = SR)
    fg.write(p1, out0, ResourceState::RenderTarget);

    const uint32_t p2 = fg.add_pass("p2");
    fg.write(p2, t2, ResourceState::RenderTarget);   // t2 born here, reuses backing
    fg.set_execute(p2, [&](const PassContext& ctx) { handle_t2 = ctx.resolve(t2); });

    const uint32_t p3 = fg.add_pass("p3");
    fg.read(p3, t2, ResourceState::ShaderRead);
    fg.write(p3, out1, ResourceState::RenderTarget);

    FakeBackend backend;
    GpuResourceRegistry registry(backend);
    RecordingRecorder recorder;
    recorder.seed(GpuResourceHandle{ 1000, 0 }, ResourceState::RenderTarget);
    recorder.seed(GpuResourceHandle{ 1001, 0 }, ResourceState::RenderTarget);

    const CompiledFrameGraph plan = fg.compile();

    // Plan-level: t2's first barrier reads t1's final ShaderRead state and is an
    // alias activation.
    const Barrier* t2_first = first_barrier_for(plan, t2);
    WZ_CHECK(t2_first != nullptr);
    if (t2_first) {
        WZ_CHECK_EQ(t2_first->from, ResourceState::ShaderRead);
        WZ_CHECK_EQ(t2_first->to, ResourceState::RenderTarget);
        WZ_CHECK(t2_first->alias_activation);
    }
    // t1's first barrier is a genuine first use: from Undefined, NOT an
    // activation.
    const Barrier* t1_first = first_barrier_for(plan, t1);
    WZ_CHECK(t1_first != nullptr);
    if (t1_first) {
        WZ_CHECK_EQ(t1_first->from, ResourceState::Undefined);
        WZ_CHECK_FALSE(t1_first->alias_activation);
    }

    // Executing through the stateful recorder must not trip its from-state
    // check, and the two transients must resolve to the same backing.
    WZ_CHECK(fg.execute(plan, registry, recorder, /*frame_timeline*/ 1));
    WZ_CHECK(handle_t1.valid());
    WZ_CHECK(handle_t2.valid());
    WZ_CHECK(handle_t1 == handle_t2);
    WZ_CHECK_EQ(backend.creates, 1);
}

// Same shape, but t2's desc DIFFERS (different size) so it cannot share t1's
// backing: different handles, and t2's first barrier is a genuine Undefined
// first use, not an alias activation.
static void mismatched_desc_transients_do_not_alias()
{
    FrameGraph fg;
    const FrameGraphResource out0 =
        fg.import("out0", GpuResourceHandle{ 1000, 0 }, ResourceState::RenderTarget);
    const FrameGraphResource out1 =
        fg.import("out1", GpuResourceHandle{ 1001, 0 }, ResourceState::RenderTarget);
    fg.mark_output(out0);
    fg.mark_output(out1);

    const FrameGraphResource t1 = fg.create_transient("t1", transient_target_sized(4096));
    const FrameGraphResource t2 = fg.create_transient("t2", transient_target_sized(8192));

    GpuResourceHandle handle_t1, handle_t2;

    const uint32_t p0 = fg.add_pass("p0");
    fg.write(p0, t1, ResourceState::RenderTarget);
    fg.set_execute(p0, [&](const PassContext& ctx) { handle_t1 = ctx.resolve(t1); });

    const uint32_t p1 = fg.add_pass("p1");
    fg.read(p1, t1, ResourceState::ShaderRead);
    fg.write(p1, out0, ResourceState::RenderTarget);

    const uint32_t p2 = fg.add_pass("p2");
    fg.write(p2, t2, ResourceState::RenderTarget);
    fg.set_execute(p2, [&](const PassContext& ctx) { handle_t2 = ctx.resolve(t2); });

    const uint32_t p3 = fg.add_pass("p3");
    fg.read(p3, t2, ResourceState::ShaderRead);
    fg.write(p3, out1, ResourceState::RenderTarget);

    FakeBackend backend;
    GpuResourceRegistry registry(backend);
    RecordingRecorder recorder;
    recorder.seed(GpuResourceHandle{ 1000, 0 }, ResourceState::RenderTarget);
    recorder.seed(GpuResourceHandle{ 1001, 0 }, ResourceState::RenderTarget);

    const CompiledFrameGraph plan = fg.compile();

    const Barrier* t2_first = first_barrier_for(plan, t2);
    WZ_CHECK(t2_first != nullptr);
    if (t2_first) {
        WZ_CHECK_EQ(t2_first->from, ResourceState::Undefined);   // fresh backing
        WZ_CHECK_FALSE(t2_first->alias_activation);
    }

    WZ_CHECK(fg.execute(plan, registry, recorder, /*frame_timeline*/ 1));
    WZ_CHECK(handle_t1.valid());
    WZ_CHECK(handle_t2.valid());
    WZ_CHECK_FALSE(handle_t1 == handle_t2);   // distinct backings
    WZ_CHECK_EQ(backend.creates, 2);
}

// ── Bug 5: create_transient scrubs a caller-supplied identity ────────────────
// Two transients created with the SAME non-anonymous identity but OVERLAPPING
// lifetimes must resolve to DIFFERENT backings. If create_transient did not
// scrub identity, the registry would dedup them by identity and hand both the
// same backing — corrupting two live resources onto one allocation.
static void transient_identity_is_scrubbed()
{
    FrameGraph fg;
    const FrameGraphResource out = fg.create_transient("out", transient_target());
    fg.mark_output(out);

    // Both carry identity{asset_id = 7}; both alive across the same passes.
    const FrameGraphResource a =
        fg.create_transient("a", transient_target_with_identity(7));
    const FrameGraphResource b =
        fg.create_transient("b", transient_target_with_identity(7));

    GpuResourceHandle handle_a, handle_b;

    const uint32_t p0 = fg.add_pass("p0");
    fg.write(p0, a, ResourceState::RenderTarget);
    fg.write(p0, b, ResourceState::RenderTarget);
    fg.set_execute(p0, [&](const PassContext& ctx) {
        handle_a = ctx.resolve(a);
        handle_b = ctx.resolve(b);
    });

    const uint32_t p1 = fg.add_pass("p1");
    fg.read(p1, a, ResourceState::ShaderRead);   // a and b overlap [0,1]
    fg.read(p1, b, ResourceState::ShaderRead);
    fg.write(p1, out, ResourceState::RenderTarget);

    FakeBackend backend;
    GpuResourceRegistry registry(backend);
    RecordingRecorder recorder;

    const CompiledFrameGraph plan = fg.compile();
    WZ_CHECK(fg.execute(plan, registry, recorder, /*frame_timeline*/ 1));

    WZ_CHECK(handle_a.valid());
    WZ_CHECK(handle_b.valid());
    WZ_CHECK_FALSE(handle_a == handle_b);   // overlapping -> must NOT alias
}

// ── Bug 7: execute() refuses unsafe plans and cleans up on failure ───────────
// A plan flagged non-acyclic must record NOTHING: no barriers, no callbacks, no
// acquires. (compile() itself is acyclic by construction under pass-index
// versioning, so the flag is set directly to exercise execute()'s guard.)
static void execute_rejects_cyclic_plan()
{
    FrameGraph fg;
    const FrameGraphResource color = fg.create_transient("color", transient_target());
    const FrameGraphResource out = fg.create_transient("out", transient_target());
    fg.mark_output(out);

    bool ran = false;
    const uint32_t producer = fg.add_pass("producer");
    fg.write(producer, color, ResourceState::RenderTarget);
    fg.set_execute(producer, [&](const PassContext&) { ran = true; });
    const uint32_t consumer = fg.add_pass("consumer");
    fg.read(consumer, color, ResourceState::ShaderRead);
    fg.write(consumer, out, ResourceState::RenderTarget);
    fg.set_execute(consumer, [&](const PassContext&) { ran = true; });

    FakeBackend backend;
    GpuResourceRegistry registry(backend);
    RecordingRecorder recorder;

    CompiledFrameGraph plan = fg.compile();
    plan.acyclic = false;   // pretend a cycle was detected

    WZ_CHECK_FALSE(fg.execute(plan, registry, recorder, /*frame_timeline*/ 1));
    WZ_CHECK_EQ(recorder.barriers.size(), static_cast<size_t>(0));
    WZ_CHECK_FALSE(ran);
    WZ_CHECK_EQ(backend.creates, 0);   // no transient acquired
}

// A plan compiled from an earlier revision of the graph must be refused after
// the graph is mutated: execute() records nothing.
static void execute_rejects_stale_plan()
{
    FrameGraph fg;
    const FrameGraphResource color = fg.create_transient("color", transient_target());
    const FrameGraphResource out = fg.create_transient("out", transient_target());
    fg.mark_output(out);

    bool ran = false;
    const uint32_t producer = fg.add_pass("producer");
    fg.write(producer, color, ResourceState::RenderTarget);
    fg.set_execute(producer, [&](const PassContext&) { ran = true; });
    const uint32_t consumer = fg.add_pass("consumer");
    fg.read(consumer, color, ResourceState::ShaderRead);
    fg.write(consumer, out, ResourceState::RenderTarget);
    fg.set_execute(consumer, [&](const PassContext&) { ran = true; });

    FakeBackend backend;
    GpuResourceRegistry registry(backend);
    RecordingRecorder recorder;

    const CompiledFrameGraph plan = fg.compile();
    // Mutate the graph AFTER compiling: the plan's source revision no longer
    // matches, so the stale plan must be rejected.
    fg.add_pass("late_addition");

    WZ_CHECK_FALSE(fg.execute(plan, registry, recorder, /*frame_timeline*/ 1));
    WZ_CHECK_EQ(recorder.barriers.size(), static_cast<size_t>(0));
    WZ_CHECK_FALSE(ran);
    WZ_CHECK_EQ(backend.creates, 0);
}

// When a transient backing fails to acquire, execute() returns false and
// releases every backing it DID acquire, so the registry's resident set returns
// to its pre-execute size (after a collect drains the deferred releases).
static void execute_releases_acquired_backings_on_failure()
{
    FrameGraph fg;
    const FrameGraphResource out =
        fg.import("out", GpuResourceHandle{ 1000, 0 }, ResourceState::RenderTarget);
    fg.mark_output(out);

    // Two transients with DIFFERENT descs (disjoint groups). The first (size
    // 4096) acquires fine; the second (size 8192) is made to fail. They are
    // disjoint in lifetime, but distinct descs guarantee two groups anyway.
    const FrameGraphResource ok = fg.create_transient("ok", transient_target_sized(4096));
    const FrameGraphResource bad = fg.create_transient("bad", transient_target_sized(8192));

    const uint32_t p0 = fg.add_pass("p0");
    fg.write(p0, ok, ResourceState::RenderTarget);
    const uint32_t p1 = fg.add_pass("p1");
    fg.read(p1, ok, ResourceState::ShaderRead);
    fg.write(p1, bad, ResourceState::RenderTarget);
    const uint32_t p2 = fg.add_pass("p2");
    fg.read(p2, bad, ResourceState::ShaderRead);
    fg.write(p2, out, ResourceState::RenderTarget);

    FakeBackend backend;
    backend.fail_size = 8192;   // the `bad` transient's acquire returns null
    GpuResourceRegistry registry(backend);
    RecordingRecorder recorder;

    const size_t before = registry.resident_count();

    const CompiledFrameGraph plan = fg.compile();
    WZ_CHECK_FALSE(fg.execute(plan, registry, recorder, /*frame_timeline*/ 1));
    // Nothing recorded: the failure happened during acquisition, before any
    // barrier or callback.
    WZ_CHECK_EQ(recorder.barriers.size(), static_cast<size_t>(0));
    // The `ok` backing was acquired then released on the failure path; a collect
    // past its (zero) timeline reclaims it, returning to the pre-execute count.
    registry.collect(/*completed*/ 0);
    WZ_CHECK_EQ(registry.resident_count(), before);
}

int main()
{
    WZ_RUN(executes_passes_in_topological_order);
    WZ_RUN(issues_derived_barriers_during_execution);
    WZ_RUN(aliased_transients_share_backing);
    WZ_RUN(transients_released_after_frame);
    WZ_RUN(transient_reclaimed_exactly_at_frame_timeline);
    WZ_RUN(aliased_transient_first_barrier_uses_prior_state);
    WZ_RUN(mismatched_desc_transients_do_not_alias);
    WZ_RUN(transient_identity_is_scrubbed);
    WZ_RUN(execute_rejects_cyclic_plan);
    WZ_RUN(execute_rejects_stale_plan);
    WZ_RUN(execute_releases_acquired_backings_on_failure);
    WZ_TEST_RETURN();
}
