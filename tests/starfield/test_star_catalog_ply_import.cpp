// tests/starfield/test_star_catalog_ply_import.cpp
//
// The FIRST test of import_star_catalog_ply_bytes (issue #310, A4-C9). That
// function had no coverage of any kind -- none of its error branches, and no
// value-domain case -- which is why the defect below survived.
//
// The defect: the magnitude window was the only filter, and it ADMITS NaN,
// because `NaN < min` and `NaN > max` are both false. A NaN vmag then makes
// flux = pow(10, -0.4*NaN) = NaN, so radiance and magnitude reach the GPU as
// NaN. The star field draws with additive blending, where NaN + dst = NaN, so
// one bad row does not merely fail to draw a star -- it poisons whatever was
// already in those pixels, and anything a later neighbourhood filter spreads it
// into. A NaN in x/y/z survives normalize() for the same reason (`len == 0.0f`
// is false for NaN).
//
// The sibling importer over the SAME shared reader (gaussian splat) has always
// rejected non-finite values. This was a divergent guard, not a design choice.
//
// Note these fixtures are BINARY PLY on purpose: ASCII files were accidentally
// immune because tinyply rejects NaN while parsing text, so an ASCII version of
// this test would pass against the old code. Binary is what tycho2_prep writes.

#include <gtest/gtest.h>

#include <starfield/star_catalog_ply_importer.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    namespace sf = wz::engine::starfield;

    void append_f32(std::vector<std::uint8_t>& v, float f)
    {
        std::uint8_t b[4];
        std::memcpy(b, &f, 4);
        v.insert(v.end(), b, b + 4);
    }

    // Binary star PLY: x, y, z, vmag, bv per vertex.
    std::vector<std::uint8_t> make_star_ply(
        const std::vector<std::array<float, 5>>& rows)
    {
        const std::string header =
            "ply\nformat binary_little_endian 1.0\n"
            "element vertex " + std::to_string(rows.size()) + "\n"
            "property float x\nproperty float y\nproperty float z\n"
            "property float vmag\nproperty float bv\n"
            "end_header\n";

        std::vector<std::uint8_t> bytes(header.begin(), header.end());
        for (const auto& r : rows)
            for (const float f : r)
                append_f32(bytes, f);
        return bytes;
    }

    sf::StarImportParams wide_window()
    {
        sf::StarImportParams p;
        p.magnitude_min = -100.0;
        p.magnitude_max = 100.0;
        return p;
    }
}

TEST(StarCatalogPlyImport, ImportsFiniteStars)
{
    const auto bytes = make_star_ply({
        { 1.0f, 0.0f, 0.0f, 2.0f, 0.5f },
        { 0.0f, 1.0f, 0.0f, 3.0f, 0.2f },
    });

    const auto result = sf::import_star_catalog_ply_bytes(bytes, wide_window());
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.catalog.stars.size(), 2u);
    EXPECT_EQ(result.non_finite_rows_skipped, 0u);
}

TEST(StarCatalogPlyImport, NonFiniteRowsAreSkippedAndCounted)
{
    const float nan_f = std::numeric_limits<float>::quiet_NaN();
    const float inf_f = std::numeric_limits<float>::infinity();

    const auto bytes = make_star_ply({
        { 1.0f, 0.0f, 0.0f, 2.0f,  0.5f },   // good
        { 0.0f, 1.0f, 0.0f, nan_f, 0.5f },   // NaN magnitude -- passed the window
        { nan_f, 0.0f, 1.0f, 3.0f, 0.5f },   // NaN direction -- survived normalize
        { 0.0f, 0.0f, 1.0f, inf_f, 0.5f },   // infinite magnitude
        { 0.0f, 0.0f, 1.0f, 4.0f,  0.5f },   // good
    });

    const auto result = sf::import_star_catalog_ply_bytes(bytes, wide_window());
    ASSERT_TRUE(result.ok) << result.error;

    EXPECT_EQ(result.catalog.stars.size(), 2u);
    EXPECT_EQ(result.non_finite_rows_skipped, 3u);

    // The load-bearing assertion: nothing non-finite reaches the catalogue, and
    // therefore nothing non-finite reaches the additively-blended GPU buffer.
    for (const auto& star : result.catalog.stars) {
        EXPECT_TRUE(std::isfinite(star.direction.x));
        EXPECT_TRUE(std::isfinite(star.direction.y));
        EXPECT_TRUE(std::isfinite(star.direction.z));
        EXPECT_TRUE(std::isfinite(star.radiance.x));
        EXPECT_TRUE(std::isfinite(star.radiance.y));
        EXPECT_TRUE(std::isfinite(star.radiance.z));
        EXPECT_TRUE(std::isfinite(star.solid_angle));
        EXPECT_TRUE(std::isfinite(star.magnitude));
    }
}

// A catalogue that is ENTIRELY non-finite must fail rather than silently
// producing an empty-but-successful import.
TEST(StarCatalogPlyImport, AllNonFiniteRowsFailsTheImport)
{
    const float nan_f = std::numeric_limits<float>::quiet_NaN();

    const auto bytes = make_star_ply({
        { nan_f, nan_f, nan_f, nan_f, 0.0f },
        { nan_f, nan_f, nan_f, nan_f, 0.0f },
    });

    const auto result = sf::import_star_catalog_ply_bytes(bytes, wide_window());
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
    EXPECT_EQ(result.non_finite_rows_skipped, 2u);
}
