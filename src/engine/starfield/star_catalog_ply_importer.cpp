// src/engine/starfield/star_catalog_ply_importer.cpp

#include <engine/starfield/star_catalog_ply_importer.h>

#include <ply/ply_reader.h>

#include <cstddef>

namespace wz::engine::starfield
{
    namespace
    {
        const wz::external::ply::ScalarTable* find_vertex_table(
            const wz::external::ply::Document& document)
        {
            for (const wz::external::ply::ScalarTable& table :
                     document.scalar_tables) {
                if (table.element_name == "vertex") {
                    return &table;
                }
            }
            return nullptr;
        }

        // Index of a named property, or -1.
        int property_index(
            const wz::external::ply::ScalarTable& table, const char* name)
        {
            for (std::size_t i = 0; i < table.properties.size(); ++i) {
                if (table.properties[i].name == name) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        double cell(
            const wz::external::ply::ScalarTable& table,
            std::size_t row, int prop)
        {
            return table.values[row * table.properties.size()
                + static_cast<std::size_t>(prop)];
        }
    } // namespace

    StarCatalogPlyImportResult import_star_catalog_ply_bytes(
        std::span<const std::uint8_t> bytes,
        const StarImportParams& params)
    {
        StarCatalogPlyImportResult result;

        const wz::external::ply::ReadResult read =
            wz::external::ply::read_ply_bytes(bytes);
        if (!read.ok) {
            result.error = "failed to parse star PLY: " + read.error.message;
            return result;
        }

        const wz::external::ply::ScalarTable* table =
            find_vertex_table(read.document);
        if (!table) {
            result.error = "star PLY has no \"vertex\" element";
            return result;
        }

        const int ix = property_index(*table, "x");
        const int iy = property_index(*table, "y");
        const int iz = property_index(*table, "z");
        const int iv = property_index(*table, "vmag");
        const int ibv = property_index(*table, "bv");   // optional
        if (ix < 0 || iy < 0 || iz < 0 || iv < 0) {
            result.error =
                "star PLY vertex is missing x / y / z / vmag properties";
            return result;
        }

        result.catalog.source_name = table->element_name;
        result.catalog.stars.reserve(static_cast<std::size_t>(table->row_count));
        for (std::size_t row = 0; row < table->row_count; ++row) {
            const double vmag = cell(*table, row, iv);
            if (vmag < params.magnitude_min || vmag > params.magnitude_max) {
                continue;
            }
            const wz::math::Vec3 dir{
                static_cast<float>(cell(*table, row, ix)),
                static_cast<float>(cell(*table, row, iy)),
                static_cast<float>(cell(*table, row, iz)),
            };
            const bool has_bv = ibv >= 0;
            const double bv = has_bv ? cell(*table, row, ibv) : 0.0;
            result.catalog.stars.push_back(
                star_from_direction(dir, vmag, bv, has_bv, params));
        }

        if (result.catalog.stars.empty()) {
            result.error = "star PLY produced no stars (all culled?)";
            return result;
        }

        result.ok = true;
        return result;
    }

} // namespace wz::engine::starfield
