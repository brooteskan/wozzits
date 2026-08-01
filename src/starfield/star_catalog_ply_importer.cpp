// src/starfield/star_catalog_ply_importer.cpp

#include <starfield/star_catalog_ply_importer.h>

#include <ply/ply_reader.h>

#include <cmath>
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
        std::size_t non_finite_rows = 0;
        for (std::size_t row = 0; row < table->row_count; ++row) {
            const double vmag = cell(*table, row, iv);

            // The magnitude window ADMITS NaN -- both comparisons are false for
            // it -- so this cull was not the filter it looked like (issue #310,
            // A4-C9). A NaN vmag then makes flux = pow(10, -0.4*NaN) = NaN, so
            // radiance and magnitude reach the GPU as NaN; the star field is
            // drawn with additive blending, where NaN + dst = NaN, so a single
            // bad row does not merely lose one star, it poisons whatever was
            // already in those framebuffer pixels and anything a later
            // neighbourhood filter spreads it into. A NaN in x/y/z survives
            // normalize() untouched for the same reason (len == 0.0f is false
            // for NaN), so the direction needs checking too.
            //
            // ASCII PLYs were accidentally immune because tinyply rejects NaN
            // while parsing text; binary ones -- which is what tycho2_prep
            // writes -- were not.
            //
            // The sibling importer over this same reader has always rejected
            // the whole file on any non-finite value. Skipping the ROW rather
            // than failing the file, because a star catalogue is a bulk
            // observational import where one bad row among millions is a data
            // problem, not a reason to lose the sky.
            const double x = cell(*table, row, ix);
            const double y = cell(*table, row, iy);
            const double z = cell(*table, row, iz);
            if (!std::isfinite(vmag) || !std::isfinite(x)
                || !std::isfinite(y) || !std::isfinite(z)) {
                ++non_finite_rows;
                continue;
            }

            if (vmag < params.magnitude_min || vmag > params.magnitude_max) {
                continue;
            }
            const wz::math::Vec3 dir{
                static_cast<float>(x),
                static_cast<float>(y),
                static_cast<float>(z),
            };
            const bool has_bv = ibv >= 0;
            double bv = has_bv ? cell(*table, row, ibv) : 0.0;
            if (!std::isfinite(bv))
                bv = 0.0;   // colour index degrades to neutral, not to NaN tint
            result.catalog.stars.push_back(
                star_from_direction(dir, vmag, bv, has_bv, params));
        }

        result.non_finite_rows_skipped = non_finite_rows;

        if (result.catalog.stars.empty()) {
            result.error = "star PLY produced no stars (all culled?)";
            return result;
        }

        result.ok = true;
        return result;
    }

} // namespace wz::engine::starfield
