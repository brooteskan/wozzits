// tests/asset_gaussian_splat/terrain_splat_geometry_fields.cpp
//
// Tests for geometry-affecting fields in the terrain splat recipe:
//   - Imported normal field replaces/blends with finite-difference normal
//   - Normal decoding: zero_to_one, minus_one_to_one, up_axis, flip
//   - Curvature field modifies radius/opacity
//   - Peaks field modifies color/opacity/radius
//   - Defaults preserve existing output when no optional fields supplied
//   - Normal debug views produce expected color output

#include <engine/assets/gaussian_splat/gaussian_splat_compilers.h>
#include <engine/assets/gaussian_splat/gaussian_splat_terrain_recipe.h>
#include <engine/assets/gaussian_splat/gaussian_splat_terrain_math.h>
#include <engine/assets/scalar_field/scalar_field_set.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using namespace wz::engine::assets;
using namespace wz::engine::assets::terrain_math;

namespace
{
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

    // Build a 3x3 field with known value and known min/max range.
    ScalarFieldData make_ranged_field(float value, float mn, float mx)
    {
        ScalarFieldData f;
        f.width  = 3;
        f.height = 3;
        f.depth  = 1;
        f.values.assign(9, value);
        f.min_value = mn;
        f.max_value = mx;
        return f;
    }

    TerrainSplatFieldRecipe make_base_recipe()
    {
        TerrainSplatFieldRecipe recipe;
        recipe.height_field_name = "height";
        recipe.geometry.height_scale    = 1.0f;
        recipe.geometry.step_x          = 1.0f;
        recipe.geometry.step_z          = 1.0f;
        recipe.geometry.overlap_factor  = 1.5f;
        recipe.geometry.opacity         = 0.95f;
        recipe.geometry.subsample_step  = 1;
        recipe.geometry.normal_smoothing_enabled = false;
        recipe.color_mode = TerrainSplatColorMode::SlopeHeightDebug;
        return recipe;
    }

    // Helper to extract the rotation quaternion from a splat.
    Quat get_rotation(const GaussianSplat& s)
    {
        return { s.rotation[0], s.rotation[1], s.rotation[2], s.rotation[3] };
    }

    // Rotate Y-hat (0,1,0) by quaternion q -> the splat's local Y axis
    // which should be the surface normal direction.
    Vec3f rotate_y_by_quat(const Quat& q)
    {
        // The Y column of the rotation matrix from quat (w,x,y,z):
        //   row0 = 2(xy - wz)
        //   row1 = 1 - 2(x² + z²)
        //   row2 = 2(yz + wx)
        const float x2 = 2.0f * q.x * q.y - 2.0f * q.w * q.z;
        const float y2 = 1.0f - 2.0f * q.x * q.x - 2.0f * q.z * q.z;
        const float z2 = 2.0f * q.y * q.z + 2.0f * q.w * q.x;
        return normalize3({ x2, y2, z2 });
    }
}


// ═══════════════════════════════════════════════════════════════════
// Defaults: no optional fields -> same as existing compiler
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainGeometryFields, NoOptionalFields_MatchesExistingOutput)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    auto recipe = make_base_recipe();

    // With no geometry fields, should delegate to existing compiler.
    auto recipe_cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);

    auto direct_cloud = make_terrain_surface_splat_cloud(
        recipe.geometry, *fields.get("height"));

    ASSERT_EQ(recipe_cloud.splats.size(), direct_cloud.splats.size());

    for (size_t i = 0; i < recipe_cloud.splats.size(); ++i)
    {
        const auto& a = recipe_cloud.splats[i];
        const auto& b = direct_cloud.splats[i];
        EXPECT_FLOAT_EQ(a.position[0], b.position[0]);
        EXPECT_FLOAT_EQ(a.position[1], b.position[1]);
        EXPECT_FLOAT_EQ(a.position[2], b.position[2]);
        EXPECT_FLOAT_EQ(a.rotation[0], b.rotation[0]);
        EXPECT_FLOAT_EQ(a.rotation[1], b.rotation[1]);
        EXPECT_FLOAT_EQ(a.rotation[2], b.rotation[2]);
        EXPECT_FLOAT_EQ(a.rotation[3], b.rotation[3]);
        EXPECT_FLOAT_EQ(a.scale[0], b.scale[0]);
        EXPECT_FLOAT_EQ(a.scale[1], b.scale[1]);
        EXPECT_FLOAT_EQ(a.scale[2], b.scale[2]);
        EXPECT_FLOAT_EQ(a.opacity, b.opacity);
        EXPECT_FLOAT_EQ(a.color_dc[0], b.color_dc[0]);
    }
}


