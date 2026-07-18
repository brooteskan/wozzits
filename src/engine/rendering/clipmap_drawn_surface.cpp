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

    float clipmap_sample_height_coarse(
        const ClipmapHeightFieldView& field,
        float world_x,
        float world_z,
        float two_cL,
        uint32_t coarse_mip) noexcept
    {
        if (!(two_cL > 0.0f)) {
            return clipmap_sample_height_world(
                field, world_x, world_z, coarse_mip);
        }
        const float gx = world_x / two_cL;
        const float gz = world_z / two_cL;
        const float fx = std::floor(gx);
        const float fz = std::floor(gz);
        const float rx = gx - fx;
        const float rz = gz - fz;

        const auto tap = [&](float cx, float cz) noexcept {
            return clipmap_sample_height_world(
                field, cx * two_cL, cz * two_cL, coarse_mip);
        };
        const float h00 = tap(fx, fz);
        const float h10 = tap(fx + 1.0f, fz);
        const float h01 = tap(fx, fz + 1.0f);
        const float h11 = tap(fx + 1.0f, fz + 1.0f);

        const float h0 = h00 + (h10 - h00) * rx;
        const float h1 = h01 + (h11 - h01) * rx;
        return h0 + (h1 - h0) * rz;
    }

    namespace
    {
        float drawn_vertex_height_impl(
            const ClipmapHeightFieldView& field,
            const ClipmapDrawnSurfaceParams& params,
            uint32_t level,
            float g_x,
            float g_z,
            bool apply_trim) noexcept;
    }

    float clipmap_drawn_vertex_height(
        const ClipmapHeightFieldView& field,
        const ClipmapDrawnSurfaceParams& params,
        uint32_t level,
        float g_x,
        float g_z) noexcept
    {
        return drawn_vertex_height_impl(
            field, params, level, g_x, g_z, /*apply_trim*/ true);
    }

    namespace
    {
    float drawn_vertex_height_impl(
        const ClipmapHeightFieldView& field,
        const ClipmapDrawnSurfaceParams& params,
        uint32_t level,
        float g_x,
        float g_z,
        bool apply_trim) noexcept
    {
        if (!field.valid() || !params.valid()) {
            return 0.0f;
        }

        const float c_l = std::exp2(static_cast<float>(level)) * params.c0;
        const float two_cl = 2.0f * c_l;
        const float four_cl = 2.0f * two_cl;

        // This ring's own snap. floor so the grid origin steps consistently,
        // and to 2*c_L so adjacent levels stay nested.
        const float t_fine_x =
            std::floor(params.observer_xz[0] / two_cl) * two_cl;
        const float t_fine_z =
            std::floor(params.observer_xz[1] / two_cl) * two_cl;

        // Morph band, as a fraction of this level's world half-extent. The
        // vertex's own distance from the ring centre, in world units, measured
        // Chebyshev -- so the band is a square, matching the ring.
        const float half_world =
            0.5f * static_cast<float>(params.base_resolution) * c_l;
        const float dist = (std::max)(std::abs(g_x), std::abs(g_z));

        // The OUTERMOST ring has no coarser neighbour to hand off to, so it
        // neither morphs nor trims: it stays rigid to its edge, reading its own
        // mip. Mirrors the same suppression in clipmap_vs.hlsl, which needs the
        // ring count in snap_params.w to know it is last.
        const bool is_outermost = level + 1u >= params.level_count;

        // POSITION trim. Each ring snaps to its OWN grid, so two adjacent rings
        // can land up to one coarse cell apart; the shader absorbs that in a
        // one-coarse-cell ring at the outer edge by blending this ring's snap
        // toward the next coarser one. The vertex therefore MOVES, and it reads
        // the height wherever it lands.
        //
        // Worth including even though the trim band is thin and its endpoints
        // both coincide with the untrimmed grid: at intermediate blends the
        // vertex sits up to a cell away from where the untrimmed grid would put
        // it, and the pinning test measures that as a real height difference
        // rather than a rounding one.
        const float a_trim = (apply_trim && !is_outermost)
            ? std::clamp((dist - (half_world - two_cl)) / two_cl, 0.0f, 1.0f)
            : 0.0f;
        const float t_coarse_x =
            std::floor(params.observer_xz[0] / four_cl) * four_cl;
        const float t_coarse_z =
            std::floor(params.observer_xz[1] / four_cl) * four_cl;
        const float world_x =
            t_fine_x + (t_coarse_x - t_fine_x) * a_trim + g_x;
        const float world_z =
            t_fine_z + (t_coarse_z - t_fine_z) * a_trim + g_z;

        const float morph_start = kClipmapMorphStart * half_world;
        const float morph_end = kClipmapMorphEnd * half_world;
        const float a = is_outermost
            ? 0.0f
            : std::clamp(
                (dist - morph_start)
                    / (std::max)(morph_end - morph_start, 1e-6f),
                0.0f,
                1.0f);

        const uint32_t last_mip =
            static_cast<uint32_t>(field.levels.size()) - 1u;
        const uint32_t mip_fine = (std::min)(level, last_mip);
        const uint32_t mip_coarse = (std::min)(level + 1u, last_mip);

        const float h_fine =
            clipmap_sample_height_world(field, world_x, world_z, mip_fine);
        if (a <= 0.0f) {
            return h_fine;
        }
        const float h_coarse = clipmap_sample_height_coarse(
            field, world_x, world_z, two_cl, mip_coarse);
        return h_fine + (h_coarse - h_fine) * a;
    }
    }  // namespace

    float clipmap_drawn_surface_height(
        const ClipmapHeightFieldView& field,
        const ClipmapDrawnSurfaceParams& params,
        float world_x,
        float world_z) noexcept
    {
        if (!field.valid() || !params.valid()) {
            return 0.0f;
        }

        // Ring membership: the FINEST level whose square footprint covers the
        // point. Each level is centred on its own snap, so the test has to be
        // redone per level rather than derived from one distance.
        const float half_m =
            0.5f * static_cast<float>(params.base_resolution);
        uint32_t level = params.level_count - 1u;
        float c_l = 0.0f;
        float t_x = 0.0f;
        float t_z = 0.0f;
        for (uint32_t candidate = 0; candidate < params.level_count;
            ++candidate)
        {
            const float cc = std::exp2(static_cast<float>(candidate))
                * params.c0;
            const float two_cc = 2.0f * cc;
            const float cx =
                std::floor(params.observer_xz[0] / two_cc) * two_cc;
            const float cz =
                std::floor(params.observer_xz[1] / two_cc) * two_cc;
            const float reach = half_m * cc;
            if (std::abs(world_x - cx) <= reach
                && std::abs(world_z - cz) <= reach)
            {
                level = candidate;
                c_l = cc;
                t_x = cx;
                t_z = cz;
                break;
            }
            if (candidate + 1u == params.level_count) {
                // Past the coarsest ring's reach: the lattice does not cover
                // this point at all. Answer with the coarsest ring anyway, so a
                // query just outside the drawn horizon degrades smoothly rather
                // than stepping to the true surface.
                level = candidate;
                c_l = cc;
                t_x = cx;
                t_z = cz;
            }
        }

        // The cell of that ring containing the point, and the four vertices
        // around it. Lattice offsets are exact multiples of c_L, which is what
        // lets each vertex be evaluated independently.
        const float gx = (world_x - t_x) / c_l;
        const float gz = (world_z - t_z) / c_l;
        const float fx = std::floor(gx);
        const float fz = std::floor(gz);
        const float rx = gx - fx;
        const float rz = gz - fz;

        // UNTRIMMED vertices, deliberately. The trim moves a vertex to
        // T_coarse + g instead of T_fine + g, so evaluating trimmed heights
        // while locating the cell on the untrimmed grid would blend values
        // sampled somewhere other than the corners being blended -- worse than
        // either choice made consistently. Reconstructing the trimmed geometry
        // properly means locating the point in a warped, non-uniform grid.
        //
        // Skipping it is defensible in a way the raw per-vertex difference
        // makes look worse than it is: the trim redistributes vertices ALONG a
        // band that still spans the same XZ and still interpolates the same
        // field, so as a function of XZ -- which is what a constraint asks --
        // the two surfaces stay close even where individual vertices move a
        // long way. clipmap_drawn_vertex_height keeps the trim, so the pinning
        // test still measures the shader faithfully.
        const auto vertex = [&](float ix, float iz) noexcept {
            return drawn_vertex_height_impl(
                field, params, level, ix * c_l, iz * c_l, /*apply_trim*/ false);
        };
        const float h00 = vertex(fx, fz);
        const float h10 = vertex(fx + 1.0f, fz);
        const float h01 = vertex(fx, fz + 1.0f);
        const float h11 = vertex(fx + 1.0f, fz + 1.0f);

        const float h0 = h00 + (h10 - h00) * rx;
        const float h1 = h01 + (h11 - h01) * rx;
        return h0 + (h1 - h0) * rz;
    }
}
