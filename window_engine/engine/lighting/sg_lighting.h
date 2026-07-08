#pragma once

// engine/lighting/sg_lighting.h
//
// General spherical-Gaussian (SG) lighting evaluator -- the surface-side half of
// the SG lighting model (umbrella #259). The design is "one primitive, one
// evaluator, three sources":
//
//   * PRIMITIVE  A SphericalGaussian is a directional radiance lobe. It is the
//                single currency of the whole model. It is layout-identical to
//                assets::sky::SkyGaussianLobe: the SG SKY is merely ONE source
//                of these lobes, not a special case.
//
//   * EVALUATOR  Given incident radiance expressed as a set of SGs plus a
//                surface (normal / view / albedo / roughness), compute the
//                outgoing radiance in closed form. Source-agnostic: it does not
//                know or care whether the lobes came from the sky, an emitter,
//                or a baked probe volume.
//
//   * SOURCES    (later seams) A = sky SGs (distant environment); B = sharp SGs
//                / point sources (emitters); C = a baked probe-splat volume
//                queried to the SGs near a shade point (indirect). All three
//                feed THIS evaluator unchanged.
//
// This evaluator is the PBR surface-shading foundation the engine currently
// lacks (mesh shading is hardcoded half-Lambert) -- i.e. the load-bearing
// prerequisite for analytic SG IBL (#264) and for the model as a whole.
//
// Seam 1 (this file) is the DIFFUSE core: the primitive, its integral, the exact
// SG-SG inner product, the clamped-cosine-as-SG lobe, irradiance, and Lambertian
// diffuse. SPECULAR (reflection-warped NDF-as-SG x light SG, with Fresnel and
// geometry terms) is the next sub-seam and is intentionally not here yet.
//
// Pure CPU, deterministic, no RNG, no GPU, no asset-system registration.

#include <math/math_types.h>

#include <span>

namespace wz::engine::lighting
{
    inline constexpr float kPi = 3.14159265358979323846f;
    inline constexpr float kTwoPi = 6.28318530717958647692f;

    // The one radiance primitive of the lighting model:
    //   L(w) = amplitude * exp(sharpness * (dot(axis, w) - 1))
    // axis is a unit vector; sharpness > 0 is the angular concentration;
    // amplitude is RGB radiance. Layout-identical to SkyGaussianLobe.
    struct SphericalGaussian
    {
        wz::math::Vec3 axis{ 0.0f, 1.0f, 0.0f };
        float          sharpness = 1.0f;
        wz::math::Vec3 amplitude{ 0.0f, 0.0f, 0.0f };
    };

    // A surface shading point. view / roughness / metalness are carried now but
    // consumed by the specular sub-seam; diffuse uses normal + albedo only.
    struct SurfaceSample
    {
        wz::math::Vec3 normal{ 0.0f, 1.0f, 0.0f };   // unit, outward
        wz::math::Vec3 view{ 0.0f, 1.0f, 0.0f };     // unit, toward the eye
        wz::math::Vec3 albedo{ 0.5f, 0.5f, 0.5f };   // diffuse reflectance [0,1]
        float          roughness = 0.5f;             // [0,1] (specular seam)
        float          metalness = 0.0f;             // [0,1] (specular seam)
    };

    // Evaluate one lobe in unit direction w.
    wz::math::Vec3 evaluate(const SphericalGaussian& g, const wz::math::Vec3& w);

    // Closed-form integral over the unit sphere:
    //   amplitude * (2*pi / lambda) * (1 - exp(-2*lambda)).
    wz::math::Vec3 integral(const SphericalGaussian& g);

    // Closed-form product integral  \int_{S^2} a(v) * b(v) dv,  per RGB channel.
    // The product of two SGs is itself an SG; this integrates that product.
    // Symmetric in a, b. This is the algebraic heart of every SG lighting term.
    wz::math::Vec3 inner_product(
        const SphericalGaussian& a, const SphericalGaussian& b);

    // The clamped cosine lobe  max(0, dot(normal, v))  approximated as a single
    // SG, energy-normalized so its sphere integral is exactly pi (the exact
    // integral of the clamped cosine). Sharpness is the L2-best fit from
    // Neubelt & Pettineo, "The Order: 1886" (SIGGRAPH 2013).
    SphericalGaussian cosine_lobe_sg(const wz::math::Vec3& normal);

    // Irradiance  E(n) = \int L(v) max(0, dot(n, v)) dv,  summed over the SG
    // lights, evaluated as the inner product of each light with the cosine lobe.
    wz::math::Vec3 irradiance(
        std::span<const SphericalGaussian> lights,
        const wz::math::Vec3& normal);

    // Lambertian diffuse outgoing radiance = albedo/pi * irradiance.
    wz::math::Vec3 diffuse_radiance(
        std::span<const SphericalGaussian> lights,
        const SurfaceSample& surface);

    // Specular outgoing radiance from the SG lights, via the warped-NDF method
    // (Wang et al. 2009; Pettineo's real-time SG formulation): the GGX NDF is
    // approximated as one SG about the reflection vector, convolved with each
    // light through inner_product(), then weighted by the Fresnel and Smith
    // geometry terms evaluated at the dominant reflection direction. A single-
    // lobe NDF grows approximate as roughness rises. specular_f0 is the Fresnel
    // reflectance at normal incidence. Uses surface.view / .roughness.
    wz::math::Vec3 specular_radiance(
        std::span<const SphericalGaussian> lights,
        const SurfaceSample& surface,
        const wz::math::Vec3& specular_f0);

    // Full BRDF response = diffuse + specular under the metalness convention:
    //   dielectric (metalness 0): f0 = 0.04, diffuse tint = albedo
    //   metal      (metalness 1): f0 = albedo, no diffuse
    // Interpolates both across the [0,1] metalness range.
    wz::math::Vec3 shade(
        std::span<const SphericalGaussian> lights,
        const SurfaceSample& surface);

    // ---- Source B: distant emitters (sun / moon / bright stars) ---------------
    //
    // The far end of the frequency split (umbrella #259): points too sharp to fit
    // as SG dome lobes. Treated as directional delta lights, so they feed the
    // EXACT Cook-Torrance GGX BRDF -- no SG convolution, no single-lobe NDF
    // approximation. Layout-mirrors assets::sky::SkyPointSource so the fit's
    // point_sources convert trivially.
    struct DistantLight
    {
        wz::math::Vec3 direction{ 0.0f, 1.0f, 0.0f };   // unit, toward the emitter
        wz::math::Vec3 radiance{ 0.0f, 0.0f, 0.0f };    // RGB
        float          solid_angle = 0.0f;              // steradians subtended
    };

    // Diffuse + specular from distant emitters (perpendicular irradiance =
    // radiance * solid_angle), under the same metalness convention as shade().
    // Additive with shade()'s environment term: total = shade(env) +
    // emitter_radiance(emitters).
    wz::math::Vec3 emitter_radiance(
        std::span<const DistantLight> emitters,
        const SurfaceSample& surface);

} // namespace wz::engine::lighting
