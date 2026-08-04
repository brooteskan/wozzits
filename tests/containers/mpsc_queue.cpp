#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <algorithm>
#include <atomic>
#include <random>
#include <chrono>
#include <cstdlib>

#include <containers/mpsc_queue.h>

#include "logging_test_harness.h"

using namespace wz::core::containers;

namespace
{
    class MPSCQueueTest : public ::testing::Test
    {
    protected:
        MPSCQueue<uint64_t> queue;

        std::mutex output_mutex;
        std::vector<uint64_t> output;

        void push_output(uint64_t v)
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            output.push_back(v);
        }

        void drain_queue()
        {
            uint64_t value;
            while (queue.try_pop(value))
            {
                push_output(value);
            }
        }

        std::vector<uint64_t> get_output()
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            return output;
        }
    };

    // The long concurrency soaks below are enabled (not DISABLED_) so they show
    // up and can be run, but they are gated behind an env var so the default
    // ctest run stays fast: absent WZ_RUN_STRESS they GTEST_SKIP (visible), and
    // they run when it is set. This is the "label" the issue asks for instead of
    // DISABLED_ -- concurrency defects hid here precisely because the stress
    // tests were switched off. See issue #328.
    // getenv_s, not the deprecated getenv: the build avoids a target-wide
    // _CRT_SECURE_NO_WARNINGS on purpose (CMakeLists.txt) and controller_win32
    // reads env the same way.
    bool stress_enabled()
    {
        char buf[16] = {};
        size_t len = 0;
        if (getenv_s(&len, buf, sizeof(buf), "WZ_RUN_STRESS") != 0 || len == 0)
            return false;
        return buf[0] != '\0' && buf[0] != '0';
    }

    // Soak duration for ChaosStress, overridable so a full soak is available
    // without editing the test. Defaults to a short run when opted in.
    int stress_seconds(int fallback)
    {
        char buf[16] = {};
        size_t len = 0;
        if (getenv_s(&len, buf, sizeof(buf), "WZ_STRESS_SECONDS") != 0 || len == 0)
            return fallback;
        int parsed = std::atoi(buf);
        return parsed > 0 ? parsed : fallback;
    }
}

TEST(ThreadTestHarness, NullHarnessTest)
{
    ThreadTestHarness harness;

    const int threads = 8;
    std::atomic<int> counter{0};

    harness.spawn(threads, [&](int)
                  { counter.fetch_add(1, std::memory_order_relaxed); });

    // Threads should be waiting at the start barrier
    EXPECT_EQ(counter.load(), 0);

    harness.start();
    harness.join_all();

    // After start + join, all threads must have run exactly once
    EXPECT_EQ(counter.load(), threads);
}

TEST(ThreadTestHarness, BarrierCorrectness)
{
    ThreadTestHarness harness;

    const int threads = 16;

    std::atomic<int> entered{0};
    std::atomic<int> finished{0};

    harness.spawn(threads, [&](int)
                  {
        entered.fetch_add(1, std::memory_order_relaxed);

        // simulate work
        std::this_thread::yield();

        finished.fetch_add(1, std::memory_order_relaxed); });

    // Give threads time to start and block on the barrier
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // If the harness is correct, none should have entered yet
    EXPECT_EQ(entered.load(), 0);
    EXPECT_EQ(finished.load(), 0);

    harness.start();
    harness.join_all();

    EXPECT_EQ(entered.load(), threads);
    EXPECT_EQ(finished.load(), threads);
}

TEST_F(MPSCQueueTest, MultipleProducers)
{
    ThreadTestHarness harness;

    const int producer_count = 8;
    const int items_per_thread = 1000;

    harness.spawn(producer_count, [&](int id)
                  {
        for (int i = 0; i < items_per_thread; ++i)
        {
            queue.push(id * items_per_thread + i);
        } });

    harness.start();
    harness.join_all();

    drain_queue();

    auto result = get_output();

    EXPECT_EQ(result.size(), producer_count * items_per_thread);
}

TEST_F(MPSCQueueTest, SingleThreadPushPop)
{
    ThreadTestHarness harness;

    queue.push(1);
    queue.push(2);
    queue.push(3);

    drain_queue();

    auto out = get_output();
    EXPECT_EQ(out.size(), 3);
    EXPECT_EQ(out[0], 1);
    EXPECT_EQ(out[1], 2);
    EXPECT_EQ(out[2], 3);
}

