// tests/engine/runtime_channel_tests.cpp
//
// The two cross-thread primitives EditorRuntimeControl is being rebuilt onto
// (#300 layer 1): Mailbox<T> (fire-and-forget, with coalesce/dedupe/append
// policy) and Request<Args,R> (blocking request/response). These pin the
// behaviours the hand-rolled channels encoded per-verb -- including the two that
// were bugs in the hand-rolled versions: two concurrent callers each getting
// their OWN result (#313 B4-C2), and "engine never answered" staying distinct
// from "answered with a default value" (#313 D3-P039).

#include <gtest/gtest.h>

#include <engine/app/runtime_channel.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using wz::app::Mailbox;
    using wz::app::Request;
    using wz::app::RequestOutcome;

    // Spin the engine-thread service() until it has run once, mirroring the
    // once-per-frame poll in run_project_runtime.
    template <class Req, class Fn>
    void service_until_done(Req& req, bool& done, Fn&& fn)
    {
        while (!done) {
            req.service(fn);
            std::this_thread::yield();
        }
    }
}

// ── Mailbox ────────────────────────────────────────────────────────────────

TEST(Mailbox, AppendKeepsEveryPostInOrder)
{
    Mailbox<int> box;  // default: Keep
    box.post(1);
    box.post(2);
    box.post(1);

    std::vector<int> got;
    box.drain([&](int v) { got.push_back(v); });
    EXPECT_EQ(got, (std::vector<int>{ 1, 2, 1 }));
}

TEST(Mailbox, ReplaceCoalescesByKeyLatestWinsAndKeepsDistinctKeys)
{
    using P = std::pair<int, int>;  // .first = key, .second = value
    Mailbox<P> box(
        Mailbox<P>::OnDuplicate::Replace,
        [](const P& a, const P& b) { return a.first == b.first; });

    box.post({ 1, 10 });
    box.post({ 2, 20 });
    box.post({ 1, 11 });
    box.post({ 1, 12 });  // latest for key 1

    std::vector<P> got;
    box.drain([&](const P& v) { got.push_back(v); });

    ASSERT_EQ(got.size(), 2u);          // one entry per key
    EXPECT_EQ(got[0], (P{ 1, 12 }));    // key 1 coalesced to the latest, in place
    EXPECT_EQ(got[1], (P{ 2, 20 }));    // distinct key preserved
}

TEST(Mailbox, DropDedupesByKeyKeepingTheFirst)
{
    Mailbox<int> box(
        Mailbox<int>::OnDuplicate::Drop,
        [](const int& a, const int& b) { return a == b; });

    box.post(1);
    box.post(2);
    box.post(1);  // dropped
    box.post(3);
    box.post(2);  // dropped

    std::vector<int> got;
    box.drain([&](int v) { got.push_back(v); });
    EXPECT_EQ(got, (std::vector<int>{ 1, 2, 3 }));
}

TEST(Mailbox, DrainsExactlyOnceThenIsEmpty)
{
    Mailbox<int> box;
    box.post(7);

    int count = 0;
    box.drain([&](int) { ++count; });
    EXPECT_EQ(count, 1);

    count = 0;
    box.drain([&](int) { ++count; });
    EXPECT_EQ(count, 0);  // a second drain finds an empty queue
}

TEST(Mailbox, EmptyDrainDoesNothing)
{
    Mailbox<int> box;
    int count = 0;
    box.drain([&](int) { ++count; });
    EXPECT_EQ(count, 0);
    EXPECT_TRUE(box.empty());
}

TEST(Mailbox, ConcurrentPostsAndDrainsLoseNothing)
{
    constexpr int kPosts = 1000;
    Mailbox<int> box;
    std::atomic<int> total{ 0 };

    // A drainer racing the producer, then a final drain to catch the tail.
    std::thread drainer([&] {
        for (int spins = 0; spins < 4000; ++spins) {
            box.drain([&](int) { total.fetch_add(1, std::memory_order_relaxed); });
            std::this_thread::yield();
        }
    });
    for (int i = 0; i < kPosts; ++i) {
        box.post(i);
    }
    drainer.join();
    box.drain([&](int) { total.fetch_add(1, std::memory_order_relaxed); });

    // Append keeps every post, and drain hands each out exactly once.
    EXPECT_EQ(total.load(), kPosts);
    EXPECT_TRUE(box.empty());
}

// ── Request ──────────────────────────────────────────────────────────────

TEST(Request, RoundTripDeliversTheServicedValue)
{
    Request<int, int> req;
    RequestOutcome<int> out;

    std::thread caller([&] { out = req.call(21); });

    bool done = false;
    service_until_done(req, done, [&](int a) {
        done = true;
        return a * 2;
    });
    caller.join();

    EXPECT_TRUE(out.serviced);
    EXPECT_EQ(out.value, 42);
}

TEST(Request, ServiceWithNoPendingRequestIsANoOp)
{
    Request<int, int> req;
    bool ran = false;
    req.service([&](int a) {
        ran = true;
        return a;
    });
    EXPECT_FALSE(ran);
}

