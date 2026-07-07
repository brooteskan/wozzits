#include <gtest/gtest.h>

#include <engine/assets/sky_gaussian/sky_gaussian.h>
#include <engine/assets/sky_gaussian/sky_gaussian_fitter.h>
#include <engine/assets/sky_gaussian/sky_gaussian_serialize.h>

#include <external/json/json_writer.h>
#include <math/vec3.h>

#include <cmath>
#include <string>
#include <vector>

using namespace wz::engine::assets::sky;
using wz::math::Vec3;

namespace
{
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

    SkyGaussianSet fit_once(const EquirectSampler& src)
    {
        FitParams p;
        p.target_lobes = 6;
        p.point_source_count = 1;
        p.sample_count = 4096;
        p.refine_iterations = 6;
        p.log_domain_loss = true;
        return fit_sky_gaussians(src, p, nullptr);
    }
} // namespace

TEST(SkyGaussianDeterminism, ByteIdenticalAcrossRuns)
{
    SkyGaussianSet gt;
    auto add = [&](Vec3 dir, float sharp, Vec3 amp) {
        SkyGaussianLobe g;
        g.direction = wz::math::normalize(dir);
        g.sharpness = sharp;
        g.amplitude = amp;
        gt.lobes.push_back(g);
    };
    add(Vec3{ 1.0f, 0.3f, 0.0f }, 55.0f, Vec3{ 2.5f, 0.7f, 0.4f });
    add(Vec3{ -0.3f, 0.4f, 1.0f }, 80.0f, Vec3{ 0.5f, 2.4f, 0.9f });
    add(Vec3{ 0.1f, 1.0f, -0.4f }, 40.0f, Vec3{ 1.4f, 1.4f, 2.0f });

    const int W = 256;
    const int H = 128;
    const std::vector<float> px = render_equirect(gt, W, H);

    EquirectSampler src;
    src.pixels = px.data();
    src.width = W;
    src.height = H;
    src.channels = 4;

    const SkyGaussianSet a = fit_once(src);
    const SkyGaussianSet b = fit_once(src);

    const std::string ja =
        wz::json::serialize_json(sky_gaussian_to_json(a));
    const std::string jb =
        wz::json::serialize_json(sky_gaussian_to_json(b));

    EXPECT_EQ(ja, jb);
}
