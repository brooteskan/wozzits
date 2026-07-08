#pragma once

// engine/starfield/tycho2.h
//
// Tycho-2 catalogue (CDS I/259, Hoeg et al. 2000) parsing -- the catalog-format
// front of the star field (Seam C / issue #266, the "two-stage tycho2_prep"
// path). Tycho-2 is ~2.5M stars in a fixed-width ASCII catalog.dat; this turns
// one record line into the same catalog-agnostic CatalogRecord the astronomy
// kernel consumes, so no new astronomy lives here -- only the format decode +
// the Tycho BT/VT -> Johnson V, B-V conversion.
//
// The prep CLI (wozzits_tycho2_prep) parses the whole catalog with this and
// bakes a compact PLY point cloud; the StarCatalogFromPLY compiler then imports
// that blob with the creative dials. This header is the tested, reusable core.
//
// Pure CPU, deterministic, no GPU.

#include <engine/starfield/star_catalog.h>

#include <optional>
#include <string_view>

namespace wz::engine::starfield
{
    // Parse one catalog.dat line into a CatalogRecord (RA hours / Dec deg / V
    // mag / B-V). Byte columns are 1-indexed per the CDS I/259 ReadMe:
    //   pflag 14 | mRAdeg 16-27 | mDEdeg 29-40 | BTmag 111-116 | VTmag 124-129
    //   observed RAdeg 153-164 | DEdeg 166-177
    // The mean ICRS position is used when present; a record flagged 'X' (no mean
    // position) falls back to the observed Tycho-2 position. Johnson V and B-V
    // come from the standard Tycho transform  V = VT - 0.090 (BT-VT),
    // B-V = 0.850 (BT-VT); a record with only one of BT/VT keeps that as V with
    // has_bv = false. Returns nullopt for a line with no usable position or no
    // magnitude at all.
    std::optional<CatalogRecord> parse_tycho2_line(std::string_view line);

} // namespace wz::engine::starfield