TEST(Request, AbandonReleasesABlockedCallerAsUnserviced)
{
    Request<int, int> req;
    RequestOutcome<int> out{ true, 999 };  // seed with non-default to prove it clears

    std::thread caller([&] { out = req.call(5); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // let it park

    req.abandon();
    caller.join();

    // serviced=false is distinct from any real value the engine could return.
    EXPECT_FALSE(out.serviced);
    EXPECT_EQ(out.value, 0);
}

TEST(Request, TwoConcurrentCallersEachGetTheirOwnResult)
{
    // The #313 B4-C2 regression pin: with one shared cv a publish woke every
    // waiter and a second caller could swallow the first's result. A per-request
    // cv makes each caller's answer its own.
    Request<int, int> req;
    std::atomic<int> ra{ -1 };
    std::atomic<int> rb{ -1 };

    std::thread a([&] {
        auto o = req.call(10);
        ra.store(o.serviced ? o.value : -1);
    });
    std::thread b([&] {
        auto o = req.call(20);
        rb.store(o.serviced ? o.value : -1);
    });

    int serviced_count = 0;
    while (serviced_count < 2) {
        req.service([&](int v) {
            ++serviced_count;
            return v * 2;
        });
        std::this_thread::yield();
    }
    a.join();
    b.join();

    // Each caller got exactly its own doubled value -- neither the other's, and
    // no swallowed/duplicated result.
    const int va = ra.load();
    const int vb = rb.load();
    EXPECT_TRUE((va == 20 && vb == 40) || (va == 40 && vb == 20))
        << "va=" << va << " vb=" << vb;
}

TEST(Request, MoveOnlyResultRoundTrips)
{
    // The shape bind_asset_graph needs later: a move-only payload crossing the
    // channel by move, never copied.
    Request<int, std::unique_ptr<int>> req;
    std::unique_ptr<int> result;

    std::thread caller([&] {
        auto o = req.call(7);
        if (o.serviced) {
            result = std::move(o.value);
        }
    });

    bool done = false;
    service_until_done(req, done, [&](int a) {
        done = true;
        return std::make_unique<int>(a * 3);
    });
    caller.join();

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, 21);
}

TEST(Request, ServiceThatThrowsWakesTheCallerAsUnserviced)
{
    // A throwing service fn (the case bind_asset_graph's binder can hit) must not
    // leave the caller parked: it wakes as serviced=false -- NEVER a default
    // value that could read as success -- and the exception still propagates out
    // of service() so the engine loop logs it.
    Request<int, int> req;
    RequestOutcome<int> out{ true, 123 };  // seeded non-default to prove it clears

    std::thread caller([&] { out = req.call(5); });

    bool threw = false;
    while (!threw) {
        try {
            req.service([](int) -> int { throw std::runtime_error("boom"); });
        }
        catch (const std::runtime_error&) {
            threw = true;  // the throw reached the servicer, as in the engine loop
        }
        std::this_thread::yield();
    }
    caller.join();

    EXPECT_FALSE(out.serviced);
    EXPECT_EQ(out.value, 0);
}

// ── Request: deferred (cross-thread) completion — claim() + publish() ────────
// The #305 step 4b / #300 layer 2 split: the engine thread claim()s a request,
// and a DIFFERENT thread (the IO lane) publish()es the result once the blocking
// work finishes. These pin that the caller wakes with the right value no matter
// which thread finished, and that the abandon()/no-op edges hold.

TEST(Request, ClaimThenPublishOnAnotherThreadDeliversTheResult)
{
    Request<int, int> req;
    RequestOutcome<int> out;

    std::thread caller([&] { out = req.call(9); });

    // Engine thread claims and takes the payload...
    int payload = 0;
    while (!req.claim(payload)) {
        std::this_thread::yield();
    }
    // ...and a separate thread finishes the work and publishes.
    std::thread finisher([&] { req.publish(payload * 5); });
    finisher.join();
    caller.join();

    EXPECT_TRUE(out.serviced);
    EXPECT_EQ(out.value, 45);
}

TEST(Request, ClaimWithNoPendingRequestReturnsFalseAndLeavesTheArgUntouched)
{
    Request<int, int> req;
    int a = 77;
    EXPECT_FALSE(req.claim(a));
    EXPECT_EQ(a, 77);  // no request to take -- the destination is not written
}

TEST(Request, PublishWithoutAClaimIsANoOpAndDoesNotWedgeTheNextCycle)
{
    Request<int, int> req;
    req.publish(5);  // nothing is InFlight -- must change nothing

    // A normal claim/publish cycle still works right afterwards.
    RequestOutcome<int> out;
    std::thread caller([&] { out = req.call(2); });
    int a = 0;
    while (!req.claim(a)) {
        std::this_thread::yield();
    }
    req.publish(a + 100);
    caller.join();

    EXPECT_TRUE(out.serviced);
    EXPECT_EQ(out.value, 102);
}

TEST(Request, AbandonWhileClaimedStillDeliversTheLaterPublish)
{
    // A claim()ed request is InFlight: the servicer owns the payload, so an
    // abandon() (teardown) landing mid-flight must NOT reclaim it underneath them.
    // The caller stays parked and wakes on the publish the servicer still makes --
    // the same handover service() gives for an abandon during its own fn, now
    // across threads.
    Request<int, int> req;
    RequestOutcome<int> out{ false, -1 };

    std::thread caller([&] { out = req.call(3); });
    int a = 0;
    while (!req.claim(a)) {
        std::this_thread::yield();
    }

    req.abandon();       // teardown lands while the work is in flight
    req.publish(a * 7);  // the servicer finishes anyway

    caller.join();

    EXPECT_TRUE(out.serviced)
        << "an abandon mid-flight must not swallow the servicer's publish";
    EXPECT_EQ(out.value, 21);
}
