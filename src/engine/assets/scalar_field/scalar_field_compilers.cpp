// src/engine/assets/scalar_field/scalar_field_compilers.cpp

#include <engine/assets/scalar_field/scalar_field_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <cmath>
#include <cstring>
#include <limits>

namespace wz::engine::assets::internal
{
    namespace
    {
        // Validates all values (rejects NaN and infinity) and computes
        // min_value / max_value. Returns false and logs on the first bad value.
        bool compute_min_max(
            const std::vector<float>& values,
            float& min_value,
            float& max_value,
            wz::Logger& logger,
            std::string_view label)
        {
            if (values.empty()) {
                logger.error(std::string(label) + " has no values");
                return false;
            }

            min_value = std::numeric_limits<float>::max();
            max_value = std::numeric_limits<float>::lowest();

            for (uint32_t i = 0; i < static_cast<uint32_t>(values.size()); ++i) {
                const float v = values[i];

                if (std::isnan(v)) {
                    logger.error(std::string(label)
                        + " contains NaN at sample index " + std::to_string(i));
                    return false;
                }

                if (std::isinf(v)) {
                    logger.error(std::string(label)
                        + " contains infinity at sample index " + std::to_string(i));
                    return false;
                }

                if (v < min_value) min_value = v;
                if (v > max_value) max_value = v;
            }

            return true;
        }
    }


