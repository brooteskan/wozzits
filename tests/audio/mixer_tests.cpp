// tests/audio/mixer_tests.cpp
//
// Unit tests for the Mixer: voice summing, master gain, the safety limiter,
// budget/stealing, and handle staleness. render() clears its own output, so it
// doubles as the headless offline-render harness (audio-track item 4).

#include <gtest/gtest.h>

#include <audio/mixer.h>

#include <algorithm>
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

    TEST(MixerTests, PlayActivatesVoice)
    {
        const std::vector<float> src(8, 1.0f);
        Mixer mixer(8);

        const VoiceHandle h = mixer.play(view(src, 1, 48000));
        EXPECT_TRUE(h.valid());
        EXPECT_EQ(mixer.active_voice_count(), 1u);
    }

    TEST(MixerTests, InvalidSourceReturnsInvalidHandle)
    {
        Mixer mixer(8);
        const VoiceHandle h = mixer.play(AudioBufferView{});
        EXPECT_FALSE(h.valid());
        EXPECT_EQ(mixer.active_voice_count(), 0u);
    }

    TEST(MixerTests, TwoVoicesSum)
    {
        const std::vector<float> src(4, 1.0f);
        Mixer mixer(8);
        mixer.play(view(src, 1, 48000), 0.25f);
        mixer.play(view(src, 1, 48000), 0.25f);

        std::vector<float> out(4, 999.0f);
        mixer.render(out.data(), 4, 1, 48000);

        for (float s : out)
            EXPECT_FLOAT_EQ(s, 0.5f);
    }

    TEST(MixerTests, MasterGainApplies)
    {
        const std::vector<float> src(4, 1.0f);
        Mixer mixer(8);
        mixer.set_master_gain(0.5f);
        mixer.play(view(src, 1, 48000), 1.0f);

        std::vector<float> out(4, 0.0f);
        mixer.render(out.data(), 4, 1, 48000);

        for (float s : out)
            EXPECT_FLOAT_EQ(s, 0.5f);
    }

    TEST(MixerTests, LimiterHardClipsOverflow)
    {
        const std::vector<float> src(4, 1.0f);
        Mixer mixer(8);
        // Two full-scale voices sum to 2.0; the limiter must clamp to 1.0.
        mixer.play(view(src, 1, 48000), 1.0f);
        mixer.play(view(src, 1, 48000), 1.0f);

        std::vector<float> out(4, 0.0f);
        mixer.render(out.data(), 4, 1, 48000);

        for (float s : out)
            EXPECT_FLOAT_EQ(s, 1.0f);
    }

    TEST(MixerTests, RenderClearsStalePreviousContents)
    {
        Mixer mixer(8); // no voices
        std::vector<float> out(4, 7.0f);
        mixer.render(out.data(), 4, 1, 48000);
        for (float s : out)
            EXPECT_FLOAT_EQ(s, 0.0f);
    }

    TEST(MixerTests, StopSilencesVoice)
    {
        const std::vector<float> src(4, 1.0f);
        Mixer mixer(8);
        const VoiceHandle h =
            mixer.play(view(src, 1, 48000), 1.0f, 1.0f, /*looping*/ true);

        std::vector<float> out(4, 0.0f);
        mixer.render(out.data(), 4, 1, 48000);
        EXPECT_FLOAT_EQ(out[0], 1.0f); // audible before stop

        mixer.stop(h);
        EXPECT_EQ(mixer.active_voice_count(), 0u);

        mixer.render(out.data(), 4, 1, 48000);
        for (float s : out)
            EXPECT_FLOAT_EQ(s, 0.0f); // silent after stop
    }

    TEST(MixerTests, FadeOutClientRampsThenFreesSlot)
    {
        const std::vector<float> src(8, 1.0f);
        Mixer mixer(8);
        mixer.play(view(src, 1, 48000), 1.0f, 1.0f, /*looping*/ true, /*id*/ 5);

        mixer.fade_out_client(5, /*frames*/ 4);

        std::vector<float> out(6, 0.0f);
        mixer.render(out.data(), 6, 1, 48000);

        EXPECT_FLOAT_EQ(out[0], 1.0f);
        EXPECT_FLOAT_EQ(out[3], 0.25f);
        EXPECT_FLOAT_EQ(out[4], 0.0f);
        EXPECT_EQ(mixer.active_voice_count(), 0u); // freed after the fade
    }

    TEST(MixerTests, SetGainClientAppliesInstantly)
    {
        const std::vector<float> src(8, 1.0f);
        Mixer mixer(8);
        mixer.play(view(src, 1, 48000), 1.0f, 1.0f, true, /*id*/ 3);

        mixer.set_gain_client(3, 0.5f, /*ramp_frames*/ 0);

        std::vector<float> out(4, 0.0f);
        mixer.render(out.data(), 4, 1, 48000);
        for (float s : out)
            EXPECT_FLOAT_EQ(s, 0.5f);
    }

    TEST(MixerTests, BudgetIsBoundedAndStealsOldest)
    {
        const std::vector<float> src(8, 1.0f);
        Mixer mixer(2); // capacity 2

        mixer.play(view(src, 1, 48000), 1.0f, 1.0f, true);
        mixer.play(view(src, 1, 48000), 1.0f, 1.0f, true);
        EXPECT_EQ(mixer.active_voice_count(), 2u);

        // Third play steals a slot rather than growing the pool.
        const VoiceHandle third =
            mixer.play(view(src, 1, 48000), 1.0f, 1.0f, true);
        EXPECT_TRUE(third.valid());
        EXPECT_EQ(mixer.active_voice_count(), 2u);
        EXPECT_EQ(mixer.capacity(), 2u);
    }

    TEST(MixerTests, StolenSlotInvalidatesOldHandle)
    {
        const std::vector<float> a(4, 1.0f);
        const std::vector<float> b(4, 1.0f);
        Mixer mixer(1); // capacity 1 forces a steal

        const VoiceHandle ha =
            mixer.play(view(a, 1, 48000), 1.0f, 1.0f, true);
        const VoiceHandle hb =
            mixer.play(view(b, 1, 48000), 1.0f, 1.0f, true); // steals A's slot

        EXPECT_TRUE(hb.valid());
        // Stopping via the stale handle A must NOT stop B.
        mixer.stop(ha);
        EXPECT_EQ(mixer.active_voice_count(), 1u);

        std::vector<float> out(4, 0.0f);
        mixer.render(out.data(), 4, 1, 48000);
        EXPECT_FLOAT_EQ(out[0], 1.0f); // B still audible
    }

    // ─── Grain-cloud generator pool ───────────────────────────────────────────────

    namespace {
        GrainCloudDesc grain_desc(const AudioBufferView& src,
                                  float density,
                                  float gain = 1.0f,
                                  uint32_t seed = 1u)
        {
            GrainCloudDesc d;
            d.sources[0] = src;
            d.weights[0] = 1.0f;
            d.source_count = 1;
            d.max_grains = 16;
            d.seed = seed;
            d.gain = gain;
            d.density = density;
            d.grain_ms = 10.0f;
            return d;
        }

        double abs_energy(const std::vector<float>& b)
        {
            double e = 0.0;
            for (float s : b) e += std::abs(static_cast<double>(s));
            return e;
        }
    }

    TEST(MixerTests, PlayGrainCloudActivatesGeneratorWithoutTouchingVoices)
    {
        const std::vector<float> src(4800, 1.0f);
        Mixer mixer(8, 4);

        const GrainCloudHandle h =
            mixer.play_grain_cloud(grain_desc(view(src, 1, 48000), 300.0f));
        EXPECT_TRUE(h.valid());
        EXPECT_EQ(mixer.active_grain_cloud_count(), 1u);
        EXPECT_EQ(mixer.active_voice_count(), 0u);  // separate pool

        std::vector<float> out(2400, 0.0f);
        mixer.render(out.data(), 2400, 1, 48000);
        EXPECT_GT(abs_energy(out), 0.0);
    }

    TEST(MixerTests, GrainCloudAndVoiceSumOnTheSameBus)
    {
        const std::vector<float> voice_src(2400, 0.0f);  // silent voice
        const std::vector<float> grain_src(4800, 1.0f);
        Mixer mixer(8, 4);

        mixer.play(view(voice_src, 1, 48000));
        mixer.play_grain_cloud(grain_desc(view(grain_src, 1, 48000), 300.0f));
        EXPECT_EQ(mixer.active_voice_count(), 1u);
        EXPECT_EQ(mixer.active_grain_cloud_count(), 1u);

        std::vector<float> out(2400, 0.0f);
        mixer.render(out.data(), 2400, 1, 48000);
        EXPECT_GT(abs_energy(out), 0.0);  // grain contribution present
    }

    TEST(MixerTests, NoSourcesYieldsInvalidGrainHandle)
    {
        Mixer mixer(8, 4);
        GrainCloudDesc d;  // source_count == 0
        const GrainCloudHandle h = mixer.play_grain_cloud(d);
        EXPECT_FALSE(h.valid());
        EXPECT_EQ(mixer.active_grain_cloud_count(), 0u);
    }

    TEST(MixerTests, SetGrainParamClientSilencesViaGain)
    {
        const std::vector<float> src(4800, 1.0f);
        Mixer mixer(8, 4);
        mixer.play_grain_cloud(grain_desc(view(src, 1, 48000), 300.0f), /*client*/ 7u);

        std::vector<float> a(2400, 0.0f);
        mixer.render(a.data(), 2400, 1, 48000);
        ASSERT_GT(abs_energy(a), 0.0);

        // Drop the cloud's gain to 0 (jump) by client id; output goes silent.
        mixer.set_grain_param_client(7u, GrainParam::Gain, 0.0f, 0u);
        std::vector<float> b(2400, 0.0f);
        mixer.render(b.data(), 2400, 1, 48000);
        EXPECT_NEAR(abs_energy(b), 0.0, 1.0e-4);
    }

    TEST(MixerTests, StopClientStopsGrainCloudAfterTail)
    {
        const std::vector<float> src(4800, 1.0f);
        Mixer mixer(8, 4);
        mixer.play_grain_cloud(grain_desc(view(src, 1, 48000), 300.0f), /*client*/ 7u);

        std::vector<float> warm(2400, 0.0f);
        mixer.render(warm.data(), 2400, 1, 48000);
        ASSERT_EQ(mixer.active_grain_cloud_count(), 1u);

        mixer.stop_client(7u);  // stop spawning; grains tail out
        std::vector<float> tail(960, 0.0f);  // past the 10 ms grain length
        mixer.render(tail.data(), 960, 1, 48000);
        EXPECT_EQ(mixer.active_grain_cloud_count(), 0u);
    }

    // A live SetGrainParam(BlendDepth) command enables the source crossfade on a
    // running cloud (region-change use case): steady while off, swinging once on.
    TEST(MixerTests, SetGrainParamBlendDepthEnablesCrossfadeLive)
    {
        const std::vector<float> loud(48000, 1.0f);
        const std::vector<float> silent(48000, 0.0f);
        GrainCloudDesc d;
        d.sources[0] = view(loud, 1, 48000);
        d.sources[1] = view(silent, 1, 48000);
        d.weights[0] = 1.0f;
        d.weights[1] = 1.0f;
        d.source_count = 2;
        d.max_grains = 32;
        d.seed = 99u;
        d.density = 800.0f;
        d.grain_ms = 5.0f;
        d.blend_rate = 1.0f;   // LFO running...
        d.blend_depth = 0.0f;  // ...but no blend yet

        Mixer mixer(8, 4);
        mixer.play_grain_cloud(d, /*client*/ 7u);

        auto window_spread = [&]() {
            std::vector<float> out(48000, 0.0f);  // 1 s = one cycle at 1 Hz
            mixer.render(out.data(), 48000, 1, 48000);
            double e[8] = { 0 };
            for (size_t f = 0; f < out.size(); ++f) {
                e[f / 6000] += std::abs(static_cast<double>(out[f]));
            }
            const double hi = *std::max_element(e, e + 8);
            const double lo = *std::min_element(e, e + 8);
            return hi / (lo + 1.0e-9);
        };

        EXPECT_LT(window_spread(), 1.5);  // depth 0 → steady 50/50

        // Live: enable full blend depth by client id.
        mixer.set_grain_param_client(7u, GrainParam::BlendDepth, 1.0f, 0u);
        EXPECT_GT(window_spread(), 3.0);  // now the loud clip fades in and out
    }

    TEST(MixerTests, GrainCloudPoolStealsOldestWhenFull)
    {
        const std::vector<float> src(4800, 1.0f);
        Mixer mixer(8, 1);  // single cloud slot forces a steal

        const GrainCloudHandle a =
            mixer.play_grain_cloud(grain_desc(view(src, 1, 48000), 300.0f), 1u);
        const GrainCloudHandle b =
            mixer.play_grain_cloud(grain_desc(view(src, 1, 48000), 300.0f), 2u);
        EXPECT_TRUE(a.valid());
        EXPECT_TRUE(b.valid());
        EXPECT_NE(a.generation, b.generation);     // same slot, bumped generation
        EXPECT_EQ(mixer.active_grain_cloud_count(), 1u);
    }

} // namespace wz::audio::test
