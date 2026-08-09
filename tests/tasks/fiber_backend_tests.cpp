// tests/tasks/fiber_backend_tests.cpp
//
// S2a coverage for the fibre backend seam (#293): a worker fibre can be switched
// to, passed data, switch back, and -- the property the S2 scheduler is built on
// -- RESUME mid-execution exactly where it last switched away. Fibres carry no
// captures, so state travels through the create_fiber `arg`.

#include <gtest/gtest.h>

#include <tasks/fiber_backend.h>

namespace
{
    using wz::tasks::convert_fiber_to_thread;
    using wz::tasks::convert_thread_to_fiber;
    using wz::tasks::create_fiber;
    using wz::tasks::current_fiber;
    using wz::tasks::destroy_fiber;
    using wz::tasks::Fiber;
    using wz::tasks::switch_to_fiber;

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
        RunOnce s;
        s.main = convert_thread_to_fiber();
        ASSERT_TRUE(s.main.valid());

        Fiber worker = create_fiber(0, &run_once_body, &s);
        ASSERT_TRUE(worker.valid());

        switch_to_fiber(worker);  // runs run_once_body, which switches back

        EXPECT_EQ(s.ran, 1);
        EXPECT_EQ(s.received, 42);
        EXPECT_EQ(current_fiber().impl, s.main.impl);

        destroy_fiber(worker);
        convert_fiber_to_thread();
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
        Counting s;
        s.main = convert_thread_to_fiber();

        Fiber f = create_fiber(0, &counting_body, &s);
        ASSERT_TRUE(f.valid());

        switch_to_fiber(f);
        EXPECT_EQ(s.count, 1);
        switch_to_fiber(f);
        EXPECT_EQ(s.count, 2);
        switch_to_fiber(f);
        EXPECT_EQ(s.count, 3);

        destroy_fiber(f);
        convert_fiber_to_thread();
    }
}
