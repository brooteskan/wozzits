#include <gtest/gtest.h>

#include <engine/assets/sky_gaussian/sky_gaussian.h>
#include <engine/assets/sky_gaussian/sky_gaussian_serialize.h>

#include <external/json/json_parser.h>
#include <external/json/json_writer.h>
#include <math/vec3.h>

#include <string>

using namespace wz::engine::assets::sky;
using wz::math::Vec3;

namespace
{
    void expect_vec3_near(const Vec3& a, const Vec3& b, float tol)
    {
        EXPECT_NEAR(a.x, b.x, tol);
        EXPECT_NEAR(a.y, b.y, tol);
        EXPECT_NEAR(a.z, b.z, tol);
    }
} // namespace

TEST(SkyGaussianSerialize, RoundTripPreservesFields)
{
    SkyGaussianSet in;
    in.source_name = "studio_sky.exr";
    in.source_width = 2048;
    in.source_height = 1024;

    {
        SkyGaussianLobe g;
        g.direction = wz::math::normalize(Vec3{ 0.1f, 1.0f, 0.2f });
        g.sharpness = 123.5f;
        g.amplitude = Vec3{ 4.25f, 2.5f, 0.75f };
        in.lobes.push_back(g);
    }
    {
        SkyGaussianLobe g;
        g.direction = wz::math::normalize(Vec3{ -1.0f, 0.0f, 0.5f });
        g.sharpness = 12.0f;
        g.amplitude = Vec3{ 0.5f, 0.6f, 0.7f };
        in.lobes.push_back(g);
    }
    {
        SkyPointSource ps;
        ps.direction = wz::math::normalize(Vec3{ 0.3f, 0.9f, -0.1f });
        ps.radiance = Vec3{ 120.0f, 118.0f, 100.0f };
        ps.solid_angle = 6.8e-5f;
        in.point_sources.push_back(ps);
    }

    const wz::json::JSONValue json = sky_gaussian_to_json(in);
    const std::string text = wz::json::serialize_json(json);

    const wz::json::JSONParseResult parsed =
        wz::json::parse_json_string(text);
    ASSERT_TRUE(parsed.ok) << parsed.error.message;
    ASSERT_TRUE(parsed.document.root != nullptr);

    SkyGaussianSet out;
    std::string error;
    ASSERT_TRUE(sky_gaussian_from_json(*parsed.document.root, out, error))
        << error;

    EXPECT_EQ(out.source_name, in.source_name);
    EXPECT_EQ(out.source_width, in.source_width);
    EXPECT_EQ(out.source_height, in.source_height);

    ASSERT_EQ(out.lobes.size(), in.lobes.size());
    for (size_t i = 0; i < in.lobes.size(); ++i) {
        expect_vec3_near(out.lobes[i].direction, in.lobes[i].direction, 1e-5f);
        EXPECT_NEAR(out.lobes[i].sharpness, in.lobes[i].sharpness, 1e-4f);
        expect_vec3_near(out.lobes[i].amplitude, in.lobes[i].amplitude, 1e-5f);
    }

    ASSERT_EQ(out.point_sources.size(), in.point_sources.size());
    for (size_t i = 0; i < in.point_sources.size(); ++i) {
        expect_vec3_near(
            out.point_sources[i].direction, in.point_sources[i].direction,
            1e-5f);
        expect_vec3_near(
            out.point_sources[i].radiance, in.point_sources[i].radiance,
            1e-3f);
        EXPECT_NEAR(out.point_sources[i].solid_angle,
            in.point_sources[i].solid_angle, 1e-8f);
    }
}
