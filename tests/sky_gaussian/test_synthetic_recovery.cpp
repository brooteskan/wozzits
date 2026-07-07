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
    SkyGaussianSet make_ground_truth()
    {
        SkyGaussianSet gt;
        auto add = [&](Vec3 dir, float sharp, Vec3 amp) {
            SkyGaussianLobe g;
            g.direction = wz::math::normalize(dir);
            g.sharpness = sharp;
            g.amplitude = amp;
            gt.lobes.push_back(g);
        };
        // Four well-separated lobes (all pairwise > 60 deg apart), kept off the
        // equirect poles, with distinct RGB and distinct sharpness.
        add(Vec3{ 1.0f, 0.2f, 0.2f }, 50.0f, Vec3{ 3.0f, 0.6f, 0.6f });
        add(Vec3{ -0.2f, 0.3f, 1.0f }, 70.0f, Vec3{ 0.5f, 3.0f, 0.7f });
        add(Vec3{ -1.0f, 0.25f, -0.2f }, 90.0f, Vec3{ 0.6f, 0.6f, 3.0f });
        add(Vec3{ 0.2f, 0.9f, -0.3f }, 60.0f, Vec3{ 2.2f, 2.0f, 0.5f });
        return gt;
    }

    // Render into an equirect buffer using the exact inverse of
    // EquirectSampler::sample's forward mapping.
    std::vector<float> render_equirect(const SkyGaussianSet& s, int W, int H)
    {
        std::vector<float> px(static_cast<size_t>(W) * H * 4, 0.0f);
        for (int y = 0; y < H; ++y) {
            const float v = (static_cast<float>(y) + 0.5f)
                / static_cast<float>(H);
            const float phi = (0.5f - v) * kPi;
            const float cph = std::cos(phi);
            const float sph = std::sin(phi);
            for (int x = 0; x < W; ++x) {
                const float u = (static_cast<float>(x) + 0.5f)
                    / static_cast<float>(W);
                const float theta = (u - 0.5f) * kTwoPi;
                const Vec3 dir{ cph * std::sin(theta), sph,
                                cph * std::cos(theta) };
                const Vec3 c = evaluate_set(s, dir);
                const size_t idx =
                    (static_cast<size_t>(y) * W + x) * 4;
                px[idx + 0] = c.x;
                px[idx + 1] = c.y;
                px[idx + 2] = c.z;
                px[idx + 3] = 1.0f;
            }
        }
        return px;
    }
} // namespace

TEST(SkyGaussianRecovery, RecoversWellSeparatedLobes)
{
    const SkyGaussianSet gt = make_ground_truth();

    const int W = 512;
    const int H = 256;
    const std::vector<float> px = render_equirect(gt, W, H);

    EquirectSampler src;
    src.pixels = px.data();
    src.width = W;
    src.height = H;
    src.channels = 4;

    FitParams p;
    p.target_lobes = static_cast<int>(gt.lobes.size());
    p.point_source_count = 0;
    p.sample_count = 8192;
    p.refine_iterations = 8;
    p.min_sharpness = 4.0f;
    p.max_sharpness = 4000.0f;
    p.log_domain_loss = true;

    FitReport report;
    const SkyGaussianSet fit = fit_sky_gaussians(src, p, &report);
    ASSERT_EQ(static_cast<int>(fit.lobes.size()),
        static_cast<int>(gt.lobes.size()));

    std::vector<int> used(fit.lobes.size(), 0);
    for (size_t gi = 0; gi < gt.lobes.size(); ++gi) {
        const SkyGaussianLobe& g = gt.lobes[gi];

        // Match each ground-truth lobe to the closest recovered direction.
        int best = -1;
        float best_dot = -2.0f;
        for (size_t fi = 0; fi < fit.lobes.size(); ++fi) {
            const Vec3 fdir = wz::math::normalize(fit.lobes[fi].direction);
            const float d = wz::math::dot(g.direction, fdir);
            if (d > best_dot) {
                best_dot = d;
                best = static_cast<int>(fi);
            }
        }
        ASSERT_GE(best, 0);

        const float clamped = std::min(1.0f, std::max(-1.0f, best_dot));
        const float angle_deg = std::acos(clamped) * 180.0f / kPi;
        EXPECT_LT(angle_deg, 6.0f)
            << "ground-truth lobe " << gi << " direction not matched";

        EXPECT_EQ(used[best], 0)
            << "two ground-truth lobes matched the same recovered lobe";
        used[best] = 1;

        const SkyGaussianLobe& f = fit.lobes[best];

        // Sharpness within a modest relative tolerance.
        EXPECT_NEAR(f.sharpness, g.sharpness, 0.45f * g.sharpness)
            << "lobe " << gi << " sharpness";

        // Amplitude within a modest relative tolerance (+ small abs floor for
        // the dim channels).
        EXPECT_NEAR(f.amplitude.x, g.amplitude.x, 0.35f * g.amplitude.x + 0.15f);
        EXPECT_NEAR(f.amplitude.y, g.amplitude.y, 0.35f * g.amplitude.y + 0.15f);
        EXPECT_NEAR(f.amplitude.z, g.amplitude.z, 0.35f * g.amplitude.z + 0.15f);
    }

    // Sanity: the fit should reproduce most of the total energy.
    EXPECT_GT(report.energy_ratio, 0.75f);
    EXPECT_LT(report.energy_ratio, 1.25f);
}
