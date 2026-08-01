#pragma once

// starfield/star_catalog_ply_importer.h
//
// Import a baked star PLY point cloud (stage-1 tycho2_prep output, or any PLY
// with per-vertex x/y/z = unit direction + vmag + optional bv) into a built
// StarCatalog, applying the creative import dials (Seam C-2/T-2, issue #266).
// This is the runtime half of the two-stage Tycho-2 path: the compact binary PLY
// reads fast, and the astronomy/dials run through the shared kernel.

#include <starfield/star_catalog.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace wz::engine::starfield
{
    struct StarCatalogPlyImportResult
    {
        bool        ok = false;
        StarCatalog catalog;
        std::string error;

        // Rows dropped because a required value was NaN or infinite (issue
        // #310, A4-C9). Reported rather than merely skipped so a silently
        // degraded catalogue is visible: the magnitude window admitted NaN, and
        // a NaN star reaches the additively-blended GPU buffer where it
        // poisons the pixels it lands on rather than just failing to draw.
        std::size_t non_finite_rows_skipped = 0;
    };

    // Parse PLY bytes and build a StarCatalog. The "vertex" element must carry
    // float properties x, y, z (the celestial direction) and vmag; bv is optional
    // (absent -> neutral color). Rows outside the params' magnitude window are
    // dropped; the surviving rows go through star_from_direction (warp / remap /
    // flux / tint).
    StarCatalogPlyImportResult import_star_catalog_ply_bytes(
        std::span<const std::uint8_t> bytes,
        const StarImportParams& params);

} // namespace wz::engine::starfield
