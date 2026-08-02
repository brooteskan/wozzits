#include <gtest/gtest.h>

#include <engine/lighting/sg_lighting.h>

#include <math/vec3.h>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

using namespace wz::engine::lighting;
using wz::math::Vec3;

namespace
{
    // Deterministic near-uniform sphere quadrature (golden-angle spiral). Every
    // sample carries equal solid angle 4*pi/n, so a plain sum * omega is a
    // Riemann estimate of the spherical integral. No RNG -> reproducible.
    std::vector<Vec3> fib_sphere(int n)
    {
        std::vector<Vec3> out;
        out.reserve(static_cast<std::size_t>(n));
        const float ga = kPi * (3.0f - std::sqrt(5.0f)); // golden angle
        for (int i = 0; i < n; ++i) {
            const float y = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f)
                                       / static_cast<float>(n);
            const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
            const float t = ga * static_cast<float>(i);
            out.push_back(Vec3{ r * std::cos(t), y, r * std::sin(t) });
        }
        return out;
    }
}

// ---------------------------------------------------------------------------
// Primitive
// ---------------------------------------------------------------------------

TEST(SgLighting, EvaluateAtAxisEqualsAmplitude)
{
    SphericalGaussian g;
    g.axis = wz::math::normalize(Vec3{ 0.3f, 1.0f, -0.2f });
    g.sharpness = 25.0f;
    g.amplitude = Vec3{ 1.5f, 2.0f, 0.5f };

    const Vec3 v = evaluate(g, g.axis);
    EXPECT_NEAR(v.x, 1.5f, 1e-4f);
    EXPECT_NEAR(v.y, 2.0f, 1e-4f);
    EXPECT_NEAR(v.z, 0.5f, 1e-4f);
}

TEST(SgLighting, IntegralMatchesQuadrature)
{
    SphericalGaussian g;
    g.axis = wz::math::normalize(Vec3{ 0.1f, 0.9f, 0.4f });
    g.sharpness = 7.0f;
    g.amplitude = Vec3{ 1.0f, 0.6f, 0.3f };

    const auto dirs = fib_sphere(65536);
    const float omega = 4.0f * kPi / static_cast<float>(dirs.size());

    Vec3 numeric{ 0.0f, 0.0f, 0.0f };
    for (const Vec3& w : dirs) {
        numeric = numeric + evaluate(g, w) * omega;
    }

    const Vec3 analytic = integral(g);
    EXPECT_NEAR(numeric.x, analytic.x, 0.02f * analytic.x + 1e-4f);
    EXPECT_NEAR(numeric.y, analytic.y, 0.02f * analytic.y + 1e-4f);
    EXPECT_NEAR(numeric.z, analytic.z, 0.02f * analytic.z + 1e-4f);
}

// ---------------------------------------------------------------------------
// Inner product -- the load-bearing algebra
// ---------------------------------------------------------------------------

TEST(SgLighting, InnerProductMatchesQuadrature)
{
    SphericalGaussian a;
    a.axis = wz::math::normalize(Vec3{ 0.2f, 1.0f, -0.3f });
    a.sharpness = 6.0f;
    a.amplitude = Vec3{ 1.0f, 0.6f, 0.3f };

    SphericalGaussian b;
    b.axis = wz::math::normalize(Vec3{ -0.4f, 0.8f, 0.5f });
    b.sharpness = 9.0f;
    b.amplitude = Vec3{ 0.5f, 0.9f, 0.7f };

    const auto dirs = fib_sphere(131072);
    const float omega = 4.0f * kPi / static_cast<float>(dirs.size());

    // Componentwise reference: integral of the per-channel product.
    Vec3 numeric{ 0.0f, 0.0f, 0.0f };
    for (const Vec3& w : dirs) {
        const Vec3 ea = evaluate(a, w);
        const Vec3 eb = evaluate(b, w);
        numeric = numeric + Vec3{ ea.x * eb.x, ea.y * eb.y, ea.z * eb.z } * omega;
    }

    const Vec3 analytic = inner_product(a, b);
    EXPECT_NEAR(numeric.x, analytic.x, 0.03f * analytic.x + 1e-4f);
    EXPECT_NEAR(numeric.y, analytic.y, 0.03f * analytic.y + 1e-4f);
    EXPECT_NEAR(numeric.z, analytic.z, 0.03f * analytic.z + 1e-4f);
}

