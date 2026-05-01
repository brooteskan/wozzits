#include <engine/engine.h>

#include <platform/win32/win32.h> // we should probably add a pump_messages in the wozzits api to call into win32

#include <time/w_time.h>
#include <logging/logger.h>
#include <input/input.h>
#include <event/event.h>

// #include <render/submit.h>

namespace wz::engine
{
    static Context g_ctx;

    Context &context()
    {
        return g_ctx;
    }

    void shutdown()
    {
        g_ctx.running = false;
    }

    void run(UpdateFn update, void *user_data)
    {
        using namespace wz::time;

        Context &ctx = context();
        ctx.running = true;

        const double seconds_per_tick =
            1.0 / double(TimeSource::ticks_per_second());

        Tick last = TimeSource::now_ticks();
        uint64_t frame_index = 0;

        wz::input::InputState input{};
        wz::input::InputState prev_input{};

        while (ctx.running)
        {
            platform::win32::w32_pump_messages();

            Tick now = TimeSource::now_ticks();

            Tick dt = (now > last) ? (now - last) : 1;
            Tick end = last + dt;

            FrameContext fctx;

            fctx.frame.index = frame_index++;
            fctx.frame.interval.start = last;
            fctx.frame.interval.end = end;

            std::vector<wz::event::Event> frame_events;
            frame_events.reserve(4096);

            wz::event::Event e;
            while (wz::event::event_queue.try_pop(e))
            {
                frame_events.push_back(std::move(e));
            }


            prev_input = input;
            

            wz::input::build_input(fctx.input,
                prev_input,
                frame_events.data(),
                frame_events.size(),
                fctx.frame);

            input = fctx.input;

            // wz::core::render::submit(fctx.render_ir);

            last = end;

            update(ctx, fctx, user_data);
        }
    }
}