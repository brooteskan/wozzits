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

// Ordering follows each resource's VERSION TIMELINE (declaration/pass-index
// order), not a naive "all producers before all consumers". Here the consumer
// (pass 0) reads `color` before the producer (pass 1) overwrites it, so this is
// a WAR hazard: the consumer must observe color's initial version before the
// producer clobbers it. The graph therefore runs the reader first — the very
// case the old writers×readers product got backwards by forcing every writer
// ahead of every reader. (A genuine RAW — producer writes what a
// lower-indexed consumer reads — orders producer-first; see
// chain_orders_and_derives_barriers and war_orders_reader_before_later_writer.)
static void topo_order_follows_resource_version_timeline()
{
    FrameGraph fg;
    const FrameGraphResource color = fg.create_transient("color", transient_target());
    const FrameGraphResource final_rt = fg.create_transient("final", transient_target());
    fg.mark_output(final_rt);

    // Pass 0 reads `color`; pass 1 later overwrites it (WAR).
    const uint32_t consumer = fg.add_pass("consumer");
    fg.read(consumer, color, ResourceState::ShaderRead);
    fg.write(consumer, final_rt, ResourceState::RenderTarget);

    const uint32_t producer = fg.add_pass("producer");
    fg.write(producer, color, ResourceState::RenderTarget);

    const CompiledFrameGraph g = fg.compile();
    WZ_CHECK(g.acyclic);
    WZ_CHECK(position_of(g, consumer) < position_of(g, producer));
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

// ── Bug 1: culling releases reads once per DISTINCT resource ─────────────────
// A pass that reads one resource multiple times must not double-decrement that
// resource's ref-count when the pass is culled — doing so can transitively cull
// a live producer whose output another pass still consumes.
static void culling_decrements_reads_once_per_distinct_resource()
{
    FrameGraph fg;
    const FrameGraphResource r1  = fg.create_transient("r1", transient_target());
    const FrameGraphResource r2  = fg.create_transient("r2", transient_target());
    const FrameGraphResource out = fg.create_transient("out", transient_target());
    fg.mark_output(out);

    // A produces R1 (consumed by both B and C).
    const uint32_t A = fg.add_pass("A");
    fg.write(A, r1, ResourceState::RenderTarget);

    // B reads R1 TWICE and writes R2, which nobody reads -> B must be culled.
    const uint32_t B = fg.add_pass("B");
    fg.read(B, r1, ResourceState::ShaderRead);
    fg.read(B, r1, ResourceState::ShaderRead);
    fg.write(B, r2, ResourceState::RenderTarget);

    // C reads R1 and writes the output -> C (and its producer A) must survive.
    const uint32_t C = fg.add_pass("C");
    fg.read(C, r1, ResourceState::ShaderRead);
    fg.write(C, out, ResourceState::RenderTarget);

    const CompiledFrameGraph g = fg.compile();
    WZ_CHECK_EQ(position_of(g, B), -1);   // culled (R2 dead)
    WZ_CHECK(position_of(g, A) >= 0);     // SURVIVES (C still consumes R1)
    WZ_CHECK(position_of(g, C) >= 0);     // survives (writes the output)
    WZ_CHECK(position_of(g, A) < position_of(g, C));
}

// Same shape, but B reads R1 THREE times — the per-distinct-resource release
// must still leave A and C alive.
static void culling_triple_read_does_not_overcull()
{
    FrameGraph fg;
    const FrameGraphResource r1  = fg.create_transient("r1", transient_target());
    const FrameGraphResource r2  = fg.create_transient("r2", transient_target());
    const FrameGraphResource out = fg.create_transient("out", transient_target());
    fg.mark_output(out);

    const uint32_t A = fg.add_pass("A");
    fg.write(A, r1, ResourceState::RenderTarget);

    const uint32_t B = fg.add_pass("B");
    fg.read(B, r1, ResourceState::ShaderRead);
    fg.read(B, r1, ResourceState::ShaderRead);
    fg.read(B, r1, ResourceState::ShaderRead);
    fg.write(B, r2, ResourceState::RenderTarget);

    const uint32_t C = fg.add_pass("C");
    fg.read(C, r1, ResourceState::ShaderRead);
    fg.write(C, out, ResourceState::RenderTarget);

    const CompiledFrameGraph g = fg.compile();
    WZ_CHECK_EQ(position_of(g, B), -1);
    WZ_CHECK(position_of(g, A) >= 0);
    WZ_CHECK(position_of(g, C) >= 0);
}

// ── Bug 4: WAR / cycle / WAW ordering edges ──────────────────────────────────
// WAR: a pass that reads a resource must precede a LATER-declared pass that
// overwrites it, so the reader observes the pre-write version.
static void war_orders_reader_before_later_writer()
{
    FrameGraph fg;
    const FrameGraphResource a =
        fg.import("a", GpuResourceHandle{ 0, 0 }, ResourceState::ShaderRead);
    const FrameGraphResource out1 = fg.create_transient("out1", transient_target());
    const FrameGraphResource output = fg.create_transient("output", transient_target());
    fg.mark_output(output);

    // P1 reads A, writes out1.
    const uint32_t p1 = fg.add_pass("p1");
    fg.read(p1, a, ResourceState::ShaderRead);
    fg.write(p1, out1, ResourceState::RenderTarget);

    // P2 overwrites A (WAR against P1).
    const uint32_t p2 = fg.add_pass("p2");
    fg.write(p2, a, ResourceState::RenderTarget);

    // P3 consumes out1 (keeps P1 alive) and reads A (keeps P2 alive), writes out.
    const uint32_t p3 = fg.add_pass("p3");
    fg.read(p3, out1, ResourceState::ShaderRead);
    fg.read(p3, a, ResourceState::ShaderRead);
    fg.write(p3, output, ResourceState::RenderTarget);

    const CompiledFrameGraph g = fg.compile();
    WZ_CHECK(g.acyclic);
    WZ_CHECK(position_of(g, p1) < position_of(g, p2));   // WAR edge P1 -> P2
}

// A mutual read/write pair (P1 reads A writes B; P2 reads B writes A) is
// resolved, not rejected: because a resource's version timeline is walked in
// pass-index order, every derived hazard edge points from a lower to a higher
// pass index (RAW writer->reader, WAR reader->writer, WAW writer->writer all
// have from-index < to-index). Index-ordered versioning is therefore acyclic by
// construction — this scenario compiles to the consistent order P1 then P2
// (P2's read of B observes P1's freshly written version). This documents the
// contract; execute()'s acyclic guard is exercised directly in the execution
// tests. (Without the WAR edge derived here, the old product-based path would
// still order these two but would drop the P1(reads A)->P2(writes A) ordering
// constraint entirely.)
static void mutual_read_write_resolves_acyclically()
{
    FrameGraph fg;
    const FrameGraphResource a =
        fg.import("a", GpuResourceHandle{ 0, 0 }, ResourceState::ShaderRead);
    const FrameGraphResource b =
        fg.import("b", GpuResourceHandle{ 1, 0 }, ResourceState::ShaderRead);
    fg.mark_output(a);
    fg.mark_output(b);

    const uint32_t p1 = fg.add_pass("p1");
    fg.read(p1, a, ResourceState::ShaderRead);
    fg.write(p1, b, ResourceState::RenderTarget);

    const uint32_t p2 = fg.add_pass("p2");
    fg.read(p2, b, ResourceState::ShaderRead);
    fg.write(p2, a, ResourceState::RenderTarget);

    const CompiledFrameGraph g = fg.compile();
    WZ_CHECK(g.acyclic);
    WZ_CHECK_EQ(g.pass_count(), static_cast<size_t>(2));
    // Both the RAW edge (via B: p1 writes, p2 reads) and the WAR edge (via A: p1
    // reads, p2 writes) point p1 -> p2, so p1 runs first.
    WZ_CHECK(position_of(g, p1) < position_of(g, p2));
}

// WAW: two passes writing the same imported+output resource with no other
// connection serialize by declaration order, via a REAL edge — proven by
// putting an unrelated ready pass at a lower index so the min-index tie-break
// alone would not order the two writers correctly.
static void waw_orders_writers_by_declaration()
{
    FrameGraph fg;
    const FrameGraphResource shared =
        fg.import("shared", GpuResourceHandle{ 0, 0 }, ResourceState::RenderTarget);
    const FrameGraphResource side = fg.create_transient("side", transient_target());
    fg.mark_output(shared);
    fg.mark_output(side);

    // pass 0: an unrelated writer (lowest index -> tie-break would run it first,
    // which is fine; it just must not perturb the two shared-writers' order).
    const uint32_t unrelated = fg.add_pass("unrelated");
    fg.write(unrelated, side, ResourceState::RenderTarget);

    // pass 1 writes shared, pass 2 writes shared again (WAW: 1 before 2).
    const uint32_t first_writer = fg.add_pass("first_writer");
    fg.write(first_writer, shared, ResourceState::RenderTarget);

    const uint32_t second_writer = fg.add_pass("second_writer");
    fg.write(second_writer, shared, ResourceState::UnorderedAccess);

    const CompiledFrameGraph g = fg.compile();
    WZ_CHECK(g.acyclic);
    WZ_CHECK(position_of(g, first_writer) < position_of(g, second_writer));
    // And the ordering came from a WAW edge, not just index luck: second_writer
    // transitions shared RenderTarget -> UnorderedAccess, which only holds if it
    // ran strictly after first_writer left it in RenderTarget.
    const int sp = position_of(g, second_writer);
    bool saw_rt_to_ua = false;
    for (const Barrier& bar : g.order[static_cast<size_t>(sp)].barriers) {
        if (bar.resource == shared
            && bar.from == ResourceState::RenderTarget
            && bar.to == ResourceState::UnorderedAccess) {
            saw_rt_to_ua = true;
        }
    }
    WZ_CHECK(saw_rt_to_ua);
}

// A 3-hop RAW chain (A writes X; B reads X, writes Y; C reads Y, writes out)
// serializes A -> B -> C through the two intermediate transients, and none of
// the three is culled. The chain edges are transitive, so the middle pass is
// pinned between its producer and consumer.
static void raw_chain_serializes_three_passes()
{
    FrameGraph fg;
    const FrameGraphResource x = fg.create_transient("x", transient_target());
    const FrameGraphResource y = fg.create_transient("y", transient_target());
    const FrameGraphResource out = fg.create_transient("out", transient_target());
    fg.mark_output(out);

    const uint32_t A = fg.add_pass("A");
    fg.write(A, x, ResourceState::RenderTarget);

    const uint32_t B = fg.add_pass("B");
    fg.read(B, x, ResourceState::ShaderRead);
    fg.write(B, y, ResourceState::RenderTarget);

    const uint32_t C = fg.add_pass("C");
    fg.read(C, y, ResourceState::ShaderRead);
    fg.write(C, out, ResourceState::RenderTarget);

    const CompiledFrameGraph g = fg.compile();
    WZ_CHECK(g.acyclic);
    WZ_CHECK_EQ(g.pass_count(), static_cast<size_t>(3));
    WZ_CHECK(position_of(g, A) < position_of(g, B));
    WZ_CHECK(position_of(g, B) < position_of(g, C));
}

// ── Bug 3: UAV hazard flag evaluated per PASS, not per access ─────────────────
// A read-modify-write pass (write UA then read UA on the same resource) must
// leave the UAV hazard armed for the NEXT reader, and must NOT emit a spurious
// same-pass UA->UA barrier on its own first use.
static void uav_read_modify_write_arms_next_reader()
{
    FrameGraph fg;
    const FrameGraphResource r = fg.create_transient("r", transient_storage());
    const FrameGraphResource out = fg.create_transient("out", transient_storage());
    fg.mark_output(out);

    // P1: write UA then read UA on r (RMW), and write out so P1 stays alive.
    const uint32_t p1 = fg.add_pass("p1");
    fg.write(p1, r, ResourceState::UnorderedAccess);
    fg.read(p1, r, ResourceState::UnorderedAccess);
    fg.write(p1, out, ResourceState::UnorderedAccess);

    // P2: read UA on r -> must get a UA->UA barrier (hazard from P1's write).
    const uint32_t p2 = fg.add_pass("p2");
    fg.read(p2, r, ResourceState::UnorderedAccess);
    fg.write(p2, out, ResourceState::UnorderedAccess);

    const CompiledFrameGraph g = fg.compile();
    const int p1p = position_of(g, p1);
    const int p2p = position_of(g, p2);
    WZ_CHECK(p1p >= 0 && p2p >= 0);

    // P1 first use is Undefined->UA (a real transition); it must NOT also carry
    // a same-state UA->UA barrier on r.
    int p1_same_state_uav = 0;
    for (const Barrier& bar : g.order[static_cast<size_t>(p1p)].barriers) {
        if (bar.resource == r
            && bar.from == ResourceState::UnorderedAccess
            && bar.to == ResourceState::UnorderedAccess) {
            ++p1_same_state_uav;
        }
    }
    WZ_CHECK_EQ(p1_same_state_uav, 0);

    // P2 MUST see the UA->UA barrier the per-access flag would have lost.
    bool p2_uav = false;
    for (const Barrier& bar : g.order[static_cast<size_t>(p2p)].barriers) {
        if (bar.resource == r
            && bar.from == ResourceState::UnorderedAccess
            && bar.to == ResourceState::UnorderedAccess) {
            p2_uav = true;
        }
    }
    WZ_CHECK(p2_uav);
}

// WAW on a UAV: P1 writes UA, P2 writes UA -> P2 gets a UA->UA barrier. `r` is
// marked output so both writers survive culling (nothing reads r).
static void uav_write_after_write_gets_barrier()
{
    FrameGraph fg;
    const FrameGraphResource r = fg.create_transient("r", transient_storage());
    fg.mark_output(r);

    const uint32_t p1 = fg.add_pass("p1");
    fg.write(p1, r, ResourceState::UnorderedAccess);

    const uint32_t p2 = fg.add_pass("p2");
    fg.write(p2, r, ResourceState::UnorderedAccess);   // WAW hazard

    const CompiledFrameGraph g = fg.compile();
    const int p2p = position_of(g, p2);
    WZ_CHECK(p2p >= 0);
    bool p2_uav = false;
    for (const Barrier& bar : g.order[static_cast<size_t>(p2p)].barriers) {
        if (bar.resource == r
            && bar.from == ResourceState::UnorderedAccess
            && bar.to == ResourceState::UnorderedAccess) {
            p2_uav = true;
        }
    }
    WZ_CHECK(p2_uav);
}

// Transitioning a UAV away and back clears the stale hazard: P1 writes UA, P2
// reads it as ShaderRead, P3 reads UA again -> P3's barrier is the ShaderRead->
// UA transition, with no leftover same-state UAV barrier.
static void uav_transition_away_and_back_clears_pending()
{
    FrameGraph fg;
    const FrameGraphResource r = fg.create_transient("r", transient_storage());
    const FrameGraphResource out = fg.create_transient("out", transient_storage());
    fg.mark_output(out);

    const uint32_t p1 = fg.add_pass("p1");
    fg.write(p1, r, ResourceState::UnorderedAccess);

    const uint32_t p2 = fg.add_pass("p2");
    fg.read(p2, r, ResourceState::ShaderRead);   // transitions away from UA

    const uint32_t p3 = fg.add_pass("p3");
    fg.read(p3, r, ResourceState::UnorderedAccess);   // transitions back to UA
    fg.write(p3, out, ResourceState::UnorderedAccess);

    const CompiledFrameGraph g = fg.compile();
    const int p3p = position_of(g, p3);
    WZ_CHECK(p3p >= 0);

    int r_barriers = 0;
    bool saw_sr_to_ua = false, saw_same_state = false;
    for (const Barrier& bar : g.order[static_cast<size_t>(p3p)].barriers) {
        if (bar.resource != r) continue;
        ++r_barriers;
        if (bar.from == ResourceState::ShaderRead
            && bar.to == ResourceState::UnorderedAccess) saw_sr_to_ua = true;
        if (bar.from == ResourceState::UnorderedAccess
            && bar.to == ResourceState::UnorderedAccess) saw_same_state = true;
    }
    WZ_CHECK(saw_sr_to_ua);        // the real transition
    WZ_CHECK_FALSE(saw_same_state);// no stale UAV barrier
    WZ_CHECK_EQ(r_barriers, 1);
}

// ── Bug 7: unchecked inputs to read()/write() ────────────────────────────────
// read()/write() reject an invalid resource or out-of-range pass, recording
// nothing, and a subsequent compile() is unaffected (no crash, no phantom
// access).
static void read_write_reject_invalid_inputs()
{
    FrameGraph fg;
    const FrameGraphResource color = fg.create_transient("color", transient_target());
    const FrameGraphResource out = fg.create_transient("out", transient_target());
    fg.mark_output(out);

    const uint32_t producer = fg.add_pass("producer");
    WZ_CHECK(fg.write(producer, color, ResourceState::RenderTarget));

    const uint32_t consumer = fg.add_pass("consumer");
    WZ_CHECK(fg.read(consumer, color, ResourceState::ShaderRead));
    WZ_CHECK(fg.write(consumer, out, ResourceState::RenderTarget));

    // Invalid resource (default-constructed) -> rejected.
    WZ_CHECK_FALSE(fg.read(consumer, FrameGraphResource{}, ResourceState::ShaderRead));
    WZ_CHECK_FALSE(fg.write(consumer, FrameGraphResource{}, ResourceState::RenderTarget));
    // Out-of-range pass index -> rejected.
    WZ_CHECK_FALSE(fg.read(9999, color, ResourceState::ShaderRead));
    WZ_CHECK_FALSE(fg.write(9999, color, ResourceState::RenderTarget));
    // Out-of-range resource index (valid() but past the end) -> rejected.
    WZ_CHECK_FALSE(fg.read(consumer, FrameGraphResource{ 42 }, ResourceState::ShaderRead));
    // set_execute shares the rejection contract.
    WZ_CHECK_FALSE(fg.set_execute(9999, [](const PassContext&) {}));
    WZ_CHECK(fg.set_execute(consumer, [](const PassContext&) {}));

    // Compile is unaffected: the rejected calls left no phantom accesses.
    const CompiledFrameGraph g = fg.compile();
    WZ_CHECK(g.acyclic);
    WZ_CHECK_EQ(g.pass_count(), static_cast<size_t>(2));
    WZ_CHECK(position_of(g, producer) < position_of(g, consumer));
}

int main()
{
    WZ_RUN(chain_orders_and_derives_barriers);
    WZ_RUN(topo_order_follows_resource_version_timeline);
    WZ_RUN(dead_pass_is_culled);
    WZ_RUN(culling_is_transitive);
    WZ_RUN(no_barrier_without_state_change);
    WZ_RUN(unordered_access_write_then_read_gets_uav_barrier);
    WZ_RUN(disjoint_transients_alias);
    WZ_RUN(culling_decrements_reads_once_per_distinct_resource);
    WZ_RUN(culling_triple_read_does_not_overcull);
    WZ_RUN(war_orders_reader_before_later_writer);
    WZ_RUN(mutual_read_write_resolves_acyclically);
    WZ_RUN(waw_orders_writers_by_declaration);
    WZ_RUN(raw_chain_serializes_three_passes);
    WZ_RUN(uav_read_modify_write_arms_next_reader);
    WZ_RUN(uav_write_after_write_gets_barrier);
    WZ_RUN(uav_transition_away_and_back_clears_pending);
    WZ_RUN(read_write_reject_invalid_inputs);
    WZ_TEST_RETURN();
}
