#pragma once

// engine/starfield/star_catalog.h
//
// Star-catalog astronomy kernel -- the data-side front of the star field (Seam C
// of umbrella #259, issue #266). Stars are the far end of the frequency split:
// delta-like points that must NOT be fit as SG dome lobes (thousands of
// near-delta lobes would destroy the SG win). They belong in the point-source
// layer that source B (#262) introduced -- each star is exactly a
// SkyPointSource { direction, radiance, solid_angle }.
//
// This file is the pure, deterministic astronomy: it turns a catalog row
// (RA / Dec / apparent magnitude / B-V color index) into a render-ready Star. It
// is CATALOG-AGNOSTIC -- a BSC5 / HYG / Tycho-2 parser (a later seam, and the
// general column-table + filter-node infra behind it) fills CatalogRecord; the
// math here is identical regardless of source. It is also render-agnostic: no
// GPU, no asset registration, no instanced-quad path (later seams).
//
// "Based in reality yet completely authorable" (#259/#266): the catalog is the
// SEED. A small palette of grade dials (exposure, color saturation, a magnitude
// cull) rides here; the geometric dials (celestial->world rotation, warp field,
// twinkle, magnitude remap curve) belong to the importer node (Seam C-4) that
// wraps this kernel.
//
// Pure CPU, deterministic, no RNG, no GPU.

#include <math/math_types.h>

#include <span>
#include <string>
#include <vector>

namespace wz::engine::starfield
{
    inline constexpr double kPi = 3.14159265358979323846;
    inline constexpr double kHoursToRadians = kPi / 12.0;    // 15 deg / hour
    inline constexpr double kDegreesToRadians = kPi / 180.0;

    // One raw catalog row, before any engine interpretation. A parser (later
    // seam) fills these from BSC5 / HYG / Tycho-2; the astronomy is the same.
    struct CatalogRecord
    {
        double ra_hours = 0.0;    // right ascension, [0, 24) hours
        double dec_deg  = 0.0;    // declination, [-90, +90] degrees
        double vmag     = 0.0;    // apparent visual magnitude (smaller = brighter)
        double bv       = 0.0;    // B-V color index (~ -0.3 hot .. +2.0 cool)
        bool   has_bv   = true;   // false -> treated as neutral (A0, bv = 0)
    };

    // A render-ready star. direction / radiance / solid_angle are exactly the
    // SkyPointSource / DistantLight / ResidentSkyPoint (32-byte) fields, so a
    // built catalog drops straight into the point-source layer without a second
    // layout. magnitude is carried for the render seam (sprite size / LOD) and is
    // recoverable from radiance, so it need not survive to the 32-byte record.
    struct Star
    {
        wz::math::Vec3 direction{ 1.0f, 0.0f, 0.0f };  // unit, default celestial frame
        wz::math::Vec3 radiance{ 0.0f, 0.0f, 0.0f };   // linear RGB
        float          solid_angle = 0.0f;             // steradians (nominal)
        float          magnitude   = 0.0f;             // apparent visual magnitude
    };

    // Authorable grade dials for the catalog build (all faithful-import defaults,
    // so a correct import is just the starting point). Geometric dials live on
    // the importer node (Seam C-4).
    struct StarImportParams
    {
        // flux = exposure * 10^(-0.4 * (vmag - reference_magnitude)).
        double reference_magnitude = 0.0;    // magnitude that maps to unit flux
        double exposure            = 1.0;    // overall brightness scale

        // Chroma push about neutral: 0 -> white (ignore B-V), 1 -> catalog color,
        // >1 -> exaggerated temperature spread. Luminance-preserving (recolor
        // only; brightness stays governed by flux).
        double color_saturation    = 1.0;

        // Inclusive apparent-magnitude window; rows outside are dropped. This is
        // the coarse cull / perf lever until the general filter node exists. The
        // cull tests the ORIGINAL catalog magnitude, before the remap below.
        double magnitude_min       = -30.0;  // brighter bound (e.g. the Sun)
        double magnitude_max       =  30.0;  // fainter bound (cull faint stars)

