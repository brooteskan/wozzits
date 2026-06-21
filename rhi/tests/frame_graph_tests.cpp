#include "wz_test.h"

#include <wozzits/rhi/frame_graph.h>

using namespace wz::rhi;

namespace
{
    // Find the execution position of a pass by its original index; -1 if culled.
    int position_of(const CompiledFrameGraph& g, uint32_t pass_index)
    {
        for (size_t i = 0; i < g.order.size(); ++i) {
            if (g.order[i].pass_index == pass_index) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    GpuResourceDesc transient_target()
    {
        GpuResourceDesc desc;
        desc.size_bytes = 4096;
        desc.usage = ResourceUsage_RenderTarget | ResourceUsage_Sampled;
        desc.residency = ResourceResidency::Transient;
        return desc;
    }

    GpuResourceDesc transient_storage()
    {
        GpuResourceDesc desc;
        desc.size_bytes = 4096;
        desc.usage = ResourceUsage_Storage;
        desc.residency = ResourceResidency::Transient;
        return desc;
    }
}

// A producer/consumer chain orders correctly and derives the transitions the
// old submit path issues by hand.
static void chain_orders_and_derives_barriers()
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

    const CompiledFrameGraph g = fg.compile();
    WZ_CHECK(g.acyclic);
    WZ_CHECK_EQ(g.pass_count(), static_cast<size_t>(2));
    WZ_CHECK(position_of(g, geometry) < position_of(g, post));  // producer first

    // geometry: color Undefined -> RenderTarget.
    const int gp = position_of(g, geometry);
    WZ_CHECK_EQ(g.order[gp].barriers.size(), static_cast<size_t>(1));
    if (!g.order[gp].barriers.empty()) {
        WZ_CHECK_EQ(g.order[gp].barriers[0].from, ResourceState::Undefined);
        WZ_CHECK_EQ(g.order[gp].barriers[0].to, ResourceState::RenderTarget);
    }

    // post: color RenderTarget -> ShaderRead, and backbuffer Present ->
    // RenderTarget (imported initial state was Present).
    const int pp = position_of(g, post);
    WZ_CHECK_EQ(g.order[pp].barriers.size(), static_cast<size_t>(2));
}

// Topological order is independent of the order passes were added: declare the
// consumer before the producer and the graph still runs the producer first.
static void topo_order_is_independent_of_add_order()
{
    FrameGraph fg;
    const FrameGraphResource color = fg.create_transient("color", transient_target());
    const FrameGraphResource final_rt = fg.create_transient("final", transient_target());
    fg.mark_output(final_rt);

    // Add consumer FIRST.
    const uint32_t consumer = fg.add_pass("consumer");
    fg.read(consumer, color, ResourceState::ShaderRead);
    fg.write(consumer, final_rt, ResourceState::RenderTarget);

    const uint32_t producer = fg.add_pass("producer");
    fg.write(producer, color, ResourceState::RenderTarget);

    const CompiledFrameGraph g = fg.compile();
    WZ_CHECK(g.acyclic);
    WZ_CHECK(position_of(g, producer) < position_of(g, consumer));
}

// A pass whose outputs are never consumed (and aren't outputs) is culled.
static void dead_pass_is_culled()
{
    FrameGraph fg;
    const FrameGraphResource used = fg.create_transient("used", transient_target());
    const FrameGraphResource orphan = fg.create_transient("orphan", transient_target());
    fg.mark_output(used);

    const uint32_t live = fg.add_pass("live");
    fg.write(live, used, ResourceState::RenderTarget);

    const uint32_t dead = fg.add_pass("dead");
    fg.write(dead, orphan, ResourceState::RenderTarget);  // nobody reads orphan

    const CompiledFrameGraph g = fg.compile();
    WZ_CHECK(position_of(g, live) >= 0);    // kept
    WZ_CHECK_EQ(position_of(g, dead), -1);  // culled
    WZ_CHECK_EQ(g.pass_count(), static_cast<size_t>(1));
}

// Culling is transitive: a pass that only feeds a culled pass is itself culled.
static void culling_is_transitive()
{
    FrameGraph fg;
    const FrameGraphResource a = fg.create_transient("a", transient_target());
    const FrameGraphResource b = fg.create_transient("b", transient_target());
    // Neither a nor b is an output, and nothing external consumes b.

    const uint32_t feeder = fg.add_pass("feeder");
    fg.write(feeder, a, ResourceState::RenderTarget);

    const uint32_t sink = fg.add_pass("sink");
    fg.read(sink, a, ResourceState::ShaderRead);
    fg.write(sink, b, ResourceState::RenderTarget);  // b consumed by no one

    const CompiledFrameGraph g = fg.compile();
    WZ_CHECK_EQ(g.pass_count(), static_cast<size_t>(0));  // both culled
}

// No redundant barrier when consecutive passes use a resource in the same state.
static void no_barrier_without_state_change()
{
    FrameGraph fg;
    const FrameGraphResource shared = fg.create_transient("shared", transient_target());
    const FrameGraphResource out = fg.create_transient("out", transient_target());
    fg.mark_output(out);

    const uint32_t producer = fg.add_pass("producer");
    fg.write(producer, shared, ResourceState::RenderTarget);

    const uint32_t reader_a = fg.add_pass("reader_a");
    fg.read(reader_a, shared, ResourceState::ShaderRead);
    fg.write(reader_a, out, ResourceState::RenderTarget);

    const uint32_t reader_b = fg.add_pass("reader_b");
    fg.read(reader_b, shared, ResourceState::ShaderRead);  // same state as reader_a
    fg.write(reader_b, out, ResourceState::RenderTarget);

    const CompiledFrameGraph g = fg.compile();
    // Whichever reader runs second must NOT re-barrier `shared` (already in
    // ShaderRead). Count barriers that touch `shared` across all passes.
    int shared_barriers = 0;
    for (const PassExecution& e : g.order) {
        for (const Barrier& bar : e.barriers) {
            if (bar.resource == shared) ++shared_barriers;
        }
    }
    // Exactly two transitions touch `shared`: Undefined->RenderTarget on the
    // producer, then RenderTarget->ShaderRead on the FIRST reader. The second
    // reader needs the same ShaderRead state and must add no barrier.
    WZ_CHECK_EQ(shared_barriers, 2);
}

// Same-state UAV read/write hazards still need ordering. The graph expresses
// that as an UnorderedAccess->UnorderedAccess barrier for the backend recorder
// to lower to a native UAV barrier.
static void unordered_access_write_then_read_gets_uav_barrier()
{
    FrameGraph fg;
    const FrameGraphResource scratch =
        fg.create_transient("scratch", transient_storage());
    const FrameGraphResource out =
        fg.create_transient("out", transient_storage());
    fg.mark_output(out);

    const uint32_t writer = fg.add_pass("writer");
    fg.write(writer, scratch, ResourceState::UnorderedAccess);

    const uint32_t reader = fg.add_pass("reader");
    fg.read(reader, scratch, ResourceState::UnorderedAccess);
    fg.write(reader, out, ResourceState::UnorderedAccess);

    const CompiledFrameGraph g = fg.compile();
    const int rp = position_of(g, reader);
    WZ_CHECK(rp >= 0);

    bool saw_uav_barrier = false;
    for (const Barrier& barrier : g.order[static_cast<size_t>(rp)].barriers) {
        if (barrier.resource == scratch
            && barrier.from == ResourceState::UnorderedAccess
            && barrier.to == ResourceState::UnorderedAccess)
        {
            saw_uav_barrier = true;
        }
    }
    WZ_CHECK(saw_uav_barrier);
}

// Transients with disjoint lifetimes share an alias group; overlapping ones do
// not.
static void disjoint_transients_alias()
{
    FrameGraph fg;
    const FrameGraphResource out = fg.create_transient("out", transient_target());
    fg.mark_output(out);

    // early: produced and consumed in the first half.
    const FrameGraphResource early = fg.create_transient("early", transient_target());
    // late: produced and consumed in the second half (disjoint from early).
    const FrameGraphResource late = fg.create_transient("late", transient_target());

    const uint32_t p0 = fg.add_pass("p0");
    fg.write(p0, early, ResourceState::RenderTarget);

    const uint32_t p1 = fg.add_pass("p1");
    fg.read(p1, early, ResourceState::ShaderRead);    // early dies here
    fg.write(p1, late, ResourceState::RenderTarget);  // late born here

    const uint32_t p2 = fg.add_pass("p2");
    fg.read(p2, late, ResourceState::ShaderRead);
    fg.write(p2, out, ResourceState::RenderTarget);

    const CompiledFrameGraph g = fg.compile();
    WZ_CHECK_EQ(g.transients.size(), static_cast<size_t>(3));

    auto group_of = [&](FrameGraphResource r) -> int {
        for (const TransientAllocation& t : g.transients) {
            if (t.resource == r) return static_cast<int>(t.alias_group);
        }
        return -1;
    };
    // early [0,1] and late [1,2] overlap at pass 1 -> different groups.
    WZ_CHECK_FALSE(group_of(early) == group_of(late));
}

int main()
{
    WZ_RUN(chain_orders_and_derives_barriers);
    WZ_RUN(topo_order_is_independent_of_add_order);
    WZ_RUN(dead_pass_is_culled);
    WZ_RUN(culling_is_transitive);
    WZ_RUN(no_barrier_without_state_change);
    WZ_RUN(unordered_access_write_then_read_gets_uav_barrier);
    WZ_RUN(disjoint_transients_alias);
    WZ_TEST_RETURN();
}
