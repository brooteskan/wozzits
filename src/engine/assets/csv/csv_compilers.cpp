// src/engine/assets/csv/csv_compilers.cpp

#include <engine/assets/csv/csv_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <charconv>
#include <cmath>

namespace wz::engine::assets::internal
{
    void register_csv_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        CSVTable& csv_table)
    {
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
    }

} // namespace wz::engine::assets::internal
