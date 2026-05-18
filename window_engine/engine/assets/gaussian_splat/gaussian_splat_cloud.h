// window_engine/engine/assets/gaussian_splat/gaussian_splat_cloud.h
#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    struct GaussianSplat
    {
        float position[3] = {};
        float opacity = 0.0f;

        float scale[3] = {};
        float rotation[4] = {};

        float color_dc[3] = {};

        // Flattened f_rest_N values for this splat, in schema numeric order.
        std::vector<float> color_rest;
    };

    struct GaussianSplatBounds
    {
        float min[3] = {
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)()
        };

        float max[3] = {
            (std::numeric_limits<float>::lowest)(),
            (std::numeric_limits<float>::lowest)(),
            (std::numeric_limits<float>::lowest)()
        };

        bool valid = false;
    };

    struct GaussianSplatCloudData
    {
        std::vector<GaussianSplat> splats;
        GaussianSplatBounds bounds;

        float opacity_min = 0.0f;
        float opacity_max = 0.0f;

        float scale_min = 0.0f;
        float scale_max = 0.0f;

        uint32_t f_rest_count = 0;

        bool valid() const noexcept
        {
            return !splats.empty();
        }

        bool empty() const noexcept
        {
            return splats.empty();
        }

        uint64_t splat_count() const noexcept
        {
            return static_cast<uint64_t>(splats.size());
        }
    };

    struct GaussianSplatImportResult
    {
        bool ok = false;
        GaussianSplatCloudData cloud;
        std::string error;
    };
}