TEST_F(MPSCQueueTest, MultiProducerCorrectness)
{
    ThreadTestHarness harness;

    const int num_threads = 8;
    const int items_per_thread = 1000;

    harness.spawn(num_threads, [&](int t)
                  {
        for (int i = 0; i < items_per_thread; ++i)
        {
            queue.push(t * 100000 + i);
        } });

    harness.start();
    harness.join_all();

    drain_queue();

    auto out = get_output();
    EXPECT_EQ(out.size(), num_threads * items_per_thread);

    std::unordered_set<uint64_t> seen;

    for (auto v : out)
    {
        EXPECT_TRUE(seen.insert(v).second) << "Duplicate: " << v;
    }
}

TEST_F(MPSCQueueTest, FIFOApproximationSingleConsumer)
{
    const int n = 10000;

    for (int i = 0; i < n; ++i)
        queue.push(i);

    drain_queue();

    auto out = get_output();
    EXPECT_EQ(out.size(), n);

    for (int i = 1; i < n; ++i)
    {
        EXPECT_NE(out[i], out[i - 1]);
    }
}

// Fast enough (~200ms) to stay in the default suite ungated, so every run
// exercises real 16-thread contention rather than switching it off.
TEST_F(MPSCQueueTest, HighContentionStress)
{
    ThreadTestHarness harness;

    const int threads = 16;
    const int per_thread = 5000;

    harness.spawn(threads, [&](int t)
                  {
        for (int i = 0; i < per_thread; ++i)
        {
            queue.push(t * 100000 + i);
        } });

    harness.start();
    harness.join_all();

    drain_queue();

    auto out = get_output();
    EXPECT_EQ(out.size(), threads * per_thread);

    std::unordered_set<uint64_t> seen;
    for (auto v : out)
        EXPECT_TRUE(seen.insert(v).second);
}

TEST_F(MPSCQueueTest, EmptyQueueBehavior)
{
    uint64_t v = -1;
    EXPECT_FALSE(queue.try_pop(v));
}

// Defect 3: MPSCQueue is unbounded, MPSCRingBuffer is not, and their names do
// not say so. Push far past any ring-buffer capacity and assert every element
// is retrievable in order -- a characterisation test that pins the intended
// difference so a future reader cannot mistake this container for the bounded
// one. (MPSCRingBuffer's own tests cap at capacity 1024 and expect try_push to
// return false when full; here try_push must never refuse for fullness.)
TEST_F(MPSCQueueTest, PushAllocatesWithoutBound)
{
    const uint64_t n = 100000; // >> MPSCRingBuffer capacity (1024)

    for (uint64_t i = 0; i < n; ++i)
        ASSERT_TRUE(queue.try_push(i)) << "MPSCQueue refused a push at " << i;

    drain_queue();

    auto out = get_output();
    ASSERT_EQ(out.size(), n);

    for (uint64_t i = 0; i < n; ++i)
        EXPECT_EQ(out[i], i);
}

// Defect 2, told as the caller-side mistake it causes (cross-ref #313 B4-S1 on
// the sibling MPSCRingBuffer): try_push publishes in two steps -- tail.exchange
// links the node into the tail, then prev->next.store makes it reachable from
// head. Between those stores the element is genuinely queued but invisible to
// the consumer, so try_pop returns false. A drain loop that stops the first
// time try_pop returns false therefore drops a push that is merely in flight.
//
// The push seam makes the transient deterministic: the producer of element 2 is
// parked exactly inside that window while a fully-published element 1 sits ahead
// of it. The naive drain gets 1 and stops one short; element 2 was there the
// whole time, proven by releasing the producer and popping it.
TEST(MPSCQueueInFlight, DrainingUntilTryPopFailsCanLoseElements)
{
    MPSCQueue<uint64_t> q;

    q.push(1); // element 1: fully published, visible

    std::atomic<bool> parked{false};
    std::atomic<bool> release{false};

    // Park the producer of element 2 between tail.exchange and prev->next.store.
    q.test_hook_push_after_tail_exchange = [&]
    {
        parked.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire))
            std::this_thread::yield();
    };

    std::thread producer([&]
                         { q.push(2); });

    while (!parked.load(std::memory_order_acquire))
        std::this_thread::yield();

    // The caller mistake: drain until try_pop first returns false.
    std::vector<uint64_t> drained;
    uint64_t v;
    while (q.try_pop(v))
        drained.push_back(v);

    // Got 1, stopped. Element 2 is in flight and invisible -- lost by this loop
    // even though it is genuinely queued.
    ASSERT_EQ(drained.size(), 1u);
    EXPECT_EQ(drained[0], 1u);

    // It really was queued: release the producer and it pops.
    release.store(true, std::memory_order_release);
    producer.join();
    q.test_hook_push_after_tail_exchange = nullptr;

    ASSERT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, 2u);
    EXPECT_FALSE(q.try_pop(v));
}