TEST(SgLighting, InnerProductIsSymmetric)
{
    SphericalGaussian a;
    a.axis = wz::math::normalize(Vec3{ 0.5f, 0.2f, 0.8f });
    a.sharpness = 4.0f;
    a.amplitude = Vec3{ 0.7f, 0.4f, 1.1f };

    SphericalGaussian b;
    b.axis = wz::math::normalize(Vec3{ -0.1f, 0.9f, -0.4f });
    b.sharpness = 11.0f;
    b.amplitude = Vec3{ 1.2f, 0.3f, 0.6f };

    const Vec3 ab = inner_product(a, b);
    const Vec3 ba = inner_product(b, a);
    EXPECT_NEAR(ab.x, ba.x, 1e-5f);
    EXPECT_NEAR(ab.y, ba.y, 1e-5f);
    EXPECT_NEAR(ab.z, ba.z, 1e-5f);
}

// ---------------------------------------------------------------------------
// Cosine lobe / irradiance / diffuse
// ---------------------------------------------------------------------------

TEST(SgLighting, CosineLobeIntegratesToPi)
{
    // Energy normalization must make the sphere integral exactly pi, on any axis.
    for (const Vec3& n : { Vec3{ 0.0f, 1.0f, 0.0f },
                           wz::math::normalize(Vec3{ 0.3f, 0.7f, -0.6f }) }) {
        const Vec3 e = integral(cosine_lobe_sg(n));
        EXPECT_NEAR(e.x, kPi, 1e-3f);
        EXPECT_NEAR(e.y, kPi, 1e-3f);
        EXPECT_NEAR(e.z, kPi, 1e-3f);
    }
}

TEST(SgLighting, IrradianceApproximatesClampedCosine)
{
    // A single SG light; compare the closed-form irradiance against the true
    // integral \int L(v) max(0, n.v) dv. The single-SG cosine approximation is
    // known to carry ~10% method error, so the tolerance is deliberately loose.
    const Vec3 n{ 0.0f, 1.0f, 0.0f };

    const std::array<Vec3, 2> axes = {
        Vec3{ 0.0f, 1.0f, 0.0f },                            // overhead
        wz::math::normalize(Vec3{ 1.0f, 1.0f, 0.0f }),       // ~45 degrees
    };

    const auto dirs = fib_sphere(131072);
    const float omega = 4.0f * kPi / static_cast<float>(dirs.size());

    for (const Vec3& axis : axes) {
        SphericalGaussian light;
        light.axis = axis;
        light.sharpness = 6.0f;
        light.amplitude = Vec3{ 2.0f, 2.0f, 2.0f };

        float numeric = 0.0f;
        for (const Vec3& w : dirs) {
            const float ndotl = wz::math::dot(n, w);
            if (ndotl > 0.0f) {
                numeric += evaluate(light, w).x * ndotl * omega;
            }
        }

        const std::array<SphericalGaussian, 1> lights = { light };
        const Vec3 analytic = irradiance(lights, n);

        EXPECT_GT(analytic.x, 0.0f);
        EXPECT_NEAR(analytic.x, numeric, 0.15f * numeric + 1e-3f);
    }
}

TEST(SgLighting, DiffuseIsLambertianAndFallsOffWithAngle)
{
    SphericalGaussian light;
    light.axis = Vec3{ 0.0f, 1.0f, 0.0f };   // straight overhead
    light.sharpness = 5.0f;
    light.amplitude = Vec3{ 3.0f, 3.0f, 3.0f };
    const std::array<SphericalGaussian, 1> lights = { light };

    // Exact consistency: diffuse == albedo/pi * irradiance.
    SurfaceSample s;
    s.normal = Vec3{ 0.0f, 1.0f, 0.0f };
    s.albedo = Vec3{ 0.8f, 0.4f, 0.2f };

    const Vec3 e = irradiance(lights, s.normal);
    const Vec3 d = diffuse_radiance(lights, s);
    EXPECT_NEAR(d.x, s.albedo.x * e.x / kPi, 1e-5f);
    EXPECT_NEAR(d.y, s.albedo.y * e.y / kPi, 1e-5f);
    EXPECT_NEAR(d.z, s.albedo.z * e.z / kPi, 1e-5f);

    // A normal facing the light is brighter than one tilted 60 degrees away.
    SurfaceSample tilted = s;
    tilted.normal = wz::math::normalize(Vec3{ std::sin(1.047f), std::cos(1.047f), 0.0f });
    const Vec3 dt = diffuse_radiance(lights, tilted);
    EXPECT_GT(d.x, dt.x);
    EXPECT_GT(dt.x, 0.0f);
}

