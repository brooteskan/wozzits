// src/engine/lighting/sg_lighting.cpp

#include <engine/lighting/sg_lighting.h>

#include <math/vec3.h>

#include <algorithm>
#include <cmath>

namespace wz::engine::lighting
{
    using wz::math::Vec3;

    namespace
    {
        // L2-best single-SG sharpness for the clamped cosine lobe
        // (Neubelt & Pettineo, "The Order: 1886", SIGGRAPH 2013). The amplitude
        // that pairs with it is derived in cosine_lobe_sg to preserve energy.
        constexpr float kCosineLobeSharpness = 2.133f;

        // Sharpness floor: guards the 1/lambda factors against a zero-width lobe.
        constexpr float kSharpnessFloor = 1e-8f;

        // Fresnel reflectance at normal incidence for a common dielectric.
        constexpr float kDielectricF0 = 0.04f;

        // Squared-roughness floor: keeps the NDF SG finite as roughness -> 0.
        constexpr float kMinRoughnessSq = 1e-4f;

        // GGX NDF (roughness m) approximated as an SG about `axis`:
        //   sharpness = 2/m^2, amplitude = 1/(pi m^2). Energy ~ 1.
        SphericalGaussian distribution_sg(const Vec3& axis, float roughness)
        {
            const float m2 = std::max(roughness * roughness, kMinRoughnessSq);
            SphericalGaussian sg;
            sg.axis = axis;
            sg.sharpness = 2.0f / m2;
            const float a = 1.0f / (kPi * m2);
            sg.amplitude = Vec3{ a, a, a };
            return sg;
        }

        // Schlick Fresnel, F0 + (1-F0)(1-cos)^5.
        Vec3 fresnel_schlick(const Vec3& f0, float cos_theta)
        {
            const float c = std::clamp(cos_theta, 0.0f, 1.0f);
            const float f = std::pow(1.0f - c, 5.0f);
            return Vec3{
                f0.x + (1.0f - f0.x) * f,
                f0.y + (1.0f - f0.y) * f,
                f0.z + (1.0f - f0.z) * f,
            };
        }

        // Smith GGX visibility factor V1; the product of two V1s is the full
        // visibility term V = G / (4 n.l n.v).
        float smith_ggx_v1(float m2, float n_dot_x)
        {
            const float x = std::max(n_dot_x, 1e-4f);
            return 1.0f / (x + std::sqrt(m2 + (1.0f - m2) * x * x));
        }

        // Scalar GGX NDF for the exact (punctual) microfacet BRDF.
        float ggx_ndf(float n_dot_h, float m2)
        {
            const float d = n_dot_h * n_dot_h * (m2 - 1.0f) + 1.0f;
            return m2 / (kPi * d * d + 1e-9f);
        }

        // Smith GGX geometry term G1 (height form): 2(n.x) / (n.x + sqrt(...)).
        float smith_g1(float n_dot_x, float m2)
        {
            const float x = std::max(n_dot_x, 0.0f);
            return 2.0f * x / (x + std::sqrt(m2 + (1.0f - m2) * x * x) + 1e-9f);
        }
    }

    Vec3 evaluate(const SphericalGaussian& g, const Vec3& w)
    {
        const float d = wz::math::dot(g.axis, w);
        return g.amplitude * std::exp(g.sharpness * (d - 1.0f));
    }

    Vec3 integral(const SphericalGaussian& g)
    {
        const float l = g.sharpness > kSharpnessFloor ? g.sharpness : kSharpnessFloor;
        const float scale = (kTwoPi / l) * (1.0f - std::exp(-2.0f * l));
        return g.amplitude * scale;
    }

