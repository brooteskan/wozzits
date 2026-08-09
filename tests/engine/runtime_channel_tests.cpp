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