// ---------------------------------------------------------------------------
// Specular
// ---------------------------------------------------------------------------

namespace
{
    // A glossy view/surface setup: eye up-and-to-the-right, normal up. The
    // mirror direction reflects to the up-and-to-the-left.
    SurfaceSample glossy_surface(float roughness)
    {
        SurfaceSample s;
        s.normal = Vec3{ 0.0f, 1.0f, 0.0f };
        s.view = wz::math::normalize(Vec3{ 1.0f, 1.0f, 0.0f });
        s.albedo = Vec3{ 1.0f, 1.0f, 1.0f };
        s.roughness = roughness;
        return s;
    }

    Vec3 reflect_view(const SurfaceSample& s)
    {
        const float n_dot_v = wz::math::dot(s.normal, s.view);
        return wz::math::normalize(s.normal * (2.0f * n_dot_v) - s.view);
    }

    SphericalGaussian sg_light(const Vec3& axis, float sharpness, float amp)
    {
        SphericalGaussian g;
        g.axis = wz::math::normalize(axis);
        g.sharpness = sharpness;
        g.amplitude = Vec3{ amp, amp, amp };
        return g;
    }
}

TEST(SgLighting, SpecularZeroWhenViewedFromBehind)
{
    SurfaceSample s = glossy_surface(0.2f);
    s.view = Vec3{ 0.0f, -1.0f, 0.0f };   // eye below the surface

    const SphericalGaussian light = sg_light(Vec3{ 0.0f, 1.0f, 0.0f }, 20.0f, 5.0f);
    const std::array<SphericalGaussian, 1> lights = { light };
    const Vec3 spec = specular_radiance(lights, s, Vec3{ 0.04f, 0.04f, 0.04f });

    EXPECT_FLOAT_EQ(spec.x, 0.0f);
}

TEST(SgLighting, SpecularPeaksAtMirrorDirection)
{
    const SurfaceSample s = glossy_surface(0.2f);
    const Vec3 f0{ 0.04f, 0.04f, 0.04f };

    // A light sitting on the reflection vector vs. one sitting on the normal.
    const std::array<SphericalGaussian, 1> on_mirror = {
        sg_light(reflect_view(s), 40.0f, 5.0f) };
    const std::array<SphericalGaussian, 1> off_mirror = {
        sg_light(s.normal, 40.0f, 5.0f) };

    const Vec3 spec_on = specular_radiance(on_mirror, s, f0);
    const Vec3 spec_off = specular_radiance(off_mirror, s, f0);

    EXPECT_GT(spec_on.x, 0.0f);
    EXPECT_GT(spec_on.x, spec_off.x);
}

