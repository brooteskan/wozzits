#pragma once

#include <engine/assets/hdri/hdri_image_loader.h>

namespace wz::engine::assets
{
    struct HDRILightingMetadata
    {
        float environment_light_color[3]{ 1.0f, 1.0f, 1.0f };
        float environment_light_intensity = 0.0f;

        float dominant_light_direction[3]{ 0.0f, -1.0f, 0.0f };
        float dominant_light_color[3]{ 1.0f, 1.0f, 1.0f };
        float dominant_light_intensity = 0.0f;
        float dominant_light_confidence = 0.0f;
    };

    [[nodiscard]] bool derive_hdri_lighting_metadata(
        const HDRImageData& image,
        float exposure,
        float rotation_x_radians,
        float rotation_y_radians,
        float rotation_z_radians,
        uint32_t sample_resolution,
        HDRILightingMetadata& out) noexcept;

    [[nodiscard]] bool derive_hdri_lighting_metadata(
        const HDRImageData& image,
        float exposure,
        float rotation_x_radians,
        float rotation_y_radians,
        float rotation_z_radians,
        HDRILightingMetadata& out) noexcept;

    [[nodiscard]] HDRILightingMetadata transform_hdri_lighting_metadata(
        const HDRILightingMetadata& metadata,
        float exposure,
        float rotation_x_radians,
        float rotation_y_radians,
        float rotation_z_radians) noexcept;

} // namespace wz::engine::assets
