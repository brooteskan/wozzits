// src/engine/rendering/clipmap_drawn_surface.cpp

#include <engine/rendering/clipmap_drawn_surface.h>

#include <algorithm>
#include <cmath>

namespace wz::engine::rendering
{
    namespace
    {
        // Hardware bilinear at a texel coordinate, clamp addressing. The tap
        // sits at texel centres, so a texture coordinate t in texel units maps
        // to the pair floor(t - 0.5), floor(t - 0.5) + 1 -- the -0.5 is what
        // makes uv == (i + 0.5)/dims return texel i exactly.
        float bilinear_clamped(
            const ClipmapHeightMipView& level,
            float tx,
            float tz) noexcept
        {
            const auto clamp_index = [](float v, uint32_t extent) noexcept {
                const int32_t last = static_cast<int32_t>(extent) - 1;
                const int32_t i = static_cast<int32_t>(std::floor(v));
                return static_cast<uint32_t>(std::clamp(i, 0, last));
            };

            const float fx = tx - 0.5f;
            const float fz = tz - 0.5f;
            const uint32_t x0 = clamp_index(fx, level.width);
            const uint32_t z0 = clamp_index(fz, level.height);
            const uint32_t x1 = clamp_index(fx + 1.0f, level.width);
            const uint32_t z1 = clamp_index(fz + 1.0f, level.height);

            const float rx = std::clamp(fx - std::floor(fx), 0.0f, 1.0f);
            const float rz = std::clamp(fz - std::floor(fz), 0.0f, 1.0f);

            const auto at = [&](uint32_t x, uint32_t z) noexcept {
                return level.values[
                    static_cast<size_t>(z) * level.width + x];
            };

            const float h0 = at(x0, z0) + (at(x1, z0) - at(x0, z0)) * rx;
            const float h1 = at(x0, z1) + (at(x1, z1) - at(x0, z1)) * rx;
            return h0 + (h1 - h0) * rz;
        }
    }

    float clipmap_sample_height_world(
        const ClipmapHeightFieldView& field,
        float world_x,
        float world_z,
        uint32_t mip) noexcept
    {
        if (!field.valid()) {
            return 0.0f;
        }

        const uint32_t last_mip =
            static_cast<uint32_t>(field.levels.size()) - 1u;
        const ClipmapHeightMipView& level =
            field.levels[(std::min)(mip, last_mip)];
        if (!level.values || level.width == 0u || level.height == 0u) {
            return 0.0f;
        }

        // uv = (world - origin) / size, exactly the world_to_uv the renderer
        // packs into the clipmap constants (scale 1/size, offset -origin/size).
        const float u =
            (world_x - field.world_origin[0]) / field.world_size[0];
        const float v =
            (world_z - field.world_origin[1]) / field.world_size[1];

        // The shader adds half a texel OF THE SAMPLED MIP before the fetch. It
        // recomputes those dimensions as floor(base_dims / 2^mip); we use the
        // level's own, which agree -- iterated floor-halving equals a single
        // floor-divide by 2^mip -- and cannot disagree with the data we index.
        //
        // Note this shift scales with the mip, so it is a lateral bias of
        // (c_L - c_0)/2 on coarse rings rather than a fixed half-texel. That is
        // a property of the shipping shader, and mirroring it is the point:
        // this function must reproduce what is DRAWN, not what would be drawn
        // by a shader without it.
        const float sample_u =
            u + 0.5f / static_cast<float>(level.width);
        const float sample_v =
            v + 0.5f / static_cast<float>(level.height);

        return bilinear_clamped(
            level,
            sample_u * static_cast<float>(level.width),
            sample_v * static_cast<float>(level.height));
    }
}
