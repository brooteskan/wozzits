// tests/audio/voice_tests.cpp
//
// Unit tests for the interpolating-sampler Voice. Pure DSP over raw float
// buffers — no device, no asset system. render_add ACCUMULATES, so every test
// zero-initialises its output buffer first.

#include <gtest/gtest.h>

#include <audio/voice.h>

#include <cmath>
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

    // ── Spatial mode (seam 1) ───────────────────────────────────────────────────

    // A voice never put in spatial mode must render byte-for-byte as the default
    // path: equal-power-pan code is fully bypassed.
    TEST(VoiceTests, NonSpatialVoiceUnchangedBySpatialFeature)
    {
        const std::vector<float> src = { 0.0f, 1.0f, 2.0f, 3.0f };

        Voice a;
        a.start(view(src, 1, 48000), 1.0f, 1.0f, false);
        std::vector<float> out_a(8, 0.0f); // stereo, 4 frames
        a.render_add(out_a.data(), 4, 2, 48000);

        // Mono source broadcasts identically to both channels (the legacy path).
        for (size_t f = 0; f < 4; ++f) {
            EXPECT_FLOAT_EQ(out_a[2 * f + 0], src[f]) << "frame " << f;
            EXPECT_FLOAT_EQ(out_a[2 * f + 1], src[f]) << "frame " << f;
        }
        EXPECT_FALSE(a.spatial());
    }

    // A hard-right source: equal-power gains put almost all energy in the RIGHT
    // channel, and the RIGHT (near) channel LEADS the left by the ITD.
    TEST(VoiceTests, SpatialHardRightPansRightAndDelaysLeft)
    {
        // An impulse so the ITD lead/lag is unambiguous: first sample 1, rest 0.
        std::vector<float> src(64, 0.0f);
        src[0] = 1.0f;

        Voice v;
        v.start(view(src, 1, 48000), /*gain*/ 1.0f, 1.0f, false);
        v.set_spatial(true);
        // pan = +1 (hard right): gain_l = cos(pi/2) ≈ 0, gain_r = sin(pi/2) = 1.
        // itd = +8 frames: left (far) leg delayed 8 frames. Jump (ramp 0) so the
        // gains/ITD are exact from frame 0.
        v.set_spatial_params(/*gain_l*/ 0.0f, /*gain_r*/ 1.0f,
                             /*itd_frames*/ 8.0f, /*ramp*/ 0);

        std::vector<float> out(2 * 32, 0.0f); // stereo, 32 frames
        v.render_add(out.data(), 32, 2, 48000);

        // RIGHT near: impulse lands at frame 0. LEFT far: gain_l ≈ 0 so it is
        // silent regardless — assert the right leads (right has the energy now).
        EXPECT_GT(out[2 * 0 + 1], 0.9f);            // right ch, frame 0
        EXPECT_NEAR(out[2 * 0 + 0], 0.0f, 1.0e-3f); // left ch, frame 0 (near 0)

        // Right channel carries (essentially) all the energy.
        double el = 0.0, er = 0.0;
        for (size_t f = 0; f < 32; ++f) {
            el += std::abs(static_cast<double>(out[2 * f + 0]));
            er += std::abs(static_cast<double>(out[2 * f + 1]));
        }
        EXPECT_GT(er, el);
    }

    // The ITD itself: with EQUAL L/R gains (so both legs are audible) a positive
    // itd_frames delays the LEFT leg — the impulse appears in RIGHT first, then in
    // LEFT `itd` frames later.
    TEST(VoiceTests, SpatialItdDelaysFarLeg)
    {
        std::vector<float> src(64, 0.0f);
        src[0] = 1.0f;

        Voice v;
        v.start(view(src, 1, 48000), 1.0f, 1.0f, false);
        v.set_spatial(true);
        // Equal gains, integer ITD = 5 frames so the lerp lands exactly on a tap.
        v.set_spatial_params(/*gain_l*/ 1.0f, /*gain_r*/ 1.0f,
                             /*itd_frames*/ 5.0f, /*ramp*/ 0);

        std::vector<float> out(2 * 16, 0.0f);
        v.render_add(out.data(), 16, 2, 48000);

        // Right (near) impulse at frame 0; left (far) impulse at frame 5.
        EXPECT_FLOAT_EQ(out[2 * 0 + 1], 1.0f); // right, frame 0
        EXPECT_FLOAT_EQ(out[2 * 0 + 0], 0.0f); // left,  frame 0 (delayed)
        EXPECT_FLOAT_EQ(out[2 * 5 + 0], 1.0f); // left,  frame 5 (the delay)
        EXPECT_FLOAT_EQ(out[2 * 5 + 1], 0.0f); // right, frame 5 (already passed)
    }

    // A centered source: equal gains, ~0 ITD => both channels identical.
    TEST(VoiceTests, SpatialCenteredHasEqualGainsAndNoDelay)
    {
        const std::vector<float> src = { 0.25f, 0.5f, 0.75f, 1.0f };

        Voice v;
        v.start(view(src, 1, 48000), 1.0f, 1.0f, false);
        v.set_spatial(true);
        // pan = 0: gain_l = gain_r = cos(pi/4) = sin(pi/4) ≈ 0.7071; itd = 0.
        const float g = std::cos(3.14159265358979f * 0.25f);
        v.set_spatial_params(g, g, 0.0f, 0);

        std::vector<float> out(2 * 4, 0.0f);
        v.render_add(out.data(), 4, 2, 48000);

        for (size_t f = 0; f < 4; ++f) {
            EXPECT_NEAR(out[2 * f + 0], g * src[f], 1.0e-5f) << "L frame " << f;
            EXPECT_FLOAT_EQ(out[2 * f + 0], out[2 * f + 1]) << "frame " << f;
        }
    }

    // Doppler rides the existing pitch field: pitch > 1 advances the read faster
    // (higher), pitch < 1 slower (lower). A spatial voice reuses set_pitch.
    TEST(VoiceTests, SpatialPitchShiftsReadRateForDoppler)
    {
        // Ramp source so the read position is visible in the output value.
        const std::vector<float> src = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };

        Voice fast;
        fast.start(view(src, 1, 48000), 1.0f, /*pitch*/ 1.0f, false);
        fast.set_spatial(true);
        fast.set_spatial_params(1.0f, 1.0f, 0.0f, 0); // centered, no ITD
        fast.set_pitch(2.0f); // Doppler: receding-to-approaching makes it faster

        std::vector<float> of(2 * 3, 0.0f);
        fast.render_add(of.data(), 3, 2, 48000);
        // step = 2 => reads source[0], source[2], source[4] on the near leg.
        EXPECT_FLOAT_EQ(of[2 * 0 + 1], 0.0f);
        EXPECT_FLOAT_EQ(of[2 * 1 + 1], 2.0f);
        EXPECT_FLOAT_EQ(of[2 * 2 + 1], 4.0f);

        Voice slow;
        slow.start(view(src, 1, 48000), 1.0f, 1.0f, false);
        slow.set_spatial(true);
        slow.set_spatial_params(1.0f, 1.0f, 0.0f, 0);
        slow.set_pitch(0.5f);
        std::vector<float> os(2 * 3, 0.0f);
        slow.render_add(os.data(), 3, 2, 48000);
        // step = 0.5 => reads 0.0, 0.5, 1.0 (interpolated) — slower advance.
        EXPECT_FLOAT_EQ(os[2 * 0 + 1], 0.0f);
        EXPECT_FLOAT_EQ(os[2 * 1 + 1], 0.5f);
        EXPECT_FLOAT_EQ(os[2 * 2 + 1], 1.0f);
    }

    // Mono out sums both legs (ITD combs — that's expected): a centered source on
    // mono out yields gain_l*near + gain_r*far.
    TEST(VoiceTests, SpatialMonoOutSumsBothLegs)
    {
        const std::vector<float> src = { 1.0f, 1.0f, 1.0f, 1.0f };
        Voice v;
        v.start(view(src, 1, 48000), 1.0f, 1.0f, false);
        v.set_spatial(true);
        v.set_spatial_params(0.5f, 0.5f, /*itd*/ 0.0f, 0);

        std::vector<float> out(4, 0.0f); // mono out
        v.render_add(out.data(), 4, 1, 48000);

        // Both legs read the same sample (itd 0): 0.5*1 + 0.5*1 = 1.0.
        for (float s : out)
            EXPECT_FLOAT_EQ(s, 1.0f);
    }

} // namespace wz::audio::test