    Vec3 inner_product(const SphericalGaussian& a, const SphericalGaussian& b)
    {
        // The product of two SGs is an SG with axis dm/|dm|, sharpness |dm|, and
        // amplitude A_a*A_b*exp(|dm| - la - lb), where dm = la*axis_a + lb*axis_b.
        // Integrating that SG over the sphere gives the result below.
        const Vec3  dm = a.axis * a.sharpness + b.axis * b.sharpness;
        float       lm = wz::math::length(dm);
        if (lm < kSharpnessFloor) {
            lm = kSharpnessFloor;
        }

        const float scale =
            std::exp(lm - a.sharpness - b.sharpness)
            * (kTwoPi / lm) * (1.0f - std::exp(-2.0f * lm));

        return Vec3{
            a.amplitude.x * b.amplitude.x * scale,
            a.amplitude.y * b.amplitude.y * scale,
            a.amplitude.z * b.amplitude.z * scale,
        };
    }

    SphericalGaussian cosine_lobe_sg(const Vec3& normal)
    {
        const float l = kCosineLobeSharpness;

        // Energy normalization: pick the amplitude so the sphere integral equals
        // pi (the exact integral of max(0, n.v)). integral() of this lobe is
        // a*(2pi/l)(1 - e^-2l), so a = l / (2*(1 - e^-2l)) yields exactly pi.
        const float a = l / (2.0f * (1.0f - std::exp(-2.0f * l)));

        SphericalGaussian g;
        g.axis = wz::math::normalize(normal);
        g.sharpness = l;
        g.amplitude = Vec3{ a, a, a };
        return g;
    }

    Vec3 irradiance(std::span<const SphericalGaussian> lights, const Vec3& normal)
    {
        const SphericalGaussian cos_sg = cosine_lobe_sg(normal);

        Vec3 e{ 0.0f, 0.0f, 0.0f };
        for (const SphericalGaussian& light : lights) {
            e = e + inner_product(light, cos_sg);
        }
        return e;
    }

    Vec3 diffuse_radiance(
        std::span<const SphericalGaussian> lights, const SurfaceSample& surface)
    {
        const Vec3  e = irradiance(lights, surface.normal);
        const float inv_pi = 1.0f / kPi;
        return Vec3{
            surface.albedo.x * e.x * inv_pi,
            surface.albedo.y * e.y * inv_pi,
            surface.albedo.z * e.z * inv_pi,
        };
    }

    Vec3 specular_radiance(
        std::span<const SphericalGaussian> lights,
        const SurfaceSample& surface,
        const Vec3& specular_f0)
    {
        const Vec3  n = surface.normal;
        const Vec3  v = surface.view;
        const float n_dot_v = wz::math::dot(n, v);
        if (n_dot_v <= 0.0f) {
            return Vec3{ 0.0f, 0.0f, 0.0f };   // surface faces away from the eye
        }

        const float m2 = std::max(surface.roughness * surface.roughness,
                                  kMinRoughnessSq);

        // Reflection of the view about the normal: r = 2 (n.v) n - v.
        const Vec3 r = wz::math::normalize(n * (2.0f * n_dot_v) - v);

        // GGX NDF as an SG about n, warped to the reflection lobe about r. The
        // warp shrinks the sharpness by the reflection Jacobian 4|n.v|.
        const SphericalGaussian ndf = distribution_sg(n, surface.roughness);
        SphericalGaussian warp;
        warp.axis      = r;
        // Clamp n.v in the warp denominator: at grazing it would explode the
        // lobe into a near-delta (bright, aliased rim). 0.2 caps the sharpening
        // without touching the highlight at moderate angles.
        warp.sharpness = ndf.sharpness / (4.0f * std::max(n_dot_v, 0.2f));
        warp.amplitude = ndf.amplitude;

        // Microfacet terms are evaluated once at the dominant direction r; the
        // half vector of (r, v) is the normal, so Fresnel uses n.v.
        const float n_dot_r  = std::max(wz::math::dot(n, r), 0.0f);
        const float vis      = smith_ggx_v1(m2, n_dot_r) * smith_ggx_v1(m2, n_dot_v);
        const Vec3  fresnel  = fresnel_schlick(specular_f0, n_dot_v);
        const float weight   = vis * n_dot_r;

        Vec3 out{ 0.0f, 0.0f, 0.0f };
        for (const SphericalGaussian& light : lights) {
            const Vec3 c = inner_product(warp, light);   // NDF (x) light
            out = out + Vec3{
                std::max(0.0f, c.x * fresnel.x * weight),
                std::max(0.0f, c.y * fresnel.y * weight),
                std::max(0.0f, c.z * fresnel.z * weight),
            };
        }
        return out;
    }