// Defect 1: empty() must be safe from a thread that is NOT the sole consumer.
// The old body loaded head and then dereferenced head->next, racing try_pop's
// `delete head_node` -- a heap-use-after-free. The head==tail body dereferences
// no node and is safe from any thread.
//
// This exercises that path: a non-consumer thread (this one) hammers empty()
// while a consumer pops and frees nodes. On a plain build it passes either way,
// because a use-after-free usually reads still-mapped memory by luck -- the
// failure is only *reliable* under AddressSanitizer. It was made red first out
// of tree: under ASan the old body aborts in empty() (memory freed in try_pop)
// and the head==tail body runs clean (see the commit message for the harness).
TEST(MPSCQueueEmptyContract, EmptyIsSafeFromAProducerThreadWhileTheConsumerPops)
{
    MPSCQueue<uint64_t> q;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> calls{0};

    std::thread consumer([&]
                         {
        uint64_t v;
        while (!stop.load(std::memory_order_acquire))
            q.try_pop(v); });

    std::thread producer([&]
                         {
        uint64_t i = 0;
        while (!stop.load(std::memory_order_acquire))
            q.try_push(i++); });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < deadline)
    {
        (void)q.empty();
        calls.fetch_add(1, std::memory_order_relaxed);
    }

    stop.store(true, std::memory_order_release);
    producer.join();
    consumer.join();

    // The real assertion is "reached here without a use-after-free". empty()'s
    // exact result is a racy snapshot under contention and is not asserted.
    EXPECT_GT(calls.load(), 0u);
}

// Defect 2 under the head==tail empty(): while a push is in flight -- tail
// advanced by exchange, prev->next not yet stored -- the element is invisible
// to the consumer's try_pop (it cannot be popped yet), but empty() reports the
// queue as NON-empty, because tail has moved past head. empty() and try_pop
// deliberately disagree here, and empty() is the one telling the truth about
// outstanding work -- which is why a drain loops until empty(), not until
// try_pop first fails.
TEST(MPSCQueueInFlight, APushInFlightIsInvisibleToTheConsumer)
{
    MPSCQueue<uint64_t> q;

    std::atomic<bool> parked{false};
    std::atomic<bool> release{false};

    q.test_hook_push_after_tail_exchange = [&]
    {
        parked.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire))
            std::this_thread::yield();
    };

    std::thread producer([&]
                         { q.push(42); });

    while (!parked.load(std::memory_order_acquire))
        std::this_thread::yield();

    // Push in flight: element 42 is queued (tail points at it) but not reachable
    // from head yet.
    uint64_t v = 0;
    EXPECT_FALSE(q.try_pop(v)); // invisible to the consumer's pop
    EXPECT_FALSE(q.empty());    // but empty() sees it: NOT empty

    release.store(true, std::memory_order_release);
    producer.join();
    q.test_hook_push_after_tail_exchange = nullptr;

    EXPECT_FALSE(q.empty());
    ASSERT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, 42u);
    EXPECT_TRUE(q.empty()); // now genuinely empty
}

TEST_F(MPSCQueueTest, ConcurrentProducerConsumer)
{
    const int producers = 2;
    const int per_thread = 5;

    std::atomic<bool> start{false};
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    std::vector<std::thread> threads;

    // Consumer
    std::thread consumer([&]()
                         {
            while (!start.load()) {}

            uint64_t value;
            while (consumed.load() < producers * per_thread)
            {
                if (queue.try_pop(value))
                {
                    consumed++;
                }
            } });

    // Producers
    for (int t = 0; t < producers; ++t)
    {
        threads.emplace_back([&, t]()
                             {
                while (!start.load()) {}

                for (int i = 0; i < per_thread; ++i)
                {
                    queue.push(t * 100000 + i);
                    produced++;
                } });
    }

    start = true;

    for (auto &th : threads)
        th.join();

    consumer.join();

    EXPECT_EQ(produced.load(), producers * per_thread);
    EXPECT_EQ(consumed.load(), producers * per_thread);
}

