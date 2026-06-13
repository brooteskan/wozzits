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
        BackendResource create(const GpuResourceDesc&) override
        {
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

    struct RecordingRecorder final : CommandRecorder
    {
        std::vector<RecordedBarrier> barriers;
        void barrier(GpuResourceHandle r, ResourceState from, ResourceState to) override
        {
            barriers.push_back(RecordedBarrier{ r, from, to });
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
}

// Pass callbacks fire in topological order, and only for surviving passes.
static void executes_passes_in_topological_order()
{
    FrameGraph fg;
    const FrameGraphResource color = fg.create_transient("color", transient_target());
    const FrameGraphResource out = fg.create_transient("out", transient_target());
    fg.mark_output(out);

    std::vector<std::string> ran;

    // Declare consumer before producer to prove ordering is by dependency.
    const uint32_t consumer = fg.add_pass("consumer");
    fg.read(consumer, color, ResourceState::ShaderRead);
    fg.write(consumer, out, ResourceState::RenderTarget);
    fg.set_execute(consumer, [&](const PassContext&) { ran.push_back("consumer"); });

    const uint32_t producer = fg.add_pass("producer");
    fg.write(producer, color, ResourceState::RenderTarget);
    fg.set_execute(producer, [&](const PassContext&) { ran.push_back("producer"); });

    FakeBackend backend;
    GpuResourceRegistry registry(backend);
    RecordingRecorder recorder;

    const CompiledFrameGraph plan = fg.compile();
    fg.execute(plan, registry, recorder);

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
    const FrameGraphResource backbuffer =
        fg.import("backbuffer", GpuResourceHandle{ 0, 0 }, ResourceState::Present);
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

    const CompiledFrameGraph plan = fg.compile();
    fg.execute(plan, registry, recorder);

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
    const FrameGraphResource out0 =
        fg.import("out0", GpuResourceHandle{ 1, 0 }, ResourceState::RenderTarget);
    const FrameGraphResource out1 =
        fg.import("out1", GpuResourceHandle{ 2, 0 }, ResourceState::RenderTarget);
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

    const CompiledFrameGraph plan = fg.compile();
    fg.execute(plan, registry, recorder);

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
    fg.execute(plan, registry, recorder, /*frame_timeline*/ 100);

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

int main()
{
    WZ_RUN(executes_passes_in_topological_order);
    WZ_RUN(issues_derived_barriers_during_execution);
    WZ_RUN(aliased_transients_share_backing);
    WZ_RUN(transients_released_after_frame);
    WZ_TEST_RETURN();
}
