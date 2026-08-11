// tests/tasks/fiber_backend_tests.cpp
//
// S2a coverage for the fibre backend seam (#293): a worker fibre can be switched
// to, passed data, switch back, and -- the property the S2 scheduler is built on
// -- RESUME mid-execution exactly where it last switched away. Fibres carry no
// captures, so state travels through the create_fiber `arg`.

#include <gtest/gtest.h>

#include <tasks/fiber_backend.h>

#include <cstddef>
#include <thread>

namespace
{
    using wz::tasks::convert_fiber_to_thread;
    using wz::tasks::convert_thread_to_fiber;
    using wz::tasks::create_fiber;
    using wz::tasks::current_fiber;
    using wz::tasks::destroy_fiber;
    using wz::tasks::Fiber;
    using wz::tasks::switch_to_fiber;

    // RAII for the thread<->fibre conversion. Every test here used to convert by
    // hand and convert back at the end, so an ASSERT firing in between returned
    // from the test with the THREAD STILL CONVERTED -- which is gtest's thread,
    // reused by every later test in this binary. One early failure would poison
    // the rest of the run and the reported failure would be the wrong test.
    struct ScopedThreadFiber
    {
        Fiber main;

        ScopedThreadFiber() : main(convert_thread_to_fiber()) {}
        ~ScopedThreadFiber()
        {
            if (main.valid()) {
                convert_fiber_to_thread();
            }
        }

        ScopedThreadFiber(const ScopedThreadFiber&) = delete;
        ScopedThreadFiber& operator=(const ScopedThreadFiber&) = delete;
    };

    // Same for a created fibre: destroyed even if the body of the test bails.
    struct ScopedFiber
    {
        Fiber fiber;

        ScopedFiber(std::size_t stack_size, wz::tasks::FiberEntry entry, void* arg)
            : fiber(create_fiber(stack_size, entry, arg))
        {
        }
        ~ScopedFiber()
        {
            if (fiber.valid()) {
                destroy_fiber(fiber);
            }
        }

        ScopedFiber(const ScopedFiber&) = delete;
        ScopedFiber& operator=(const ScopedFiber&) = delete;
    };

    struct RunOnce
    {
        Fiber main;
        int ran = 0;
        int received = 0;
    };

    void run_once_body(void* arg)
    {
        RunOnce* s = static_cast<RunOnce*>(arg);
        s->ran += 1;
        s->received = 42;
        switch_to_fiber(s->main);
        for (;;)  // never fall off the end of a fibre
            switch_to_fiber(s->main);
    }

    TEST(FiberBackend, SwitchRunsTheFiberAndReturns)
    {
        ScopedThreadFiber thread_fiber;
        ASSERT_TRUE(thread_fiber.main.valid());

        RunOnce s;
        s.main = thread_fiber.main;

        ScopedFiber worker(0, &run_once_body, &s);
        ASSERT_TRUE(worker.fiber.valid());

        switch_to_fiber(worker.fiber);  // runs run_once_body, which switches back

        EXPECT_EQ(s.ran, 1);
        EXPECT_EQ(s.received, 42);
        EXPECT_EQ(current_fiber().impl, s.main.impl);
    }

    struct Counting
    {
        Fiber main;
        int count = 0;
    };

    void counting_body(void* arg)
    {
        Counting* s = static_cast<Counting*>(arg);
        for (;;)
        {
            s->count += 1;
            switch_to_fiber(s->main);  // suspend here; resume on the next switch-in
        }
    }

    // The core property: each switch-in resumes AFTER the previous switch-out, so
    // the fibre's own stack/locals persist across suspensions.
    TEST(FiberBackend, ResumesWhereItLeftOff)
    {
        ScopedThreadFiber thread_fiber;
        Counting s;
        s.main = thread_fiber.main;

        ScopedFiber f(0, &counting_body, &s);
        ASSERT_TRUE(f.fiber.valid());

        switch_to_fiber(f.fiber);
        EXPECT_EQ(s.count, 1);
        switch_to_fiber(f.fiber);
        EXPECT_EQ(s.count, 2);
        switch_to_fiber(f.fiber);
        EXPECT_EQ(s.count, 3);
    }

