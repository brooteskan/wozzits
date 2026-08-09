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
}
