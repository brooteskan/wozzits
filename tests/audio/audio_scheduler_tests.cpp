// tests/audio/audio_scheduler_tests.cpp
//
// Tests for the realtime control split: lock-free command handoff plus
// sample-accurate Play/Stop/SetMasterGain scheduling. Most tests are
// single-threaded (post then process); one exercises the real cross-thread
// SPSC handoff with a timing-independent outcome.

#include <gtest/gtest.h>

#include <audio/audio_scheduler.h>

#include <atomic>
#include <cmath>
#include <thread>
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

        AudioCommand play_cmd(const AudioBufferView& src,
                              uint64_t at,
                              float gain = 1.0f,
                              bool looping = true,
                              uint32_t client_id = 0)
        {
            AudioCommand c;
            c.type = AudioCommandType::Play;
            c.sample_time = at;
            c.source = src;
            c.gain = gain;
            c.pitch = 1.0f;
            c.looping = looping;
            c.client_id = client_id;
            return c;
        }
    }

    TEST(AudioSchedulerTests, PlayAtBlockStartProducesSound)
    {
        const std::vector<float> src(8, 1.0f);
        AudioScheduler sched(8, 64);
        ASSERT_TRUE(sched.post(play_cmd(view(src, 1, 48000), 0)));

        std::vector<float> out(4, 0.0f);
        sched.process(out.data(), 4, 1, 48000);

        for (float s : out)
            EXPECT_FLOAT_EQ(s, 1.0f);
        EXPECT_EQ(sched.sample_clock(), 4u);
    }

    TEST(AudioSchedulerTests, PlayScheduledMidBlockIsSampleAccurate)
    {
        const std::vector<float> src(8, 1.0f);
        AudioScheduler sched(8, 64);
        ASSERT_TRUE(sched.post(play_cmd(view(src, 1, 48000), /*at*/ 2)));

        std::vector<float> out(4, 0.0f);
        sched.process(out.data(), 4, 1, 48000);

        // Silent until frame 2, then sounding.
        EXPECT_FLOAT_EQ(out[0], 0.0f);
        EXPECT_FLOAT_EQ(out[1], 0.0f);
        EXPECT_FLOAT_EQ(out[2], 1.0f);
        EXPECT_FLOAT_EQ(out[3], 1.0f);
    }

    TEST(AudioSchedulerTests, FutureCommandIsDeferredToItsBlock)
    {
        const std::vector<float> src(16, 1.0f);
        AudioScheduler sched(8, 64);
        ASSERT_TRUE(sched.post(play_cmd(view(src, 1, 48000), /*at*/ 6)));

        std::vector<float> b1(4, 0.0f);
        sched.process(b1.data(), 4, 1, 48000); // frames 0..3: not due
        for (float s : b1)
            EXPECT_FLOAT_EQ(s, 0.0f);

        std::vector<float> b2(4, 0.0f);
        sched.process(b2.data(), 4, 1, 48000); // frames 4..7: fires at 6
        EXPECT_FLOAT_EQ(b2[0], 0.0f); // frame 4
        EXPECT_FLOAT_EQ(b2[1], 0.0f); // frame 5
        EXPECT_FLOAT_EQ(b2[2], 1.0f); // frame 6
        EXPECT_FLOAT_EQ(b2[3], 1.0f); // frame 7
    }

    TEST(AudioSchedulerTests, StopAtOffsetSilencesRemainder)
    {
        const std::vector<float> src(8, 1.0f);
        AudioScheduler sched(8, 64);
        ASSERT_TRUE(sched.post(
            play_cmd(view(src, 1, 48000), /*at*/ 0, 1.0f, true, /*id*/ 7)));

        AudioCommand stop;
        stop.type = AudioCommandType::Stop;
        stop.sample_time = 2;
        stop.client_id = 7;
        ASSERT_TRUE(sched.post(stop));

        std::vector<float> out(4, 0.0f);
        sched.process(out.data(), 4, 1, 48000);

        EXPECT_FLOAT_EQ(out[0], 1.0f);
        EXPECT_FLOAT_EQ(out[1], 1.0f);
        EXPECT_FLOAT_EQ(out[2], 0.0f);
        EXPECT_FLOAT_EQ(out[3], 0.0f);
    }

    TEST(AudioSchedulerTests, StopWithFadeRampsOutInsteadOfClipping)
    {
        const std::vector<float> src(16, 1.0f);
        AudioScheduler sched(8, 64);
        ASSERT_TRUE(sched.post(
            play_cmd(view(src, 1, 48000), /*at*/ 0, 1.0f, true, /*id*/ 7)));

        AudioCommand stop;
        stop.type = AudioCommandType::Stop;
        stop.sample_time = 0;
        stop.client_id = 7;
        stop.ramp_frames = 4; // fade instead of hard cut
        ASSERT_TRUE(sched.post(stop));

        std::vector<float> out(6, 0.0f);
        sched.process(out.data(), 6, 1, 48000);

        // Play then fade both at offset 0: a smooth ramp to silence, no click.
        EXPECT_FLOAT_EQ(out[0], 1.0f);
        EXPECT_FLOAT_EQ(out[1], 0.75f);
        EXPECT_FLOAT_EQ(out[2], 0.5f);
        EXPECT_FLOAT_EQ(out[3], 0.25f);
        EXPECT_FLOAT_EQ(out[4], 0.0f);
        EXPECT_EQ(sched.mixer().active_voice_count(), 0u);
    }

    TEST(AudioSchedulerTests, SetMasterGainAppliesAtOffset)
    {
        const std::vector<float> src(8, 1.0f);
        AudioScheduler sched(8, 64);
        ASSERT_TRUE(sched.post(play_cmd(view(src, 1, 48000), 0, 1.0f, true)));

        AudioCommand g;
        g.type = AudioCommandType::SetMasterGain;
        g.sample_time = 2;
        g.value = 0.5f;
        ASSERT_TRUE(sched.post(g));

        std::vector<float> out(4, 0.0f);
        sched.process(out.data(), 4, 1, 48000);

        EXPECT_FLOAT_EQ(out[0], 1.0f);
        EXPECT_FLOAT_EQ(out[1], 1.0f);
        EXPECT_FLOAT_EQ(out[2], 0.5f);
        EXPECT_FLOAT_EQ(out[3], 0.5f);
    }

    TEST(AudioSchedulerTests, MultipleCommandsAtSameOffsetAllApply)
    {
        const std::vector<float> src(8, 1.0f);
        AudioScheduler sched(8, 64);
        ASSERT_TRUE(sched.post(play_cmd(view(src, 1, 48000), 0, 0.25f, true)));
        ASSERT_TRUE(sched.post(play_cmd(view(src, 1, 48000), 0, 0.25f, true)));

        std::vector<float> out(4, 0.0f);
        sched.process(out.data(), 4, 1, 48000);

        for (float s : out)
            EXPECT_FLOAT_EQ(s, 0.5f);
        EXPECT_EQ(sched.mixer().active_voice_count(), 2u);
    }

    TEST(AudioSchedulerTests, FullQueueRejectsPost)
    {
        const std::vector<float> src(8, 1.0f);
        AudioScheduler sched(8, /*queue capacity*/ 2); // usable depth 1

        EXPECT_TRUE(sched.post(play_cmd(view(src, 1, 48000), 0)));
        EXPECT_FALSE(sched.post(play_cmd(view(src, 1, 48000), 0)));
    }

    TEST(AudioSchedulerTests, SampleClockAccumulatesAcrossBlocks)
    {
        AudioScheduler sched(8, 64);
        std::vector<float> out(4, 0.0f);
        sched.process(out.data(), 4, 1, 48000);
        sched.process(out.data(), 4, 1, 48000);
        EXPECT_EQ(sched.sample_clock(), 8u);
    }

    // Real cross-thread handoff: a producer thread posts P looping voices while
    // the consumer drains. Looping voices never self-deactivate, so once all P
    // are applied the active count is exactly P regardless of timing — a
    // timing-independent assertion.
    TEST(AudioSchedulerTests, CrossThreadHandoffAppliesAllCommands)
    {
        constexpr uint32_t kVoices = 8;
        const std::vector<float> src(64, 1.0f);
        AudioScheduler sched(kVoices, 1024);

        std::atomic<bool> producing{true};
        std::thread producer([&] {
            for (uint32_t i = 0; i < kVoices; ++i) {
                AudioCommand c =
                    play_cmd(view(src, 1, 48000), 0, 1.0f, true, i + 1);
                while (!sched.post(c))
                    std::this_thread::yield(); // retry until queue drains
            }
            producing.store(false);
        });

        std::vector<float> out(32, 0.0f);
        for (int guard = 0; guard < 1000000; ++guard) {
            sched.process(out.data(), 32, 1, 48000);
            if (!producing.load() && sched.mixer().active_voice_count() == kVoices)
                break;
        }

        producer.join();
        // Drain anything still queued after the producer finished.
        sched.process(out.data(), 32, 1, 48000);

        EXPECT_EQ(sched.mixer().active_voice_count(), kVoices);
    }

    // ─── Grain-cloud commands through the queue ───────────────────────────────────

    TEST(AudioSchedulerTests, PlayGrainCloudCommandStartsGenerator)
    {
        const std::vector<float> src(4800, 1.0f);
        AudioScheduler sched(8);

        // The descriptor is carried by pointer; keep it alive across process().
        GrainCloudDesc desc;
        desc.sources[0] = view(src, 1, 48000);
        desc.weights[0] = 1.0f;
        desc.source_count = 1;
        desc.max_grains = 16;
        desc.density = 300.0f;
        desc.grain_ms = 10.0f;

        AudioCommand c;
        c.type = AudioCommandType::PlayGrainCloud;
        c.sample_time = 0;
        c.grain = &desc;
        c.client_id = 5u;
        ASSERT_TRUE(sched.post(c));

        std::vector<float> out(2400, 0.0f);
        sched.process(out.data(), 2400, 1, 48000);
        EXPECT_EQ(sched.mixer().active_grain_cloud_count(), 1u);

        double e = 0.0;
        for (float s : out) e += std::abs(static_cast<double>(s));
        EXPECT_GT(e, 0.0);
    }

    TEST(AudioSchedulerTests, SetGrainParamCommandRoutesToCloud)
    {
        const std::vector<float> src(4800, 1.0f);
        AudioScheduler sched(8);

        GrainCloudDesc desc;
        desc.sources[0] = view(src, 1, 48000);
        desc.weights[0] = 1.0f;
        desc.source_count = 1;
        desc.max_grains = 16;
        desc.density = 300.0f;
        desc.grain_ms = 10.0f;

        AudioCommand start;
        start.type = AudioCommandType::PlayGrainCloud;
        start.grain = &desc;
        start.client_id = 5u;
        ASSERT_TRUE(sched.post(start));

        std::vector<float> a(2400, 0.0f);
        sched.process(a.data(), 2400, 1, 48000);
        double ea = 0.0;
        for (float s : a) ea += std::abs(static_cast<double>(s));
        ASSERT_GT(ea, 0.0);

        // Gain -> 0 via SetGrainParam routed by client id silences the cloud.
        AudioCommand mute;
        mute.type = AudioCommandType::SetGrainParam;
        mute.grain_param = static_cast<uint8_t>(GrainParam::Gain);
        mute.value = 0.0f;
        mute.ramp_frames = 0;
        mute.client_id = 5u;
        ASSERT_TRUE(sched.post(mute));

        std::vector<float> b(2400, 0.0f);
        sched.process(b.data(), 2400, 1, 48000);
        double eb = 0.0;
        for (float s : b) eb += std::abs(static_cast<double>(s));
        EXPECT_NEAR(eb, 0.0, 1.0e-4);
    }

} // namespace wz::audio::test
