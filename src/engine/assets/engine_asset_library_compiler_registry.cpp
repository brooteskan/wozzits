// src/engine/assets/engine_asset_library_compiler_registry.cpp

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/csv/csv.h>
#include <external/json/json_parser.h>
#include <engine/assets/json/json.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/mesh/procedural_mesh.h>
#include <gpu/shader.h>

#include <charconv>
#include <cmath>



namespace wz::engine::assets::internal
{
    namespace
    {
        wz::asset::AssetNode compile_procedural_mesh_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshData(*make_mesh)())
        {
            if (!dep_nodes.empty()) {
                logger.error("procedural mesh node should not have dependencies");
                return compile_failed_node(input);
            }

            MeshData data = make_mesh();

            if (!data.valid()) {
                logger.error("procedural mesh builder produced invalid mesh data");
                return compile_failed_node(input);
            }

            wz::asset::ResourceHandle handle =
                mesh_table.add(std::move(data));

            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }
    }


    // ── make_engine_compiler_registry ─────────────────────────────────────────

    wz::asset::CompilerRegistry make_engine_compiler_registry(
        wz::gpu::Device& device,
        wz::Logger& logger,
        ScalarFieldTable& scalar_field_table,
        CSVTable& csv_table,
        JSONTable& json_table,
        TOMLTable& toml_table,
        MeshTable& mesh_table)
    {
        wz::asset::CompilerRegistry registry;

        register_file_carrier_compilers(registry, logger);
        register_json_compilers(registry, logger, json_table);
        register_toml_compilers(registry, logger, toml_table);

        // ── HLSL file carrier compiler ────────────────────────────────────────
        //
        // Separate from the raw file carrier so HLSL files are not confused
        // with raw binary assets at the schema dispatch level.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kHLSLFileSchema,
            .output_type = wz::asset::AssetType::ShaderSource,
            .compile = [&logger](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                const auto* file =
                    std::any_cast<FileSourceDesc>(&input.meta);

                if (!file) {
                    logger.error("HLSL file carrier missing FileSourceDesc");
                    return compile_failed_node(input);
                }

                auto file_result = wz::fs::read_file(file->full_path);
                if (!file_result) {
                    logger.error("failed to read file: " + file->full_path);
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = std::move(file_result.value);
                return out;
            }
            });


        // ── HLSL shader compiler ──────────────────────────────────────────────

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kHLSLShaderSchema,
            .output_type = wz::asset::AssetType::Shader,
            .compile = [&logger, &device](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<wz::gpu::HLSLCompileDesc>(&input.meta);

                if (!desc) {
                    logger.error("shader node missing HLSLCompileDesc");
                    return compile_failed_node(input);
                }

                if (dep_nodes.empty()) {
                    logger.error("shader node has no source dependencies");
                    return compile_failed_node(input);
                }

                if (desc->primary_source_index >= dep_nodes.size()) {
                    logger.error("shader primary_source_index out of range");
                    return compile_failed_node(input);
                }

                std::vector<std::span<const uint8_t>> sources;
                sources.reserve(dep_nodes.size());

                for (const wz::asset::AssetNode& dep : dep_nodes) {
                    const auto* bytes =
                        std::get_if<std::vector<uint8_t>>(&dep.payload);

                    if (!bytes) {
                        logger.error("file dep node has no byte payload");
                        return compile_failed_node(input);
                    }

                    sources.push_back({ bytes->data(), bytes->size() });
                }

                wz::gpu::GPUHandle gpu_handle =
                    wz::gpu::compile_hlsl(device, sources, *desc);

                if (!gpu_handle.valid()) {
                    logger.error("gpu.compile_hlsl failed");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = wz::asset::ResourceHandle{
                    .id = gpu_handle.id,
                    .epoch = gpu_handle.epoch,
                    .type = wz::asset::AssetType::Shader,
                };

                return out;
            }
            });


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

                if (!internal::compute_min_max(values, min_val, max_val,
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


            // ── Procedural mesh compilers ─────────────────────────────────────────
    //
    // Dispatch on procedural mesh schemas.
    // These are CPU-side mesh assets: generated MeshData is stored in MeshTable,
    // and the compiled AssetNode stores the returned ResourceHandle.

            registry.register_compiler(wz::asset::AssetCompiler{
                .input_schema = kProceduralTriangleMeshSchema,
                .output_type = kAssetTypeMesh,
                .compile = [&logger, &mesh_table](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
                {
                    return compile_procedural_mesh_node(
                        input,
                        dep_nodes,
                        logger,
                        mesh_table,
                        &make_triangle_mesh);
                }
                });

            registry.register_compiler(wz::asset::AssetCompiler{
                .input_schema = kProceduralQuadMeshSchema,
                .output_type = kAssetTypeMesh,
                .compile = [&logger, &mesh_table](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
                {
                    return compile_procedural_mesh_node(
                        input,
                        dep_nodes,
                        logger,
                        mesh_table,
                        &make_quad_mesh);
                }
                });

            registry.register_compiler(wz::asset::AssetCompiler{
                .input_schema = kProceduralCubeMeshSchema,
                .output_type = kAssetTypeMesh,
                .compile = [&logger, &mesh_table](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
                {
                    return compile_procedural_mesh_node(
                        input,
                        dep_nodes,
                        logger,
                        mesh_table,
                        &make_cube_mesh);
                }
                });

        // ── CSV table compiler ────────────────────────────────────────────────
        //
        // Dispatches on kCSVTableSchema.
        // Expects exactly one dependency: a kCSVFileSchema node whose compiled
        // payload is the raw bytes of the CSV file.
        //
        // Parsing is RFC 4180 compatible:
        //   - Fields separated by commas.
        //   - Fields may be enclosed in double quotes.
        //   - A double quote inside a quoted field is escaped as "".
        //   - Multiline quoted fields are accepted.
        //   - CRLF and bare LF are both accepted as line terminators.
        //   - Ragged rows are accepted; is_ragged is set when widths differ.
        //
        // Header detection is controlled by CSVHeaderMode stored in the node meta:
        //   Auto        — row 0 is a header if none of its cells parse as a
        //                 finite floating-point number via std::from_chars.
        //   ForceHeader — row 0 is always the header.
        //   ForceData   — all rows are data; has_header is never set.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kCSVTableSchema,
            .output_type  = kAssetTypeCSVTable,
            .compile = [&logger, &csv_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                // ── 1. Validate metadata ──────────────────────────────────────

                const auto* desc =
                    std::any_cast<CSVCompileDesc>(&input.meta);

                if (!desc) {
                    logger.error("CSV table node missing CSVCompileDesc");
                    return compile_failed_node(input);
                }

                // ── 2. Validate dependency ────────────────────────────────────

                if (dep_nodes.empty()) {
                    logger.error("CSV table node has no file dependency");
                    return compile_failed_node(input);
                }

                const auto* bytes =
                    std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);

                if (!bytes) {
                    logger.error("CSV file dep node has no byte payload");
                    return compile_failed_node(input);
                }

                // ── 3. Parse CSV bytes into rows ──────────────────────────────
                //
                // Walks the raw bytes directly. Produces a vector of rows, each
                // a vector of string cells. A trailing newline does not produce
                // an extra empty row.

                std::vector<std::vector<std::string>> all_rows;
                {
                    std::vector<std::string> current_row;
                    std::string              current_cell;
                    bool                     in_quotes = false;

                    const char* p   = reinterpret_cast<const char*>(bytes->data());
                    const char* end = p + bytes->size();

                    while (p < end) {
                        const char c = *p;

                        if (in_quotes) {
                            if (c == '"') {
                                if (p + 1 < end && *(p + 1) == '"') {
                                    current_cell += '"';
                                    p += 2;
                                } else {
                                    in_quotes = false;
                                    ++p;
                                }
                            } else {
                                current_cell += c;
                                ++p;
                            }
                        } else {
                            if (c == '"') {
                                in_quotes = true;
                                ++p;
                            } else if (c == ',') {
                                current_row.push_back(std::move(current_cell));
                                current_cell.clear();
                                ++p;
                            } else if (c == '\r') {
                                current_row.push_back(std::move(current_cell));
                                current_cell.clear();
                                all_rows.push_back(std::move(current_row));
                                current_row.clear();
                                ++p;
                                if (p < end && *p == '\n') ++p;
                            } else if (c == '\n') {
                                current_row.push_back(std::move(current_cell));
                                current_cell.clear();
                                all_rows.push_back(std::move(current_row));
                                current_row.clear();
                                ++p;
                            } else {
                                current_cell += c;
                                ++p;
                            }
                        }
                    }

                    // Flush any remaining content not terminated by a newline.
                    if (!current_cell.empty() || !current_row.empty()) {
                        current_row.push_back(std::move(current_cell));
                        all_rows.push_back(std::move(current_row));
                    }
                }

                // ── 4. Detect / apply header mode ─────────────────────────────

                // Returns true if the cell cannot be parsed as a finite float.
                auto is_non_numeric = [](const std::string& cell) -> bool {
                    if (cell.empty()) return true;
                    double value = 0.0;
                    const auto result = std::from_chars(
                        cell.data(), cell.data() + cell.size(), value);
                    if (result.ec != std::errc{})           return true;
                    if (result.ptr != cell.data() + cell.size()) return true;
                    return !std::isfinite(value);
                };

                bool treat_first_as_header = false;

                switch (desc->header_mode) {
                case CSVHeaderMode::ForceHeader:
                    treat_first_as_header = !all_rows.empty();
                    break;
                case CSVHeaderMode::ForceData:
                    treat_first_as_header = false;
                    break;
                case CSVHeaderMode::Auto:
                default:
                    if (!all_rows.empty()) {
                        treat_first_as_header = true;
                        for (const auto& cell : all_rows[0]) {
                            if (!is_non_numeric(cell)) {
                                treat_first_as_header = false;
                                break;
                            }
                        }
                    }
                    break;
                }

                // ── 5. Build CSVData ──────────────────────────────────────────

                CSVData data;
                data.has_header = treat_first_as_header;

                const size_t data_start = treat_first_as_header ? 1 : 0;

                if (treat_first_as_header && !all_rows.empty())
                    data.headers = all_rows[0];

                for (size_t i = data_start; i < all_rows.size(); ++i)
                    data.rows.push_back(std::move(all_rows[i]));

                // Compute max_column_count and is_ragged across all data rows
                // (headers are not included in these metrics).
                for (const auto& row : data.rows) {
                    const uint32_t w = static_cast<uint32_t>(row.size());
                    if (w > data.max_column_count)
                        data.max_column_count = w;
                }

                if (!data.rows.empty()) {
                    const uint32_t first_w =
                        static_cast<uint32_t>(data.rows[0].size());
                    for (const auto& row : data.rows) {
                        if (static_cast<uint32_t>(row.size()) != first_w) {
                            data.is_ragged = true;
                            break;
                        }
                    }
                }

                // ── 6. Store in table and return compiled node ────────────────

                wz::asset::ResourceHandle handle = csv_table.add(std::move(data));

                wz::asset::AssetNode out = input;
                out.stage   = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
        });

        return registry;
    }
}