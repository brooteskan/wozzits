#include <engine/assets/vector_field/vector_field_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr std::array<std::string_view, 6>
            kVectorFieldDomainOptions = {
                "Unknown",
                "Spatial 1D",
                "Spatial 2D",
                "Lookup 1D",
                "Lookup 2D",
                "Baked computation",
            };

        template<class Enum, std::size_t Count>
        Enum enum_param(
            const wz::asset::ParamBlock& params,
            std::string_view name,
            Enum fallback,
            const std::array<std::string_view, Count>&)
        {
            const int64_t value =
                params.get<int64_t>(name, static_cast<int64_t>(fallback));
            if (value >= 0 && value < static_cast<int64_t>(Count)) {
                return static_cast<Enum>(value);
            }
            return fallback;
        }

        VectorFieldCompileDesc vector_field_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            VectorFieldCompileDesc desc{};
            desc.width = params.get<uint32_t>("width", desc.width);
            desc.height = params.get<uint32_t>("height", desc.height);
            desc.depth = params.get<uint32_t>("depth", desc.depth);
            desc.components_per_channel =
                params.get<uint32_t>(
                    "components_per_channel",
                    desc.components_per_channel);
            desc.domain_kind =
                enum_param(
                    params,
                    "domain_kind",
                    desc.domain_kind,
                    kVectorFieldDomainOptions);

            const uint32_t channel_count =
                std::min(params.get<uint32_t>("channel_count", 0u), 8u);
            desc.channels.reserve(channel_count);
            for (uint32_t i = 0; i < channel_count; ++i) {
                const std::string param_name =
                    "channel_" + std::to_string(i) + "_name";
                std::string fallback =
                    i == 0u
                    ? std::string("vector")
                    : "channel_" + std::to_string(i);
                desc.channels.push_back(VectorFieldChannelDesc{
                    .name = params.get<std::string>(
                        param_name,
                        std::move(fallback)),
                });
            }
            return desc;
        }

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
            .input_ports = {
                { "source_file", kAssetTypeRawFile },
            },
            .parameters = {
                {
                    .name = "width",
                    .type = wz::asset::ParamType::Int,
                    .label = "Width",
                    .default_num = 0,
                    .min = 0,
                    .max = 65536,
                },
                {
                    .name = "height",
                    .type = wz::asset::ParamType::Int,
                    .label = "Height",
                    .default_num = 1,
                    .min = 1,
                    .max = 65536,
                },
                {
                    .name = "depth",
                    .type = wz::asset::ParamType::Int,
                    .label = "Depth",
                    .default_num = 1,
                    .min = 1,
                    .max = 65536,
                },
                {
                    .name = "components_per_channel",
                    .type = wz::asset::ParamType::Int,
                    .label = "Components",
                    .default_num = 3,
                    .min = 2,
                    .max = 4,
                },
                {
                    .name = "channel_count",
                    .type = wz::asset::ParamType::Int,
                    .label = "Channels",
                    .default_num = 1,
                    .min = 1,
                    .max = 8,
                },
                {
                    .name = "channel_0_name",
                    .type = wz::asset::ParamType::String,
                    .label = "Channel 0",
                    .default_str = "vector",
                },
                {
                    .name = "channel_1_name",
                    .type = wz::asset::ParamType::String,
                    .label = "Channel 1",
                    .default_str = "channel_1",
                },
                {
                    .name = "channel_2_name",
                    .type = wz::asset::ParamType::String,
                    .label = "Channel 2",
                    .default_str = "channel_2",
                },
                {
                    .name = "channel_3_name",
                    .type = wz::asset::ParamType::String,
                    .label = "Channel 3",
                    .default_str = "channel_3",
                },
                {
                    .name = "channel_4_name",
                    .type = wz::asset::ParamType::String,
                    .label = "Channel 4",
                    .default_str = "channel_4",
                },
                {
                    .name = "channel_5_name",
                    .type = wz::asset::ParamType::String,
                    .label = "Channel 5",
                    .default_str = "channel_5",
                },
                {
                    .name = "channel_6_name",
                    .type = wz::asset::ParamType::String,
                    .label = "Channel 6",
                    .default_str = "channel_6",
                },
                {
                    .name = "channel_7_name",
                    .type = wz::asset::ParamType::String,
                    .label = "Channel 7",
                    .default_str = "channel_7",
                },
                {
                    .name = "domain_kind",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Domain",
                    .default_num =
                        static_cast<double>(
                            VectorFieldDomainKind::Spatial2D),
                    .options = kVectorFieldDomainOptions,
                },
            },
            .compile = [&logger, &vector_field_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>)
                    -> wz::asset::AssetNode
            {
                VectorFieldCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<VectorFieldCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(
                                &input.meta))
                    {
                        param_desc = vector_field_desc_from_params(*params);
                        desc = &param_desc;
                    }
                }

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
