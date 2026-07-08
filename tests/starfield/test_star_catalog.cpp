#include <gtest/gtest.h>

#include <engine/starfield/star_catalog.h>

#include <math/vec3.h>

#include <array>
#include <cmath>
#include <vector>

using namespace wz::engine::starfield;
using wz::math::Vec3;

namespace
{
    float vlen(const Vec3& v)
    {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    float lum(const Vec3& c)
    {
        return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
    }

    // A handful of real bright stars (RA hours / Dec deg / apparent Vmag / B-V).
    // Public catalog facts; used to sanity-check the astronomy end to end.
    const CatalogRecord kSirius{ 6.7525, -16.7161, -1.46, 0.00, true };  // A1V, white
    const CatalogRecord kVega{ 18.6156, 38.7837, 0.03, 0.00, true };     // A0V, white
    const CatalogRecord kRigel{ 5.2423, -8.2016, 0.13, -0.03, true };    // B8Iab, blue
    const CatalogRecord kBetelgeuse{ 5.9195, 7.4071, 0.42, 1.85, true }; // M1-2Ia, red
    const CatalogRecord kPolaris{ 2.5303, 89.2641, 1.98, 0.60, true };   // F7Ib
}

// ---------------------------------------------------------------------------
// RA/Dec -> direction
// ---------------------------------------------------------------------------

TEST(StarCatalog, PoleMapsToWorldUp)
{
    const Vec3 north = direction_from_ra_dec(7.3, 90.0);   // RA irrelevant at pole
    EXPECT_NEAR(north.x, 0.0f, 1e-5f);
    EXPECT_NEAR(north.y, 1.0f, 1e-5f);
    EXPECT_NEAR(north.z, 0.0f, 1e-5f);

    const Vec3 south = direction_from_ra_dec(19.1, -90.0);
    EXPECT_NEAR(south.y, -1.0f, 1e-5f);
}

TEST(StarCatalog, EquinoxAxes)
{
    const Vec3 vernal = direction_from_ra_dec(0.0, 0.0);    // vernal equinox -> +X
    EXPECT_NEAR(vernal.x, 1.0f, 1e-5f);
    EXPECT_NEAR(vernal.y, 0.0f, 1e-5f);
    EXPECT_NEAR(vernal.z, 0.0f, 1e-5f);

    const Vec3 ra6 = direction_from_ra_dec(6.0, 0.0);       // RA 6h -> +Z
    EXPECT_NEAR(ra6.x, 0.0f, 1e-5f);
    EXPECT_NEAR(ra6.z, 1.0f, 1e-5f);

    const Vec3 ra12 = direction_from_ra_dec(12.0, 0.0);     // RA 12h -> -X
    EXPECT_NEAR(ra12.x, -1.0f, 1e-5f);
}

TEST(StarCatalog, DirectionsAreUnitLength)
{
    for (const CatalogRecord& r : { kSirius, kVega, kRigel, kBetelgeuse, kPolaris }) {
        const Vec3 d = direction_from_ra_dec(r.ra_hours, r.dec_deg);
        EXPECT_NEAR(vlen(d), 1.0f, 1e-5f);
    }
}

// ---------------------------------------------------------------------------
// Magnitude -> flux (Pogson)
// ---------------------------------------------------------------------------

TEST(StarCatalog, PogsonReferenceAndStep)
{
    StarImportParams p;   // reference_magnitude 0, exposure 1

    // m == reference -> unit flux.
    EXPECT_NEAR(flux_from_magnitude(0.0, p), 1.0, 1e-9);

    // Exactly 5 magnitudes fainter -> 1/100 the flux.
    EXPECT_NEAR(flux_from_magnitude(5.0, p), 0.01, 1e-9);

    // Brighter (negative) magnitude -> more flux; Sirius ~ 3.8x a mag-0 star.
    const double sirius = flux_from_magnitude(kSirius.vmag, p);
    EXPECT_GT(sirius, 1.0);
    EXPECT_NEAR(sirius, 3.83, 0.05);
}

TEST(StarCatalog, ExposureAndReferenceShift)
{
    StarImportParams base;
    StarImportParams hot = base;
    hot.exposure = 4.0;
    EXPECT_NEAR(flux_from_magnitude(2.0, hot),
                4.0 * flux_from_magnitude(2.0, base), 1e-9);

    StarImportParams shifted = base;
    shifted.reference_magnitude = 2.0;   // now m==2 maps to unit flux
    EXPECT_NEAR(flux_from_magnitude(2.0, shifted), 1.0, 1e-9);
}

// ---------------------------------------------------------------------------
// B-V -> temperature -> tint
// ---------------------------------------------------------------------------

TEST(StarCatalog, TemperatureMonotonicInBv)
{
    const double hot = temperature_from_bv(-0.3);   // blue
    const double a0 = temperature_from_bv(0.0);     // white
    const double sunlike = temperature_from_bv(0.65);
    const double cool = temperature_from_bv(1.85);  // red

    EXPECT_GT(hot, a0);
    EXPECT_GT(a0, sunlike);
    EXPECT_GT(sunlike, cool);

    // Ballesteros ballpark: A0 (B-V 0) ~ 10000 K; the Sun (B-V 0.65) ~ 5800 K.
    EXPECT_NEAR(a0, 10125.0, 200.0);
    EXPECT_NEAR(sunlike, 5800.0, 400.0);
}

TEST(StarCatalog, TintHotIsBluerCoolIsRedder)
{
    const Vec3 hot = tint_from_bv(-0.3, true, 1.0);   // blue star
    const Vec3 cool = tint_from_bv(1.85, true, 1.0);  // red star

    // Blue/red ratio is higher for the hot star, lower for the cool star.
    EXPECT_GT(hot.z / hot.x, 1.0f);
    EXPECT_LT(cool.z / cool.x, 1.0f);
    EXPECT_GT(hot.z / hot.x, cool.z / cool.x);
}

TEST(StarCatalog, TintLuminanceNormalized)
{
    for (double bv : { -0.3, 0.0, 0.3, 0.65, 1.0, 1.85 }) {
        for (double sat : { 0.5, 1.0, 2.0 }) {
            EXPECT_NEAR(lum(tint_from_bv(bv, true, sat)), 1.0f, 1e-4f);
        }
    }
}

TEST(StarCatalog, SaturationZeroIsWhiteAndPushExaggerates)
{
    const Vec3 white = tint_from_bv(-0.3, true, 0.0);
    EXPECT_NEAR(white.x, 1.0f, 1e-4f);
    EXPECT_NEAR(white.y, 1.0f, 1e-4f);
    EXPECT_NEAR(white.z, 1.0f, 1e-4f);

    // A hot star: more saturation -> more blue-shifted (higher blue/red ratio).
    const Vec3 s1 = tint_from_bv(-0.3, true, 1.0);
    const Vec3 s2 = tint_from_bv(-0.3, true, 2.0);
    EXPECT_GT(s2.z / s2.x, s1.z / s1.x);
}

TEST(StarCatalog, NoColorIndexIsNeutral)
{
    const Vec3 t = tint_from_bv(9.9, /*has_bv=*/false, 1.0);   // bv ignored
    const Vec3 a0 = tint_from_bv(0.0, true, 1.0);
    EXPECT_NEAR(t.x, a0.x, 1e-4f);
    EXPECT_NEAR(t.y, a0.y, 1e-4f);
    EXPECT_NEAR(t.z, a0.z, 1e-4f);
}

// ---------------------------------------------------------------------------
// Assembly -> the 32-byte point-source layout
// ---------------------------------------------------------------------------

TEST(StarCatalog, StarFromRecordFillsPointLayout)
{
    StarImportParams p;
    const Star s = star_from_record(kSirius, p);

    EXPECT_NEAR(vlen(s.direction), 1.0f, 1e-5f);          // unit direction
    EXPECT_FLOAT_EQ(s.magnitude, static_cast<float>(kSirius.vmag));
    EXPECT_GT(s.solid_angle, 0.0f);

    // radiance = tint * flux, and the tint is luminance-normalized, so the
    // radiance luminance is the flux.
    EXPECT_NEAR(lum(s.radiance),
                static_cast<float>(flux_from_magnitude(kSirius.vmag, p)), 1e-3f);
}

TEST(StarCatalog, BuildCatalogCullsByMagnitudeAndKeepsOrder)
{
    const std::array<CatalogRecord, 4> rows{
        CatalogRecord{ 1.0, 10.0, -1.0, 0.0, true },   // bright, keep
        CatalogRecord{ 2.0, 20.0, 3.0, 0.0, true },    // mid, keep
        CatalogRecord{ 3.0, 30.0, 8.0, 0.0, true },    // faint, cull
        CatalogRecord{ 4.0, 40.0, 4.5, 0.0, true },    // keep
    };

    StarImportParams p;
    p.magnitude_max = 5.0;   // drop anything fainter than mag 5

    const std::vector<Star> stars = build_catalog(rows, p);
    ASSERT_EQ(stars.size(), 3u);

    // Order preserved: magnitudes are -1, 3, 4.5 in sequence.
    EXPECT_FLOAT_EQ(stars[0].magnitude, -1.0f);
    EXPECT_FLOAT_EQ(stars[1].magnitude, 3.0f);
    EXPECT_FLOAT_EQ(stars[2].magnitude, 4.5f);
}

TEST(StarCatalog, RealStarsRankAndColor)
{
    const std::array<CatalogRecord, 5> rows{
        kSirius, kVega, kRigel, kBetelgeuse, kPolaris,
    };
    StarImportParams p;
    const std::vector<Star> stars = build_catalog(rows, p);
    ASSERT_EQ(stars.size(), 5u);

    // Sirius (Vmag -1.46) is the brightest -> greatest radiance luminance.
    float brightest = -1.0f;
    std::size_t brightest_i = 0;
    for (std::size_t i = 0; i < stars.size(); ++i) {
        const float l = lum(stars[i].radiance);
        if (l > brightest) { brightest = l; brightest_i = i; }
    }
    EXPECT_EQ(brightest_i, 0u);   // kSirius is row 0

    // Betelgeuse (red, index 3) is redder than Rigel (blue, index 2): higher
    // red/blue ratio in the tint. Radiance = tint * scalar flux, so the ratio
    // is the tint ratio.
    const Vec3& rigel = stars[2].radiance;
    const Vec3& betelgeuse = stars[3].radiance;
    EXPECT_GT(betelgeuse.x / betelgeuse.z, rigel.x / rigel.z);
}

// ---------------------------------------------------------------------------
// Creative dials: magnitude remap + warp field (Seam C-4)
// ---------------------------------------------------------------------------

TEST(StarCatalog, RemapDefaultIsNoOp)
{
    StarImportParams p;   // pivot 0, contrast 1
    for (double m : { -1.46, 0.0, 2.5, 6.0 }) {
        EXPECT_NEAR(remap_magnitude(m, p), m, 1e-9);
    }
}

TEST(StarCatalog, RemapContrastCompressesAboutPivot)
{
    StarImportParams p;
    p.magnitude_pivot = 3.0;
    p.magnitude_contrast = 0.5;

    // The pivot is a fixed point.
    EXPECT_NEAR(remap_magnitude(3.0, p), 3.0, 1e-9);
    // A faint star (mag 5) is pulled halfway to the pivot -> brighter (lower mag).
    EXPECT_NEAR(remap_magnitude(5.0, p), 4.0, 1e-9);
    // A bright star (mag 1) is pulled up toward the pivot -> dimmer.
    EXPECT_NEAR(remap_magnitude(1.0, p), 2.0, 1e-9);
}

TEST(StarCatalog, RemapMakesFaintStarsPopInFlux)
{
    // contrast < 1 compresses the range, so a faint star ends up with more flux.
    StarImportParams faithful;
    StarImportParams popped;
    popped.magnitude_pivot = 0.0;
    popped.magnitude_contrast = 0.5;

    const CatalogRecord faint{ 3.0, 10.0, 5.0, 0.0, true };
    const Star a = star_from_record(faint, faithful);
    const Star b = star_from_record(faint, popped);

    EXPECT_GT(lum(b.radiance), lum(a.radiance));
    // The carried magnitude (drives sprite size) reflects the remap.
    EXPECT_NEAR(b.magnitude, 2.5f, 1e-4f);
}

TEST(StarCatalog, WarpZeroIsIdentity)
{
    StarImportParams p;   // warp_amplitude 0
    for (const CatalogRecord& r : { kSirius, kVega, kPolaris }) {
        const Vec3 d = direction_from_ra_dec(r.ra_hours, r.dec_deg);
        const Vec3 w = warp_direction(d, p);
        EXPECT_NEAR(w.x, d.x, 1e-6f);
        EXPECT_NEAR(w.y, d.y, 1e-6f);
        EXPECT_NEAR(w.z, d.z, 1e-6f);
    }
}

TEST(StarCatalog, WarpDisplacesButStaysUnitAndDeterministic)
{
    StarImportParams p;
    p.warp_amplitude = 0.2;
    p.warp_frequency = 3.0;

    const Vec3 d = direction_from_ra_dec(kVega.ra_hours, kVega.dec_deg);
    const Vec3 w1 = warp_direction(d, p);
    const Vec3 w2 = warp_direction(d, p);

    // Unit length preserved.
    EXPECT_NEAR(vlen(w1), 1.0f, 1e-5f);
    // Actually moved.
    const float moved =
        std::abs(w1.x - d.x) + std::abs(w1.y - d.y) + std::abs(w1.z - d.z);
    EXPECT_GT(moved, 1e-3f);
    // Deterministic (no RNG): identical inputs -> identical output.
    EXPECT_FLOAT_EQ(w1.x, w2.x);
    EXPECT_FLOAT_EQ(w1.y, w2.y);
    EXPECT_FLOAT_EQ(w1.z, w2.z);
}