    void register_scalar_field_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        ScalarFieldTable& scalar_field_table)
    {
        // ── Scalar field compiler (file-backed) ───────────────────────────────
        //
        // Dispatches on kScalarFieldFromRawF32Schema.
        // Expects exactly one dependency: a kRawFileSchema node whose compiled
        // payload is a std::vector<uint8_t> of raw float32 bytes.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kScalarFieldFromRawF32Schema,
            .output_type = kAssetTypeScalarField,
            .compile = [&logger, &scalar_field_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                // ── 1. Validate metadata ──────────────────────────────────────

                const auto* desc =
                    std::any_cast<ScalarFieldCompileDesc>(&input.meta);

                if (!desc) {
                    logger.error("scalar field node missing ScalarFieldCompileDesc");
                    return compile_failed_node(input);
                }

                if (desc->width == 0 || desc->height == 0 || desc->depth == 0) {
                    logger.error("scalar field has zero dimension ("
                        + std::to_string(desc->width) + "x"
                        + std::to_string(desc->height) + "x"
                        + std::to_string(desc->depth) + ")");
                    return compile_failed_node(input);
                }

                // ── 2. Validate dependency ────────────────────────────────────

                if (dep_nodes.empty()) {
                    logger.error("scalar field node has no file dependency");
                    return compile_failed_node(input);
                }

                const auto* bytes =
                    std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);

                if (!bytes) {
                    logger.error("scalar field dep node has no byte payload");
                    return compile_failed_node(input);
                }

                // ── 3. Validate byte count ────────────────────────────────────

                const uint32_t count =
                    desc->width * desc->height * desc->depth;

                const uint32_t expected_bytes =
                    count * static_cast<uint32_t>(sizeof(float));

                if (bytes->size() != expected_bytes) {
                    logger.error("scalar field byte count mismatch: expected "
                        + std::to_string(expected_bytes)
                        + " bytes ("
                        + std::to_string(desc->width) + "x"
                        + std::to_string(desc->height) + "x"
                        + std::to_string(desc->depth) + "xf32"
                        + "), got "
                        + std::to_string(bytes->size()));
                    return compile_failed_node(input);
                }

                // ── 4. Reinterpret bytes as float32 values ────────────────────

                std::vector<float> values(count);
                std::memcpy(values.data(), bytes->data(), expected_bytes);

                // ── 5. Validate values; compute min/max ───────────────────────

                float min_val = 0.0f;
                float max_val = 0.0f;

                if (!compute_min_max(values, min_val, max_val,
                                     logger, "scalar field"))
                {
                    return compile_failed_node(input);
                }

                // ── 6. Store in ScalarFieldTable ──────────────────────────────

                ScalarFieldData data;
                data.width = desc->width;
                data.height = desc->height;
                data.depth = desc->depth;
                data.format = desc->format;
                data.domain_kind = desc->domain_kind;
                data.min_value = min_val;
                data.max_value = max_val;
                data.values = std::move(values);

                wz::asset::ResourceHandle handle =
                    scalar_field_table.add(std::move(data));

                // ── 7. Return compiled node ───────────────────────────────────

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });


        // ── Scalar field compiler (procedural) ────────────────────────────────
        //
        // Dispatches on kScalarFieldProceduralSchema.
        // Generates values from ProceduralScalarFieldCompileDesc metadata alone;
        // expects no file dependencies in dep_nodes.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kScalarFieldProceduralSchema,
            .output_type = kAssetTypeScalarField,
            .compile = [&logger, &scalar_field_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                // ── 1. Validate metadata ──────────────────────────────────────

                const auto* desc =
                    std::any_cast<ProceduralScalarFieldCompileDesc>(&input.meta);

                if (!desc) {
                    logger.error("procedural scalar field node missing "
                        "ProceduralScalarFieldCompileDesc");
                    return compile_failed_node(input);
                }

                if (desc->width == 0 || desc->height == 0 || desc->depth == 0) {
                    logger.error("procedural scalar field has zero dimension ("
                        + std::to_string(desc->width) + "x"
                        + std::to_string(desc->height) + "x"
                        + std::to_string(desc->depth) + ")");
                    return compile_failed_node(input);
                }

                if (desc->depth != 1) {
                    logger.error(
                        "procedural scalar field depth > 1 is not supported in V1 "
                        "(got depth=" + std::to_string(desc->depth) + ")");
                    return compile_failed_node(input);
                }

                if (desc->format != ScalarFieldFormat::Float32) {
                    logger.error(
                        "procedural scalar field only supports Float32 in V1");
                    return compile_failed_node(input);
                }

                if (!dep_nodes.empty()) {
                    logger.error(
                        "procedural scalar field should not have dependencies");
                    return compile_failed_node(input);
                }

                // ── 2. Generate values ────────────────────────────────────────

                const uint32_t width = desc->width;
                const uint32_t height = desc->height;
                const uint32_t count = width * height; // depth == 1

                std::vector<float> values(count);

                constexpr float pi = 3.14159265358979323846f;

                for (uint32_t y = 0; y < height; ++y) {
                    for (uint32_t x = 0; x < width; ++x) {
                        float value = 0.0f;

                        switch (desc->generator) {

                        case ScalarFieldGenerator::GradientX:
                            value = (width > 1)
                                ? static_cast<float>(x) /
                                  static_cast<float>(width - 1)
                                : 0.0f;
                            break;

                        case ScalarFieldGenerator::GradientY:
                            value = (height > 1)
                                ? static_cast<float>(y) /
                                  static_cast<float>(height - 1)
                                : 0.0f;
                            break;

                        case ScalarFieldGenerator::RadialGradient:
                        {
                            const float cx = 0.5f * static_cast<float>(width - 1);
                            const float cy = 0.5f * static_cast<float>(height - 1);
                            const float dx = static_cast<float>(x) - cx;
                            const float dy = static_cast<float>(y) - cy;
                            const float max_dist =
                                std::sqrt(cx * cx + cy * cy);
                            const float dist =
                                std::sqrt(dx * dx + dy * dy);
                            value = (max_dist > 0.0f)
                                ? std::min(dist / max_dist, 1.0f)
                                : 0.0f;
                            value *= desc->amplitude;
                            break;
                        }

                        case ScalarFieldGenerator::Checkerboard:
                        {
                            const uint32_t cell =
                                std::max(1u, static_cast<uint32_t>(
                                    desc->frequency));
                            value = ((x / cell + y / cell) % 2 != 0)
                                ? desc->amplitude
                                : 0.0f;
                            break;
                        }

                        case ScalarFieldGenerator::SineWaves:
                        {
                            // u in [0, 1] across width; guard for width == 1.
                            const float u = (width > 1)
                                ? static_cast<float>(x) /
                                  static_cast<float>(width - 1)
                                : 0.0f;
                            // For amplitude == 1, output is [0, 1].
                            // Larger amplitudes intentionally produce values
                            // outside [0, 1]; min_value/max_value record the
                            // true generated range.
                            value = (0.5f + 0.5f * std::sin(
                                u * desc->frequency * 2.0f * pi))
                                * desc->amplitude;
                            break;
                        }

                        } // switch

                        values[x + y * width] = value;
                    }
                }

                // ── 3. Validate values; compute min/max ───────────────────────

                float min_val = 0.0f;
                float max_val = 0.0f;

                if (!compute_min_max(values, min_val, max_val,
                                     logger, "procedural scalar field"))
                {
                    return compile_failed_node(input);
                }

                // ── 4. Store in ScalarFieldTable ──────────────────────────────

                ScalarFieldData data;
                data.width = desc->width;
                data.height = desc->height;
                data.depth = desc->depth;
                data.format = desc->format;
                data.domain_kind = desc->domain_kind;
                data.min_value = min_val;
                data.max_value = max_val;
                data.values = std::move(values);

                wz::asset::ResourceHandle handle =
                    scalar_field_table.add(std::move(data));

                // ── 5. Return compiled node ───────────────────────────────────

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }

} // namespace wz::engine::assets::internal
