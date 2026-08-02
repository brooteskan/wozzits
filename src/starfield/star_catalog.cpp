// src/starfield/star_catalog.cpp

#include <starfield/star_catalog.h>

#include <math/vec3.h>

#include <algorithm>
#include <cmath>

namespace wz::engine::starfield
{
    using wz::math::Vec3;

    namespace
    {
        // Rec.709 / linear-sRGB luminance weights.
        constexpr float kLumR = 0.2126f;
        constexpr float kLumG = 0.7152f;
        constexpr float kLumB = 0.0722f;

        float luminance(const Vec3& c)
        {
            return kLumR * c.x + kLumG * c.y + kLumB * c.z;
        }

        // Rescale so the color's luminance is exactly 1 (leaves hue/chroma
        // untouched). A ~black tint is returned unchanged.
        Vec3 normalize_luminance(const Vec3& c)
        {
            const float lum = luminance(c);
            if (lum <= 1e-6f) {
                return c;
            }
            return c * (1.0f / lum);
        }

        // sRGB gamma -> linear for a single channel in [0, 1].
        float srgb_to_linear(float s)
        {
            if (s <= 0.04045f) {
                return s / 12.92f;
            }
            return std::pow((s + 0.055f) / 1.055f, 2.4f);
        }
    }

    Vec3 direction_from_ra_dec(double ra_hours, double dec_deg)
    {
        const double ra = ra_hours * kHoursToRadians;
        const double dec = dec_deg * kDegreesToRadians;
        const double cos_dec = std::cos(dec);

        // North celestial pole -> +Y (world up); vernal equinox -> +X; RA 6h ->
        // +Z. Already unit length (cos^2*(cos^2+sin^2) + sin^2 == 1).
        return Vec3{
            static_cast<float>(cos_dec * std::cos(ra)),
            static_cast<float>(std::sin(dec)),
            static_cast<float>(cos_dec * std::sin(ra)),
        };
    }

    double flux_from_magnitude(double vmag, const StarImportParams& params)
    {
        return params.exposure
            * std::pow(10.0, -0.4 * (vmag - params.reference_magnitude));
    }

    double remap_magnitude(double vmag, const StarImportParams& params)
    {
        return params.magnitude_pivot
            + (vmag - params.magnitude_pivot) * params.magnitude_contrast;
    }

    Vec3 warp_direction(const Vec3& dir, const StarImportParams& params)
    {
        if (params.warp_amplitude <= 0.0) {
            return dir;
        }
        // Divergence-free (curl-noise) warp on the sphere (issue #269). A plain
        // displacement folds -> stars pile into caustic streaks; instead advect
        // each direction along an INCOMPRESSIBLE tangent flow, which preserves
        // local density by construction (no caustics at any amplitude).
        //
        // On the unit sphere the divergence-free tangent fields are exactly the
        // skew-gradients of a scalar stream function phi:
        //     v = dir x grad(phi),   div_sphere(v) == 0 identically.
        // The `dir x` both projects grad(phi) into the tangent plane and rotates
        // it 90 degrees. phi is a few fixed incommensurate sine octaves of plane
        // waves (analytic gradient, deterministic, no RNG) -- the same spirit as
        // the old field but as a stream function, not a displacement. Frequency
        // lives only inside cos (spatial scale); amplitude is the final scale --
        // decoupled, so the existing dials keep their meaning.
        struct Wave { Vec3 k; float amp; };
        static const Wave waves[] = {          // amps ~sum to 1
            { normalize(Vec3{ 1.0f,  0.7f, -0.3f }), 0.45f },
            { normalize(Vec3{-0.4f,  1.0f,  0.8f }), 0.32f },
            { normalize(Vec3{ 0.9f, -0.5f,  1.0f }), 0.23f },
        };
        const float f = static_cast<float>(params.warp_frequency);

        // grad(phi) for phi = sum (amp/f) sin(f k.dir): a genuine scalar
        // gradient (so dir x grad stays exactly divergence-free), with f kept
        // out of the field magnitude to decouple amplitude from frequency.
        Vec3 grad{ 0.0f, 0.0f, 0.0f };
        for (const Wave& w : waves) {
            grad = grad + w.k * (w.amp * std::cos(f * dot(w.k, dir)));
        }

        const Vec3 v = cross(dir, grad);  // tangent, divergence-free on S^2
        return normalize(dir + v * static_cast<float>(params.warp_amplitude));
    }

