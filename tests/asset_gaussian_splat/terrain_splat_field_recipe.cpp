// tests/asset_gaussian_splat/terrain_splat_field_recipe.cpp
//
// Tests for multi-field terrain splat recipe compilation.
// Uses tiny synthetic fields (3x3) to verify:
//   - validation: missing height field, missing color field refs
//   - SlopeHeightDebug fallback produces expected slope color
//   - RgbFields maps three fields into independent R, G, B
//   - GrayscaleField maps one field into R = G = B
//   - FieldMap normalize / scale / bias pipeline

#include <engine/assets/gaussian_splat/gaussian_splat_compilers.h>
#include <engine/assets/gaussian_splat/gaussian_splat_terrain_recipe.h>
#include <engine/assets/gaussian_splat/gaussian_splat_terrain_preset.h>
#include <engine/assets/scalar_field/scalar_field_set.h>

#include <gtest/gtest.h>

#include <cmath>

using namespace wz::engine::assets;

namespace
{
    constexpr float kSH_C0 = 0.28209479177387814f;

    // Decode SH DC back to display value.
    float sh_dc_to_display(float sh_dc)
    {
        return sh_dc * kSH_C0 + 0.5f;
    }

    // Build a flat 3x3 field with uniform value.
    ScalarFieldData make_flat_field(float value)
    {
        ScalarFieldData f;
        f.width  = 3;
        f.height = 3;
        f.depth  = 1;
        f.values.assign(9, value);
        f.min_value = value;
        f.max_value = value;
        return f;
    }

    // Build a 3x3 field with a gradient from lo to hi along x.
    ScalarFieldData make_gradient_field(float lo, float hi)
    {
        ScalarFieldData f;
        f.width  = 3;
        f.height = 3;
        f.depth  = 1;
        f.values.resize(9);
        f.min_value = lo;
        f.max_value = hi;
        for (uint32_t iz = 0; iz < 3; ++iz)
            for (uint32_t ix = 0; ix < 3; ++ix)
            {
                float t = static_cast<float>(ix) / 2.0f;  // 0, 0.5, 1.0
                f.values[ix + iz * 3] = lo + t * (hi - lo);
            }
        return f;
    }

    // Create a baseline recipe with unit-grid geometry.
    TerrainSplatFieldRecipe make_base_recipe()
    {
        TerrainSplatFieldRecipe recipe;
        recipe.height_field_name = "height";
        recipe.geometry.height_scale = 1.0f;
        recipe.geometry.step_x       = 1.0f;
        recipe.geometry.step_z       = 1.0f;
        recipe.geometry.overlap_factor = 1.5f;
        recipe.geometry.opacity      = 0.95f;
        recipe.geometry.subsample_step = 1;
        recipe.geometry.normal_smoothing_enabled = false;
        recipe.color_mode = TerrainSplatColorMode::SlopeHeightDebug;
        return recipe;
    }
}


// ═══════════════════════════════════════════════════════════════════
// Validation
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainSplatFieldRecipe, EmptyFieldSet_Fails)
{
    ScalarFieldSet fields;
    auto recipe = make_base_recipe();

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_FALSE(val.ok);
    EXPECT_TRUE(cloud.empty());
    EXPECT_FALSE(val.error.empty());
}

TEST(TerrainSplatFieldRecipe, MissingHeightField_Fails)
{
    ScalarFieldSet fields;
    fields.add("rockiness", make_flat_field(0.5f));

    auto recipe = make_base_recipe();
    recipe.height_field_name = "height";  // not in field set

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_FALSE(val.ok);
    EXPECT_TRUE(cloud.empty());
    EXPECT_NE(val.error.find("height"), std::string::npos);
}

TEST(TerrainSplatFieldRecipe, RgbFields_MissingRedField_Fails)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));
    fields.add("soil",   make_flat_field(0.5f));

    auto recipe = make_base_recipe();
    recipe.color_mode = TerrainSplatColorMode::RgbFields;
    recipe.color_r = FieldMap{ "missing_field", true, 1.0f, 0.0f };
    recipe.color_g = FieldMap{ "soil",          true, 1.0f, 0.0f };
    recipe.color_b = FieldMap{ "height",        true, 1.0f, 0.0f };

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_FALSE(val.ok);
    EXPECT_TRUE(cloud.empty());
    EXPECT_NE(val.error.find("missing_field"), std::string::npos);
}

