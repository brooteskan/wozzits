#include <gtest/gtest.h>

#include <engine/assets/sky_gaussian/sky_gaussian.h>
#include <engine/assets/sky_gaussian/sky_gaussian_fitter.h>

#include <math/vec3.h>

#include <cmath>
#include <vector>

using namespace wz::engine::assets::sky;
using wz::math::Vec3;

namespace
{
    std::vector<float> render_set(const SkyGaussianSet& s, int W, int H)
    {
        std::vector<float> px(static_cast<size_t>(W) * H * 4, 0.0f);
        for (int y = 0; y < H; ++y) {
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(H);
            const float phi = (0.5f - v) * kPi;
            const float cph = std::cos(phi), sph = std::sin(phi);
            for (int x = 0; x < W; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(W);
                const float theta = (u - 0.5f) * kTwoPi;
                const Vec3 dir{ cph * std::sin(theta), sph, cph * std::cos(theta) };
                const Vec3 c = evaluate_set(s, dir);
                const size_t idx = (static_cast<size_t>(y) * W + x) * 4;
                px[idx + 0] = c.x; px[idx + 1] = c.y; px[idx + 2] = c.z; px[idx + 3] = 1.0f;
            }
        }
        return px;
    }

    SkyGaussianSet colourful_source()
    {
        SkyGaussianSet s;
        auto add = [&](Vec3 d, float sh, Vec3 a) {
            s.lobes.push_back({ wz::math::normalize(d), sh, a });
        };
        add(Vec3{ 1.0f, 0.3f, 0.2f }, 40.0f, Vec3{ 3.0f, 0.8f, 0.4f });
        add(Vec3{ -0.3f, 0.5f, 1.0f }, 55.0f, Vec3{ 0.6f, 2.5f, 1.2f });
        add(Vec3{ 0.1f, 0.95f, -0.2f }, 45.0f, Vec3{ 1.5f, 1.6f, 2.2f });
        return s;
    }

    SkyGaussianSet fit_with(const std::vector<float>& px, int W, int H,
                            const FitParams& p)
    {
        EquirectSampler src;
        src.pixels = px.data(); src.width = W; src.height = H; src.channels = 4;
        return fit_sky_gaussians(src, p, nullptr);
    }
}

// The grade is applied AFTER fitting, so a base fit and a graded fit share
// identical lobes and differ only by the (deterministic) colour transform.
TEST(SkyGaussianGrade, ExposureSaturationTintAreExactPostTransforms)
{
    const int W = 256, H = 128;
    const std::vector<float> px = render_set(colourful_source(), W, H);

    FitParams base;
    base.target_lobes = 8;
    base.point_source_count = 0;

    const SkyGaussianSet b = fit_with(px, W, H, base);
    ASSERT_GT(b.lobes.size(), 0u);

    FitParams pe = base; pe.exposure = 3.0f;
    FitParams ps = base; ps.saturation = 0.0f;
    FitParams pt = base; pt.tint = Vec3{ 2.0f, 0.5f, 0.0f };

    const SkyGaussianSet e = fit_with(px, W, H, pe);
    const SkyGaussianSet s = fit_with(px, W, H, ps);
    const SkyGaussianSet t = fit_with(px, W, H, pt);

    ASSERT_EQ(e.lobes.size(), b.lobes.size());
    ASSERT_EQ(s.lobes.size(), b.lobes.size());
    ASSERT_EQ(t.lobes.size(), b.lobes.size());

    for (size_t i = 0; i < b.lobes.size(); ++i) {
        const Vec3 a = b.lobes[i].amplitude;

        // Same underlying fit: directions/sharpness unchanged by the grade.
        EXPECT_FLOAT_EQ(e.lobes[i].direction.x, b.lobes[i].direction.x);
        EXPECT_FLOAT_EQ(e.lobes[i].sharpness, b.lobes[i].sharpness);

        // Exposure: uniform brightness multiply.
        EXPECT_FLOAT_EQ(e.lobes[i].amplitude.x, 3.0f * a.x);
        EXPECT_FLOAT_EQ(e.lobes[i].amplitude.y, 3.0f * a.y);
        EXPECT_FLOAT_EQ(e.lobes[i].amplitude.z, 3.0f * a.z);

        // Saturation 0: every channel collapses to luminance (grey).
        const float l = luminance(a);
        EXPECT_FLOAT_EQ(s.lobes[i].amplitude.x, l);
        EXPECT_FLOAT_EQ(s.lobes[i].amplitude.y, l);
        EXPECT_FLOAT_EQ(s.lobes[i].amplitude.z, l);

        // Tint: per-channel colour multiply (blue killed here).
        EXPECT_FLOAT_EQ(t.lobes[i].amplitude.x, 2.0f * a.x);
        EXPECT_FLOAT_EQ(t.lobes[i].amplitude.y, 0.5f * a.y);
        EXPECT_FLOAT_EQ(t.lobes[i].amplitude.z, 0.0f);
    }
}

// A sky-only fit (ground_weight = 0) must place no lobes below the horizon,
// even when the source has a bright feature there.
TEST(SkyGaussianHemisphere, GroundWeightZeroExcludesBelowHorizonLobes)
{
    SkyGaussianSet src_set;
    src_set.lobes.push_back({ Vec3{ 0.0f, 1.0f, 0.0f }, 30.0f, Vec3{ 2.0f, 2.0f, 2.5f } });
    src_set.lobes.push_back({ Vec3{ 0.0f, -1.0f, 0.0f }, 30.0f, Vec3{ 2.5f, 2.0f, 1.5f } });

    const int W = 256, H = 128;
    const std::vector<float> px = render_set(src_set, W, H);

    FitParams full;                 // full sphere
    full.target_lobes = 16;
    full.point_source_count = 0;
    const SkyGaussianSet a = fit_with(px, W, H, full);

    FitParams sky;                  // sky only
    sky.target_lobes = 16;
    sky.point_source_count = 0;
    sky.ground_weight = 0.0f;
    const SkyGaussianSet b = fit_with(px, W, H, sky);

    // Full-sphere fit represents the nadir feature; sky-only fit does not.
    float a_min_y = 1.0f, b_min_y = 1.0f;
    for (const auto& lb : a.lobes) a_min_y = std::min(a_min_y, lb.direction.y);
    for (const auto& lb : b.lobes) b_min_y = std::min(b_min_y, lb.direction.y);

    EXPECT_LT(a_min_y, -0.5f) << "full-sphere fit should capture the nadir lobe";
    EXPECT_GT(b_min_y, -0.05f) << "sky-only fit should place no below-horizon lobes";
}