    // ── explicit stack size ─────────────────────────────────────────────────
    //
    // Every test above passes stack_size 0 (the default), so the parameter was
    // never actually exercised -- a backend that ignored it, or mis-rounded it,
    // would pass the whole suite. The pool creates its fibres with a real size.

    struct DeepStack
    {
        Fiber main;
        std::size_t touched = 0;
    };

    void deep_stack_body(void* arg)
    {
        DeepStack* s = static_cast<DeepStack*>(arg);
        // A real frame on the FIBRE's stack, written and read back, so the test
        // fails on a stack that was not actually allocated at the requested size
        // rather than merely on a null handle.
        volatile unsigned char scratch[16 * 1024];
        for (std::size_t i = 0; i < sizeof(scratch); ++i) {
            scratch[i] = static_cast<unsigned char>(i & 0xFF);
        }
        std::size_t sum = 0;
        for (std::size_t i = 0; i < sizeof(scratch); ++i) {
            sum += scratch[i];
        }
        s->touched = sum;
        for (;;) {
            switch_to_fiber(s->main);
        }
    }

    TEST(FiberBackend, HonoursAnExplicitStackSize)
    {
        ScopedThreadFiber thread_fiber;
        DeepStack s;
        s.main = thread_fiber.main;

        ScopedFiber f(256 * 1024, &deep_stack_body, &s);
        ASSERT_TRUE(f.fiber.valid());

        switch_to_fiber(f.fiber);

        // sum of (i & 0xFF) over 16 KiB = 64 blocks of 0..255
        constexpr std::size_t kExpected = 64u * (255u * 256u / 2u);
        EXPECT_EQ(s.touched, kExpected)
            << "the fibre's own stack did not survive a real frame";
    }

    // ── cross-thread migration ──────────────────────────────────────────────
    //
    // THE shape behind the release-only SIGSEGV fixed in acedccc0: a fibre is
    // started on one worker, parks, and is resumed by a DIFFERENT worker. The pool
    // does this on every steal of a resumable fibre, and nothing here covered it --
    // both tests above run start-to-finish on the gtest thread.
    //
    // The fibre must resume on thread B exactly where it suspended on thread A,
    // with its own stack intact, and must return to whichever thread's main fibre
    // is currently switching it in (return_to is repointed before each switch, the
    // same way the scheduler hands a fibre its current worker's home fibre).

    struct Migrating
    {
        Fiber* return_to = nullptr;
        int stack_local_observed = 0;
        std::thread::id ran_on;
    };

    void migrating_body(void* arg)
    {
        Migrating* s = static_cast<Migrating*>(arg);
        int local_on_fiber_stack = 0;  // lives on the FIBRE's stack, not the struct
        for (;;) {
            local_on_fiber_stack += 1;
            s->stack_local_observed = local_on_fiber_stack;
            s->ran_on = std::this_thread::get_id();
            switch_to_fiber(*s->return_to);
        }
    }

    TEST(FiberBackend, ResumesOnADifferentThreadWithItsStackIntact)
    {
        Migrating s;

        // ── thread A: start it ──
        ScopedThreadFiber main_a;
        ASSERT_TRUE(main_a.main.valid());
        s.return_to = &main_a.main;

        ScopedFiber f(64 * 1024, &migrating_body, &s);
        ASSERT_TRUE(f.fiber.valid());

        switch_to_fiber(f.fiber);
        ASSERT_EQ(s.stack_local_observed, 1);
        const std::thread::id thread_a = s.ran_on;
        EXPECT_EQ(thread_a, std::this_thread::get_id());

        // ── thread B: resume the SAME fibre ──
        std::thread::id thread_b{};
        int observed_on_b = 0;
        std::thread b([&] {
            ScopedThreadFiber main_b;
            ASSERT_TRUE(main_b.main.valid());
            s.return_to = &main_b.main;  // return to THIS thread's home fibre

            switch_to_fiber(f.fiber);

            observed_on_b = s.stack_local_observed;
            thread_b = std::this_thread::get_id();
        });
        b.join();

        EXPECT_NE(thread_b, thread_a) << "the resume must have happened on B";
        EXPECT_EQ(s.ran_on, thread_b) << "the fibre body ran on thread B";
        EXPECT_EQ(observed_on_b, 2)
            << "the fibre must resume where it suspended on thread A, with its "
               "own stack intact -- 2 means the local survived the migration";
    }
}
