// src/engine/assets/gaussian_splat/gaussian_splat_color_lod_compiler.cpp
//
// Deterministic, single-threaded uniform-grid neighbor pass for the
// GaussianSplatColorLOD derived asset.

#include <engine/assets/gaussian_splat/gaussian_splat_color_lod_compiler.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <any>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wz::engine::assets
{
    namespace
    {
        // 3DGS color decoding constants (mirrors make_gpu_splat_vertex).
        constexpr float kSH_C0 = 0.28209479177387814f;

        // Tiny epsilon to avoid divide-by-zero in weight normalization.
        constexpr float kEpsilonWeight = 1e-12f;

        struct CellKey
        {
            int32_t x, y, z;
            bool operator==(const CellKey&) const = default;
        };

        struct CellKeyHash
        {
            // Multiplicative hash chosen for stable cross-build behavior.
            // (No reliance on std::hash<int>, which is implementation-defined.)
            size_t operator()(const CellKey& k) const noexcept
            {
                const uint64_t x = static_cast<uint32_t>(k.x);
                const uint64_t y = static_cast<uint32_t>(k.y);
                const uint64_t z = static_cast<uint32_t>(k.z);

                uint64_t h = x * 0x9E3779B185EBCA87ull;
                h ^= y * 0xC2B2AE3D27D4EB4Full;
                h ^= z * 0x165667B19E3779F9ull;
                return static_cast<size_t>(h);
            }
        };

        // Decode the cloud's stored color_dc (SH DC) into linear-RGB [0,1].
        // Mirrors make_gpu_splat_vertex but without the GPU packing.
        inline void decode_color(
            const GaussianSplat& s, float& r, float& g, float& b) noexcept
        {
            r = 0.5f + kSH_C0 * s.color_dc[0];
            g = 0.5f + kSH_C0 * s.color_dc[1];
            b = 0.5f + kSH_C0 * s.color_dc[2];
        }

        // Decode the cloud's logit opacity via sigmoid into [0,1].
        inline float decode_opacity(const GaussianSplat& s) noexcept
        {
            return 1.0f / (1.0f + std::exp(-s.opacity));
        }

        inline CellKey cell_of(
            const float pos[3], float cell_size) noexcept
        {
            // std::floor → int32 for negative coordinates correctness.
            const float inv = 1.0f / cell_size;
            return CellKey{
                static_cast<int32_t>(std::floor(pos[0] * inv)),
                static_cast<int32_t>(std::floor(pos[1] * inv)),
                static_cast<int32_t>(std::floor(pos[2] * inv)),
            };
        }

        GaussianSplatColorLODCompileDesc
        gaussian_splat_color_lod_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            GaussianSplatColorLODCompileDesc desc{};
            desc.neighbor_radius_world =
                params.get<float>(
                    "neighbor_radius_world",
                    desc.neighbor_radius_world);
            desc.gaussian_sigma_world =
                params.get<float>(
                    "gaussian_sigma_world",
                    desc.gaussian_sigma_world);
            desc.include_self =
                params.get<bool>("include_self", desc.include_self);
            desc.use_opacity_weight =
                params.get<bool>(
                    "use_opacity_weight",
                    desc.use_opacity_weight);
            desc.color_variance_gain =
                params.get<float>(
                    "color_variance_gain",
                    desc.color_variance_gain);
            return desc;
        }
    }

    GaussianSplatColorLODData compute_gaussian_splat_color_lod(
        const GaussianSplatCloudData& cloud,
        const GaussianSplatColorLODCompileDesc& desc,
        GaussianSplatColorLODCompileStats* stats_out)
    {
        using clock = std::chrono::steady_clock;
        const auto t_start = clock::now();

        GaussianSplatColorLODData out{};

        const uint32_t n = static_cast<uint32_t>(cloud.splats.size());
        if (n == 0)
            return out;

        const float cell_size = (desc.neighbor_radius_world > 1e-6f)
            ? desc.neighbor_radius_world
            : 1e-6f;

        const float sigma = (desc.gaussian_sigma_world > 1e-6f)
            ? desc.gaussian_sigma_world
            : 1e-6f;
        const float two_sigma_sq = 2.0f * sigma * sigma;

        const float radius_sq = cell_size * cell_size;

        // ── 1. Build uniform grid ──
        // Cells store splat indices in ascending insertion order, which is
        // also ascending splat-index order because we iterate i = 0..n-1.
        std::unordered_map<CellKey, std::vector<uint32_t>, CellKeyHash> grid;
        grid.reserve(n);

        for (uint32_t i = 0; i < n; ++i)
        {
            const CellKey ck = cell_of(cloud.splats[i].position, cell_size);
            grid[ck].push_back(i);
        }

        // ── 2. Predecode colors + opacities (small, avoids repeated exp) ──
        std::vector<float> color_r(n), color_g(n), color_b(n);
        std::vector<float> opacity(n);
        for (uint32_t i = 0; i < n; ++i)
        {
            decode_color(cloud.splats[i], color_r[i], color_g[i], color_b[i]);
            opacity[i] = desc.use_opacity_weight
                ? decode_opacity(cloud.splats[i])
                : 1.0f;
        }

        // ── 3. Two-pass accumulation per splat ──
        // Pass A: weighted mean color.
        // Pass B: weighted color variance, using the mean from pass A.
        out.neighborhood_color.assign(static_cast<size_t>(n) * 3u, 0.0f);
        out.lod_confidence.assign(static_cast<size_t>(n), 0.0f);

        uint64_t total_neighbor_count = 0;
        uint32_t max_neighbor_count = 0;

        // Stable neighbor cell iteration order: (dz, dy, dx) in -1..+1.
        const int kOffsets[3] = { -1, 0, 1 };

        for (uint32_t i = 0; i < n; ++i)
        {
            const float pix = cloud.splats[i].position[0];
            const float piy = cloud.splats[i].position[1];
            const float piz = cloud.splats[i].position[2];

            const CellKey ci = cell_of(cloud.splats[i].position, cell_size);

            // Pass A: weighted mean.
            float sum_w = 0.0f;
            float sum_wr = 0.0f, sum_wg = 0.0f, sum_wb = 0.0f;
            uint32_t neighbor_count = 0;

            for (int dz : kOffsets)
            for (int dy : kOffsets)
            for (int dx : kOffsets)
            {
                const CellKey ck{ ci.x + dx, ci.y + dy, ci.z + dz };
                auto it = grid.find(ck);
                if (it == grid.end()) continue;

                for (uint32_t j : it->second)
                {
                    if (j == i && !desc.include_self)
                        continue;

                    const float dxw = cloud.splats[j].position[0] - pix;
                    const float dyw = cloud.splats[j].position[1] - piy;
                    const float dzw = cloud.splats[j].position[2] - piz;
                    const float d2 = dxw * dxw + dyw * dyw + dzw * dzw;

                    if (d2 > radius_sq)
                        continue;

                    float w = std::exp(-d2 / two_sigma_sq);
                    if (desc.use_opacity_weight)
                        w *= opacity[j];

                    sum_w  += w;
                    sum_wr += w * color_r[j];
                    sum_wg += w * color_g[j];
                    sum_wb += w * color_b[j];
                    ++neighbor_count;
                }
            }

            float mean_r = 0.0f, mean_g = 0.0f, mean_b = 0.0f;
            if (sum_w > kEpsilonWeight)
            {
                const float inv = 1.0f / sum_w;
                mean_r = sum_wr * inv;
                mean_g = sum_wg * inv;
                mean_b = sum_wb * inv;
            }
            else
            {
                // Fallback: self color. Either no neighbors found, or
                // include_self=false and the splat is isolated.
                mean_r = color_r[i];
                mean_g = color_g[i];
                mean_b = color_b[i];
            }

            // Pass B: weighted variance (against mean from pass A).
            float sum_w2 = 0.0f;
            float sum_var = 0.0f;

            for (int dz : kOffsets)
            for (int dy : kOffsets)
            for (int dx : kOffsets)
            {
                const CellKey ck{ ci.x + dx, ci.y + dy, ci.z + dz };
                auto it = grid.find(ck);
                if (it == grid.end()) continue;

                for (uint32_t j : it->second)
                {
                    if (j == i && !desc.include_self)
                        continue;

                    const float dxw = cloud.splats[j].position[0] - pix;
                    const float dyw = cloud.splats[j].position[1] - piy;
                    const float dzw = cloud.splats[j].position[2] - piz;
                    const float d2 = dxw * dxw + dyw * dyw + dzw * dzw;

                    if (d2 > radius_sq)
                        continue;

                    float w = std::exp(-d2 / two_sigma_sq);
                    if (desc.use_opacity_weight)
                        w *= opacity[j];

                    const float dr = color_r[j] - mean_r;
                    const float dg = color_g[j] - mean_g;
                    const float db = color_b[j] - mean_b;

                    sum_var += w * (dr * dr + dg * dg + db * db);
                    sum_w2  += w;
                }
            }

            const float variance = (sum_w2 > kEpsilonWeight)
                ? (sum_var / sum_w2)
                : 0.0f;

            const float conf_raw = 1.0f - variance * desc.color_variance_gain;
            const float confidence =
                (conf_raw < 0.0f) ? 0.0f : (conf_raw > 1.0f ? 1.0f : conf_raw);

            out.neighborhood_color[3 * i + 0] = mean_r;
            out.neighborhood_color[3 * i + 1] = mean_g;
            out.neighborhood_color[3 * i + 2] = mean_b;
            out.lod_confidence[i] = confidence;

            total_neighbor_count += neighbor_count;
            if (neighbor_count > max_neighbor_count)
                max_neighbor_count = neighbor_count;
        }

        const auto t_end = clock::now();
        const double elapsed_ms =
            std::chrono::duration<double, std::milli>(t_end - t_start).count();

        if (stats_out)
        {
            stats_out->splat_count           = n;
            stats_out->occupied_cell_count   = grid.size();
            stats_out->average_neighbor_count =
                (n > 0)
                    ? static_cast<uint32_t>(total_neighbor_count / n)
                    : 0u;
            stats_out->max_neighbor_count    = max_neighbor_count;
            stats_out->compile_time_ms       = elapsed_ms;
        }

        return out;
    }


    namespace internal
    {
        void register_gaussian_splat_color_lod_compiler(
            wz::asset::CompilerRegistry& registry,
            wz::Logger& logger,
            GaussianSplatCloudTable& cloud_table,
            GaussianSplatColorLODTable& lod_table)
        {
            registry.register_compiler(wz::asset::AssetCompiler{
                .input_schema = kGaussianSplatColorLODSchema,
                .output_type  = kAssetTypeGaussianSplatColorLOD,
                .input_ports = {
                    { "splat_cloud", kAssetTypeGaussianSplatCloud },
                },
                .parameters = {
                    {
                        .name = "neighbor_radius_world",
                        .type = wz::asset::ParamType::Float,
                        .label = "Neighbor radius",
                        .default_num = 0.05,
                        .min = 0.0,
                        .max = 10.0,
                    },
                    {
                        .name = "gaussian_sigma_world",
                        .type = wz::asset::ParamType::Float,
                        .label = "Gaussian sigma",
                        .default_num = 0.025,
                        .min = 0.0,
                        .max = 10.0,
                    },
                    {
                        .name = "include_self",
                        .type = wz::asset::ParamType::Bool,
                        .label = "Include self",
                        .default_num = 1.0,
                    },
                    {
                        .name = "use_opacity_weight",
                        .type = wz::asset::ParamType::Bool,
                        .label = "Use opacity weight",
                        .default_num = 1.0,
                    },
                    {
                        .name = "color_variance_gain",
                        .type = wz::asset::ParamType::Float,
                        .label = "Color variance gain",
                        .default_num = 4.0,
                        .min = 0.0,
                        .max = 100.0,
                    },
                },
                .compile = [&logger, &cloud_table, &lod_table](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode* const> /*dep_nodes*/,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
                {
                    GaussianSplatColorLODCompileDesc param_desc{};
                    const auto* desc =
                        std::any_cast<GaussianSplatColorLODCompileDesc>(&input.meta);
                    if (!desc) {
                        if (const auto* params =
                                std::any_cast<wz::asset::ParamBlock>(
                                    &input.meta))
                        {
                            param_desc =
                                gaussian_splat_color_lod_desc_from_params(
                                    *params);
                            desc = &param_desc;
                        }
                    }

                    if (!desc) {
                        logger.error(
                            "gaussian splat color LOD: missing compile desc");
                        return compile_failed_node(input);
                    }

                    if (dep_handles.size() != 1) {
                        logger.error(
                            "gaussian splat color LOD: expected exactly one "
                            "compiled cloud dependency");
                        return compile_failed_node(input);
                    }

                    const GaussianSplatCloudData* cloud =
                        cloud_table.get(dep_handles[0]);
                    if (!cloud || !cloud->valid()) {
                        logger.error(
                            "gaussian splat color LOD: source cloud not found "
                            "or invalid");
                        return compile_failed_node(input);
                    }

                    GaussianSplatColorLODCompileStats stats{};
                    GaussianSplatColorLODData data =
                        compute_gaussian_splat_color_lod(*cloud, *desc, &stats);

                    if (!data.valid()) {
                        logger.error(
                            "gaussian splat color LOD: compile produced no data");
                        return compile_failed_node(input);
                    }

                    // Compile stats — visible in the toolhost console.
                    logger.info(
                        "gaussian splat color LOD compiled: "
                        "splats=" + std::to_string(stats.splat_count) +
                        " cells=" + std::to_string(stats.occupied_cell_count) +
                        " avg_neighbors=" + std::to_string(stats.average_neighbor_count) +
                        " max_neighbors=" + std::to_string(stats.max_neighbor_count) +
                        " time_ms=" + std::to_string(stats.compile_time_ms));

                    wz::asset::ResourceHandle handle = lod_table.add(std::move(data));
                    if (!handle.valid()) {
                        logger.error(
                            "gaussian splat color LOD: failed to store data in table");
                        return compile_failed_node(input);
                    }

                    wz::asset::AssetNode out = input;
                    out.stage   = wz::asset::AssetStage::Compiled;
                    out.payload = handle;
                    return out;
                }
                });
        }
    }
}
