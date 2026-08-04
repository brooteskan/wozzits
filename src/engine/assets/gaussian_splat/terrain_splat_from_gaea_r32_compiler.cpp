// src/engine/assets/gaussian_splat/terrain_splat_from_gaea_r32_compiler.cpp
//
// Compiler for the TerrainSplatFromGaeaR32 recipe asset.
//
// Inputs (two file deps, in order):
//   dep[0] — .r32 raw float32 heightmap bytes
//   dep[1] — .json sidecar with world-space interpretation parameters:
//              { "format":         "r32_float"    (optional, informational)
//                "width":          uint           (optional)
//                "height":         uint           (optional)
//                "height_scale":   float          (required)
//                "step_x":         float          (required)
//                "step_z":         float          (required)
//                "overlap_factor": float          (optional)
//                "thickness":      float          (optional)
//                "subsample_step": uint           (optional)
//                "opacity":        float          (optional)
//                "flat_luminance": float          (optional)
//                "steep_luminance":float          (optional) }
//
// Output: kAssetTypeGaussianSplatCloud — the intermediate ScalarFieldData
// is built transiently and not exposed as a separate asset.

#include <engine/assets/gaussian_splat/gaussian_splat_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <engine/assets/json/json.h>
#include <external/json/json_read_helpers.h>

