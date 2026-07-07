// src/engine/assets/sky_gaussian/sky_gaussian.cpp

#include <engine/assets/sky_gaussian/sky_gaussian.h>

#include <math/vec3.h>

#include <cmath>

namespace wz::engine::assets::sky
{
    using wz::math::Vec3;

    Vec3 evaluate_lobe(const SkyGaussianLobe& g, const Vec3& w)
    {
        const float d = wz::math::dot(g.direction, w);
        const float e = std::exp(g.sharpness * (d - 1.0f));
        return g.amplitude * e;
    }

    Vec3 evaluate_set(const SkyGaussianSet& s, const Vec3& w)
    {
        Vec3 acc{ 0.0f, 0.0f, 0.0f };
        for (const auto& g : s.lobes) {
            acc = acc + evaluate_lobe(g, w);
        }
        return acc;
    }

    Vec3 lobe_energy(const SkyGaussianLobe& g)
    {
        const float lambda = g.sharpness > 1e-8f ? g.sharpness : 1e-8f;
        const float scale =
            (kTwoPi / lambda) * (1.0f - std::exp(-2.0f * lambda));
        return g.amplitude * scale;
    }

    float luminance(const Vec3& rgb)
    {
        return 0.2126f * rgb.x + 0.7152f * rgb.y + 0.0722f * rgb.z;
    }

} // namespace wz::engine::assets::sky