    double temperature_from_bv(double bv)
    {
        // Ballesteros 2012 (arXiv:1201.1809): a blackbody two-band model.
        //   T = 4600 * ( 1/(0.92*bv + 1.7) + 1/(0.92*bv + 0.62) ) [Kelvin]
        // Valid across the main sequence; both denominators stay positive for
        // realistic B-V (roughly -0.4 .. +2.0).
        // Clamped to the validity range the comment above already states.
        // Both denominators have a POLE inside the domain a catalogue can
        // actually deliver -- 1/(0.92bv + 0.62) blows up at bv = -0.674, which
        // is only just past the bluest main-sequence stars and well inside
        // Tycho-2's noise. Unclamped, T crosses +/-inf and changes SIGN there,
        // and tint_from_temperature's own clamp is applied to the OUTPUT, so it
        // faithfully turned a negative temperature into the reddest tint
        // available: measured, bv = -0.67391 gave (0.64, 1.00, 2.05) and
        // bv = -0.67392 gave (3.94, 0.23, 0.00). The bluest stars in the sky
        // rendered as the reddest, which is the maximum possible inversion of
        // the intended colour (issue #316, C3-C17).
        const double a = 0.92 * std::clamp(bv, -0.4, 2.0);
        return 4600.0 * (1.0 / (a + 1.7) + 1.0 / (a + 0.62));
    }

    Vec3 tint_from_temperature(double kelvin)
    {
        // Tanner Helland's blackbody -> sRGB approximation (t = T/100), valid
        // ~1000..40000 K. Produces gamma-encoded 0..255 channels.
        const double t = std::clamp(kelvin, 1000.0, 40000.0) / 100.0;

        double r;
        if (t <= 66.0) {
            r = 255.0;
        } else {
            r = 329.698727446 * std::pow(t - 60.0, -0.1332047592);
        }

        double g;
        if (t <= 66.0) {
            g = 99.4708025861 * std::log(t) - 161.1195681661;
        } else {
            g = 288.1221695283 * std::pow(t - 60.0, -0.0755148492);
        }

        double b;
        if (t >= 66.0) {
            b = 255.0;
        } else if (t <= 19.0) {
            b = 0.0;
        } else {
            b = 138.5177312231 * std::log(t - 10.0) - 305.0447927307;
        }

        const auto clamp255 = [](double v) {
            return static_cast<float>(std::clamp(v, 0.0, 255.0) / 255.0);
        };

        const Vec3 linear{
            srgb_to_linear(clamp255(r)),
            srgb_to_linear(clamp255(g)),
            srgb_to_linear(clamp255(b)),
        };
        return normalize_luminance(linear);
    }

    Vec3 tint_from_bv(double bv, bool has_bv, double color_saturation)
    {
        const double temperature = temperature_from_bv(has_bv ? bv : 0.0);
        const Vec3 base = tint_from_temperature(temperature);

        // Push chroma about neutral white: s = 0 -> white, 1 -> catalog tint,
        // >1 -> exaggerated. Extrapolation about (1,1,1) keeps luminance ~1
        // before the final re-normalize soaks up any clamp drift.
        const float s = static_cast<float>(color_saturation);
        const Vec3 pushed{
            std::max(1.0f + s * (base.x - 1.0f), 0.0f),
            std::max(1.0f + s * (base.y - 1.0f), 0.0f),
            std::max(1.0f + s * (base.z - 1.0f), 0.0f),
        };
        return normalize_luminance(pushed);
    }

    Star star_from_direction(
        const Vec3& dir, double vmag, double bv, bool has_bv,
        const StarImportParams& params)
    {
        Star star;
        star.direction = warp_direction(normalize(dir), params);

        // Remap the magnitude, then derive both flux and the carried magnitude
        // (which drives sprite size) from the remapped value so the appearance
        // dials stay consistent.
        const double remapped = remap_magnitude(vmag, params);
        const float flux = static_cast<float>(flux_from_magnitude(remapped, params));
        const Vec3 tint = tint_from_bv(bv, has_bv, params.color_saturation);
        star.radiance = tint * flux;

        star.solid_angle = static_cast<float>(params.solid_angle);
        star.magnitude = static_cast<float>(remapped);
        return star;
    }

    Star star_from_record(const CatalogRecord& record, const StarImportParams& params)
    {
        return star_from_direction(
            direction_from_ra_dec(record.ra_hours, record.dec_deg),
            record.vmag, record.bv, record.has_bv, params);
    }

    std::vector<Star> build_catalog(
        std::span<const CatalogRecord> records,
        const StarImportParams& params)
    {
        std::vector<Star> stars;
        stars.reserve(records.size());
        for (const CatalogRecord& record : records) {
            // Reject direction. The accept spelling let a NaN magnitude through
            // BOTH comparisons and into the catalogue as a star with NaN
            // radiance -- the same defect the PLY importer's own comment
            // identifies as #310, and this was the last of the three sibling
            // culls still written the unsafe way (issue #316, C3-H17). The
            // position fields matter for the same reason: a NaN ra/dec becomes
            // a star with a NaN direction.
            if (!std::isfinite(record.vmag)
                || !std::isfinite(record.ra_hours)
                || !std::isfinite(record.dec_deg)) {
                continue;
            }
            if (!(record.vmag >= params.magnitude_min)
                || !(record.vmag <= params.magnitude_max)) {
                continue;
            }
            stars.push_back(star_from_record(record, params));
        }
        return stars;
    }

} // namespace wz::engine::starfield
