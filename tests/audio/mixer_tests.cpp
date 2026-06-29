// tests/audio/mixer_tests.cpp
//
// Unit tests for the Mixer: voice summing, master gain, the safety limiter,
// budget/stealing, and handle staleness. render() clears its own output, so it
// doubles as the headless offline-render harness (audio-track item 4).

#include <gtest/gtest.h>

#include <audio/mixer.h>

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

} // namespace wz::audio::test
