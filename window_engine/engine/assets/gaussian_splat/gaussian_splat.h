#pragma once

// engine/assets/gaussian_splat/gaussian_splat.h

#include <asset/types.h>

#include <cstdint>
#include <vector>
#include <string>

namespace wz::engine::assets
{
    enum class GaussianSplatColorMode : uint8_t
    {
        RGB,
    };

    struct GaussianSplat
    {
        float position[3] = {};

        // V1 stores decomposed anisotropic shape. The renderer can initially
        // ignore rotation/scale and draw these as debug points/quads.
        float scale[3] = { 1.0f, 1.0f, 1.0f };
        float rotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // x, y, z, w

        float opacity = 1.0f;
        float color[3] = { 1.0f, 1.0f, 1.0f };
    };

    struct GaussianSplatCloudData
    {
        GaussianSplatColorMode color_mode = GaussianSplatColorMode::RGB;

        std::vector<GaussianSplat> splats;

        float bounds_min[3] = {};
        float bounds_max[3] = {};

        bool valid() const noexcept;
        uint32_t splat_count() const noexcept;
    };

    class GaussianSplatCloudTable
    {
    public:
        GaussianSplatCloudTable();

        wz::asset::ResourceHandle add(GaussianSplatCloudData cloud);
        const GaussianSplatCloudData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        std::vector<GaussianSplatCloudData> clouds_;
        std::vector<uint32_t> epochs_;
    };

    struct ProceduralGaussianSplatCloudDesc
    {
        std::string name;

        uint32_t count = 0;
        float radius = 1.0f;
        float splat_scale = 0.05f;
    };

    struct ProceduralGaussianSplatCloudCompileDesc
    {
        uint32_t count = 0;
        float radius = 1.0f;
        float splat_scale = 0.05f;
    };
}