TEST(SgLighting, RoughnessBroadensAndLowersTheHighlight)
{
    const Vec3 f0{ 0.04f, 0.04f, 0.04f };

    // On the mirror axis, a sharp light: sharper surface -> higher peak.
    {
        const SurfaceSample lo = glossy_surface(0.1f);
        const SurfaceSample hi = glossy_surface(0.4f);
        const std::array<SphericalGaussian, 1> l_lo = { sg_light(reflect_view(lo), 40.0f, 5.0f) };
        const std::array<SphericalGaussian, 1> l_hi = { sg_light(reflect_view(hi), 40.0f, 5.0f) };
        EXPECT_GT(specular_radiance(l_lo, lo, f0).x,
                  specular_radiance(l_hi, hi, f0).x);
    }

    // Off the mirror axis, the rougher surface catches the light more.
    {
        const SurfaceSample lo = glossy_surface(0.1f);
        const SurfaceSample hi = glossy_surface(0.4f);
        // Rotate the light ~25 degrees off the shared mirror direction.
        const Vec3 r = reflect_view(lo);
        const float a = 0.436f; // radians
        const Vec3 off = wz::math::normalize(
            Vec3{ r.x * std::cos(a) - r.y * std::sin(a),
                  r.x * std::sin(a) + r.y * std::cos(a),
                  r.z });
        const std::array<SphericalGaussian, 1> light = { sg_light(off, 40.0f, 5.0f) };
        EXPECT_GT(specular_radiance(light, hi, f0).x,
                  specular_radiance(light, lo, f0).x);
    }
}

TEST(SgLighting, ShadeDecomposesByMetalness)
{
    const SphericalGaussian light = sg_light(Vec3{ 0.2f, 1.0f, 0.1f }, 12.0f, 2.0f);
    const std::array<SphericalGaussian, 1> lights = { light };

    SurfaceSample s = glossy_surface(0.3f);
    s.albedo = Vec3{ 0.8f, 0.5f, 0.2f };

    // Metal: no diffuse, f0 = albedo.
    s.metalness = 1.0f;
    const Vec3 metal = shade(lights, s);
    const Vec3 metal_expected = specular_radiance(lights, s, s.albedo);
    EXPECT_NEAR(metal.x, metal_expected.x, 1e-6f);
    EXPECT_NEAR(metal.y, metal_expected.y, 1e-6f);
    EXPECT_NEAR(metal.z, metal_expected.z, 1e-6f);

    // Dielectric: diffuse(albedo) + specular(f0 = 0.04).
    s.metalness = 0.0f;
    const Vec3 dielectric = shade(lights, s);
    const Vec3 diff = diffuse_radiance(lights, s);
    const Vec3 spec = specular_radiance(lights, s, Vec3{ 0.04f, 0.04f, 0.04f });
    EXPECT_NEAR(dielectric.x, diff.x + spec.x, 1e-6f);
    EXPECT_NEAR(dielectric.y, diff.y + spec.y, 1e-6f);
    EXPECT_NEAR(dielectric.z, diff.z + spec.z, 1e-6f);
}

TEST(SgLighting, SpecularInBallparkOfCookTorrance)
{
    // Ground-truth: Monte-Carlo integrate  L(l) * GGX_specular(l,v,n) * (n.l)
    // over the hemisphere, and check the SG specular is within a loose factor.
    // The single-lobe NDF approximation forbids a tight tolerance; this only
    // catches structural errors (missing cosine, wrong 1/(4 n.l n.v), etc.).
    const SurfaceSample s = glossy_surface(0.35f);
    const float f0 = 0.04f;
    const float m2 = s.roughness * s.roughness;

    const SphericalGaussian light = sg_light(reflect_view(s), 15.0f, 3.0f);
    const std::array<SphericalGaussian, 1> lights = { light };

    const auto dirs = fib_sphere(262144);
    const float omega = 4.0f * kPi / static_cast<float>(dirs.size());

    double numeric = 0.0;
    for (const Vec3& l : dirs) {
        const float n_dot_l = wz::math::dot(s.normal, l);
        if (n_dot_l <= 0.0f) {
            continue;
        }
        const float n_dot_v = wz::math::dot(s.normal, s.view);
        const Vec3  h = wz::math::normalize(l + s.view);
        const float n_dot_h = std::max(wz::math::dot(s.normal, h), 0.0f);
        const float v_dot_h = std::max(wz::math::dot(s.view, h), 0.0f);

        const float dn = n_dot_h * n_dot_h * (m2 - 1.0f) + 1.0f;
        const float D = m2 / (kPi * dn * dn + 1e-9f);
        const auto g1 = [m2](float x) {
            const float xx = std::max(x, 0.0f);
            return 2.0f * xx / (xx + std::sqrt(m2 + (1.0f - m2) * xx * xx) + 1e-9f);
        };
        const float G = g1(n_dot_l) * g1(n_dot_v);
        const float F = f0 + (1.0f - f0) * std::pow(1.0f - v_dot_h, 5.0f);

        const float brdf = D * G * F / (4.0f * n_dot_l * n_dot_v + 1e-9f);
        numeric += static_cast<double>(evaluate(light, l).x) * brdf * n_dot_l * omega;
    }

    const float analytic = specular_radiance(lights, s, Vec3{ f0, f0, f0 }).x;

    EXPECT_GT(analytic, 0.0f);
    EXPECT_GT(numeric, 0.0);
    EXPECT_GT(analytic, 0.33f * static_cast<float>(numeric));
    EXPECT_LT(analytic, 3.0f * static_cast<float>(numeric));
}