// ═══════════════════════════════════════════════════════════════════
// Imported normal field
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainGeometryFields, ImportedNormal_ReplacesFiniteDifference)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    // Imported normal pointing straight up: (0, 1, 0) in [-1,1] range.
    fields.add("normal_x", make_ranged_field( 0.0f, -1.0f, 1.0f));
    fields.add("normal_y", make_ranged_field( 1.0f, -1.0f, 1.0f));
    fields.add("normal_z", make_ranged_field( 0.0f, -1.0f, 1.0f));

    auto recipe = make_base_recipe();
    recipe.normal.source = NormalSource::ImportedField;

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_TRUE(val.ok);
    EXPECT_FALSE(cloud.empty());

    // All splats should have normal pointing up (0, 1, 0).
    for (const auto& s : cloud.splats)
    {
        Vec3f n = rotate_y_by_quat(get_rotation(s));
        EXPECT_NEAR(n.y, 1.0f, 0.01f) << "normal should point up";
        EXPECT_NEAR(n.x, 0.0f, 0.01f);
        EXPECT_NEAR(n.z, 0.0f, 0.01f);
    }
}

TEST(TerrainGeometryFields, ImportedNormal_TiltedNormal)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    // Imported normal tilted toward +X: roughly (0.707, 0.707, 0).
    const float v = 0.70710678f;
    fields.add("normal_x", make_ranged_field( v,   -1.0f, 1.0f));
    fields.add("normal_y", make_ranged_field( v,   -1.0f, 1.0f));
    fields.add("normal_z", make_ranged_field( 0.0f, -1.0f, 1.0f));

    auto recipe = make_base_recipe();
    recipe.normal.source = NormalSource::ImportedField;

    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);
    EXPECT_FALSE(cloud.empty());

    for (const auto& s : cloud.splats)
    {
        Vec3f n = rotate_y_by_quat(get_rotation(s));
        EXPECT_NEAR(n.x, v, 0.02f);
        EXPECT_NEAR(n.y, v, 0.02f);
        EXPECT_NEAR(n.z, 0.0f, 0.02f);
    }
}


// ═══════════════════════════════════════════════════════════════════
// Normal blend
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainGeometryFields, NormalBlend0_UsesFiniteDifference)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));
    // Imported: tilted normal
    fields.add("normal_x", make_ranged_field( 1.0f, -1.0f, 1.0f));
    fields.add("normal_y", make_ranged_field( 0.0f, -1.0f, 1.0f));
    fields.add("normal_z", make_ranged_field( 0.0f, -1.0f, 1.0f));

    auto recipe = make_base_recipe();
    recipe.normal.source = NormalSource::Blend;
    recipe.normal.blend  = 0.0f;  // pure FD

    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);
    EXPECT_FALSE(cloud.empty());

    // Flat height -> FD normal is (0, 1, 0).
    for (const auto& s : cloud.splats)
    {
        Vec3f n = rotate_y_by_quat(get_rotation(s));
        EXPECT_NEAR(n.y, 1.0f, 0.02f) << "blend=0 should be FD normal (up)";
    }
}

TEST(TerrainGeometryFields, NormalBlend1_UsesImported)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));
    // Imported: pointing along +X
    fields.add("normal_x", make_ranged_field( 1.0f, -1.0f, 1.0f));
    fields.add("normal_y", make_ranged_field( 0.0f, -1.0f, 1.0f));
    fields.add("normal_z", make_ranged_field( 0.0f, -1.0f, 1.0f));

    auto recipe = make_base_recipe();
    recipe.normal.source = NormalSource::Blend;
    recipe.normal.blend  = 1.0f;  // pure imported

    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);
    EXPECT_FALSE(cloud.empty());

    for (const auto& s : cloud.splats)
    {
        Vec3f n = rotate_y_by_quat(get_rotation(s));
        EXPECT_NEAR(n.x, 1.0f, 0.02f) << "blend=1 should be imported (+X)";
        EXPECT_NEAR(n.y, 0.0f, 0.02f);
    }
}