TEST(TerrainSplatFieldRecipe, GrayscaleField_MissingField_Fails)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    auto recipe = make_base_recipe();
    recipe.color_mode  = TerrainSplatColorMode::GrayscaleField;
    recipe.color_gray  = FieldMap{ "nonexistent", true, 1.0f, 0.0f };

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_FALSE(val.ok);
    EXPECT_TRUE(cloud.empty());
}

TEST(TerrainSplatFieldRecipe, RgbFields_EmptyFieldName_Fails)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    auto recipe = make_base_recipe();
    recipe.color_mode = TerrainSplatColorMode::RgbFields;
    recipe.color_r = FieldMap{ "",       true, 1.0f, 0.0f };
    recipe.color_g = FieldMap{ "height", true, 1.0f, 0.0f };
    recipe.color_b = FieldMap{ "height", true, 1.0f, 0.0f };

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_FALSE(val.ok);
}


// ═══════════════════════════════════════════════════════════════════
// SlopeHeightDebug — existing fallback
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainSplatFieldRecipe, SlopeHeightDebug_FlatField_ProducesExpectedColor)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    auto recipe = make_base_recipe();
    recipe.color_mode = TerrainSplatColorMode::SlopeHeightDebug;
    recipe.geometry.flat_luminance  = 0.55f;
    recipe.geometry.steep_luminance = 0.30f;

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_TRUE(val.ok);
    EXPECT_FALSE(cloud.empty());

    // Flat field → slope_factor=0 → display = flat_luminance = 0.55
    for (const auto& splat : cloud.splats)
    {
        float display_r = sh_dc_to_display(splat.color_dc[0]);
        EXPECT_NEAR(display_r, 0.55f, 0.01f);
        // R = G = B for slope/height debug
        EXPECT_FLOAT_EQ(splat.color_dc[0], splat.color_dc[1]);
        EXPECT_FLOAT_EQ(splat.color_dc[1], splat.color_dc[2]);
    }
}


// ═══════════════════════════════════════════════════════════════════
// RgbFields — three fields mapped to R, G, B
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainSplatFieldRecipe, RgbFields_UniformFields_MapsCorrectly)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    // Uniform fields with known values and distinct min/max for normalization.
    ScalarFieldData rock;
    rock.width = 3; rock.height = 3; rock.depth = 1;
    rock.values.assign(9, 0.8f);
    rock.min_value = 0.0f;
    rock.max_value = 1.0f;
    fields.add("rock", rock);

    ScalarFieldData soil;
    soil.width = 3; soil.height = 3; soil.depth = 1;
    soil.values.assign(9, 0.4f);
    soil.min_value = 0.0f;
    soil.max_value = 1.0f;
    fields.add("soil", soil);

    auto recipe = make_base_recipe();
    recipe.color_mode = TerrainSplatColorMode::RgbFields;
    recipe.color_r = FieldMap{ "rock",   true, 1.0f, 0.0f };
    recipe.color_g = FieldMap{ "soil",   true, 1.0f, 0.0f };
    recipe.color_b = FieldMap{ "height", true, 1.0f, 0.0f };

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_TRUE(val.ok);
    EXPECT_EQ(cloud.splats.size(), 9u);  // 3x3

    for (const auto& splat : cloud.splats)
    {
        // rock: value=0.8, normalized over [0,1] → 0.8
        float display_r = sh_dc_to_display(splat.color_dc[0]);
        EXPECT_NEAR(display_r, 0.8f, 0.01f);

        // soil: value=0.4, normalized over [0,1] → 0.4
        float display_g = sh_dc_to_display(splat.color_dc[1]);
        EXPECT_NEAR(display_g, 0.4f, 0.01f);

        // height: all 0.0, min=max=0 → normalized to 0.0
        float display_b = sh_dc_to_display(splat.color_dc[2]);
        EXPECT_NEAR(display_b, 0.0f, 0.01f);
    }
}