// ---------------------------------------------------------------------------
// Emitters (source B): distant delta lights via the exact Cook-Torrance BRDF
// ---------------------------------------------------------------------------

TEST(SgLighting, EmitterZeroBehindAndBelowHorizon)
{
    // Surface viewed from behind (n.v <= 0).
    SurfaceSample behind = glossy_surface(0.3f);
    behind.view = Vec3{ 0.0f, -1.0f, 0.0f };
    const std::array<DistantLight, 1> overhead = {
        DistantLight{ Vec3{ 0.0f, 1.0f, 0.0f }, Vec3{ 5.0f, 5.0f, 5.0f }, 0.001f } };
    EXPECT_FLOAT_EQ(emitter_radiance(overhead, behind).x, 0.0f);

    // Emitter below the surface's horizon (n.l <= 0).
    const SurfaceSample s = glossy_surface(0.3f);
    const std::array<DistantLight, 1> below = {
        DistantLight{ Vec3{ 0.0f, -1.0f, 0.0f }, Vec3{ 5.0f, 5.0f, 5.0f }, 0.001f } };
    EXPECT_FLOAT_EQ(emitter_radiance(below, s).x, 0.0f);
}

TEST(SgLighting, EmitterDiffuseLambertScalesLinearlyAndTints)
{
    SurfaceSample s;
    s.normal = Vec3{ 0.0f, 1.0f, 0.0f };
    s.view = Vec3{ 0.0f, 1.0f, 0.0f };
    s.albedo = Vec3{ 0.8f, 0.4f, 0.2f };
    s.roughness = 1.0f;
    s.metalness = 0.0f;

    const auto lit = [&](float radiance) {
        return std::array<DistantLight, 1>{ DistantLight{
            Vec3{ 0.0f, 1.0f, 0.0f },
            Vec3{ radiance, radiance, radiance }, 0.01f } };
    };
    const Vec3 a = emitter_radiance(lit(2.0f), s);
    const Vec3 b = emitter_radiance(lit(4.0f), s);

    EXPECT_GT(a.x, 0.0f);
    EXPECT_NEAR(b.x, 2.0f * a.x, 1e-5f);   // linear in radiance
    EXPECT_GT(a.x, a.z);                   // albedo tint carries through
}

TEST(SgLighting, EmitterSpecularPeaksAtMirror)
{
    const SurfaceSample s = glossy_surface(0.2f);
    const std::array<DistantLight, 1> on = {
        DistantLight{ reflect_view(s), Vec3{ 5.0f, 5.0f, 5.0f }, 0.001f } };
    const std::array<DistantLight, 1> off = {
        DistantLight{ s.normal, Vec3{ 5.0f, 5.0f, 5.0f }, 0.001f } };
    EXPECT_GT(emitter_radiance(on, s).x, emitter_radiance(off, s).x);
}

TEST(SgLighting, EmitterRoughnessLowersMirrorPeak)
{
    const SurfaceSample lo = glossy_surface(0.1f);
    const SurfaceSample hi = glossy_surface(0.5f);
    const std::array<DistantLight, 1> el = {
        DistantLight{ reflect_view(lo), Vec3{ 5.0f, 5.0f, 5.0f }, 0.001f } };
    const std::array<DistantLight, 1> eh = {
        DistantLight{ reflect_view(hi), Vec3{ 5.0f, 5.0f, 5.0f }, 0.001f } };
    EXPECT_GT(emitter_radiance(el, lo).x, emitter_radiance(eh, hi).x);
}