TEST_F(MPSCQueueTest, FrameDrivenStress)
{
    if (!stress_enabled())
        GTEST_SKIP() << "stress soak (~4s); set WZ_RUN_STRESS=1 to run";

    const int producers = 8;
    const int per_thread = 200000;
    const int total = producers * per_thread;

    std::atomic<int> produced{0};

    std::vector<std::thread> threads;

    // Frame buffer (simulates EventBuffer in engine)
    std::vector<int> frame_events;
    frame_events.reserve(1024);

    // -------------------------
    // PRODUCERS
    // -------------------------
    for (int t = 0; t < producers; ++t)
    {
        threads.emplace_back([&, t]()
                             {
            for (int i = 0; i < per_thread; ++i)
            {
                int value = t * per_thread + i;
                queue.push(value);

                produced.fetch_add(1, std::memory_order_relaxed);

                if ((i & 255) == 0)
                    std::this_thread::yield();
            } });
    }

    // -------------------------
    // ENGINE LOOP (FRAME DRIVEN)
    // -------------------------
    std::vector<int> all_events;
    all_events.reserve(total);

    while (true)
    {
        // simulate frame start
        frame_events.clear();

        // drain queue (THIS is your engine boundary)
        uint64_t value;
        while (queue.try_pop(value))
        {
            frame_events.push_back(value);
        }

        // "process frame"
        for (int v : frame_events)
        {
            all_events.push_back(v);
        }

        // termination condition:
        // all producers done AND queue drained
        bool all_produced =
            produced.load(std::memory_order_acquire) == total;

        bool queue_empty = queue.empty();

        if (all_produced && queue_empty)
            break;

        std::this_thread::yield();
    }

    // join producers
    for (auto &th : threads)
        th.join();

    // -------------------------
    // VALIDATION
    // -------------------------
    ASSERT_EQ(all_events.size(), total);

    std::unordered_set<int> seen;
    seen.reserve(total);

    for (uint64_t v : all_events)
    {
        EXPECT_TRUE(seen.insert(v).second)
            << "Duplicate: " << v;
    }

    EXPECT_EQ(seen.size(), total);
}

TEST_F(MPSCQueueTest, ChaosStress)
{
    if (!stress_enabled())
        GTEST_SKIP() << "stress soak; set WZ_RUN_STRESS=1 to run (WZ_STRESS_SECONDS overrides length)";

    const int producers = 128;
    const int duration_seconds = stress_seconds(20);

    std::atomic<bool> running{true};
    std::atomic<uint64_t> produced{0};
    std::atomic<uint64_t> consumed{0};

    std::vector<std::thread> threads;

    // Per-producer sequence tracking (must persist across entire run)
    std::vector<int64_t> last_seen(producers, -1);

    std::atomic<int> violations{0};

    // Consumer
    std::thread consumer([&]()
                         {
        uint64_t value;

        while (running.load(std::memory_order_acquire))
        {
            if (queue.try_pop(value))
            {
                consumed.fetch_add(1, std::memory_order_relaxed);

                uint64_t producer = value >> 48;
                uint64_t seq = value & 0x0000FFFFFFFFFFFFULL;

                if (last_seen[producer] != -1)
                {
                    if (seq != static_cast<uint64_t>(last_seen[producer] + 1))
                        violations.fetch_add(1, std::memory_order_relaxed);
                }

                last_seen[producer] = seq;
            }
            else
            {
                std::this_thread::yield();
            }
        }

        // Drain remaining items
        while (queue.try_pop(value))
        {
            consumed.fetch_add(1, std::memory_order_relaxed);

            uint64_t producer = value >> 48;
            uint64_t seq = value & 0x0000FFFFFFFFFFFFULL;

            if (last_seen[producer] != -1)
            {
                if (seq != static_cast<uint64_t>(last_seen[producer] + 1))
                    violations.fetch_add(1, std::memory_order_relaxed);
            }

            last_seen[producer] = seq;
        } });

    // Producers
    for (int t = 0; t < producers; ++t)
    {
        threads.emplace_back([&, t]()
                             {
            std::mt19937 rng(t + 1234);
            std::uniform_int_distribution<int> spin(0, 50);

            uint64_t local = 0;

            while (running.load(std::memory_order_acquire))
            {
                int burst = spin(rng);

                for (int i = 0; i < burst; ++i)
                {
                    uint64_t v = ((uint64_t)t << 48) | local++;

                    queue.push(v);
                    produced.fetch_add(1, std::memory_order_relaxed);
                }

                if (spin(rng) < 5)
                    std::this_thread::yield();
            } });
    }

    std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));

    running.store(false, std::memory_order_release);

    for (auto &th : threads)
        th.join();

    consumer.join();

    uint64_t p = produced.load();
    uint64_t c = consumed.load();

    EXPECT_EQ(p, c);

    EXPECT_EQ(violations.load(), 0);
}
