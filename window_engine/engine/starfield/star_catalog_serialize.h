#pragma once

// engine/starfield/star_catalog_serialize.h
//
// (De)serialize a baked .star_catalog.json document into the raw catalog rows +
// grade dials the star importer consumes (Seam C-2, issue #266). Kept parallel
// to sky_gaussian_serialize.h: the document carries RAW catalog records (RA /
// Dec / magnitude / B-V) plus the StarImportParams, and the compiler runs
// build_catalog at compile time -- so the dials are content-addressed and
// re-import + live-tune for free, and the on-disk sidecar stays catalog-shaped
// (a small prep tool or hand authoring produces it; the format-specific parser
// is a later seam).
//
// Schema v1:
//   {
//     "version": 1,
//     "source_name": "bright_stars",
//     "params": {                        // all optional; kernel defaults apply
//       "reference_magnitude": 0.0,
//       "exposure": 1.0,
//       "color_saturation": 1.0,
//       "magnitude_min": -30.0,
//       "magnitude_max": 6.5,
//       "solid_angle": 6.0e-8
//     },
//     "stars": [
//       { "ra_hours": 6.7525, "dec_deg": -16.716, "vmag": -1.46, "bv": 0.0 },
//       { "ra_hours": 18.615, "dec_deg":  38.784, "vmag":  0.03 }   // bv absent -> neutral
//     ]
//   }

#include <engine/starfield/star_catalog.h>

#include <external/json/json_document.h>

#include <string>
#include <vector>

namespace wz::engine::starfield
{
    // Parse the raw rows + params out of a decoded JSON document. Returns false
    // with a human error on a malformed document (bad root, "stars" not an
    // array, a row missing ra/dec/vmag). Absent "params" -> kernel defaults;
    // a row without "bv" -> has_bv = false (neutral).
    bool star_catalog_from_json(
        const wz::json::JSONValue& value,
        std::vector<CatalogRecord>& out_records,
        StarImportParams& out_params,
        std::string& out_source_name,
        std::string& error);

} // namespace wz::engine::starfield
