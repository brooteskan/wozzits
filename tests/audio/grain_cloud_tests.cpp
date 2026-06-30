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

    // The autonomous blend LFO cycles source selection over time with NO external
    // driver — energy swings as the loud clip fades in and out, vs. steady when off.
    TEST(GrainCloudTests, BlendLfoCyclesSourceSelectionAutonomously)
    {
        const std::vector<float> loud(48000, 1.0f);   // clip A: DC 1.0
        const std::vector<float> silent(48000, 0.0f); // clip B: DC 0.0 (valid)
        const AudioBufferView views[2] = {
            view(loud, 1, 48000), view(silent, 1, 48000),
        };
        const float weights[2] = { 1.0f, 1.0f };

        auto windowed_energy = [&](float rate, float depth) {
            GrainCloud cloud;
            cloud.configure(GrainCloud::Config{ .max_grains = 32, .seed = 99u });
            cloud.set_sources(views, weights, 2);
            cloud.set_density(800.0f);   // dense → many grains per window
            cloud.set_grain_size(5.0f);  // short grains track the LFO closely
            cloud.set_blend(rate, depth);
            cloud.start();
            std::vector<float> out(48000, 0.0f);  // 1 s = one full 1 Hz cycle
            cloud.render_add(out.data(), 48000, 1, 48000);
            std::vector<double> e(8, 0.0);         // 8 windows
            for (size_t f = 0; f < out.size(); ++f) {
                e[f / 6000] += std::abs(static_cast<double>(out[f]));
            }
            return e;
        };

        // Blend ON (1 Hz, full depth): A dominates for part of the cycle (loud) and
        // B (silent) for another → big energy swing across the windows.
        const auto on = windowed_energy(1.0f, 1.0f);
        const double on_max = *std::max_element(on.begin(), on.end());
        const double on_min = *std::min_element(on.begin(), on.end());
        EXPECT_GT(on_max, on_min * 3.0);

        // Blend OFF: a steady 50/50 selection → roughly even energy per window.
        const auto off = windowed_energy(0.0f, 0.0f);
        const double off_max = *std::max_element(off.begin(), off.end());
        const double off_min = *std::min_element(off.begin(), off.end());
        EXPECT_LT(off_max, off_min * 1.5);
    }

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

    // ─── Window-energy normalization ─────────────────────────────────────────────
    //
    // normalize > 0 scales each frame's grain sum by sqrt(N_ref)/sqrt(Σenv² + N_ref)
    // so loudness stays roughly constant as overlap density swings, WITHOUT touching
    // grain window shapes (the +N_ref floor leaves lone grains unchanged).

    namespace {
        double rms(const std::vector<float>& b)
        {
            double e = 0.0;
            for (float s : b) e += static_cast<double>(s) * static_cast<double>(s);
            return std::sqrt(e / static_cast<double>(b.size()));
        }
    }

    // normalize == 0 must be byte-for-byte identical to the legacy (no-normalize)
    // render — the feature is strictly opt-in.
    TEST(GrainCloudTests, NormalizeOffMatchesLegacy)
    {
        const std::vector<float> src = dc_source();
        const AudioBufferView v = view(src, 1, 48000);

        auto run = [&](float normalize) {
            GrainCloud cloud;
            cloud.configure(small_cfg());  // same seed => identical grain stream
            cloud.set_sources(&v, nullptr, 1);
            cloud.set_density(500.0f);
            cloud.set_grain_size(12.0f);
            cloud.set_normalize(normalize);
            cloud.start();
            std::vector<float> out(2400 * 2, 0.0f);
            cloud.render_add(out.data(), 2400, 2, 48000);
            return out;
        };

        const std::vector<float> legacy = run(0.0f);  // never called set_normalize > 0
        const std::vector<float> off = run(0.0f);
        ASSERT_EQ(legacy.size(), off.size());
        for (size_t i = 0; i < legacy.size(); ++i) {
            EXPECT_EQ(legacy[i], off[i]) << "at " << i;  // exact, not NEAR
        }
    }

    // ON should flatten the loudness swing between a low- and a high-density render:
    // the high/low RMS ratio is much closer to 1 with normalization than without.
    TEST(GrainCloudTests, NormalizeFlattensLevelAcrossDensity)
    {
        const std::vector<float> src = dc_source();
        const AudioBufferView v = view(src, 1, 48000);

        auto run = [&](float density, float normalize) {
            GrainCloud cloud;
            cloud.configure(GrainCloud::Config{ .max_grains = 64, .seed = 4242u });
            cloud.set_sources(&v, nullptr, 1);
            cloud.set_density(density);
            cloud.set_grain_size(40.0f);  // long grains => high overlap at density
            cloud.set_normalize(normalize);
            cloud.start();
            std::vector<float> out(9600, 0.0f);  // 200 ms
            cloud.render_add(out.data(), 9600, 1, 48000);
            return rms(out);
        };

        // Densities chosen so high clearly overlaps many more grains than low.
        const double low_off = run(80.0f, 0.0f);
        const double high_off = run(1200.0f, 0.0f);
        const double low_on = run(80.0f, 2.0f);
        const double high_on = run(1200.0f, 2.0f);

        ASSERT_GT(low_off, 0.0);
        ASSERT_GT(low_on, 0.0);

        const double ratio_off = high_off / low_off;
        const double ratio_on = high_on / low_on;

        // OFF: loudness pumps hard with density. ON: the ratio collapses toward 1.
        EXPECT_GT(ratio_off, 2.0);
        EXPECT_LT(ratio_on, ratio_off);
        EXPECT_NEAR(ratio_on, 1.0, std::abs(ratio_off - 1.0) * 0.5);
    }

    // At very low density (effectively isolated grains) the +N_ref floor means a
    // lone grain passes through nearly unchanged — no edge boost / click blow-up.
    TEST(GrainCloudTests, NormalizeKeepsLoneGrainSmooth)
    {
        const std::vector<float> src = dc_source();
        const AudioBufferView v = view(src, 1, 48000);

        auto run = [&](float normalize) {
            GrainCloud cloud;
            cloud.configure(small_cfg());  // same seed => same lone-grain stream
            cloud.set_sources(&v, nullptr, 1);
            cloud.set_density(20.0f);     // sparse: grains rarely overlap
            cloud.set_grain_size(8.0f);   // short => isolated single grains
            cloud.set_normalize(normalize);
            cloud.start();
            std::vector<float> out(9600, 0.0f);  // 200 ms
            cloud.render_add(out.data(), 9600, 1, 48000);
            return out;
        };

        const std::vector<float> bare = run(0.0f);
        const std::vector<float> norm = run(2.0f);
        ASSERT_EQ(bare.size(), norm.size());

        double peak_bare = 0.0, peak_norm = 0.0;
        for (size_t i = 0; i < bare.size(); ++i) {
            peak_bare = std::max(peak_bare, std::abs(static_cast<double>(bare[i])));
            peak_norm = std::max(peak_norm, std::abs(static_cast<double>(norm[i])));
        }
        ASSERT_GT(peak_bare, 0.0);

        // Normalization must NEVER amplify a lone grain (env² <= 1, N_ref = 2 =>
        // norm = sqrt(2)/sqrt(env²+2) < 1) and must stay within a small factor.
        EXPECT_LE(peak_norm, peak_bare + 1.0e-6);
        EXPECT_GT(peak_norm, peak_bare * 0.6);
    }

    // Same seed + params (with normalize on) must reproduce identical output.
    TEST(GrainCloudTests, NormalizeIsDeterministic)
    {
        const std::vector<float> src = dc_source();
        const AudioBufferView v = view(src, 1, 48000);

        auto run = [&]() {
            GrainCloud cloud;
            cloud.configure(GrainCloud::Config{ .max_grains = 32, .seed = 555u });
            cloud.set_sources(&v, nullptr, 1);
            cloud.set_density(600.0f);
            cloud.set_grain_size(15.0f);
            cloud.set_position_jitter(0.3f);
            cloud.set_pan(0.0f, 0.7f);
            cloud.set_normalize(3.0f);
            cloud.start();
            std::vector<float> out(4800 * 2, 0.0f);
            cloud.render_add(out.data(), 4800, 2, 48000);
            return out;
        };

        const std::vector<float> a = run();
        const std::vector<float> b = run();
        ASSERT_EQ(a.size(), b.size());
        for (size_t i = 0; i < a.size(); ++i) {
            EXPECT_FLOAT_EQ(a[i], b[i]) << "at " << i;
        }
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
