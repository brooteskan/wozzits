// tests/containers/work_stealing_deque.cpp
//
// Coverage for the S1 work-stealing deque (#293): the owner's LIFO push/pop, the
// thief's FIFO steal, and — the property the pool relies on — that under a
// concurrent owner (push + pop) racing several thieves, every pushed item is
// consumed EXACTLY once: no loss, no duplication.

#include <gtest/gtest.h>

#include <containers/work_stealing_deque.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace
{
    using wz::core::containers::WorkStealingDeque;

    // Thief count scaled to the machine instead of pinned at 4. Four is wrong in
    // both directions: on a many-core box the deque barely contends, so the
    // pop-vs-steal race this suite exists to hammer goes largely unexercised; on a
    // 2-core box four thieves plus the owner oversubscribe and the test mostly
    // measures the OS scheduler. Floored at 2 so the multi-thief path always runs,
    // capped so a 64-core machine does not spawn 63 spinning threads per test.
    int thief_count()
    {
        const unsigned hw = std::thread::hardware_concurrency();
        const unsigned thieves = hw > 1u ? hw - 1u : 1u;
        return static_cast<int>(std::clamp(thieves, 2u, 8u));
    }

    TEST(WorkStealingDeque, OwnerPopsLifo)
    {
        WorkStealingDeque<int> d;
        d.push(1);
        d.push(2);
        d.push(3);
        EXPECT_EQ(d.pop().value(), 3);
        EXPECT_EQ(d.pop().value(), 2);
        EXPECT_EQ(d.pop().value(), 1);
        EXPECT_FALSE(d.pop().has_value());
    }

    TEST(WorkStealingDeque, ThiefStealsFifo)
    {
        WorkStealingDeque<int> d;
        d.push(1);
        d.push(2);
        d.push(3);
        EXPECT_EQ(d.steal().value(), 1);
        EXPECT_EQ(d.steal().value(), 2);
        EXPECT_EQ(d.steal().value(), 3);
        EXPECT_FALSE(d.steal().has_value());
    }

    TEST(WorkStealingDeque, EmptyReturnsNullopt)
    {
        WorkStealingDeque<int> d;
        EXPECT_FALSE(d.pop().has_value());
        EXPECT_FALSE(d.steal().has_value());
    }

    // empty() itself had no test reference at all -- and the pool checks it on
    // every park decision, so a wrong answer there is a worker that sleeps on a
    // non-empty deque (lost throughput) or spins on an empty one. Approximate
    // under concurrency by contract, so this pins it only quiescently, which is
    // exactly how the pool uses it (re-checked under the wake lock).
    TEST(WorkStealingDeque, EmptyTracksPushAndDrainWhenQuiescent)
    {
        WorkStealingDeque<int> d;
        EXPECT_TRUE(d.empty());

        d.push(1);
        EXPECT_FALSE(d.empty());
        d.push(2);
        EXPECT_FALSE(d.empty());

        EXPECT_EQ(d.pop().value(), 2);
        EXPECT_FALSE(d.empty()) << "one item still queued";

        EXPECT_EQ(d.steal().value(), 1);
        EXPECT_TRUE(d.empty()) << "drained by a steal, not just by pop";

        // ...and it recovers rather than latching once drained.
        d.push(3);
        EXPECT_FALSE(d.empty());
        EXPECT_EQ(d.pop().value(), 3);
        EXPECT_TRUE(d.empty());
    }

    // One owner pushes 0..N-1 (interleaving pops so owner-pop races thief-steal);
    // thief_count() thieves steal. Every value must be consumed exactly once
    // across all threads. A correct deque conserves items, so `consumed` reaches N and the
    // union of every thread's take is exactly {0..N-1}.
    TEST(WorkStealingDeque, ConcurrentPopAndStealConserveEveryItem)
    {
        constexpr int N = 20000;
        const int kThieves = thief_count();

        WorkStealingDeque<int> d;
        std::atomic<int> consumed{ 0 };

        // Each thread writes only its own slot (0 = owner, 1.. = thieves).
        std::vector<std::vector<int>> got(1 + kThieves);

        auto thief = [&](int slot)
        {
            std::vector<int> &out = got[slot];
            while (consumed.load(std::memory_order_relaxed) < N)
            {
                if (std::optional<int> v = d.steal())
                {
                    out.push_back(*v);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        };

        std::vector<std::thread> thieves;
        for (int i = 0; i < kThieves; ++i)
            thieves.emplace_back(thief, 1 + i);

        std::vector<int> &owner_out = got[0];
        for (int i = 0; i < N; ++i)
        {
            d.push(i);
            if ((i & 3) == 0)
            {
                if (std::optional<int> v = d.pop())
                {
                    owner_out.push_back(*v);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        while (consumed.load(std::memory_order_relaxed) < N)
        {
            if (std::optional<int> v = d.pop())
            {
                owner_out.push_back(*v);
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        }

        for (std::thread &t : thieves)
            t.join();

        std::vector<int> all;
        for (const std::vector<int> &g : got)
            all.insert(all.end(), g.begin(), g.end());
        std::sort(all.begin(), all.end());

        ASSERT_EQ(all.size(), static_cast<std::size_t>(N));
        for (int i = 0; i < N; ++i)
            ASSERT_EQ(all[i], i);
    }

    // ── Array growth ────────────────────────────────────────────────────────────
    // Push well past the ring's initial capacity (256), forcing several doublings,
    // then drain: the grown ring must preserve every element and its order. Popping
    // (owner, LIFO) and stealing (thief, FIFO) both confirm the copy on resize keeps
    // each element at its logical index.

    TEST(WorkStealingDeque, GrowsPastInitialCapacityAndPopsLifo)
    {
        constexpr int N = 1000;  // > 256 → at least two doublings
        WorkStealingDeque<int> d;
        for (int i = 0; i < N; ++i)
            d.push(i);
        for (int i = N - 1; i >= 0; --i)
        {
            std::optional<int> v = d.pop();
            ASSERT_TRUE(v.has_value());
            ASSERT_EQ(*v, i);
        }
        EXPECT_FALSE(d.pop().has_value());
    }

    TEST(WorkStealingDeque, GrowsPastInitialCapacityAndStealsFifo)
    {
        constexpr int N = 1000;
        WorkStealingDeque<int> d;
        for (int i = 0; i < N; ++i)
            d.push(i);
        for (int i = 0; i < N; ++i)
        {
            std::optional<int> v = d.steal();
            ASSERT_TRUE(v.has_value());
            ASSERT_EQ(*v, i);  // FIFO order survives the resizes
        }
        EXPECT_FALSE(d.steal().has_value());
    }

    // ── Last-element boundary / ABA ──────────────────────────────────────────────
    // The one contended case: a single element that pop (owner) and steal (thief)
    // both target. Exactly one may take it. The `top` CAS is what arbitrates, and
    // because `top` only ever increases, a thief that read a now-stale slot value
    // cannot mistake a recycled index for its own (ABA-free).

    TEST(WorkStealingDeque, StealTakesTheLastElementThenPopSeesEmpty)
    {
        WorkStealingDeque<int> d;
        d.push(7);
        EXPECT_EQ(d.steal().value(), 7);
        EXPECT_FALSE(d.pop().has_value());   // pop must see empty, not double-take
        EXPECT_FALSE(d.steal().has_value());
    }

    // Deterministic version of the race, via the steal() test hook: a thief reads
    // the last element's slot and parks BEFORE its CAS; the owner then pops that same
    // element and, because the thief has not yet advanced `top`, wins the CAS. When
    // the thief resumes, its CAS sees `top` already moved and aborts. The element is
    // taken exactly once (by the owner), and the thief's stale read is discarded --
    // the precise ABA-prevention path.
    TEST(WorkStealingDeque, PopWinsTheLastElementAgainstAnInFlightSteal)
    {
        WorkStealingDeque<int> d;
        d.push(42);

        std::atomic<bool> thief_parked{ false };
        std::atomic<bool> release_thief{ false };

        d.test_hook_steal_after_read = [&]
        {
            thief_parked.store(true, std::memory_order_release);
            while (!release_thief.load(std::memory_order_acquire))
                std::this_thread::yield();
        };

        std::optional<int> stolen;
        std::thread thief([&] { stolen = d.steal(); });

        while (!thief_parked.load(std::memory_order_acquire))
            std::this_thread::yield();

        // Owner pops the last element while the thief is parked mid-steal.
        std::optional<int> popped = d.pop();

        release_thief.store(true, std::memory_order_release);
        thief.join();

        EXPECT_EQ(popped.value(), 42);     // owner won the CAS
        EXPECT_FALSE(stolen.has_value());  // thief's CAS failed → aborted
        EXPECT_FALSE(d.pop().has_value()); // and the deque is empty
        EXPECT_FALSE(d.steal().has_value());
    }

    // GROWTH UNDER AN IN-FLIGHT STEAL. The header promises that a thief holding a
    // STALE (pre-grow) array pointer is still safe, "because the value at index t
    // was copied into the new array, and old arrays are retained until
    // destruction". Nothing tested it: the after_read hook fires once the slot has
    // already been read, which is past the window. test_hook_steal_after_array_load
    // parks the thief in the right place -- between loading the array and reading
    // out of it -- so the owner can grow underneath it deterministically.
    //
    // If old arrays were freed on grow (the obvious "optimisation"), this is a
    // use-after-free; if the live range were moved rather than copied, the thief
    // reads a hole. Both come out as a wrong or missing value here.
    TEST(WorkStealingDeque, StealSurvivesTheOwnerGrowingTheArrayUnderneathIt)
    {
        WorkStealingDeque<int> d;

        // Enough to make the next pushes force at least one grow, whatever the
        // initial capacity is.
        constexpr int kSeed = 8;
        for (int i = 0; i < kSeed; ++i)
            d.push(i);

        std::atomic<bool> thief_parked{ false };
        std::atomic<bool> release_thief{ false };

        d.test_hook_steal_after_array_load = [&]
        {
            thief_parked.store(true, std::memory_order_release);
            while (!release_thief.load(std::memory_order_acquire))
                std::this_thread::yield();
        };

        std::optional<int> stolen;
        std::thread thief([&] { stolen = d.steal(); });

        while (!thief_parked.load(std::memory_order_acquire))
            std::this_thread::yield();

        // The thief now holds the OLD array pointer and has not read from it.
        // Grow well past the initial capacity underneath it.
        for (int i = kSeed; i < 4096; ++i)
            d.push(i);

        release_thief.store(true, std::memory_order_release);
        thief.join();

        // It stole the OLDEST item (FIFO), read through the stale array, and got
        // the right value rather than garbage or a fault.
        ASSERT_TRUE(stolen.has_value())
            << "the steal must complete across a concurrent grow";
        EXPECT_EQ(stolen.value(), 0);

        // ...and nothing else was lost or duplicated by the grow: 1..4095 remain,
        // each exactly once.
        d.test_hook_steal_after_array_load = nullptr;
        std::vector<int> rest;
        while (auto v = d.pop())
            rest.push_back(*v);
        ASSERT_EQ(rest.size(), 4095u);
        std::sort(rest.begin(), rest.end());
        for (std::size_t i = 0; i < rest.size(); ++i)
            ASSERT_EQ(rest[i], static_cast<int>(i) + 1) << "at index " << i;
    }

    // Keep the deque hovering at 0–1 items so almost every take is the last-element
    // pop-vs-steal CAS race — the ABA-prone boundary — under real thread contention.
    // Every value must still be consumed exactly once.
    TEST(WorkStealingDeque, NearEmptyConcurrentStressConservesEveryItem)
    {
        constexpr int N = 50000;
        const int kThieves = thief_count();

        WorkStealingDeque<int> d;
        std::atomic<int> consumed{ 0 };
        std::vector<std::vector<int>> got(1 + kThieves);

        auto thief = [&](int slot)
        {
            std::vector<int> &out = got[slot];
            while (consumed.load(std::memory_order_relaxed) < N)
                if (std::optional<int> v = d.steal())
                {
                    out.push_back(*v);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                }
        };

        std::vector<std::thread> thieves;
        for (int i = 0; i < kThieves; ++i)
            thieves.emplace_back(thief, 1 + i);

        std::vector<int> &owner_out = got[0];
        for (int i = 0; i < N; ++i)
        {
            d.push(i);
            // Immediately try to reclaim it, so the deque stays at the boundary.
            if (std::optional<int> v = d.pop())
            {
                owner_out.push_back(*v);
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        }
        while (consumed.load(std::memory_order_relaxed) < N)
            if (std::optional<int> v = d.pop())
            {
                owner_out.push_back(*v);
                consumed.fetch_add(1, std::memory_order_relaxed);
            }

        for (std::thread &t : thieves)
            t.join();

        std::vector<int> all;
        for (const std::vector<int> &g : got)
            all.insert(all.end(), g.begin(), g.end());
        std::sort(all.begin(), all.end());

        ASSERT_EQ(all.size(), static_cast<std::size_t>(N));
        for (int i = 0; i < N; ++i)
            ASSERT_EQ(all[i], i);
    }

    // ── Wide (lock-based-atomic) element type ────────────────────────────────────
    // The pool's real element is Task (~40 bytes), so its slots are a std::atomic<T>
    // WIDER than any lock-free width -- the standard library services those through
    // its per-address lock pool, a different code path from the lock-free
    // std::atomic<int> the tests above exercise. This mirrors that width with a
    // 40-byte struct and checks, under concurrent owner/thieves, that every element
    // is consumed exactly once AND arrives INTACT (no torn read splicing two
    // elements' halves) -- the property that lock-based atomic slot access must hold.
    struct WideValue
    {
        std::int64_t id;
        std::int64_t neg;  // -id - 1  (so the all-zero default is not a valid id=0)
        std::int64_t mix;  // id * 131 + 7
        std::int64_t tag;  // 0xC0FFEE
        std::int64_t sum;  // id + neg + mix + tag
    };
    static_assert(sizeof(WideValue) == 40,
                  "WideValue is meant to match Task's width (a lock-based atomic).");

    static WideValue make_wide(std::int64_t id)
    {
        WideValue v;
        v.id = id;
        v.neg = -id - 1;
        v.mix = id * 131 + 7;
        v.tag = 0xC0FFEE;
        v.sum = v.id + v.neg + v.mix + v.tag;
        return v;
    }
    static bool wide_intact(const WideValue &v)
    {
        return v.neg == -v.id - 1 && v.mix == v.id * 131 + 7 && v.tag == 0xC0FFEE &&
               v.sum == v.id + v.neg + v.mix + v.tag;
    }

    TEST(WorkStealingDeque, WideElementsConcurrentlyConservedAndIntact)
    {
        constexpr int N = 20000;
        const int kThieves = thief_count();

        WorkStealingDeque<WideValue> d;
        std::atomic<int> consumed{ 0 };
        std::vector<std::vector<WideValue>> got(1 + kThieves);

        auto thief = [&](int slot)
        {
            std::vector<WideValue> &out = got[slot];
            while (consumed.load(std::memory_order_relaxed) < N)
                if (std::optional<WideValue> v = d.steal())
                {
                    out.push_back(*v);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                }
        };

        std::vector<std::thread> thieves;
        for (int i = 0; i < kThieves; ++i)
            thieves.emplace_back(thief, 1 + i);

        std::vector<WideValue> &owner_out = got[0];
        for (int i = 0; i < N; ++i)
        {
            d.push(make_wide(i));
            if ((i & 3) == 0)
                if (std::optional<WideValue> v = d.pop())
                {
                    owner_out.push_back(*v);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                }
        }
        while (consumed.load(std::memory_order_relaxed) < N)
            if (std::optional<WideValue> v = d.pop())
            {
                owner_out.push_back(*v);
                consumed.fetch_add(1, std::memory_order_relaxed);
            }

        for (std::thread &t : thieves)
            t.join();

        std::vector<int> ids;
        for (const std::vector<WideValue> &g : got)
            for (const WideValue &v : g)
            {
                ASSERT_TRUE(wide_intact(v)) << "torn/garbage element id=" << v.id;
                ids.push_back(static_cast<int>(v.id));
            }
        std::sort(ids.begin(), ids.end());
        ASSERT_EQ(ids.size(), static_cast<std::size_t>(N));
        for (int i = 0; i < N; ++i)
            ASSERT_EQ(ids[i], i);
    }
}