// ═══════════════════════════════════════════════════════════════════
// Normal encoding: zero_to_one
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainGeometryFields, ZeroToOneEncoding_DecodesCorrectly)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    // zero_to_one: 0.5 -> 0, 1.0 -> +1, 0.0 -> -1
    // Normal pointing up: (0.5, 1.0, 0.5) -> decoded (0, 1, 0)
    fields.add("normal_x", make_ranged_field(0.5f, 0.0f, 1.0f));
    fields.add("normal_y", make_ranged_field(1.0f, 0.0f, 1.0f));
    fields.add("normal_z", make_ranged_field(0.5f, 0.0f, 1.0f));

    auto recipe = make_base_recipe();
    recipe.normal.source        = NormalSource::ImportedField;
    recipe.normal.encoded_range = NormalEncodedRange::ZeroToOne;

    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);
    EXPECT_FALSE(cloud.empty());

    for (const auto& s : cloud.splats)
    {
        Vec3f n = rotate_y_by_quat(get_rotation(s));
        EXPECT_NEAR(n.y, 1.0f, 0.02f) << "decoded normal should point up";
        EXPECT_NEAR(n.x, 0.0f, 0.02f);
        EXPECT_NEAR(n.z, 0.0f, 0.02f);
    }
}


// ═══════════════════════════════════════════════════════════════════
// Normal up_axis: Z-up -> Y-up swap
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainGeometryFields, ZUpAxis_SwapsYZ)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    // Source Z-up: normal (0, 0, 1) means "up" in source space.
    // After Z-up -> Y-up swap: becomes (0, 1, 0) in Wozzits space.
    fields.add("normal_x", make_ranged_field(0.0f, -1.0f, 1.0f));
    fields.add("normal_y", make_ranged_field(0.0f, -1.0f, 1.0f));
    fields.add("normal_z", make_ranged_field(1.0f, -1.0f, 1.0f));

    auto recipe = make_base_recipe();
    recipe.normal.source  = NormalSource::ImportedField;
    recipe.normal.up_axis = NormalUpAxis::Z;

    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);
    EXPECT_FALSE(cloud.empty());

    for (const auto& s : cloud.splats)
    {
        Vec3f n = rotate_y_by_quat(get_rotation(s));
        EXPECT_NEAR(n.y, 1.0f, 0.02f) << "Z-up source (0,0,1) -> Y-up (0,1,0)";
    }
}


// ═══════════════════════════════════════════════════════════════════
// Normal flip
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainGeometryFields, FlipX_NegatesNormalX)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    const float v = 0.70710678f;
    fields.add("normal_x", make_ranged_field( v,   -1.0f, 1.0f));
    fields.add("normal_y", make_ranged_field( v,   -1.0f, 1.0f));
    fields.add("normal_z", make_ranged_field( 0.0f, -1.0f, 1.0f));

    auto recipe = make_base_recipe();
    recipe.normal.source = NormalSource::ImportedField;
    recipe.normal.flip_x = true;

    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);
    EXPECT_FALSE(cloud.empty());

    for (const auto& s : cloud.splats)
    {
        Vec3f n = rotate_y_by_quat(get_rotation(s));
        EXPECT_NEAR(n.x, -v, 0.02f) << "flip_x should negate X component";
        EXPECT_NEAR(n.y,  v, 0.02f);
    }
}


// ═══════════════════════════════════════════════════════════════════
// Missing imported normal fields -> validation error
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainGeometryFields, MissingNormalFields_Fails)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));
    // Only add X, missing Y and Z.
    fields.add("normal_x", make_ranged_field(0.0f, -1.0f, 1.0f));

    auto recipe = make_base_recipe();
    recipe.normal.source = NormalSource::ImportedField;

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_FALSE(val.ok);
    EXPECT_TRUE(cloud.empty());
    EXPECT_FALSE(val.error.empty());
}