    Vec3 shade(std::span<const SphericalGaussian> lights, const SurfaceSample& surface)
    {
        const float mtl = std::clamp(surface.metalness, 0.0f, 1.0f);

        // Metalness convention: f0 lerps 0.04 -> albedo, diffuse tint lerps
        // albedo -> 0.
        const Vec3 f0{
            kDielectricF0 + (surface.albedo.x - kDielectricF0) * mtl,
            kDielectricF0 + (surface.albedo.y - kDielectricF0) * mtl,
            kDielectricF0 + (surface.albedo.z - kDielectricF0) * mtl,
        };

        SurfaceSample diffuse_surface = surface;
        diffuse_surface.albedo = surface.albedo * (1.0f - mtl);

        const Vec3 diffuse  = diffuse_radiance(lights, diffuse_surface);
        const Vec3 specular = specular_radiance(lights, surface, f0);
        return diffuse + specular;
    }

    Vec3 emitter_radiance(
        std::span<const DistantLight> emitters, const SurfaceSample& surface)
    {
        const Vec3  n = surface.normal;
        const Vec3  v = surface.view;
        const float n_dot_v = wz::math::dot(n, v);
        if (n_dot_v <= 0.0f) {
            return Vec3{ 0.0f, 0.0f, 0.0f };   // surface faces away from the eye
        }

        const float mtl = std::clamp(surface.metalness, 0.0f, 1.0f);
        const Vec3  f0{
            kDielectricF0 + (surface.albedo.x - kDielectricF0) * mtl,
            kDielectricF0 + (surface.albedo.y - kDielectricF0) * mtl,
            kDielectricF0 + (surface.albedo.z - kDielectricF0) * mtl,
        };
        const Vec3  diff_albedo = surface.albedo * (1.0f - mtl);
        const float m2 = std::max(surface.roughness * surface.roughness,
                                  kMinRoughnessSq);
        const float inv_pi = 1.0f / kPi;

        Vec3 out{ 0.0f, 0.0f, 0.0f };
        for (const DistantLight& e : emitters) {
            const Vec3  l = e.direction;
            const float n_dot_l = wz::math::dot(n, l);
            if (n_dot_l <= 0.0f) {
                continue;   // emitter below the surface's horizon
            }

            // Perpendicular irradiance from the emitter's cone.
            const Vec3 irr = e.radiance * e.solid_angle;

            // Lambert diffuse.
            const Vec3 diffuse{
                diff_albedo.x * irr.x * n_dot_l * inv_pi,
                diff_albedo.y * irr.y * n_dot_l * inv_pi,
                diff_albedo.z * irr.z * n_dot_l * inv_pi,
            };

            // Cook-Torrance specular. The rendering-equation (n.l) cancels the
            // (n.l) in the microfacet denominator 4(n.l)(n.v).
            const Vec3  h = wz::math::normalize(l + v);
            const float n_dot_h = std::max(wz::math::dot(n, h), 0.0f);
            const float v_dot_h = std::max(wz::math::dot(v, h), 0.0f);
            const float dterm = ggx_ndf(n_dot_h, m2);
            const float gterm = smith_g1(n_dot_l, m2) * smith_g1(n_dot_v, m2);
            const Vec3  fterm = fresnel_schlick(f0, v_dot_h);
            const float scale = dterm * gterm / (4.0f * n_dot_v);
            const Vec3  specular{
                fterm.x * irr.x * scale,
                fterm.y * irr.y * scale,
                fterm.z * irr.z * scale,
            };

            out = out + diffuse + specular;
        }
        return out;
    }

} // namespace wz::engine::lighting
