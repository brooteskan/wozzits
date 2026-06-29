// tests/audio/voice_tests.cpp
//
// Unit tests for the interpolating-sampler Voice. Pure DSP over raw float
// buffers — no device, no asset system. render_add ACCUMULATES, so every test
// zero-initialises its output buffer first.

#include <gtest/gtest.h>

#include <audio/voice.h>

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

    TEST(VoiceTests, RateOnePlaybackReproducesMonoSource)
    {
        const std::vector<float> src = { 0.0f, 1.0f, 2.0f, 3.0f };
        Voice v;
        v.start(view(src, 1, 48000), /*gain*/ 1.0f, /*pitch*/ 1.0f, false);

        std::vector<float> out(4, 0.0f);
        v.render_add(out.data(), 4, 1, 48000);

        for (size_t i = 0; i < src.size(); ++i)
            EXPECT_FLOAT_EQ(out[i], src[i]);
    }

    TEST(VoiceTests, LinearInterpolationProducesMidpoints)
    {
        // step = src_rate/out_rate = 4/8 = 0.5, so reads land on integer and
        // half-integer positions: 0, 0.5, 1.0, 1.5, ...
        const std::vector<float> src = { 0.0f, 1.0f, 2.0f, 3.0f };
        Voice v;
        v.start(view(src, 1, 4), 1.0f, 1.0f, false);

        std::vector<float> out(7, 0.0f);
        v.render_add(out.data(), 7, 1, 8);

        const std::vector<float> expected =
            { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f };
        for (size_t i = 0; i < expected.size(); ++i)
            EXPECT_FLOAT_EQ(out[i], expected[i]) << "frame " << i;
    }

    TEST(VoiceTests, NonLoopingVoiceDeactivatesWhenExhausted)
    {
        const std::vector<float> src = { 1.0f, 1.0f, 1.0f, 1.0f }; // fc=4 @4Hz
        Voice v;
        v.start(view(src, 1, 4), 1.0f, 1.0f, false); // step 0.5 at out 8Hz

        std::vector<float> out(16, 0.0f);
        v.render_add(out.data(), 16, 1, 8);

        // Positions 0..3.5 (8 frames) produce output; frame 8 sees pos 4.0 and
        // stops, leaving the tail untouched (still zero).
        for (int i = 0; i < 8; ++i)
            EXPECT_FLOAT_EQ(out[i], 1.0f) << "frame " << i;
        for (int i = 8; i < 16; ++i)
            EXPECT_FLOAT_EQ(out[i], 0.0f) << "frame " << i;
        EXPECT_FALSE(v.active());
    }

    TEST(VoiceTests, LoopingWrapsAndStaysActive)
    {
        const std::vector<float> src = { 0.0f, 1.0f }; // fc=2 @2Hz
        Voice v;
        v.start(view(src, 1, 2), 1.0f, 1.0f, true); // step 1.0 at out 2Hz

        std::vector<float> out(5, 0.0f);
        v.render_add(out.data(), 5, 1, 2);

        const std::vector<float> expected = { 0.0f, 1.0f, 0.0f, 1.0f, 0.0f };
        for (size_t i = 0; i < expected.size(); ++i)
            EXPECT_FLOAT_EQ(out[i], expected[i]) << "frame " << i;
        EXPECT_TRUE(v.active());
    }

    TEST(VoiceTests, MonoSourceBroadcastsToStereo)
    {
        const std::vector<float> src = { 0.5f };
        Voice v;
        v.start(view(src, 1, 1), 1.0f, 1.0f, false);

        std::vector<float> out(2, 0.0f); // one stereo frame
        v.render_add(out.data(), 1, 2, 1);

        EXPECT_FLOAT_EQ(out[0], 0.5f);
        EXPECT_FLOAT_EQ(out[1], 0.5f);
    }

    TEST(VoiceTests, StereoSourceDownmixesToMono)
    {
        // One stereo frame L=1.0 R=0.0 -> mono average 0.5.
        const std::vector<float> src = { 1.0f, 0.0f };
        Voice v;
        v.start(view(src, 2, 1), 1.0f, 1.0f, false);

        std::vector<float> out(1, 0.0f);
        v.render_add(out.data(), 1, 1, 1);

        EXPECT_FLOAT_EQ(out[0], 0.5f);
    }

    TEST(VoiceTests, GainScalesOutput)
    {
        const std::vector<float> src = { 1.0f, 1.0f, 1.0f, 1.0f };
        Voice v;
        v.start(view(src, 1, 48000), 0.25f, 1.0f, false);

        std::vector<float> out(4, 0.0f);
        v.render_add(out.data(), 4, 1, 48000);

        for (float s : out)
            EXPECT_FLOAT_EQ(s, 0.25f);
    }

    TEST(VoiceTests, PitchDoublesReadRate)
    {
        // pitch 2 at equal rates -> step 2.0, reads every other source frame.
        const std::vector<float> src = { 0.0f, 1.0f, 2.0f, 3.0f };
        Voice v;
        v.start(view(src, 1, 48000), 1.0f, 2.0f, false);

        std::vector<float> out(2, 0.0f);
        v.render_add(out.data(), 2, 1, 48000);

        EXPECT_FLOAT_EQ(out[0], 0.0f);
        EXPECT_FLOAT_EQ(out[1], 2.0f);
    }

    TEST(VoiceTests, SetGainRampInterpolatesToTarget)
    {
        const std::vector<float> src(8, 1.0f);
        Voice v;
        v.start(view(src, 1, 48000), /*gain*/ 1.0f, 1.0f, /*looping*/ true);
        v.set_gain(0.0f, /*ramp_frames*/ 4); // 1.0 -> 0.0 over 4 frames

        std::vector<float> out(5, 0.0f);
        v.render_add(out.data(), 5, 1, 48000);

        const std::vector<float> expected = { 1.0f, 0.75f, 0.5f, 0.25f, 0.0f };
        for (size_t i = 0; i < expected.size(); ++i)
            EXPECT_FLOAT_EQ(out[i], expected[i]) << "frame " << i;
        EXPECT_TRUE(v.active()); // a gain ramp does not stop the voice
    }

    TEST(VoiceTests, FadeOutRampsToZeroThenDeactivates)
    {
        const std::vector<float> src(8, 1.0f);
        Voice v;
        v.start(view(src, 1, 48000), 1.0f, 1.0f, /*looping*/ true);
        v.fade_out(/*frames*/ 4);

        std::vector<float> out(6, 0.0f);
        v.render_add(out.data(), 6, 1, 48000);

        // Monotonic non-increasing ramp to zero, ending in silence; no hard cut.
        const std::vector<float> expected =
            { 1.0f, 0.75f, 0.5f, 0.25f, 0.0f, 0.0f };
        for (size_t i = 0; i < expected.size(); ++i)
            EXPECT_FLOAT_EQ(out[i], expected[i]) << "frame " << i;
        EXPECT_FALSE(v.active());
    }

    TEST(VoiceTests, FadeOutZeroFramesStopsImmediately)
    {
        const std::vector<float> src(8, 1.0f);
        Voice v;
        v.start(view(src, 1, 48000), 1.0f, 1.0f, true);
        v.fade_out(0); // degenerate -> hard stop
        EXPECT_FALSE(v.active());

        std::vector<float> out(4, 0.0f);
        v.render_add(out.data(), 4, 1, 48000);
        for (float s : out)
            EXPECT_FLOAT_EQ(s, 0.0f);
    }

    TEST(VoiceTests, InvalidSourceStaysInactive)
    {
        Voice v;
        v.start(AudioBufferView{}, 1.0f, 1.0f, false);
        EXPECT_FALSE(v.active());

        std::vector<float> out(4, 0.0f);
        v.render_add(out.data(), 4, 1, 48000); // must be a no-op
        for (float s : out)
            EXPECT_FLOAT_EQ(s, 0.0f);
    }

} // namespace wz::audio::test
