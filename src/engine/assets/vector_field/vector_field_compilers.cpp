#include <engine/assets/vector_field/vector_field_compilers.h>

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
        bool valid_components_per_channel(uint32_t components)
        {
            return components >= 2 && components <= 4;
        }

        bool compute_lane_min_max(
            const std::vector<float>& values,
            uint32_t lane_count,
            std::vector<float>& min_values,
            std::vector<float>& max_values,
            wz::Logger& logger)
        {
            if (values.empty()) {
                logger.error("vector field has no values");
                return false;
            }

            min_values.assign(
                lane_count,
                std::numeric_limits<float>::max());
            max_values.assign(
                lane_count,
                std::numeric_limits<float>::lowest());

            for (uint32_t i = 0; i < static_cast<uint32_t>(values.size()); ++i) {
                const float v = values[i];

                if (std::isnan(v)) {
                    logger.error("vector field contains NaN at value index "
                        + std::to_string(i));
                    return false;
                }

                if (std::isinf(v)) {
                    logger.error("vector field contains infinity at value index "
                        + std::to_string(i));
                    return false;
                }

                const uint32_t lane = i % lane_count;
                if (v < min_values[lane]) min_values[lane] = v;
                if (v > max_values[lane]) max_values[lane] = v;
            }

            return true;
        }
    }

    void register_vector_field_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        VectorFieldTable& vector_field_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kVectorFieldFromRawF32Schema,
            .output_type = kAssetTypeVectorField,
            .compile = [&logger, &vector_field_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<VectorFieldCompileDesc>(&input.meta);

                if (!desc) {
                    logger.error("vector field node missing VectorFieldCompileDesc");
                    return compile_failed_node(input);
                }

                if (desc->width == 0 || desc->height == 0 || desc->depth == 0) {
                    logger.error("vector field has zero dimension ("
                        + std::to_string(desc->width) + "x"
                        + std::to_string(desc->height) + "x"
                        + std::to_string(desc->depth) + ")");
                    return compile_failed_node(input);
                }

                if (!valid_components_per_channel(
                        desc->components_per_channel))
                {
                    logger.error(
                        "vector field components_per_channel must be 2, 3, or 4");
                    return compile_failed_node(input);
                }

                if (desc->channels.empty()) {
                    logger.error("vector field has no channels");
                    return compile_failed_node(input);
                }

                if (desc->format != VectorFieldFormat::Float32) {
                    logger.error("vector field only supports Float32 in V1");
                    return compile_failed_node(input);
                }

                if (dep_nodes.empty()) {
                    logger.error("vector field node has no file dependency");
                    return compile_failed_node(input);
                }

                const auto* bytes =
                    std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);

                if (!bytes) {
                    logger.error("vector field dep node has no byte payload");
                    return compile_failed_node(input);
                }

                const uint32_t sample_count =
                    desc->width * desc->height * desc->depth;
                const uint32_t lane_count =
                    static_cast<uint32_t>(desc->channels.size())
                    * desc->components_per_channel;
                const uint32_t value_count = sample_count * lane_count;
                const uint32_t expected_bytes =
                    value_count * static_cast<uint32_t>(sizeof(float));

                if (bytes->size() != expected_bytes) {
                    logger.error("vector field byte count mismatch: expected "
                        + std::to_string(expected_bytes)
                        + " bytes, got "
                        + std::to_string(bytes->size()));
                    return compile_failed_node(input);
                }

                std::vector<float> values(value_count);
                std::memcpy(values.data(), bytes->data(), expected_bytes);

                std::vector<float> min_values;
                std::vector<float> max_values;
                if (!compute_lane_min_max(
                        values,
                        lane_count,
                        min_values,
                        max_values,
                        logger))
                {
                    return compile_failed_node(input);
                }

                VectorFieldData data;
                data.width = desc->width;
                data.height = desc->height;
                data.depth = desc->depth;
                data.components_per_channel = desc->components_per_channel;
                data.channels = desc->channels;
                data.format = desc->format;
                data.domain_kind = desc->domain_kind;
                data.min_values = std::move(min_values);
                data.max_values = std::move(max_values);
                data.values = std::move(values);

                wz::asset::ResourceHandle handle =
                    vector_field_table.add(std::move(data));

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            },
        });
    }

} // namespace wz::engine::assets::internal