TEST(TerrainSplatFieldRecipe, RgbFields_GradientField_VariesAcrossSplats)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));
    fields.add("grad",   make_gradient_field(0.0f, 1.0f));

    auto recipe = make_base_recipe();
    recipe.color_mode = TerrainSplatColorMode::RgbFields;
    recipe.color_r = FieldMap{ "grad",   true, 1.0f, 0.0f };
    recipe.color_g = FieldMap{ "height", true, 1.0f, 0.0f };
    recipe.color_b = FieldMap{ "height", true, 1.0f, 0.0f };

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_TRUE(val.ok);
    EXPECT_EQ(cloud.splats.size(), 9u);

    // Gradient goes 0.0, 0.5, 1.0 along x, repeating each row.
    // Splats are emitted iz outer, ix inner, so:
    //   splat 0,3,6: ix=0 → display_r ≈ 0.0
    //   splat 1,4,7: ix=1 → display_r ≈ 0.5
    //   splat 2,5,8: ix=2 → display_r ≈ 1.0
    for (size_t i = 0; i < 9; ++i)
    {
        uint32_t ix = static_cast<uint32_t>(i % 3);
        float expected = static_cast<float>(ix) / 2.0f;
        float display_r = sh_dc_to_display(cloud.splats[i].color_dc[0]);
        EXPECT_NEAR(display_r, expected, 0.02f)
            << "splat " << i << " (ix=" << ix << ")";
    }
}

TEST(TerrainSplatFieldRecipe, RgbFields_SameFieldForAllChannels)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));
    fields.add("data",   make_gradient_field(0.0f, 1.0f));

    auto recipe = make_base_recipe();
    recipe.color_mode = TerrainSplatColorMode::RgbFields;
    recipe.color_r = FieldMap{ "data", true, 1.0f, 0.0f };
    recipe.color_g = FieldMap{ "data", true, 1.0f, 0.0f };
    recipe.color_b = FieldMap{ "data", true, 1.0f, 0.0f };

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_TRUE(val.ok);
    for (const auto& splat : cloud.splats)
    {
        EXPECT_FLOAT_EQ(splat.color_dc[0], splat.color_dc[1]);
        EXPECT_FLOAT_EQ(splat.color_dc[1], splat.color_dc[2]);
    }
}


// ═══════════════════════════════════════════════════════════════════
// GrayscaleField — one field replicated to R = G = B
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainSplatFieldRecipe, GrayscaleField_UniformValue)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    ScalarFieldData gray;
    gray.width = 3; gray.height = 3; gray.depth = 1;
    gray.values.assign(9, 0.6f);
    gray.min_value = 0.0f;
    gray.max_value = 1.0f;
    fields.add("gray_data", gray);

    auto recipe = make_base_recipe();
    recipe.color_mode = TerrainSplatColorMode::GrayscaleField;
    recipe.color_gray = FieldMap{ "gray_data", true, 1.0f, 0.0f };

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_TRUE(val.ok);
    for (const auto& splat : cloud.splats)
    {
        float display = sh_dc_to_display(splat.color_dc[0]);
        EXPECT_NEAR(display, 0.6f, 0.01f);
        EXPECT_FLOAT_EQ(splat.color_dc[0], splat.color_dc[1]);
        EXPECT_FLOAT_EQ(splat.color_dc[1], splat.color_dc[2]);
    }
}

TEST(TerrainSplatFieldRecipe, GrayscaleField_GradientVaries)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));
    fields.add("ramp",   make_gradient_field(0.0f, 1.0f));

    auto recipe = make_base_recipe();
    recipe.color_mode = TerrainSplatColorMode::GrayscaleField;
    recipe.color_gray = FieldMap{ "ramp", true, 1.0f, 0.0f };

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_TRUE(val.ok);
    EXPECT_EQ(cloud.splats.size(), 9u);

    // ix=0 → 0.0, ix=1 → 0.5, ix=2 → 1.0
    for (size_t i = 0; i < 9; ++i)
    {
        uint32_t ix = static_cast<uint32_t>(i % 3);
        float expected = static_cast<float>(ix) / 2.0f;
        float display = sh_dc_to_display(cloud.splats[i].color_dc[0]);
        EXPECT_NEAR(display, expected, 0.02f);
        // All channels identical for grayscale.
        EXPECT_FLOAT_EQ(cloud.splats[i].color_dc[0],
                         cloud.splats[i].color_dc[1]);
        EXPECT_FLOAT_EQ(cloud.splats[i].color_dc[1],
                         cloud.splats[i].color_dc[2]);
    }
}