TEST(SgLighting, EmitterDiffuseFallsOffWithAngle)
{
    SurfaceSample s;
    s.view = Vec3{ 0.0f, 1.0f, 0.0f };
    s.albedo = Vec3{ 1.0f, 1.0f, 1.0f };
    s.roughness = 1.0f;
    s.metalness = 0.0f;
    s.normal = Vec3{ 0.0f, 1.0f, 0.0f };

    const std::array<DistantLight, 1> e = {
        DistantLight{ Vec3{ 0.0f, 1.0f, 0.0f }, Vec3{ 3.0f, 3.0f, 3.0f }, 0.01f } };
    const Vec3 straight = emitter_radiance(e, s);

    SurfaceSample tilted = s;
    tilted.normal = wz::math::normalize(Vec3{ std::sin(1.0f), std::cos(1.0f), 0.0f });
    const Vec3 dim = emitter_radiance(e, tilted);

    EXPECT_GT(straight.x, dim.x);
    EXPECT_GT(dim.x, 0.0f);
}

// --- issue #316 C3-C1: the sharpness floor must guard, not annihilate --------
//
// integral() and inner_product() both compute a factor of the form
// (2*pi/l) * (1 - exp(-2l)). In float32, exp(-2l) rounds to exactly 1.0f once
// 2l drops below the epsilon at 1.0 (1.19e-7), so the factor becomes exactly 0
// and the whole term vanishes -- at precisely the broad-lobe limit that
// kSharpnessFloor (1e-8) exists to protect. The exact limit as l -> 0 is 4*pi.
//
// The sweep spans the collapse band on purpose: measured, the naive form is 0 at
// l=1e-8, 10.6% low at 1e-7, and correct only from about 1e-6 up. A test at a
// single "reasonable" sharpness cannot see any of that.
TEST(SgLighting, IntegralIsExactForAnArbitrarilyBroadLobe)
{
    constexpr float kFourPi = 4.0f * 3.14159265358979323846f;

    for (const float sharpness : { 0.0f, 1e-9f, 1e-8f, 1e-7f, 1e-6f, 1e-4f }) {
        SphericalGaussian g;
        g.axis = Vec3{ 0.0f, 0.0f, 1.0f };
        g.sharpness = sharpness;
        g.amplitude = Vec3{ 1.0f, 1.0f, 1.0f };

        // A lobe this broad is essentially uniform over the sphere, so its
        // integral is 4*pi * amplitude. Before the fix this returned 0.
        const Vec3 e = integral(g);
        EXPECT_NEAR(e.x, kFourPi, 1e-2f) << "sharpness=" << sharpness;
    }

    // And the ordinary range is untouched: at the cosine-lobe sharpness the
    // value must still match what quadrature says (IntegralMatchesQuadrature
    // covers the general case; this pins that the fix changed nothing there).
    SphericalGaussian g;
    g.axis = Vec3{ 0.0f, 0.0f, 1.0f };
    g.sharpness = 2.133f;
    g.amplitude = Vec3{ 1.0f, 1.0f, 1.0f };
    EXPECT_NEAR(integral(g).x, 2.90435f, 1e-3f);
}

// The same collapse reached the SG product integral, which is what every
// irradiance and specular lookup goes through.
TEST(SgLighting, InnerProductSurvivesTwoBroadLobes)
{
    SphericalGaussian a;
    a.axis = Vec3{ 0.0f, 0.0f, 1.0f };
    a.sharpness = 1e-8f;
    a.amplitude = Vec3{ 1.0f, 1.0f, 1.0f };

    SphericalGaussian b = a;

    const Vec3 p = inner_product(a, b);
    EXPECT_GT(p.x, 1.0f) << "two near-uniform lobes cannot integrate to zero";
    EXPECT_TRUE(std::isfinite(p.x));

    // A NaN axis must land on the floor rather than propagate: the guard is now
    // written in the reject direction, matching integral()'s.
    SphericalGaussian bad = a;
    bad.axis = Vec3{ std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f };
    EXPECT_TRUE(std::isfinite(inner_product(bad, b).x));
}
