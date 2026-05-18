#pragma once

// engine/assets/gaussian_splat/gaussian_splat.h

#include <engine/assets/gaussian_splat/gaussian_splat_cloud.h>

#include <asset/types.h>

#include <cstdint>
#include <vector>
#include <string>

namespace wz::engine::assets
{
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