// ═══════════════════════════════════════════════════════════════════
// FieldMap: normalize / scale / bias
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainSplatFieldRecipe, FieldMap_ScaleAndBias)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    ScalarFieldData data;
    data.width = 3; data.height = 3; data.depth = 1;
    data.values.assign(9, 0.5f);
    data.min_value = 0.0f;
    data.max_value = 1.0f;
    fields.add("data", data);

    auto recipe = make_base_recipe();
    recipe.color_mode = TerrainSplatColorMode::GrayscaleField;
    // normalize(0.5) = 0.5, * 0.5 + 0.2 = 0.45
    recipe.color_gray = FieldMap{ "data", true, 0.5f, 0.2f };

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_TRUE(val.ok);
    for (const auto& splat : cloud.splats)
    {
        float display = sh_dc_to_display(splat.color_dc[0]);
        EXPECT_NEAR(display, 0.45f, 0.01f);
    }
}

TEST(TerrainSplatFieldRecipe, FieldMap_NoNormalize)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    ScalarFieldData data;
    data.width = 3; data.height = 3; data.depth = 1;
    data.values.assign(9, 0.3f);
    data.min_value = 0.0f;
    data.max_value = 1.0f;
    fields.add("data", data);

    auto recipe = make_base_recipe();
    recipe.color_mode = TerrainSplatColorMode::GrayscaleField;
    // normalize=false: raw 0.3 * 1.0 + 0.0 = 0.3
    recipe.color_gray = FieldMap{ "data", false, 1.0f, 0.0f };

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_TRUE(val.ok);
    for (const auto& splat : cloud.splats)
    {
        float display = sh_dc_to_display(splat.color_dc[0]);
        EXPECT_NEAR(display, 0.3f, 0.01f);
    }
}

TEST(TerrainSplatFieldRecipe, FieldMap_ClampAboveOne)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    ScalarFieldData data;
    data.width = 3; data.height = 3; data.depth = 1;
    data.values.assign(9, 0.8f);
    data.min_value = 0.0f;
    data.max_value = 1.0f;
    fields.add("data", data);

    auto recipe = make_base_recipe();
    recipe.color_mode = TerrainSplatColorMode::GrayscaleField;
    // normalize(0.8) = 0.8, * 2.0 + 0.0 = 1.6 → clamped to 1.0
    recipe.color_gray = FieldMap{ "data", true, 2.0f, 0.0f };

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_TRUE(val.ok);
    for (const auto& splat : cloud.splats)
    {
        float display = sh_dc_to_display(splat.color_dc[0]);
        EXPECT_NEAR(display, 1.0f, 0.01f);
    }
}

TEST(TerrainSplatFieldRecipe, FieldMap_ClampBelowZero)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    ScalarFieldData data;
    data.width = 3; data.height = 3; data.depth = 1;
    data.values.assign(9, 0.2f);
    data.min_value = 0.0f;
    data.max_value = 1.0f;
    fields.add("data", data);

    auto recipe = make_base_recipe();
    recipe.color_mode = TerrainSplatColorMode::GrayscaleField;
    // normalize(0.2) = 0.2, * 1.0 + (-0.5) = -0.3 → clamped to 0.0
    recipe.color_gray = FieldMap{ "data", true, 1.0f, -0.5f };

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_TRUE(val.ok);
    for (const auto& splat : cloud.splats)
    {
        float display = sh_dc_to_display(splat.color_dc[0]);
        EXPECT_NEAR(display, 0.0f, 0.01f);
    }
}


// ═══════════════════════════════════════════════════════════════════
// Geometry: cloud has expected splat count and valid bounds
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainSplatFieldRecipe, CloudHasCorrectSplatCount)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    auto recipe = make_base_recipe();
    recipe.color_mode = TerrainSplatColorMode::SlopeHeightDebug;

    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);
    EXPECT_EQ(cloud.splats.size(), 9u);  // 3x3
    EXPECT_TRUE(cloud.bounds.valid);
}

TEST(TerrainSplatFieldRecipe, SubsampleStep2_ReducesSplatCount)
{
    // 3x3 with step 2: emit cells at ix={0,2}, iz={0,2} → 4 splats
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    auto recipe = make_base_recipe();
    recipe.geometry.subsample_step = 2;

    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);
    EXPECT_EQ(cloud.splats.size(), 4u);
}

TEST(TerrainSplatFieldRecipe, NullValidationPointer_DoesNotCrash)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    auto recipe = make_base_recipe();
    // out_validation = nullptr (default)
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);
    EXPECT_FALSE(cloud.empty());
}
