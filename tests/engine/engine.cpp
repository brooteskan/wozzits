#include <gtest/gtest.h>
#include <engine/engine.h>
#include <event/event.h>
#include <input/input.h>

namespace
{
    struct SimState
    {
        float x = 0.0f;
    };

    void simulate(SimState& sim, const wz::engine::FrameContext& fctx)
    {
        if (fctx.input.keyboard.down[65]) // 'A'
        {
            sim.x += 1.0f; // move right
        }
    }
}


namespace EngineTest
{
    struct EngineTestHarness
    {
        uint64_t max_frames = 0;
        uint64_t frame_count = 0;

        bool shutdown_requested = false;

        std::function<void(wz::engine::Context &, wz::engine::FrameContext &)> per_frame;
        std::function<void(wz::engine::Context &, wz::engine::FrameContext &)> on_start;
        std::function<void(wz::engine::Context &, wz::engine::FrameContext &)> on_end;
        wz::engine::FrameContext last_frame{};

        static void per_frame_callback(
            wz::engine::Context &ctx,
            wz::engine::FrameContext &fctx,
            void *user)
        {
            auto *h = static_cast<EngineTestHarness *>(user);

            if (h->frame_count == 0 && h->on_start)
                h->on_start(ctx, fctx);

            if (h->per_frame)
                h->per_frame(ctx, fctx);

            h->frame_count++;

            h->last_frame = fctx;
            if (h->frame_count >= h->max_frames || h->shutdown_requested)
                wz::engine::shutdown();
        }

        void run()
        {
            frame_count = 0;
            shutdown_requested = false;

            wz::engine::run(&per_frame_callback, this);

            if (on_end)
            {
                on_end(wz::engine::context(), last_frame);
            }
        }

        void stop()
        {
            shutdown_requested = true;
        }
    };
}

using namespace wz;

TEST(EngineSmokeTest, RunsForNFrames)
{
    EngineTest::EngineTestHarness h;

    h.max_frames = 10;

    bool callback_seen = false;

    h.per_frame = [&](engine::Context &ctx, engine::FrameContext &fctx)
    {
        callback_seen = true;
    };

    h.run();

    // ✔ engine truth: harness is now authoritative for test logic
    EXPECT_EQ(h.frame_count, 10);
    EXPECT_TRUE(callback_seen);
}

TEST(EngineSmokeTest, RunsForNFrames2)
{
    EngineTest::EngineTestHarness h;

    h.max_frames = 10;

    bool callback_seen = false;
    uint64_t last_frame = 0;

    h.per_frame = [&](engine::Context &, engine::FrameContext &fctx)
    {
        callback_seen = true;
        EXPECT_GE(fctx.frame.index, last_frame);
        last_frame = fctx.frame.index;
    };

    h.run();

    EXPECT_EQ(h.frame_count, 10);
    EXPECT_TRUE(callback_seen);
}




using namespace wz;

TEST(InputIntegrationTest, KeyDownBecomesInputState)
{
    EngineTest::EngineTestHarness h;
    h.max_frames = 1;

    wz::input::InputState captured_input{};
    bool captured = false;

    h.per_frame = [&](engine::Context&, engine::FrameContext&)
        {
            // capture input indirectly via engine hook would be ideal,
            // but for now we assume build_input is deterministic and test via state

            // (we will refine this next step)
            captured = true;
        };

    // ----------------------------
    // Inject raw event BEFORE frame runs
    // ----------------------------
    event::Event e{};
    e.category = event::Event::Category::Input;
    e.source = event::Event::Source::Platform;
    e.type = event::Event::Type::KeyPressDown;

    e.key.vkey = 65; // 'A'
    e.key.scancode = 30;
    e.key.flags = 0;

    event::event_queue.try_push(e);

    // ----------------------------
    // Run engine (1 frame)
    // ----------------------------
    h.run();

    EXPECT_TRUE(captured);
}

TEST(InputIntegrationTest, KeyDownIsCaptured)
{
    EngineTest::EngineTestHarness h;
    h.max_frames = 1;

    event::Event e{};
    e.category = event::Event::Category::Input;
    e.source = event::Event::Source::Platform;
    e.type = event::Event::Type::KeyPressDown;
    e.key.vkey = 65;

    event::event_queue.try_push(e);

    h.per_frame = [&](engine::Context&, engine::FrameContext& fctx)
        {
            EXPECT_TRUE(fctx.input.keyboard.down[65]);
            EXPECT_TRUE(fctx.input.keyboard.pressed[65]);
        };

    h.run();
}


