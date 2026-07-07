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
    // A smooth, low-frequency sky: a zenith->horizon->nadir colour gradient
    // plus a bright spot. Exactly the kind of broad content whose lobes used to
    // over-subtract and crater the residual, terminating the fit far short of
    // target (the 36/1024 regression). Filled via the inverse of
    // EquirectSampler's forward mapping so directions line up.
    std::vector<float> render_sky(int W, int H)
    {
        std::vector<float> px(static_cast<size_t>(W) * H * 4, 0.0f);
        const Vec3 zenith{ 0.15f, 0.30f, 0.90f };
        const Vec3 horizon{ 0.70f, 0.75f, 0.85f };
        const Vec3 ground{ 0.10f, 0.14f, 0.08f };
        const Vec3 sun_dir = wz::math::normalize(Vec3{ 0.3f, 0.7f, 0.4f });
        for (int y = 0; y < H; ++y) {
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(H);
            const float phi = (0.5f - v) * kPi;
            const float cph = std::cos(phi), sph = std::sin(phi);
            for (int x = 0; x < W; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(W);
                const float theta = (u - 0.5f) * kTwoPi;
                const Vec3 dir{ cph * std::sin(theta), sph, cph * std::cos(theta) };
                Vec3 c = dir.y >= 0.0f
                    ? horizon * (1.0f - dir.y) + zenith * dir.y
                    : horizon * (1.0f + dir.y) + ground * (-dir.y);
                const float s = std::max(0.0f, wz::math::dot(dir, sun_dir));
                c = c + Vec3{ 2.0f, 2.0f, 1.8f } * std::pow(s, 200.0f);
                const size_t idx = (static_cast<size_t>(y) * W + x) * 4;
                px[idx + 0] = c.x; px[idx + 1] = c.y; px[idx + 2] = c.z; px[idx + 3] = 1.0f;
            }
        }
        return px;
    }

    int fit_count(const std::vector<float>& px, int W, int H, int target, int samples)
    {
        EquirectSampler src;
        src.pixels = px.data(); src.width = W; src.height = H; src.channels = 4;
        FitParams p;
        p.target_lobes = target;
        p.point_source_count = 1;
        p.sample_count = samples;
        FitReport r;
        const SkyGaussianSet set = fit_sky_gaussians(src, p, &r);
        EXPECT_EQ(static_cast<int>(set.lobes.size()), r.lobes_placed);
        return r.lobes_placed;
    }
}

// The fit must actually reach target_lobes and must do so independent of
// sample_count. Before the non-negative-residual fix, a higher sample count
// cratered the residual sooner and placed FEWER lobes (192 @ 8k -> 36 @ 16k).
TEST(SkyGaussianConvergence, ReachesTargetAndIsSampleCountStable)
{
    const int W = 256, H = 128, target = 64;
    const std::vector<float> px = render_sky(W, H);

    const int c8k = fit_count(px, W, H, target, 8192);
    const int c16k = fit_count(px, W, H, target, 16384);

    EXPECT_EQ(c8k, target);
    EXPECT_EQ(c16k, target);
}
