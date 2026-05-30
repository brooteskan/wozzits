#include <engine/assets/hdri/hdri_lighting_metadata.h>

#include <algorithm>
#include <cmath>

namespace wz::engine::assets
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        float luminance(float r, float g, float b) noexcept
        {
            return 0.2126f * r + 0.7152f * g + 0.0722f * b;
        }

        float compress_luminance_to_lighting_units(float luminance_value) noexcept
        {
            luminance_value = (std::max)(0.0f, luminance_value);
            return luminance_value / (1.0f + luminance_value);
        }

        void normalize3(float v[3]) noexcept
        {
            const float len = std::sqrt(
                v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (len > 1e-6f) {
                v[0] /= len;
                v[1] /= len;
                v[2] /= len;
            }
        }

        void rotate_x(float v[3], float radians) noexcept
        {
            if (radians == 0.0f) {
                return;
            }

            const float c = std::cos(radians);
            const float s = std::sin(radians);
            const float y = v[1];
            const float z = v[2];
            v[1] = c * y - s * z;
            v[2] = s * y + c * z;
        }

        void rotate_y(float v[3], float radians) noexcept
        {
            if (radians == 0.0f) {
                return;
            }

            const float c = std::cos(radians);
            const float s = std::sin(radians);
            const float x = v[0];
            const float z = v[2];
            v[0] = c * x + s * z;
            v[2] = -s * x + c * z;
        }

        void rotate_z(float v[3], float radians) noexcept
        {
            if (radians == 0.0f) {
                return;
            }

            const float c = std::cos(radians);
            const float s = std::sin(radians);
            const float x = v[0];
            const float y = v[1];
            v[0] = c * x - s * y;
            v[1] = s * x + c * y;
        }
    }

    bool derive_hdri_lighting_metadata(
        const HDRImageData& image,
        float exposure,
        float rotation_x_radians,
        float rotation_y_radians,
        float rotation_z_radians,
        HDRILightingMetadata& out) noexcept
    {
        out = {};
        if (!image.valid() || image.channels < 3) {
            return false;
        }

        const float exposure_scale = std::exp2(exposure);
        float weighted_rgb[3]{};
        float weight_sum = 0.0f;
        float max_lum = 0.0f;
        uint32_t max_x = 0;
        uint32_t max_y = 0;
        float max_rgb[3]{};

        for (uint32_t y = 0; y < image.height; ++y) {
            const float v =
                (static_cast<float>(y) + 0.5f)
                / static_cast<float>(image.height);
            const float phi = v * kPi;
            const float weight = (std::max)(std::sin(phi), 1e-5f);

            for (uint32_t x = 0; x < image.width; ++x) {
                const size_t index =
                    (static_cast<size_t>(y) * image.width + x)
                    * image.channels;
                const float r = (std::max)(0.0f, image.pixels[index + 0])
                    * exposure_scale;
                const float g = (std::max)(0.0f, image.pixels[index + 1])
                    * exposure_scale;
                const float b = (std::max)(0.0f, image.pixels[index + 2])
                    * exposure_scale;
                const float lum = luminance(r, g, b);

                weighted_rgb[0] += r * weight;
                weighted_rgb[1] += g * weight;
                weighted_rgb[2] += b * weight;
                weight_sum += weight;

                if (lum > max_lum) {
                    max_lum = lum;
                    max_x = x;
                    max_y = y;
                    max_rgb[0] = r;
                    max_rgb[1] = g;
                    max_rgb[2] = b;
                }
            }
        }

        if (weight_sum <= 0.0f) {
            return false;
        }

        const float avg_rgb[3]{
            weighted_rgb[0] / weight_sum,
            weighted_rgb[1] / weight_sum,
            weighted_rgb[2] / weight_sum,
        };
        const float avg_lum = luminance(avg_rgb[0], avg_rgb[1], avg_rgb[2]);

        out.environment_light_intensity =
            compress_luminance_to_lighting_units(avg_lum);
        if (avg_lum > 1e-6f) {
            out.environment_light_color[0] = avg_rgb[0] / avg_lum;
            out.environment_light_color[1] = avg_rgb[1] / avg_lum;
            out.environment_light_color[2] = avg_rgb[2] / avg_lum;
        }

        const float u =
            (static_cast<float>(max_x) + 0.5f)
            / static_cast<float>(image.width);
        const float v =
            (static_cast<float>(max_y) + 0.5f)
            / static_cast<float>(image.height);
        const float theta = u * 2.0f * kPi + rotation_y_radians;
        const float phi = v * kPi;
        const float sin_phi = std::sin(phi);

        const float direction_to_light[3]{
            sin_phi * std::sin(theta),
            std::cos(phi),
            sin_phi * std::cos(theta),
        };
        out.dominant_light_direction[0] = -direction_to_light[0];
        out.dominant_light_direction[1] = -direction_to_light[1];
        out.dominant_light_direction[2] = -direction_to_light[2];
        rotate_x(out.dominant_light_direction, rotation_x_radians);
        rotate_z(out.dominant_light_direction, rotation_z_radians);
        normalize3(out.dominant_light_direction);

        out.dominant_light_intensity =
            compress_luminance_to_lighting_units(max_lum);
        if (max_lum > 1e-6f) {
            out.dominant_light_color[0] = max_rgb[0] / max_lum;
            out.dominant_light_color[1] = max_rgb[1] / max_lum;
            out.dominant_light_color[2] = max_rgb[2] / max_lum;
        }

        if (avg_lum > 1e-6f) {
            out.dominant_light_confidence =
                std::clamp((max_lum / avg_lum - 1.0f) / 16.0f, 0.0f, 1.0f);
        }
        else {
            out.dominant_light_confidence = max_lum > 0.0f ? 1.0f : 0.0f;
        }

        return true;
    }

    HDRILightingMetadata transform_hdri_lighting_metadata(
        const HDRILightingMetadata& metadata,
        float exposure,
        float rotation_x_radians,
        float rotation_y_radians,
        float rotation_z_radians) noexcept
    {
        HDRILightingMetadata out = metadata;
        const float exposure_scale = std::exp2(exposure);
        out.environment_light_intensity *= exposure_scale;
        out.dominant_light_intensity *= exposure_scale;

        if (rotation_x_radians != 0.0f
            || rotation_y_radians != 0.0f
            || rotation_z_radians != 0.0f)
        {
            // Authored HDRI rotations are applied as X, then Y, then Z Euler
            // rotations. Y is the normal yaw use-case; X/Z are intentionally
            // allowed so tools can tilt the sampled lighting even when that
            // makes the visual horizon physically implausible.
            rotate_x(out.dominant_light_direction, rotation_x_radians);
            rotate_y(out.dominant_light_direction, rotation_y_radians);
            rotate_z(out.dominant_light_direction, rotation_z_radians);
            normalize3(out.dominant_light_direction);
        }

        return out;
    }

} // namespace wz::engine::assets