        // Magnitude remap: reshape the apparent-magnitude scale about a pivot
        // before it becomes flux / sprite size. remapped = pivot + (vmag - pivot)
        // * contrast. contrast < 1 compresses the range so faint stars "pop"; > 1
        // exaggerates it. Purely an appearance dial (the cull uses the original).
        double magnitude_pivot     = 0.0;
        double magnitude_contrast  = 1.0;

        // Warp field: displace each star direction by a smooth, deterministic
        // field so the sky keeps a real feel but forms ORIGINAL constellations.
        // warp_amplitude is the peak displacement (0 = faithful positions),
        // warp_frequency the spatial scale of the swirl.
        double warp_amplitude      = 0.0;
        double warp_frequency      = 3.0;

        // Nominal angular size assigned to every star (steradians). Real stars
        // are sub-arcsecond; the render seam scales the sprite by magnitude, so
        // this is only a floor for the 32-byte record. ~ a 1-arcminute disc.
        double solid_angle         = 6.0e-8;
    };

    // RA/Dec -> unit direction in the DEFAULT celestial frame: north celestial
    // pole -> +Y (world up), vernal equinox (RA 0h, Dec 0) -> +X, RA 6h -> +Z.
    // The celestial->world rotation dial (importer node) reorients from here to
    // fake latitude / season / time with no ephemeris.
    wz::math::Vec3 direction_from_ra_dec(double ra_hours, double dec_deg);

    // Apparent magnitude -> linear flux via Pogson's ratio:
    //   flux = exposure * 10^(-0.4 * (vmag - reference_magnitude)).
    // A 5-magnitude step is exactly a factor of 100.
    double flux_from_magnitude(double vmag, const StarImportParams& params);

    // Reshape apparent magnitude about the pivot (the magnitude-remap dial):
    //   remapped = magnitude_pivot + (vmag - magnitude_pivot) * magnitude_contrast.
    double remap_magnitude(double vmag, const StarImportParams& params);

    // Displace a unit direction by the smooth deterministic warp field and
    // renormalize. warp_amplitude == 0 returns the direction unchanged.
    wz::math::Vec3 warp_direction(
        const wz::math::Vec3& dir, const StarImportParams& params);

    // Ballesteros (2012): B-V color index -> effective temperature in Kelvin.
    // Monotonically decreasing in B-V (bluer / more negative -> hotter).
    double temperature_from_bv(double bv);

    // Blackbody temperature (Kelvin) -> linear-RGB tint, luminance-normalized to
    // ~1 so it only recolors. Uses the widely-used Helland blackbody->sRGB
    // approximation, then converts to linear and normalizes by luminance.
    wz::math::Vec3 tint_from_temperature(double kelvin);

    // B-V color index -> linear-RGB tint (via temperature), luminance-normalized,
    // with the saturation dial pushing chroma about neutral. has_bv == false ->
    // neutral (A0, bv = 0).
    wz::math::Vec3 tint_from_bv(double bv, bool has_bv, double color_saturation);

    // Assemble one render-ready star from a catalog row + grade dials.
    Star star_from_record(const CatalogRecord& record, const StarImportParams& params);

    // Map a whole catalog, dropping rows outside the magnitude window. The order
    // of the survivors is preserved (deterministic).
    std::vector<Star> build_catalog(
        std::span<const CatalogRecord> records,
        const StarImportParams& params);

    // A built star field: the render-ready stars plus provenance. This is the set
    // the StarCatalogTable stores and the resident point buffer is published
    // from (Seam C-2). Pure -- no asset-system types (the table wraps it).
    struct StarCatalog
    {
        std::string       source_name;
        std::vector<Star> stars;

        bool valid() const noexcept { return !stars.empty(); }
    };

} // namespace wz::engine::starfield