// ═══════════════════════════════════════════════════════════════════
// Curvature: radius reduction
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainGeometryFields, Curvature_HighReducesRadius)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    // High curvature field (value=1.0 in [0,1] range).
    fields.add("curvature", make_ranged_field(1.0f, 0.0f, 1.0f));

    // Reference: no curvature.
    auto ref_recipe = make_base_recipe();
    auto ref_cloud = make_terrain_splat_cloud_from_recipe(ref_recipe, fields);

    // With curvature radius influence.
    auto recipe = make_base_recipe();
    recipe.curvature.field_name       = "curvature";
    recipe.curvature.radius_influence = 1.0f;
    recipe.curvature.min_radius_mul   = 0.5f;

    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);

    ASSERT_EQ(ref_cloud.splats.size(), cloud.splats.size());
    EXPECT_FALSE(cloud.empty());

    for (size_t i = 0; i < cloud.splats.size(); ++i)
    {
        // Scale[0] (s_u) should be reduced.  Log scale, so the value
        // should be smaller (more negative = smaller radius).
        EXPECT_LT(cloud.splats[i].scale[0], ref_cloud.splats[i].scale[0])
            << "curvature=1 should reduce radius";
        EXPECT_LT(cloud.splats[i].scale[2], ref_cloud.splats[i].scale[2])
            << "curvature=1 should reduce radius (v axis)";
    }
}

TEST(TerrainGeometryFields, Curvature_ZeroPreservesRadius)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));
    fields.add("curvature", make_ranged_field(0.0f, 0.0f, 1.0f));

    auto ref_recipe = make_base_recipe();
    auto ref_cloud = make_terrain_splat_cloud_from_recipe(ref_recipe, fields);

    auto recipe = make_base_recipe();
    recipe.curvature.field_name       = "curvature";
    recipe.curvature.radius_influence = 1.0f;
    recipe.curvature.min_radius_mul   = 0.5f;

    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);

    ASSERT_EQ(ref_cloud.splats.size(), cloud.splats.size());
    for (size_t i = 0; i < cloud.splats.size(); ++i)
    {
        // curvature=0 -> no radius change.
        EXPECT_NEAR(cloud.splats[i].scale[0],
                     ref_cloud.splats[i].scale[0], 0.01f);
    }
}


// ═══════════════════════════════════════════════════════════════════
// Curvature: opacity boost
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainGeometryFields, Curvature_HighBoostsOpacity)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));
    fields.add("curvature", make_ranged_field(1.0f, 0.0f, 1.0f));

    auto ref_recipe = make_base_recipe();
    auto ref_cloud = make_terrain_splat_cloud_from_recipe(ref_recipe, fields);

    auto recipe = make_base_recipe();
    recipe.curvature.field_name        = "curvature";
    recipe.curvature.opacity_influence = 1.0f;
    recipe.curvature.opacity_boost     = 1.25f;

    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);

    EXPECT_FALSE(cloud.empty());
    for (size_t i = 0; i < cloud.splats.size(); ++i)
    {
        // Higher opacity logit = higher opacity probability.
        EXPECT_GT(cloud.splats[i].opacity, ref_cloud.splats[i].opacity)
            << "curvature=1 should boost opacity";
    }
}


// ═══════════════════════════════════════════════════════════════════
// Peaks: color brightness
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainGeometryFields, Peaks_HighBoostsColor)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));
    fields.add("peaks", make_ranged_field(1.0f, 0.0f, 1.0f));

    auto ref_recipe = make_base_recipe();
    auto ref_cloud = make_terrain_splat_cloud_from_recipe(ref_recipe, fields);

    auto recipe = make_base_recipe();
    recipe.peaks.field_name              = "peaks";
    recipe.peaks.color_influence         = 1.0f;
    recipe.peaks.color_brightness_boost  = 0.3f;

    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);

    EXPECT_FALSE(cloud.empty());
    for (size_t i = 0; i < cloud.splats.size(); ++i)
    {
        // Color DC should be brighter.
        float ref_disp = sh_dc_to_display(ref_cloud.splats[i].color_dc[0]);
        float new_disp = sh_dc_to_display(cloud.splats[i].color_dc[0]);
        EXPECT_GT(new_disp, ref_disp)
            << "peaks=1 should boost brightness";
    }
}

TEST(TerrainGeometryFields, Peaks_ZeroPreservesColor)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));
    fields.add("peaks", make_ranged_field(0.0f, 0.0f, 1.0f));

    auto ref_recipe = make_base_recipe();
    auto ref_cloud = make_terrain_splat_cloud_from_recipe(ref_recipe, fields);

    auto recipe = make_base_recipe();
    recipe.peaks.field_name      = "peaks";
    recipe.peaks.color_influence = 1.0f;

    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);

    EXPECT_FALSE(cloud.empty());
    for (size_t i = 0; i < cloud.splats.size(); ++i)
    {
        EXPECT_NEAR(cloud.splats[i].color_dc[0],
                     ref_cloud.splats[i].color_dc[0], 0.01f);
    }
}


