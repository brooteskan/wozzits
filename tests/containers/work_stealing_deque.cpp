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

    // One owner pushes 0..N-1 (interleaving pops so owner-pop races thief-steal);
    // four thieves steal. Every value must be consumed exactly once across all
    // threads. A correct deque conserves items, so `consumed` reaches N and the
    // union of every thread's take is exactly {0..N-1}.
    TEST(WorkStealingDeque, ConcurrentPopAndStealConserveEveryItem)
    {
        constexpr int N = 20000;
        constexpr int kThieves = 4;

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

    // Keep the deque hovering at 0–1 items so almost every take is the last-element
    // pop-vs-steal CAS race — the ABA-prone boundary — under real thread contention.
    // Every value must still be consumed exactly once.
    TEST(WorkStealingDeque, NearEmptyConcurrentStressConservesEveryItem)
    {
        constexpr int N = 50000;
        constexpr int kThieves = 4;

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
        constexpr int kThieves = 4;

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
