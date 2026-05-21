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

#include <external/json/json_parser.h>

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
        const wz::json::JSONValue* find_member(
            const wz::json::JSONValue& obj,
            std::string_view key) noexcept
        {
            if (obj.kind != wz::json::JSONValueKind::Object) return nullptr;
            for (const auto& m : obj.object_members) {
                if (m.key == key && m.value)
                    return m.value.get();
            }
            return nullptr;
        }

        bool read_number(
            const wz::json::JSONValue& obj,
            std::string_view key,
            float& out) noexcept
        {
            const auto* v = find_member(obj, key);
            if (!v || v->kind != wz::json::JSONValueKind::Number) return false;
            out = static_cast<float>(v->number_value);
            return true;
        }

        bool read_uint(
            const wz::json::JSONValue& obj,
            std::string_view key,
            uint32_t& out) noexcept
        {
            const auto* v = find_member(obj, key);
            if (!v || v->kind != wz::json::JSONValueKind::Number) return false;
            if (v->number_value < 0.0) return false;
            out = static_cast<uint32_t>(v->number_value);
            return true;
        }
    }


    void register_terrain_splat_from_gaea_r32_compiler(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        GaussianSplatCloudTable& cloud_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kTerrainSplatFromGaeaR32Schema,
            .output_type  = kAssetTypeGaussianSplatCloud,
            .compile = [&logger, &cloud_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                // ── 1. Validate dependency count + payloads ──
                if (dep_nodes.size() != 2) {
                    logger.error(
                        "terrain-splat recipe: expected 2 file deps "
                        "(.r32 + .json), got "
                        + std::to_string(dep_nodes.size()));
                    return compile_failed_node(input);
                }

                const auto* r32_bytes =
                    std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);
                const auto* json_bytes =
                    std::get_if<std::vector<uint8_t>>(&dep_nodes[1].payload);

                if (!r32_bytes || !json_bytes) {
                    logger.error(
                        "terrain-splat recipe: deps missing byte payloads");
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

                // ── 3. Parse JSON sidecar ──
                const auto parsed = wz::json::parse_json_bytes(*json_bytes);
                if (!parsed.ok
                    || !parsed.document.root
                    || parsed.document.root->kind
                        != wz::json::JSONValueKind::Object)
                {
                    logger.error(
                        "terrain-splat recipe: failed to parse JSON sidecar: "
                        + parsed.error.message);
                    return compile_failed_node(input);
                }
                const wz::json::JSONValue& json = *parsed.document.root;

                // ── 4. Required + optional world parameters ──
                float height_scale = 1.0f;
                float step_x = 1.0f;
                float step_z = 1.0f;
                if (!read_number(json, "height_scale", height_scale)) {
                    logger.error(
                        "terrain-splat recipe: sidecar missing 'height_scale'");
                    return compile_failed_node(input);
                }
                if (!read_number(json, "step_x", step_x)) {
                    logger.error(
                        "terrain-splat recipe: sidecar missing 'step_x'");
                    return compile_failed_node(input);
                }
                if (!read_number(json, "step_z", step_z)) {
                    logger.error(
                        "terrain-splat recipe: sidecar missing 'step_z'");
                    return compile_failed_node(input);
                }

                float overlap_factor  = 1.25f;
                float thickness       = 0.0f;
                uint32_t subsample    = 1u;
                float opacity         = 0.95f;
                float flat_lum        = 0.55f;
                float steep_lum       = 0.30f;
                read_number(json, "overlap_factor",  overlap_factor);
                read_number(json, "thickness",       thickness);
                read_uint  (json, "subsample_step",  subsample);
                if (subsample == 0u) subsample = 1u;
                read_number(json, "opacity",         opacity);
                read_number(json, "flat_luminance",  flat_lum);
                read_number(json, "steep_luminance", steep_lum);

                // ── 5. Resolve dimensions: sidecar overrides, else square ──
                uint32_t width  = 0;
                uint32_t height = 0;
                read_uint(json, "width",  width);
                read_uint(json, "height", height);

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
