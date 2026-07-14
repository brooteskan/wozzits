#include <gtest/gtest.h>

#include <starfield/tycho2.h>

#include <string>
#include <string_view>

using namespace wz::engine::starfield;

namespace
{
    // Write a value into a fixed-width line at a 1-indexed byte position, the
    // way the CDS catalog.dat lays fields out. The parser trims, so left-aligned
    // placement within the field is fine as long as it stays in range.
    void place(std::string& line, std::size_t start1, std::string_view value)
    {
        line.replace(start1 - 1, value.size(), value);
    }

    std::string blank_line()
    {
        return std::string(180, ' ');
    }
}

TEST(Tycho2, MeanPositionWithBtAndVt)
{
    std::string line = blank_line();
    place(line, 16, "90.00000000");   // mRAdeg = 90 deg -> 6 h
    place(line, 29, "0.00000000");    // mDEdeg = 0
    place(line, 111, "1.000");        // BTmag
    place(line, 124, "0.800");        // VTmag

    const auto r = parse_tycho2_line(line);
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->ra_hours, 6.0, 1e-6);
    EXPECT_NEAR(r->dec_deg, 0.0, 1e-6);
    ASSERT_TRUE(r->has_bv);
    // V = VT - 0.090 (BT-VT) = 0.8 - 0.090*0.2 = 0.782
    EXPECT_NEAR(r->vmag, 0.782, 1e-4);
    // B-V = 0.850 (BT-VT) = 0.850*0.2 = 0.17
    EXPECT_NEAR(r->bv, 0.17, 1e-4);
}

TEST(Tycho2, NoMeanPositionFallsBackToObserved)
{
    std::string line = blank_line();
    place(line, 14, "X");             // pflag: no mean position
    place(line, 153, "45.00000000");  // observed RAdeg
    place(line, 166, "30.00000000");  // observed DEdeg
    place(line, 124, "5.000");        // VT only

    const auto r = parse_tycho2_line(line);
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->ra_hours, 3.0, 1e-6);   // 45 / 15
    EXPECT_NEAR(r->dec_deg, 30.0, 1e-6);
    EXPECT_FALSE(r->has_bv);
    EXPECT_NEAR(r->vmag, 5.0, 1e-6);
}

TEST(Tycho2, OnlyBtMagnitudeIsNeutral)
{
    std::string line = blank_line();
    place(line, 16, "180.00000000");
    place(line, 29, "-20.0000000");
    place(line, 111, "7.500");        // BT only, VT blank

    const auto r = parse_tycho2_line(line);
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->ra_hours, 12.0, 1e-6);
    EXPECT_NEAR(r->dec_deg, -20.0, 1e-6);
    EXPECT_NEAR(r->vmag, 7.5, 1e-6);
    EXPECT_FALSE(r->has_bv);
}

TEST(Tycho2, BlankOrShortLinesRejected)
{
    EXPECT_FALSE(parse_tycho2_line(blank_line()).has_value());   // no position
    EXPECT_FALSE(parse_tycho2_line("").has_value());
    EXPECT_FALSE(parse_tycho2_line("   \t  ").has_value());

    // A position but no magnitude at all is also dropped.
    std::string no_mag = blank_line();
    place(no_mag, 16, "10.00000000");
    place(no_mag, 29, "10.00000000");
    EXPECT_FALSE(parse_tycho2_line(no_mag).has_value());
}
