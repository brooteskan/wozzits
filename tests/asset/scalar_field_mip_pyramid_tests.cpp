// tests/asset/scalar_field_mip_pyramid_tests.cpp
//
// Unit coverage for the CPU box-filter height mip pyramid (#210) built by
// build_scalar_field_mip_pyramid in scalar_field_compilers.cpp. These are pure
// CPU tests -- no device -- so they run everywhere and pin the box-filter math,
// the chain length, and the non-power-of-two / odd / 1xN edge handling that the
// clipmap coarse-ring anti-aliasing depends on.

#include <gtest/gtest.h>

#include <engine/assets/scalar_field/scalar_field_compilers.h>

#include <cmath>
#include <vector>

namespace wz::engine::assets::internal::test {

    namespace {
        // floor(log2(max(w,h)))+1 -- the count the resident texture is created
        // with (#209) and the VS clamps its sampled mip to.
        uint32_t full_mip_count(uint32_t w, uint32_t h)
        {
            uint32_t largest = w > h ? w : h;
            uint32_t levels = 1u;
            while (largest > 1u) { largest >>= 1u; ++levels; }
            return levels;
        }
    }

    // A square power-of-two field builds a full chain down to 1x1, halving each
    // dimension per level, with each texel the 2x2 average of the level above.
    TEST(ScalarFieldMipPyramid, SquarePowerOfTwoHalvesAndAverages)
    {
        // 4x4 ramp: value == linear index, so a 2x2 average is easy to predict.
        const uint32_t w = 4, h = 4;
        std::vector<float> mip0(static_cast<size_t>(w) * h);
        for (uint32_t i = 0; i < mip0.size(); ++i) {
            mip0[i] = static_cast<float>(i);
        }

        const auto pyramid = build_scalar_field_mip_pyramid(mip0, w, h);

        ASSERT_EQ(pyramid.size(), full_mip_count(w, h));  // 4x4 -> 3 levels
        ASSERT_EQ(pyramid.size(), 3u);

        EXPECT_EQ(pyramid[0].width, 4u);
        EXPECT_EQ(pyramid[0].height, 4u);
        EXPECT_EQ(pyramid[0].values, mip0);  // level 0 is the field itself

        EXPECT_EQ(pyramid[1].width, 2u);
        EXPECT_EQ(pyramid[1].height, 2u);
        // mip1[0,0] = avg(0,1,4,5) = 2.5; [1,0] = avg(2,3,6,7) = 4.5;
        // [0,1] = avg(8,9,12,13) = 10.5; [1,1] = avg(10,11,14,15) = 12.5.
        EXPECT_FLOAT_EQ(pyramid[1].values[0], 2.5f);
        EXPECT_FLOAT_EQ(pyramid[1].values[1], 4.5f);
        EXPECT_FLOAT_EQ(pyramid[1].values[2], 10.5f);
        EXPECT_FLOAT_EQ(pyramid[1].values[3], 12.5f);

        EXPECT_EQ(pyramid[2].width, 1u);
        EXPECT_EQ(pyramid[2].height, 1u);
        // mip2 = avg of the four mip1 texels = avg(2.5,4.5,10.5,12.5) = 7.5,
        // which is also the mean of 0..15 (a box pyramid preserves the mean on
        // power-of-two dims).
        EXPECT_FLOAT_EQ(pyramid[2].values[0], 7.5f);
    }

    // A constant field stays constant at every level (the box average of equal
    // values is that value) -- guards against edge-tap weighting bugs.
    TEST(ScalarFieldMipPyramid, ConstantFieldIsPreservedAtEveryLevel)
    {
        const uint32_t w = 8, h = 8;
        std::vector<float> mip0(static_cast<size_t>(w) * h, 3.25f);

        const auto pyramid = build_scalar_field_mip_pyramid(mip0, w, h);
        ASSERT_EQ(pyramid.size(), full_mip_count(w, h));  // 4 levels
        for (const auto& level : pyramid) {
            for (float v : level.values) {
                EXPECT_FLOAT_EQ(v, 3.25f);
            }
        }
    }

    // Non-square dims: each dimension halves independently (floor), and the chain
    // continues until BOTH reach 1 (so 8x2 -> 4x1 -> 2x1 -> 1x1).
    TEST(ScalarFieldMipPyramid, NonSquareHalvesEachDimensionIndependently)
    {
        const uint32_t w = 8, h = 2;
        std::vector<float> mip0(static_cast<size_t>(w) * h, 1.0f);

        const auto pyramid = build_scalar_field_mip_pyramid(mip0, w, h);
        ASSERT_EQ(pyramid.size(), full_mip_count(w, h));  // max(8,2)=8 -> 4 levels

        EXPECT_EQ(pyramid[0].width, 8u); EXPECT_EQ(pyramid[0].height, 2u);
        EXPECT_EQ(pyramid[1].width, 4u); EXPECT_EQ(pyramid[1].height, 1u);
        EXPECT_EQ(pyramid[2].width, 2u); EXPECT_EQ(pyramid[2].height, 1u);
        EXPECT_EQ(pyramid[3].width, 1u); EXPECT_EQ(pyramid[3].height, 1u);
    }

