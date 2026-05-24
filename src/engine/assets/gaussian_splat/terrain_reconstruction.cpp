#include <engine/assets/gaussian_splat/terrain_reconstruction.h>
#include <gpu/terrain_coverage_kernel.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace wz::engine::assets
{
    TerrainReconResult reconstruct_terrain_grid(
        std::span<const TerrainReconSplat> splats,
        const TerrainReconDesc& desc)
    {
        TerrainReconResult result{};

        if (splats.empty()) return result;

        const int N = desc.grid_res;
        if (N < 2) return result;

        // ── Resolve domain bounds ────────────────────────────────────
        float x_min = desc.domain_min_x;
        float x_max = desc.domain_max_x;
        float z_min = desc.domain_min_z;
        float z_max = desc.domain_max_z;

        // If the caller left bounds at zero, compute from splats.
        if (x_max - x_min <= 0.0f || z_max - z_min <= 0.0f)
        {
            x_min =  1e30f; x_max = -1e30f;
            z_min =  1e30f; z_max = -1e30f;
            for (const auto& s : splats)
            {
                if (s.pos_x < x_min) x_min = s.pos_x;
                if (s.pos_x > x_max) x_max = s.pos_x;
                if (s.pos_z < z_min) z_min = s.pos_z;
                if (s.pos_z > z_max) z_max = s.pos_z;
            }
        }

        const float x_range = x_max - x_min;
        const float z_range = z_max - z_min;
        if (x_range <= 0.0f || z_range <= 0.0f) return result;

        const float cell_x = x_range / static_cast<float>(N - 1);
        const float cell_z = z_range / static_cast<float>(N - 1);

        const size_t grid_size = static_cast<size_t>(N) * N;

        // ── Accumulation buffers ─────────────────────────────────────
        std::vector<float> sum_w   (grid_size, 0.0f);
        std::vector<float> sum_wh  (grid_size, 0.0f);
        std::vector<float> sum_wc_r(grid_size, 0.0f);
        std::vector<float> sum_wc_g(grid_size, 0.0f);
        std::vector<float> sum_wc_b(grid_size, 0.0f);
        std::vector<float> sum_wn_x(grid_size, 0.0f);
        std::vector<float> sum_wn_y(grid_size, 0.0f);
        std::vector<float> sum_wn_z(grid_size, 0.0f);

        // Kernel cutoff radius in normalized space.
        const float cutoff_r =
            (desc.kernel_mode == wz::gpu::TerrainCoverageKernelMode::Gaussian)
            ? std::sqrt(-std::log(0.01f)
                  / std::max(desc.gaussian_falloff, 0.01f))
            : std::max(desc.outer_radius, 1.0f);

        // ── Scatter each splat's contribution ────────────────────────
        for (const auto& s : splats)
        {
            const float sup_x = s.radius_u * cutoff_r * desc.radius_scale;
            const float sup_z = s.radius_v * cutoff_r * desc.radius_scale;

            const int ix_min = std::max(0,
                static_cast<int>(std::floor(
                    (s.pos_x - sup_x - x_min) / cell_x)));
            const int ix_max = std::min(N - 1,
                static_cast<int>(std::ceil(
                    (s.pos_x + sup_x - x_min) / cell_x)));
            const int iz_min = std::max(0,
                static_cast<int>(std::floor(
                    (s.pos_z - sup_z - z_min) / cell_z)));
            const int iz_max = std::min(N - 1,
                static_cast<int>(std::ceil(
                    (s.pos_z + sup_z - z_min) / cell_z)));

            for (int iz = iz_min; iz <= iz_max; ++iz)
            {
                const float qz = z_min + iz * cell_z;
                const float dz = (qz - s.pos_z) / s.radius_v;

                for (int ix = ix_min; ix <= ix_max; ++ix)
                {
                    const float qx = x_min + ix * cell_x;
                    const float dx = (qx - s.pos_x) / s.radius_u;

                    const float r_norm =
                        std::sqrt(dx * dx + dz * dz) * desc.radius_scale;

                    const float w = wz::gpu::evaluate_coverage_kernel(
                        desc.kernel_mode, r_norm,
                        desc.inner_radius, desc.outer_radius,
                        desc.gaussian_falloff) * s.opacity;

                    if (w <= 1e-8f) continue;

                    const size_t idx =
                        static_cast<size_t>(iz) * N + ix;
                    sum_w[idx]    += w;
                    sum_wh[idx]   += w * s.height;
                    sum_wc_r[idx] += w * s.color[0];
                    sum_wc_g[idx] += w * s.color[1];
                    sum_wc_b[idx] += w * s.color[2];
                    sum_wn_x[idx] += w * s.normal[0];
                    sum_wn_y[idx] += w * s.normal[1];
                    sum_wn_z[idx] += w * s.normal[2];
                }
            }
        }

        // ── Resolve: divide by total weight ──────────────────────────
        std::vector<float> heights  (grid_size, 0.0f);
        std::vector<float> colors_r (grid_size, 0.5f);
        std::vector<float> colors_g (grid_size, 0.5f);
        std::vector<float> colors_b (grid_size, 0.5f);
        std::vector<float> normals_x(grid_size, 0.0f);
        std::vector<float> normals_y(grid_size, 1.0f);
        std::vector<float> normals_z(grid_size, 0.0f);
        std::vector<float> weights  (grid_size, 0.0f);

        float h_min =  1e30f;
        float h_max = -1e30f;

        for (size_t i = 0; i < grid_size; ++i)
        {
            const float w = sum_w[i];
            weights[i] = w;

            if (w > 1e-8f)
            {
                const float inv_w = 1.0f / w;
                heights[i]   = sum_wh[i]   * inv_w;
                colors_r[i]  = sum_wc_r[i] * inv_w;
                colors_g[i]  = sum_wc_g[i] * inv_w;
                colors_b[i]  = sum_wc_b[i] * inv_w;
                normals_x[i] = sum_wn_x[i] * inv_w;
                normals_y[i] = sum_wn_y[i] * inv_w;
                normals_z[i] = sum_wn_z[i] * inv_w;
            }

            if (heights[i] < h_min) h_min = heights[i];
            if (heights[i] > h_max) h_max = heights[i];
        }

        result.height_min = h_min;
        result.height_max = h_max;

        // ── Normals ──────────────────────────────────────────────────
        if (desc.use_gradient_normals)
        {
            // Derive normals from reconstructed height gradient:
            //   N = normalize(-dH/dx, 1, -dH/dz)
            for (int iz = 0; iz < N; ++iz)
            {
                for (int ix = 0; ix < N; ++ix)
                {
                    const size_t idx =
                        static_cast<size_t>(iz) * N + ix;
                    if (sum_w[idx] <= 1e-8f) continue;

                    float dh_dx, dh_dz;

                    if (ix == 0)
                        dh_dx = (heights[idx + 1] - heights[idx]) / cell_x;
                    else if (ix == N - 1)
                        dh_dx = (heights[idx] - heights[idx - 1]) / cell_x;
                    else
                        dh_dx = (heights[idx + 1] - heights[idx - 1])
                              / (2.0f * cell_x);

                    if (iz == 0)
                        dh_dz = (heights[idx + N] - heights[idx]) / cell_z;
                    else if (iz == N - 1)
                        dh_dz = (heights[idx] - heights[idx - N]) / cell_z;
                    else
                        dh_dz = (heights[idx + N] - heights[idx - N])
                              / (2.0f * cell_z);

                    float nx = -dh_dx;
                    float ny = 1.0f;
                    float nz = -dh_dz;
                    const float len =
                        std::sqrt(nx * nx + ny * ny + nz * nz);
                    if (len > 1e-8f)
                    { nx /= len; ny /= len; nz /= len; }

                    normals_x[idx] = nx;
                    normals_y[idx] = ny;
                    normals_z[idx] = nz;
                }
            }
        }
        else
        {
            // Normalize blended splat normals.
            for (size_t i = 0; i < grid_size; ++i)
            {
                if (sum_w[i] <= 1e-8f) continue;
                const float len = std::sqrt(
                    normals_x[i] * normals_x[i] +
                    normals_y[i] * normals_y[i] +
                    normals_z[i] * normals_z[i]);
                if (len > 1e-8f)
                {
                    normals_x[i] /= len;
                    normals_y[i] /= len;
                    normals_z[i] /= len;
                }
            }
        }

        // ── Build vertex data ────────────────────────────────────────
        result.vertices.resize(grid_size);
        for (int iz = 0; iz < N; ++iz)
        {
            for (int ix = 0; ix < N; ++ix)
            {
                const size_t idx =
                    static_cast<size_t>(iz) * N + ix;
                auto& v = result.vertices[idx];

                v.pos[0]    = x_min + ix * cell_x;
                v.pos[1]    = heights[idx];
                v.pos[2]    = z_min + iz * cell_z;
                v.normal[0] = normals_x[idx];
                v.normal[1] = normals_y[idx];
                v.normal[2] = normals_z[idx];
                v.color[0]  = colors_r[idx];
                v.color[1]  = colors_g[idx];
                v.color[2]  = colors_b[idx];
                v.weight    = weights[idx];
            }
        }

        // Normalize weights for debug visualisation (max → 1).
        float max_w = 0.0f;
        for (const auto& v : result.vertices)
            if (v.weight > max_w) max_w = v.weight;
        if (max_w > 1e-8f)
            for (auto& v : result.vertices)
                v.weight /= max_w;

        // ── Build index data (two triangles per grid cell) ───────────
        const int cells_x = N - 1;
        const int cells_z = N - 1;
        result.indices.resize(
            static_cast<size_t>(cells_x) * cells_z * 6);
        size_t idx_out = 0;
        for (int iz = 0; iz < cells_z; ++iz)
        {
            for (int ix = 0; ix < cells_x; ++ix)
            {
                const uint32_t tl =
                    static_cast<uint32_t>(iz * N + ix);
                const uint32_t tr = tl + 1;
                const uint32_t bl = tl + static_cast<uint32_t>(N);
                const uint32_t br = bl + 1;

                result.indices[idx_out++] = tl;
                result.indices[idx_out++] = bl;
                result.indices[idx_out++] = tr;

                result.indices[idx_out++] = tr;
                result.indices[idx_out++] = bl;
                result.indices[idx_out++] = br;
            }
        }

        return result;
    }

}  // namespace wz::engine::assets
