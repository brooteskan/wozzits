// tests/audio/grain_cloud_tests.cpp
//
// Unit tests for the granular generator (GrainCloud) and its window function.
// Pure DSP — renders into a buffer with no asset/scene/device dependency, so it
// doubles as the offline grain-render harness. Covers the window contract,
// determinism (seeded PRNG), density/gain/pan behavior, source blending, the
// grain-pool CPU bound, and the stop-tail (grains finish after spawning stops).

#include <gtest/gtest.h>

#include <audio/grain_cloud.h>
#include <audio/grain_window.h>

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

        // 100 ms of mono DC at unity — a flat source so the rendered output is the
        // grain windows themselves (no source content to confound shape tests).
        std::vector<float> dc_source(uint32_t rate = 48000)
        {
            return std::vector<float>(rate / 10, 1.0f);
        }

        double energy(const std::vector<float>& b)
        {
            double e = 0.0;
            for (float s : b) e += std::abs(static_cast<double>(s));
            return e;
        }

        // A cloud configured to emit a steady mono DC texture, ready to start().
        GrainCloud::Config small_cfg(uint32_t seed = 12345u)
        {
            return GrainCloud::Config{ .max_grains = 16, .seed = seed };
        }
    }

    // ─── Window function contract ─────────────────────────────────────────────────

    TEST(GrainWindowTests, GaussianIsZeroAtEdgesPeaksAtCenter)
    {
        EXPECT_FLOAT_EQ(grain_window_gain(GrainWindow::Gaussian, 0.0f, 0.4f), 0.0f);
        EXPECT_FLOAT_EQ(grain_window_gain(GrainWindow::Gaussian, 1.0f, 0.4f), 0.0f);

        const float mid = grain_window_gain(GrainWindow::Gaussian, 0.5f, 0.4f);
        EXPECT_NEAR(mid, 1.0f, 1.0e-5f);

        // Symmetric and rising toward the center.
        const float a = grain_window_gain(GrainWindow::Gaussian, 0.25f, 0.4f);
        const float b = grain_window_gain(GrainWindow::Gaussian, 0.75f, 0.4f);
        EXPECT_NEAR(a, b, 1.0e-5f);
        EXPECT_GT(mid, a);
        EXPECT_GT(a, 0.0f);
    }

    // ─── Emission gating ──────────────────────────────────────────────────────────

    TEST(GrainCloudTests, SilentUntilStarted)
    {
        const std::vector<float> src = dc_source();
        const AudioBufferView v = view(src, 1, 48000);

        GrainCloud cloud;
        cloud.configure(small_cfg());
        cloud.set_sources(&v, nullptr, 1);
        cloud.set_density(200.0f);
        cloud.set_grain_size(10.0f);
        // Not started.

        std::vector<float> out(480, 0.0f);
        cloud.render_add(out.data(), 480, 1, 48000);
        EXPECT_DOUBLE_EQ(energy(out), 0.0);
        EXPECT_FALSE(cloud.active());
    }

    TEST(GrainCloudTests, SilentWhenDensityZero)
    {
        const std::vector<float> src = dc_source();
        const AudioBufferView v = view(src, 1, 48000);

        GrainCloud cloud;
        cloud.configure(small_cfg());
        cloud.set_sources(&v, nullptr, 1);
        cloud.set_grain_size(10.0f);
        cloud.start();  // emitting, but density defaults to 0

        std::vector<float> out(480, 0.0f);
        cloud.render_add(out.data(), 480, 1, 48000);
        EXPECT_DOUBLE_EQ(energy(out), 0.0);
    }

    TEST(GrainCloudTests, ProducesAudioWhenStartedWithDensity)
    {
        const std::vector<float> src = dc_source();
        const AudioBufferView v = view(src, 1, 48000);

        GrainCloud cloud;
        cloud.configure(small_cfg());
        cloud.set_sources(&v, nullptr, 1);
        cloud.set_density(300.0f);
        cloud.set_grain_size(10.0f);
        cloud.start();

        std::vector<float> out(2400, 0.0f);  // 50 ms
        cloud.render_add(out.data(), 2400, 1, 48000);
        EXPECT_GT(energy(out), 0.0);
        EXPECT_GT(cloud.active_grain_count(), 0u);
    }

    // ─── Determinism ──────────────────────────────────────────────────────────────

    TEST(GrainCloudTests, DeterministicForFixedSeed)
    {
        const std::vector<float> src = dc_source();
        const AudioBufferView v = view(src, 1, 48000);

        auto run = [&](uint32_t seed) {
            GrainCloud cloud;
            cloud.configure(GrainCloud::Config{ .max_grains = 16, .seed = seed });
            cloud.set_sources(&v, nullptr, 1);
            cloud.set_density(400.0f);
            cloud.set_grain_size(8.0f);
            cloud.set_position(0.5f);
            cloud.set_position_jitter(0.4f);
            cloud.set_pitch_jitter(3.0f);
            cloud.set_pan(0.0f, 0.8f);
            cloud.start();
            std::vector<float> out(2400 * 2, 0.0f);
            cloud.render_add(out.data(), 2400, 2, 48000);
            return out;
        };

        const std::vector<float> a = run(777u);
        const std::vector<float> b = run(777u);
        ASSERT_EQ(a.size(), b.size());
        for (size_t i = 0; i < a.size(); ++i) {
            EXPECT_FLOAT_EQ(a[i], b[i]) << "at " << i;
        }

        // A different seed exercises the jitter PRNG differently → different texture.
        const std::vector<float> c = run(778u);
        EXPECT_NE(energy(a), energy(c));
    }

    // ─── Param behavior ───────────────────────────────────────────────────────────

    TEST(GrainCloudTests, HigherDensityRaisesEnergy)
    {
        const std::vector<float> src = dc_source();
        const AudioBufferView v = view(src, 1, 48000);

        auto run = [&](float density) {
            GrainCloud cloud;
            cloud.configure(small_cfg());
            cloud.set_sources(&v, nullptr, 1);
            cloud.set_density(density);
            cloud.set_grain_size(10.0f);
            cloud.start();
            std::vector<float> out(4800, 0.0f);
            cloud.render_add(out.data(), 4800, 1, 48000);
            return energy(out);
        };

        EXPECT_GT(run(600.0f), run(150.0f));
    }

    TEST(GrainCloudTests, GainScalesOutputLinearly)
    {
        const std::vector<float> src = dc_source();
        const AudioBufferView v = view(src, 1, 48000);

        auto run = [&](float gain) {
            GrainCloud cloud;
            cloud.configure(small_cfg());  // same seed => identical grain stream
            cloud.set_sources(&v, nullptr, 1);
            cloud.set_density(300.0f);
            cloud.set_grain_size(10.0f);
            cloud.set_gain(gain);
            cloud.start();
            std::vector<float> out(2400, 0.0f);
            cloud.render_add(out.data(), 2400, 1, 48000);
            return out;
        };

        const std::vector<float> full = run(1.0f);
        const std::vector<float> half = run(0.5f);
        ASSERT_EQ(full.size(), half.size());
        for (size_t i = 0; i < full.size(); ++i) {
            EXPECT_NEAR(half[i], 0.5f * full[i], 1.0e-6f);
        }
    }

    TEST(GrainCloudTests, PanRoutesToOneChannel)
    {
        const std::vector<float> src = dc_source();
        const AudioBufferView v = view(src, 1, 48000);

        auto run = [&](float pan) {
            GrainCloud cloud;
            cloud.configure(small_cfg());
            cloud.set_sources(&v, nullptr, 1);
            cloud.set_density(300.0f);
            cloud.set_grain_size(10.0f);
            cloud.set_pan(pan, 0.0f);  // no spread => exact pan
            cloud.start();
            std::vector<float> out(2400 * 2, 0.0f);
            cloud.render_add(out.data(), 2400, 2, 48000);
            double l = 0.0, r = 0.0;
            for (size_t f = 0; f < 2400; ++f) {
                l += std::abs(out[f * 2 + 0]);
                r += std::abs(out[f * 2 + 1]);
            }
            return std::pair<double, double>{ l, r };
        };

        const auto [ll, lr] = run(-1.0f);  // hard left
        EXPECT_GT(ll, 0.0);
        EXPECT_NEAR(lr, 0.0, 1.0e-4);

        const auto [rl, rr] = run(1.0f);   // hard right
        EXPECT_NEAR(rl, 0.0, 1.0e-4);
        EXPECT_GT(rr, 0.0);
    }

    // ─── Source blending ──────────────────────────────────────────────────────────

    TEST(GrainCloudTests, ZeroWeightSourceIsNeverChosen)
    {
        // Source 0 is DC +1, source 1 is DC -1 but weight 0 → output stays >= 0.
        const std::vector<float> a(4800, 1.0f);
        const std::vector<float> b(4800, -1.0f);
        const AudioBufferView views[2] = {
            view(a, 1, 48000), view(b, 1, 48000),
        };
        const float weights[2] = { 1.0f, 0.0f };

        GrainCloud cloud;
        cloud.configure(small_cfg());
        cloud.set_sources(views, weights, 2);
        cloud.set_density(400.0f);
        cloud.set_grain_size(10.0f);
        cloud.start();

        std::vector<float> out(4800, 0.0f);
        cloud.render_add(out.data(), 4800, 1, 48000);
        EXPECT_GT(energy(out), 0.0);
        for (float s : out) {
            EXPECT_GE(s, -1.0e-6f);  // never drew from the negative source
        }
    }

    // ─── CPU bound + stop tail ────────────────────────────────────────────────────

    TEST(GrainCloudTests, GrainPoolCapsActiveGrains)
    {
        const std::vector<float> src = dc_source();
        const AudioBufferView v = view(src, 1, 48000);

        GrainCloud cloud;
        cloud.configure(GrainCloud::Config{ .max_grains = 4, .seed = 1u });
        cloud.set_sources(&v, nullptr, 1);
        cloud.set_density(1.0e6f);  // absurd rate
        cloud.set_grain_size(20.0f);
        cloud.start();

        std::vector<float> out(960, 0.0f);
        cloud.render_add(out.data(), 960, 1, 48000);
        EXPECT_LE(cloud.active_grain_count(), 4u);
    }

    TEST(GrainCloudTests, StopLetsInFlightGrainsFinish)
    {
        const std::vector<float> src = dc_source();
        const AudioBufferView v = view(src, 1, 48000);

        GrainCloud cloud;
        cloud.configure(small_cfg());
        cloud.set_sources(&v, nullptr, 1);
        cloud.set_density(300.0f);
        cloud.set_grain_size(10.0f);  // grains live ~480 frames
        cloud.start();

        std::vector<float> out(2400, 0.0f);
        cloud.render_add(out.data(), 2400, 1, 48000);
        ASSERT_GT(cloud.active_grain_count(), 0u);

        cloud.stop();
        EXPECT_TRUE(cloud.active());  // grains still sounding

        // Render past the longest possible remaining grain; the tail finishes and
        // no new grains spawn.
        std::vector<float> tail(960, 0.0f);
        cloud.render_add(tail.data(), 960, 1, 48000);
        EXPECT_EQ(cloud.active_grain_count(), 0u);
        EXPECT_FALSE(cloud.active());
    }

} // namespace wz::audio::test