    // Odd dimension: the last destination texel maps to a single leftover source
    // column/row (its 2x2 block clamps to the covered texels). A 3x1 row
    // [10, 20, 30] reduces to [avg(10,20), 30] = [15, 30], NOT reading past the
    // end. The chain then continues 2x1 -> 1x1.
    TEST(ScalarFieldMipPyramid, OddWidthClampsTrailingBlockToCoveredTexels)
    {
        const uint32_t w = 3, h = 1;
        std::vector<float> mip0 = { 10.0f, 20.0f, 30.0f };

        const auto pyramid = build_scalar_field_mip_pyramid(mip0, w, h);
        ASSERT_EQ(pyramid.size(), full_mip_count(w, h));  // max(3,1)=3 -> 2 levels

        // dst_width = 3 >> 1 = 1, so the whole odd row collapses to ONE texel:
        // its single 2x2 block covers cols 0,1 (col 2 is beyond 2*dst_width-1),
        // i.e. avg(10,20) = 15. The leftover col 2 is dropped exactly as the
        // standard round-down mip reduction drops it.
        ASSERT_EQ(pyramid[1].width, 1u);
        ASSERT_EQ(pyramid[1].height, 1u);
        ASSERT_EQ(pyramid[1].values.size(), 1u);
        EXPECT_FLOAT_EQ(pyramid[1].values[0], 15.0f);
    }

    // A 3x3 odd field: dst is 1x1 (3>>1 = 1), whose block covers the top-left 2x2
    // quadrant only -> avg of those four; the trailing col/row are dropped.
    TEST(ScalarFieldMipPyramid, OddSquareReducesToTopLeftQuad)
    {
        const uint32_t w = 3, h = 3;
        std::vector<float> mip0 = {
            1.0f, 2.0f, 99.0f,
            3.0f, 4.0f, 99.0f,
            99.0f, 99.0f, 99.0f,
        };
        const auto pyramid = build_scalar_field_mip_pyramid(mip0, w, h);
        ASSERT_EQ(pyramid.size(), full_mip_count(w, h));  // 2 levels
        ASSERT_EQ(pyramid[1].width, 1u);
        ASSERT_EQ(pyramid[1].height, 1u);
        // avg(1,2,3,4) = 2.5; the 99s (trailing col/row) are never read.
        EXPECT_FLOAT_EQ(pyramid[1].values[0], 2.5f);
    }

    // 1xN degenerate field (a single row) reduces correctly down to 1x1.
    TEST(ScalarFieldMipPyramid, SingleRowReducesAlongWidthOnly)
    {
        const uint32_t w = 4, h = 1;
        std::vector<float> mip0 = { 2.0f, 4.0f, 6.0f, 8.0f };

        const auto pyramid = build_scalar_field_mip_pyramid(mip0, w, h);
        ASSERT_EQ(pyramid.size(), full_mip_count(w, h));  // 3 levels

        ASSERT_EQ(pyramid[1].width, 2u); ASSERT_EQ(pyramid[1].height, 1u);
        EXPECT_FLOAT_EQ(pyramid[1].values[0], 3.0f);  // avg(2,4)
        EXPECT_FLOAT_EQ(pyramid[1].values[1], 7.0f);  // avg(6,8)

        ASSERT_EQ(pyramid[2].width, 1u); ASSERT_EQ(pyramid[2].height, 1u);
        EXPECT_FLOAT_EQ(pyramid[2].values[0], 5.0f);  // avg(3,7)
    }

    // A 1x1 field is already fully reduced: the pyramid is a single level.
    TEST(ScalarFieldMipPyramid, SingleTexelIsOneLevel)
    {
        std::vector<float> mip0 = { 42.0f };
        const auto pyramid = build_scalar_field_mip_pyramid(mip0, 1, 1);
        ASSERT_EQ(pyramid.size(), 1u);
        EXPECT_EQ(pyramid[0].width, 1u);
        EXPECT_EQ(pyramid[0].height, 1u);
        EXPECT_FLOAT_EQ(pyramid[0].values[0], 42.0f);
    }

    // Malformed input (size mismatch or zero dim) yields an empty pyramid rather
    // than reading out of bounds -- a checkable failure the caller can detect.
    TEST(ScalarFieldMipPyramid, RejectsMalformedInput)
    {
        EXPECT_TRUE(
            build_scalar_field_mip_pyramid({ 1.0f, 2.0f }, 4, 4).empty());
        EXPECT_TRUE(
            build_scalar_field_mip_pyramid({}, 0, 4).empty());
        EXPECT_TRUE(
            build_scalar_field_mip_pyramid({ 1.0f }, 1, 0).empty());
    }

} // namespace wz::engine::assets::internal::test
