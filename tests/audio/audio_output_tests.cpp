// tests/audio/audio_output_tests.cpp
//
// Tests for the device-binding glue. We do NOT open a real device here (that
// needs hardware); instead we invoke the static pull callback directly with a
// hand-built context, which is exactly what the device thread does. Scheduler
// behaviour itself is covered by audio_scheduler_tests.

#include <gtest/gtest.h>

#include <audio/audio_output.h>

#include <vector>

namespace wz::audio::test {

    namespace {
        AudioBufferView view(const std::vector<float>& s,
                             uint32_t channels,
                             uint32_t sample_rate)
        {
            AudioBufferView v;
            v.samples = s.data();
            v.channels = channels;
            v.sample_rate = sample_rate;
            v.frame_count = s.size() / channels;
            return v;
        }
    }

    TEST(AudioOutputTests, CallbackForwardsToSchedulerAndFillsBuffer)
    {
        const std::vector<float> src(16, 1.0f);
        AudioScheduler sched(8, 64);

        AudioCommand play;
        play.type = AudioCommandType::Play;
        play.sample_time = 0;
        play.source = view(src, 1, 48000);
        play.looping = true;
        ASSERT_TRUE(sched.post(play));

        AudioFormat fmt;
        fmt.sample_rate = 48000;
        fmt.channels = 1;
        fmt.frames_per_buffer = 4;
        AudioOutput output(sched, fmt);

        std::vector<float> buffer(4, 0.0f);
        AudioCallbackContext ctx;
        ctx.output = buffer.data();
        ctx.frames = 4;
        ctx.channels = 1;
        ctx.stream_time = 0.0;

        AudioOutput::render_callback(ctx, &output);

        for (float s : buffer)
            EXPECT_FLOAT_EQ(s, 1.0f);
        EXPECT_EQ(sched.sample_clock(), 4u); // callback advanced the clock
    }

    TEST(AudioOutputTests, CallbackWithNullUserIsNoOp)
    {
        std::vector<float> buffer(4, 7.0f);
        AudioCallbackContext ctx;
        ctx.output = buffer.data();
        ctx.frames = 4;
        ctx.channels = 1;

        AudioOutput::render_callback(ctx, nullptr); // must not crash or touch data
        for (float s : buffer)
            EXPECT_FLOAT_EQ(s, 7.0f);
    }

    TEST(AudioOutputTests, NotRunningBeforeStart)
    {
        AudioScheduler sched(8, 64);
        AudioFormat fmt;
        AudioOutput output(sched, fmt);
        EXPECT_FALSE(output.running());
    }

} // namespace wz::audio::test