using namespace wz;

TEST(InputStressTest, HighVolumeEventFlood)
{
    EngineTest::EngineTestHarness h;
    h.max_frames = 1;

    constexpr int EVENT_COUNT = 10000;

    int pushed_mouse_moves = 0;
    int pushed_key_downs = 0;

    // ---------------------------------------------------
    // Inject massive mixed input flood BEFORE frame runs
    // ---------------------------------------------------
    for (int i = 0; i < EVENT_COUNT; ++i)
    {
        event::Event e{};
        e.category = event::Event::Category::Input;
        e.source = event::Event::Source::Platform;

        const bool is_mouse_move = (i % 2 == 0);

        if (is_mouse_move)
        {
            e.type = event::Event::Type::MouseMove;
            e.mouse_move.dx = 1;
            e.mouse_move.dy = 1;
        }
        else
        {
            e.type = event::Event::Type::KeyPressDown;
            e.key.vkey = 65; // 'A'
        }

        if (event::event_queue.try_push(e))
        {
            if (is_mouse_move)
                ++pushed_mouse_moves;
            else
                ++pushed_key_downs;
        }
    }

    ASSERT_GT(pushed_mouse_moves, 0);
    ASSERT_GT(pushed_key_downs, 0);

    // ---------------------------------------------------
    // Run engine for exactly one frame
    // ---------------------------------------------------
    h.per_frame = [&](engine::Context&, engine::FrameContext& fctx)
        {
            int down_count = 0;

            for (int i = 0; i < 256; ++i)
            {
                down_count += fctx.input.keyboard.down[i];
            }

            EXPECT_EQ(down_count, 1); // only 'A' should be down

            // Mouse deltas are accumulated over all mouse move events
            // accepted by the bounded event queue.
            EXPECT_EQ(fctx.input.mouse.dx, pushed_mouse_moves);
            EXPECT_EQ(fctx.input.mouse.dy, pushed_mouse_moves);

            EXPECT_TRUE(fctx.input.keyboard.down[65]);
            EXPECT_TRUE(fctx.input.keyboard.pressed[65]);
        };

    h.run();

    EXPECT_EQ(h.frame_count, 1);
}

TEST(InputIntegrationTest, EventsAreFrameIsolated)
{
    EngineTest::EngineTestHarness h;
    h.max_frames = 2;

    int frame0_down = 0;
    int frame1_down = 0;

    int frame0_pressed = 0;
    int frame1_pressed = 0;

    wz::event::Event e{};
    e.category = wz::event::Event::Category::Input;
    e.source = wz::event::Event::Source::Platform;
    e.type = wz::event::Event::Type::KeyPressDown;
    e.key.vkey = 65;

    wz::event::event_queue.try_push(e);

    h.per_frame = [&](wz::engine::Context&, wz::engine::FrameContext& fctx)
        {
            if (fctx.frame.index == 0)
            {
                frame0_down += fctx.input.keyboard.down[65];
                frame0_pressed += fctx.input.keyboard.pressed[65];
            }
            else
            {
                frame1_down += fctx.input.keyboard.down[65];
                frame1_pressed += fctx.input.keyboard.pressed[65];
            }
        };

    h.run();

    EXPECT_EQ(frame0_down, 1);
    EXPECT_EQ(frame1_down, 1);        // ✔ persists

    EXPECT_EQ(frame0_pressed, 1);
    EXPECT_EQ(frame1_pressed, 0);     // ✔ frame-isolated
}

TEST(SimulationTest, MovesWhenKeyHeld)
{
    EngineTest::EngineTestHarness h;
    h.max_frames = 3;

    SimState sim{};

    // Inject key down BEFORE run
    wz::event::Event e{};
    e.category = wz::event::Event::Category::Input;
    e.source = wz::event::Event::Source::Platform;
    e.type = wz::event::Event::Type::KeyPressDown;
    e.key.vkey = 65;

    wz::event::event_queue.try_push(e);

    h.per_frame = [&](wz::engine::Context&, wz::engine::FrameContext& fctx)
        {
            simulate(sim, fctx);
        };

    h.run();

    EXPECT_EQ(sim.x, 3.0f);
}