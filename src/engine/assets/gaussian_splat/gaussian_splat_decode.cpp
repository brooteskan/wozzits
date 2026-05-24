#include <engine/assets/gaussian_splat/gaussian_splat_decode.h>
#include <math/quaternion.h>

#include <algorithm>
#include <cmath>

namespace wz::engine::assets
{
    // SH DC coefficient for order-0 spherical harmonics.
    static constexpr float kSH_C0 = 0.28209479177387814f;

    DecodedGaussianSplat decode_splat(const GaussianSplat& s)
    {
        DecodedGaussianSplat d{};

        // Position: passthrough.
        d.position = { s.position[0], s.position[1], s.position[2] };

        // Scale: stored as log(scale), decode with exp().
        d.scale = {
            std::exp(s.scale[0]),
            std::exp(s.scale[1]),
            std::exp(s.scale[2])
        };

        // Rotation: PLY convention rotation[0]=w, [1]=x, [2]=y, [3]=z
        // → wz::math::Quaternion {x, y, z, w}.
        d.rotation = {
            s.rotation[1],   // x
            s.rotation[2],   // y
            s.rotation[3],   // z
            s.rotation[0]    // w
        };

        // Opacity: stored as logit(opacity), decode with sigmoid.
        d.opacity = 1.0f / (1.0f + std::exp(-s.opacity));

        // Color: stored as SH DC coefficients, decode to display RGB.
        d.color = {
            std::clamp(s.color_dc[0] * kSH_C0 + 0.5f, 0.0f, 1.0f),
            std::clamp(s.color_dc[1] * kSH_C0 + 0.5f, 0.0f, 1.0f),
            std::clamp(s.color_dc[2] * kSH_C0 + 0.5f, 0.0f, 1.0f)
        };

        // Axis extraction from the decoded quaternion.
        d.axis_x = wz::math::quat_axis_x(d.rotation);
        d.axis_y = wz::math::quat_axis_y(d.rotation);
        d.axis_z = wz::math::quat_axis_z(d.rotation);

        return d;
    }

}  // namespace wz::engine::assets
