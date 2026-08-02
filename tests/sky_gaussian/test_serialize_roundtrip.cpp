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

// --- issue #316: the loader is the one choke point, so it must validate ------
//
// sky_gaussian_from_json feeds the resident buffers the shaders read raw. It
// range-checked direction and amplitude (via read_float3) and NOT sharpness or
// solid_angle, on adjacent lines of the same function, so an authored 1e39
// loaded as +inf. Hand-authored .sky_gaussian.json is a real production path --
// night_sky.sky_gaussian.json is wired into the live project graph.
namespace
{
    bool load_from_text(const char* text, wz::engine::assets::sky::SkyGaussianSet& out,
                        std::string& error)
    {
        wz::json::JSONParseResult parsed =
            wz::json::parse_json_string(std::string(text));
        if (!parsed.ok || !parsed.document.root) {
            error = parsed.error.message;
            return false;
        }
        return wz::engine::assets::sky::sky_gaussian_from_json(
            *parsed.document.root, out, error);
    }
}

TEST(SkyGaussianSerialize, RejectsNonFiniteAndDegenerateFields)
{
    wz::engine::assets::sky::SkyGaussianSet set;
    std::string err;

    // A control that must still load.
    EXPECT_TRUE(load_from_text(
        R"({"lobes":[{"direction":[0,0,1],"sharpness":4,"amplitude":[1,1,1]}]})",
        set, err)) << err;

    // sharpness: non-finite, zero, and negative. A negative sharpness inverts
    // the lobe into an unbounded anti-lobe growing toward its own antipode.
    for (const char* doc : {
             R"({"lobes":[{"direction":[0,0,1],"sharpness":1e39,"amplitude":[1,1,1]}]})",
             R"({"lobes":[{"direction":[0,0,1],"sharpness":0,"amplitude":[1,1,1]}]})",
             R"({"lobes":[{"direction":[0,0,1],"sharpness":-1.1,"amplitude":[1,1,1]}]})" })
    {
        EXPECT_FALSE(load_from_text(doc, set, err)) << doc;
    }

    // a degenerate direction has no direction to recover
    EXPECT_FALSE(load_from_text(
        R"({"lobes":[{"direction":[0,0,0],"sharpness":4,"amplitude":[1,1,1]}]})",
        set, err));

    // solid_angle outside (0, 4*pi] is not a cone on the sphere
    for (const char* doc : {
             R"({"point_sources":[{"direction":[0,0,1],"radiance":[1,1,1],"solid_angle":0}]})",
             R"({"point_sources":[{"direction":[0,0,1],"radiance":[1,1,1],"solid_angle":1e39}]})",
             R"({"point_sources":[{"direction":[0,0,1],"radiance":[1,1,1],"solid_angle":100}]})" })
    {
        EXPECT_FALSE(load_from_text(doc, set, err)) << doc;
    }
}

// A merely NON-UNIT direction is repaired, not rejected: the fitter always emits
// unit axes, so a 0.99-length direction in a hand-written file means the
// direction and not a scaled one. Left unnormalized it broke the closed-form SG
// bound (measured: axis x1.05 at sharpness 20000 evaluates to +inf), and for a
// point source it silently made the emitter invisible -- the repo already ships
// one, tests/render/fixtures/test_rebind_fixture/fixture_sky.sky_gaussian.json,
// whose |direction| of 0.98995 is below its own cos_r of 0.99999, so no view
// direction could ever be inside the cone.
TEST(SkyGaussianSerialize, NonUnitDirectionIsNormalizedNotRejected)
{
    wz::engine::assets::sky::SkyGaussianSet set;
    std::string err;

    ASSERT_TRUE(load_from_text(
        R"({"lobes":[{"direction":[0,2,0],"sharpness":4,"amplitude":[1,1,1]}],
            "point_sources":[{"direction":[0.3,0.8,0.5],"radiance":[1,1,1],
                              "solid_angle":6e-05}]})",
        set, err)) << err;

    ASSERT_EQ(set.lobes.size(), 1u);
    EXPECT_NEAR(wz::math::length(set.lobes[0].direction), 1.0f, 1e-6f);

    ASSERT_EQ(set.point_sources.size(), 1u);
    const float len = wz::math::length(set.point_sources[0].direction);
    EXPECT_NEAR(len, 1.0f, 1e-6f);
    // the fixture's own cone: a unit direction is now reachable by a view ray
    const float cos_r = 1.0f - 6e-05f / (2.0f * 3.14159265358979323846f);
    EXPECT_GE(len, cos_r);
}
