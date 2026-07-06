// tests/asset_placed_field/placed_field_asset_module.cpp
//
// Seam 1 of issue #223: the PlacedField combiner asset. A PlacedField binds a
// frame-less field (a scalar field in v1) to a world-space Placement frame as a
// thin reference pair, so downstream consumers (clipmap, collision) can descend
// from ONE shared upstream and a placement is authored once. These are
// device-free tests: the combiner records dep keys only, never GPU data.

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <gtest/gtest.h>

namespace
{
    wz::fs::Path test_root(const char* name)
    {
        return wz::fs::join(wz::fs::temp_directory_path(), name);
    }
}

// The type id and schema id are stable identity contracts — pin them so an
// accidental renumber is caught. kAssetTypePlacedField sits in the geometry/
// terrain CPU block (Placement's own CPU-data block is full); the schema shares
// Placement's world-data range.
TEST(PlacedFieldAssetModule, TypeAndSchemaAreRegistered)
{
    EXPECT_EQ(
        static_cast<int>(wz::engine::assets::kAssetTypePlacedField), 200);
    EXPECT_EQ(
        wz::engine::assets::kPlacedFieldSchema.value,
        0xF11ECA55E7000A04ull);
}

// The combiner resolves to a ref-pair carrying the field key + placement key.
TEST(PlacedFieldAssetModule, BindsFieldAndPlacement)
{
    const wz::fs::Path root =
        test_root("wozzits_placed_field_binds_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    const auto field = assets.scalar_fields().create_procedural_scalar_field({
        .name = "placed_field/height",
        .width = 4,
        .height = 4,
        .depth = 1,
        .generator = ScalarFieldGenerator::GradientX,
    });
    ASSERT_TRUE(field.valid());

    const auto placement = assets.placements().create_placement({
        .name = "placed_field/frame",
        .origin = { 10.0f, 0.0f, 20.0f },
        .extent = { 500.0f, 30.0f, 600.0f },
        .base_height = 5.0f,
    });
    ASSERT_TRUE(placement.valid());

    const auto placed = assets.placed_fields().create_placed_field({
        .field_key = field.output,
        .placement_key = placement.output,
        .field_type = kAssetTypeScalarField,
    });
    ASSERT_TRUE(placed.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const PlacedFieldData* data =
        assets.placed_fields().get_placed_field_data(
            assets.placed_fields().get_placed_field(placed));
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_TRUE(data->field_key == field.output);
    EXPECT_TRUE(data->placement_key == placement.output);
    EXPECT_EQ(data->field_type, kAssetTypeScalarField);
}

// The core identity property (#223): moving the Placement re-keys the PlacedField
// (so its consumers rebuild) while the field's own key is untouched — placement
// is NOT folded into the field's large GPU texture identity.
TEST(PlacedFieldAssetModule, PlacementMoveRekeysCombinerNotField)
{
    const wz::fs::Path root =
        test_root("wozzits_placed_field_rekey_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    const auto field = assets.scalar_fields().create_procedural_scalar_field({
        .name = "placed_field/shared_height",
        .width = 4,
        .height = 4,
        .depth = 1,
        .generator = ScalarFieldGenerator::GradientX,
    });
    ASSERT_TRUE(field.valid());

    const auto placement_a = assets.placements().create_placement({
        .name = "placed_field/frame_a",
        .origin = { 0.0f, 0.0f, 0.0f },
        .extent = { 100.0f, 10.0f, 100.0f },
        .base_height = 0.0f,
    });
    const auto placement_b = assets.placements().create_placement({
        // Same field, a MOVED frame (different extent/origin).
        .name = "placed_field/frame_b",
        .origin = { 5.0f, 0.0f, 7.0f },
        .extent = { 250.0f, 20.0f, 250.0f },
        .base_height = 3.0f,
    });
    ASSERT_TRUE(placement_a.valid());
    ASSERT_TRUE(placement_b.valid());

    const auto placed_a = assets.placed_fields().create_placed_field({
        .field_key = field.output,
        .placement_key = placement_a.output,
        .field_type = kAssetTypeScalarField,
    });
    const auto placed_b = assets.placed_fields().create_placed_field({
        .field_key = field.output,
        .placement_key = placement_b.output,
        .field_type = kAssetTypeScalarField,
    });
    ASSERT_TRUE(placed_a.valid());
    ASSERT_TRUE(placed_b.valid());

    // A different placement yields a DIFFERENT PlacedField key — consumers of it
    // re-key and rebuild.
    EXPECT_FALSE(placed_a.output == placed_b.output);

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const PlacedFieldData* data_a =
        assets.placed_fields().get_placed_field_data(
            assets.placed_fields().get_placed_field(placed_a));
    const PlacedFieldData* data_b =
        assets.placed_fields().get_placed_field_data(
            assets.placed_fields().get_placed_field(placed_b));
    ASSERT_NE(data_a, nullptr);
    ASSERT_NE(data_b, nullptr);

    // Both placed fields reference the SAME field key — the field itself is never
    // re-keyed by a placement move (its GPU texture is not re-uploaded).
    EXPECT_TRUE(data_a->field_key == field.output);
    EXPECT_TRUE(data_b->field_key == field.output);
    EXPECT_TRUE(data_a->field_key == data_b->field_key);
    // ...but they carry different frames.
    EXPECT_FALSE(data_a->placement_key == data_b->placement_key);
}

// The module rejects an incomplete pair before registering anything.
TEST(PlacedFieldAssetModule, RejectsMissingDependency)
{
    const wz::fs::Path root =
        test_root("wozzits_placed_field_reject_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    const auto placement = assets.placements().create_placement({
        .name = "placed_field/lonely_frame",
        .origin = { 0.0f, 0.0f, 0.0f },
        .extent = { 1.0f, 1.0f, 1.0f },
        .base_height = 0.0f,
    });
    ASSERT_TRUE(placement.valid());

    // No field -> invalid.
    const auto no_field = assets.placed_fields().create_placed_field({
        .field_key = {},
        .placement_key = placement.output,
        .field_type = kAssetTypeScalarField,
    });
    EXPECT_FALSE(no_field.valid());

    // No placement -> invalid.
    const auto no_placement = assets.placed_fields().create_placed_field({
        .field_key = placement.output,  // any non-empty key stands in here
        .placement_key = {},
        .field_type = kAssetTypeScalarField,
    });
    EXPECT_FALSE(no_placement.valid());
}