#include <algorithm>
#include <any>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        using wz::json::find_member;
        using wz::json::read_number;
        using wz::json::read_uint;
        using wz::json::narrow_float;
    }


    void register_terrain_splat_from_gaea_r32_compiler(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        GaussianSplatCloudTable& cloud_table,
        JSONTable& json_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kTerrainSplatFromGaeaR32Schema,
            .output_type  = kAssetTypeGaussianSplatCloud,
            .input_ports = {
                { "height_file", kAssetTypeRawFile },
                { "sidecar_json", kAssetTypeJSONDocument },
            },
            .compile = [&logger, &cloud_table, &json_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode* const> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles) -> wz::asset::AssetNode
            {
                // ── 1. Validate dependency count ──
                // dep[0] = .r32 raw file bytes, dep[1] = compiled JSONDocument
                if (dep_nodes.size() != 2 || dep_handles.size() != 2) {
                    logger.error(
                        "terrain-splat recipe: expected 2 deps "
                        "(.r32 + JSON document), got "
                        + std::to_string(dep_nodes.size()));
                    return compile_failed_node(input);
                }

                const auto* r32_bytes =
                    std::get_if<std::vector<uint8_t>>(&dep_nodes[0]->payload);

                if (!r32_bytes) {
                    logger.error(
                        "terrain-splat recipe: .r32 dep missing byte payload");
                    return compile_failed_node(input);
                }

                // ── 2. .r32 byte-count → potential sample count ──
                if ((r32_bytes->size() % 4) != 0) {
                    logger.error(
                        "terrain-splat recipe: .r32 size "
                        + std::to_string(r32_bytes->size())
                        + " not a multiple of 4");
                    return compile_failed_node(input);
                }
                const uint64_t sample_count = r32_bytes->size() / 4u;

                // ── 3. Read pre-compiled JSON sidecar from JSONTable ──
                const JSONData* json_data = json_table.get(dep_handles[1]);
                if (!json_data
                    || !json_data->document.root
                    || json_data->document.root->kind
                        != wz::json::JSONValueKind::Object)
                {
                    logger.error(
                        "terrain-splat recipe: JSON sidecar dependency "
                        "is invalid or not an object");
                    return compile_failed_node(input);
                }
                const wz::json::JSONValue& json = *json_data->document.root;

                // ── 4. Required + optional world parameters ──
                auto height_scale_v = read_number(json, "height_scale");
                if (!height_scale_v) {
                    logger.error(
                        "terrain-splat recipe: sidecar missing 'height_scale'");
                    return compile_failed_node(input);
                }
                auto step_x_v = read_number(json, "step_x");
                if (!step_x_v) {
                    logger.error(
                        "terrain-splat recipe: sidecar missing 'step_x'");
                    return compile_failed_node(input);
                }
                auto step_z_v = read_number(json, "step_z");
                if (!step_z_v) {
                    logger.error(
                        "terrain-splat recipe: sidecar missing 'step_z'");
                    return compile_failed_node(input);
                }
                // Narrow through the shared helper rather than a bare cast
                // (issue #310, A4-C22). `1e308` is a legal finite JSON number
                // that becomes inf on the way to float, and the cast is UB
                // besides. The .r32's own NaN/inf rejection further down does
                // NOT cover this: the poison is manufactured AFTER that check,
                // from a legal 0.0 sample, because a Gaea file honouring the
                // documented [0,1] contract contains 0.0 at its minimum and
                // `0.0f * inf` is NaN. Those NaNs then became splat positions
                // and silently escaped the AABB reduction -- `wy < bmin` and
                // `wy > bmax` are both false for NaN -- while the non-zero
                // samples drove the bounds to +/-inf. The compile still
                // SUCCEEDED, because cloud.valid() is only !splats.empty().
                const auto height_scale_n = narrow_float(*height_scale_v);
                const auto step_x_n       = narrow_float(*step_x_v);
                const auto step_z_n       = narrow_float(*step_z_v);
                if (!height_scale_n || !step_x_n || !step_z_n) {
                    logger.error(
                        "terrain-splat recipe: sidecar 'height_scale', 'step_x' "
                        "or 'step_z' is outside the range of a float");
                    return compile_failed_node(input);
                }
                float height_scale = *height_scale_n;
                float step_x       = *step_x_n;
                float step_z       = *step_z_n;

                float overlap_factor  = 1.25f;
                float thickness       = 0.0f;
                uint32_t subsample    = 1u;
                float opacity         = 0.95f;
                float flat_lum        = 0.55f;
                float steep_lum       = 0.30f;
                if (auto v = read_number(json, "overlap_factor"))
                    if (auto n = narrow_float(*v)) overlap_factor = *n;
                if (auto v = read_number(json, "thickness"))
                    if (auto n = narrow_float(*v)) thickness = *n;
                if (auto v = read_uint(json, "subsample_step"))    subsample      = *v;
                if (subsample == 0u) subsample = 1u;
                if (auto v = read_number(json, "opacity"))
                    if (auto n = narrow_float(*v)) opacity = *n;
                if (auto v = read_number(json, "flat_luminance"))
                    if (auto n = narrow_float(*v)) flat_lum = *n;
                if (auto v = read_number(json, "steep_luminance"))
                    if (auto n = narrow_float(*v)) steep_lum = *n;

                // Normal smoothing — optional sidecar fields.
                uint32_t normal_smooth_flag   = 0u;
                uint32_t normal_smooth_radius = 2u;
                float    normal_smooth_sigma  = 1.0f;
                if (auto v = read_uint(json, "normal_smoothing_enabled"))       normal_smooth_flag   = *v;
                if (auto v = read_uint(json, "normal_smoothing_radius_cells"))  normal_smooth_radius = *v;
                // Bounded because the smoothing loop is O((2r+1)^2) PER EMITTED
                // TEXEL and its in-range test discards out-of-bounds samples
                // WITHOUT shortening the loop, so a large radius is a compile
                // that never returns rather than one that is merely slow.
                // 4096 is far past any useful smoothing kernel.
                if (normal_smooth_radius > 4096u) normal_smooth_radius = 4096u;
                if (auto v = read_number(json, "normal_smoothing_sigma_cells"))
                    if (auto n = narrow_float(*v)) normal_smooth_sigma = *n;

                // ── 5. Resolve dimensions: sidecar overrides, else square ──
                uint32_t width  = read_uint(json, "width").value_or(0u);
                uint32_t height = read_uint(json, "height").value_or(0u);

                if (width == 0 && height == 0) {
                    const double side_d = std::sqrt(
                        static_cast<double>(sample_count));
                    const uint32_t side =
                        static_cast<uint32_t>(side_d + 0.5);
                    if (static_cast<uint64_t>(side)
                        * static_cast<uint64_t>(side) != sample_count)
                    {
                        logger.error(
                            "terrain-splat recipe: .r32 sample_count "
                            + std::to_string(sample_count)
                            + " is not a perfect square; specify width "
                              "and height in the sidecar");
                        return compile_failed_node(input);
                    }
                    width  = side;
                    height = side;
                }
                else if (width == 0 || height == 0) {
                    logger.error(
                        "terrain-splat recipe: sidecar must provide BOTH "
                        "width and height when either is specified");
                    return compile_failed_node(input);
                }

                if (static_cast<uint64_t>(width)
                    * static_cast<uint64_t>(height) != sample_count)
                {
                    logger.error(
                        "terrain-splat recipe: width("
                        + std::to_string(width)
                        + ") * height("
                        + std::to_string(height)
                        + ") != sample_count("
                        + std::to_string(sample_count) + ")");
                    return compile_failed_node(input);
                }

                // ── 6. Build transient ScalarFieldData from .r32 bytes ──
                ScalarFieldData field{};
                field.width  = width;
                field.height = height;
                field.depth  = 1u;
                field.format = ScalarFieldFormat::Float32;
                field.domain_kind = ScalarFieldDomainKind::Spatial2D;
                field.layout = ScalarFieldSampleLayout::TexelCentered;
                field.origin = ScalarFieldOrigin::TopLeft;
                field.values.resize(static_cast<size_t>(sample_count));
                std::memcpy(
                    field.values.data(),
                    r32_bytes->data(),
                    static_cast<size_t>(sample_count) * sizeof(float));

                // Min/max + NaN/Inf check.
                float min_val = std::numeric_limits<float>::max();
                float max_val = std::numeric_limits<float>::lowest();
                for (uint64_t i = 0; i < sample_count; ++i) {
                    const float v = field.values[i];
                    if (std::isnan(v) || std::isinf(v)) {
                        logger.error(
                            "terrain-splat recipe: .r32 contains NaN/Inf at "
                            "sample " + std::to_string(i));
                        return compile_failed_node(input);
                    }
                    if (v < min_val) min_val = v;
                    if (v > max_val) max_val = v;
                }
                field.min_value = min_val;
                field.max_value = max_val;

                // ── 7. Build the terrain compile desc + run compile ──
                GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc desc{};
                desc.height_scale    = height_scale;
                desc.step_x          = step_x;
                desc.step_z          = step_z;
                desc.overlap_factor  = overlap_factor;
                desc.thickness       = thickness;
                desc.subsample_step  = subsample;
                desc.opacity         = opacity;
                desc.flat_luminance  = flat_lum;
                desc.steep_luminance = steep_lum;
                desc.normal_smoothing_enabled      = (normal_smooth_flag != 0u);
                desc.normal_smoothing_radius_cells = normal_smooth_radius;
                desc.normal_smoothing_sigma_cells  = normal_smooth_sigma;

                GaussianSplatCloudData cloud =
                    make_terrain_surface_splat_cloud(desc, field);

                if (!cloud.valid()) {
                    logger.error(
                        "terrain-splat recipe: terrain compile produced "
                        "empty cloud");
                    return compile_failed_node(input);
                }

                logger.info(
                    "terrain-splat recipe compiled: "
                    + std::to_string(width) + "x"
                    + std::to_string(height) + " field ("
                    + std::to_string(sample_count) + " samples) -> "
                    + std::to_string(cloud.splats.size())
                    + " splats (subsample_step="
                    + std::to_string(subsample) + ")");

                wz::asset::ResourceHandle handle =
                    cloud_table.add(std::move(cloud));
                if (!handle.valid()) {
                    logger.error(
                        "terrain-splat recipe: failed to store cloud");
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