// ═══════════════════════════════════════════════════════════════════
// Normal debug views
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainGeometryFields, NormalDebugView_FD_ShowsUpGreen)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    auto recipe = make_base_recipe();
    recipe.normal_debug = NormalDebugView::FiniteDifference;

    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);
    EXPECT_FALSE(cloud.empty());

    // Flat field -> FD normal = (0, 1, 0).
    // Debug color = n * 0.5 + 0.5 = (0.5, 1.0, 0.5).
    for (const auto& s : cloud.splats)
    {
        float r = sh_dc_to_display(s.color_dc[0]);
        float g = sh_dc_to_display(s.color_dc[1]);
        float b = sh_dc_to_display(s.color_dc[2]);
        EXPECT_NEAR(r, 0.5f, 0.02f);
        EXPECT_NEAR(g, 1.0f, 0.02f);
        EXPECT_NEAR(b, 0.5f, 0.02f);
    }
}

TEST(TerrainGeometryFields, NormalDebugView_Imported_ShowsImportedNormal)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));
    // Imported normal pointing +X: (1, 0, 0).
    fields.add("normal_x", make_ranged_field( 1.0f, -1.0f, 1.0f));
    fields.add("normal_y", make_ranged_field( 0.0f, -1.0f, 1.0f));
    fields.add("normal_z", make_ranged_field( 0.0f, -1.0f, 1.0f));

    auto recipe = make_base_recipe();
    recipe.normal.source = NormalSource::ImportedField;
    recipe.normal_debug  = NormalDebugView::Imported;

    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);
    EXPECT_FALSE(cloud.empty());

    // Debug color = (1, 0, 0) * 0.5 + 0.5 = (1.0, 0.5, 0.5).
    for (const auto& s : cloud.splats)
    {
        float r = sh_dc_to_display(s.color_dc[0]);
        float g = sh_dc_to_display(s.color_dc[1]);
        float b = sh_dc_to_display(s.color_dc[2]);
        EXPECT_NEAR(r, 1.0f, 0.02f);
        EXPECT_NEAR(g, 0.5f, 0.02f);
        EXPECT_NEAR(b, 0.5f, 0.02f);
    }
}

TEST(TerrainGeometryFields, NormalDebugView_Difference)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));
    // FD normal = (0, 1, 0) for flat field.
    // Imported = (1, 0, 0).
    fields.add("normal_x", make_ranged_field( 1.0f, -1.0f, 1.0f));
    fields.add("normal_y", make_ranged_field( 0.0f, -1.0f, 1.0f));
    fields.add("normal_z", make_ranged_field( 0.0f, -1.0f, 1.0f));

    auto recipe = make_base_recipe();
    recipe.normal.source = NormalSource::ImportedField;
    recipe.normal_debug  = NormalDebugView::Difference;

    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields);
    EXPECT_FALSE(cloud.empty());

    // diff = abs(FD - imported) = abs((0,1,0) - (1,0,0)) = (1, 1, 0)
    for (const auto& s : cloud.splats)
    {
        float r = sh_dc_to_display(s.color_dc[0]);
        float g = sh_dc_to_display(s.color_dc[1]);
        float b = sh_dc_to_display(s.color_dc[2]);
        EXPECT_NEAR(r, 1.0f, 0.02f);
        EXPECT_NEAR(g, 1.0f, 0.02f);
        EXPECT_NEAR(b, 0.0f, 0.02f);
    }
}


// ═══════════════════════════════════════════════════════════════════
// Missing curvature/peaks fields -> validation error
// ═══════════════════════════════════════════════════════════════════

TEST(TerrainGeometryFields, MissingCurvatureField_Fails)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    auto recipe = make_base_recipe();
    recipe.curvature.field_name       = "nonexistent";
    recipe.curvature.radius_influence = 1.0f;

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_FALSE(val.ok);
    EXPECT_TRUE(cloud.empty());
}

TEST(TerrainGeometryFields, MissingPeaksField_Fails)
{
    ScalarFieldSet fields;
    fields.add("height", make_flat_field(0.0f));

    auto recipe = make_base_recipe();
    recipe.peaks.field_name      = "nonexistent";
    recipe.peaks.color_influence = 1.0f;

    TerrainSplatRecipeValidation val;
    auto cloud = make_terrain_splat_cloud_from_recipe(recipe, fields, &val);

    EXPECT_FALSE(val.ok);
    EXPECT_TRUE(cloud.empty());
}
