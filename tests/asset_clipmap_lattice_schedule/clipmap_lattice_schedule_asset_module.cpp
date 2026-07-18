// tests/asset_clipmap_lattice_schedule/clipmap_lattice_schedule_asset_module.cpp
//
// The ClipmapLatticeSchedule asset: the RESOLVED geometry-clipmap LOD schedule,
// promoted out of a discarded function-local inside the lattice mesh compiler
// into a first-class asset with ONE producer. Its point is that the clipmap's
// visual lattice and its collision reconstruction can descend from the same
// numbers instead of each re-deriving (or hand-typing) them. These are
// device-free tests: the compiler reads one integer out of the height field
// (its width) and runs a pure resolver, never GPU data.

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
// accidental renumber is caught. kAssetTypeClipmapLatticeSchedule sits next to
// kAssetTypePlacedField (200) in the geometry/terrain CPU block; the schema
// shares Placement's world-data range.
TEST(ClipmapLatticeScheduleAssetModule, TypeAndSchemaAreRegistered)
{
    EXPECT_EQ(
        static_cast<int>(wz::engine::assets::kAssetTypeClipmapLatticeSchedule),
        201);
    EXPECT_EQ(
        wz::engine::assets::kClipmapLatticeScheduleSchema.value,
        0xF11ECA55E7000A05ull);
}

// The schedule binds the height field as its ONE dependency and derives its
// finest cell size from that field's resolution — a number it can only have
// obtained by reading the dep.
TEST(ClipmapLatticeScheduleAssetModule, BindsHeightField)
{
    const wz::fs::Path root =
        test_root("wozzits_clipmap_lattice_schedule_binds_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    const auto field = assets.scalar_fields().create_procedural_scalar_field({
        .name = "clipmap_lattice_schedule/height",
        .width = 256,
        .height = 1,
        .depth = 1,
        .generator = ScalarFieldGenerator::GradientX,
    });
    ASSERT_TRUE(field.valid());

    const auto schedule =
        assets.clipmap_lattice_schedules().create_clipmap_lattice_schedule({
            .name = "clipmap_lattice_schedule/binds",
            .field_key = field.output,
            .world_extent = 512.0f,
            .horizon = 256.0f,
            .triangle_budget = 200000u,
        });
    ASSERT_TRUE(schedule.valid());

    // The DAG edge itself: the schedule node lists the field as its only dep.
    bool found_edge = false;
    for (const auto& entry : assets.system().registered_assets()) {
        if (!(entry.node.key == schedule.output)) {
            continue;
        }
        found_edge = true;
        ASSERT_EQ(entry.dep_keys.size(), 1u);
        EXPECT_TRUE(entry.dep_keys[0] == field.output);
    }
    EXPECT_TRUE(found_edge);

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const ClipmapLatticeScheduleData* data =
        assets.clipmap_lattice_schedules().get_clipmap_lattice_schedule_data(
            assets.clipmap_lattice_schedules().get_clipmap_lattice_schedule(
                schedule));
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());

    // cell_size == world_extent / N. N came from the field, so a correct value
    // here proves the dependency was resolved and read.
    EXPECT_FLOAT_EQ(data->cell_size, 512.0f / 256.0f);
    // The lattice always reaches at least the requested horizon.
    EXPECT_GE(data->achieved_horizon, 256.0f);
    EXPECT_GT(data->triangle_count, 0u);
}

// The known-good schedule, verified by hand against resolve_clipmap_lattice: a
// 4096-wide field over 1000 m gives c0 = 1000/4096, and at a 1000 m horizon the
// L=6 config costs 622,592 triangles (over the 200k budget) while L=7 costs
// 180,224 (under it). The resolver takes the SMALLEST fitting level count, so
// this must land on L=7 / m=128. This is the numeric contract downstream
// consumers will read, so it is pinned exactly.
TEST(ClipmapLatticeScheduleAssetModule, ResolvesKnownGoodSchedule)
{
    const wz::fs::Path root =
        test_root("wozzits_clipmap_lattice_schedule_known_good_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    // width is the only dimension the schedule reads (N per side), so a 4096x1
    // field exercises N = 4096 without materialising 16M samples.
    const auto field = assets.scalar_fields().create_procedural_scalar_field({
        .name = "clipmap_lattice_schedule/height_4096",
        .width = 4096,
        .height = 1,
        .depth = 1,
        .generator = ScalarFieldGenerator::GradientX,
    });
    ASSERT_TRUE(field.valid());

    const auto schedule =
        assets.clipmap_lattice_schedules().create_clipmap_lattice_schedule({
            .name = "clipmap_lattice_schedule/known_good",
            .field_key = field.output,
            .world_extent = 1000.0f,
            .horizon = 1000.0f,
            .triangle_budget = 200000u,
        });
    ASSERT_TRUE(schedule.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const ClipmapLatticeScheduleData* data =
        assets.clipmap_lattice_schedules().get_clipmap_lattice_schedule_data(
            assets.clipmap_lattice_schedules().get_clipmap_lattice_schedule(
                schedule));
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->valid());

    EXPECT_EQ(data->base_resolution, 128u);
    EXPECT_EQ(data->level_count, 7u);
    EXPECT_FLOAT_EQ(data->cell_size, 1000.0f / 4096.0f);
    EXPECT_EQ(data->triangle_count, 180224u);
    // reach = floor(m/2) * 2^(L-1) * c0 = 64 * 64 * 1000/4096 = 1000 m exactly.
    EXPECT_FLOAT_EQ(data->achieved_horizon, 1000.0f);
    // The budget is honoured, and the horizon is met — the two guarantees the
    // resolver makes.
    EXPECT_LE(data->triangle_count, 200000u);
    EXPECT_GE(data->achieved_horizon, 1000.0f);
}

// The field's resolution is folded through deps_hash, so re-importing the
// heightmap at a different size re-keys the schedule (and everything descending
// from it) even though every authored dial is identical — the schedule can never
// silently keep a cell size that belongs to the old field.
TEST(ClipmapLatticeScheduleAssetModule, FieldResolutionChangeRekeysSchedule)
{
    const wz::fs::Path root =
        test_root("wozzits_clipmap_lattice_schedule_rekey_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    const auto field_1k = assets.scalar_fields().create_procedural_scalar_field({
        .name = "clipmap_lattice_schedule/rekey_height",
        .width = 1024,
        .height = 1,
        .depth = 1,
        .generator = ScalarFieldGenerator::GradientX,
    });
    const auto field_2k = assets.scalar_fields().create_procedural_scalar_field({
        // The SAME heightmap re-imported at twice the resolution.
        .name = "clipmap_lattice_schedule/rekey_height",
        .width = 2048,
        .height = 1,
        .depth = 1,
        .generator = ScalarFieldGenerator::GradientX,
    });
    ASSERT_TRUE(field_1k.valid());
    ASSERT_TRUE(field_2k.valid());
    ASSERT_FALSE(field_1k.output == field_2k.output);

    // Identical dials, identical name — only the field differs.
    const auto schedule_1k =
        assets.clipmap_lattice_schedules().create_clipmap_lattice_schedule({
            .name = "clipmap_lattice_schedule/rekey",
            .field_key = field_1k.output,
            .world_extent = 1024.0f,
            .horizon = 512.0f,
            .triangle_budget = 200000u,
        });
    const auto schedule_2k =
        assets.clipmap_lattice_schedules().create_clipmap_lattice_schedule({
            .name = "clipmap_lattice_schedule/rekey",
            .field_key = field_2k.output,
            .world_extent = 1024.0f,
            .horizon = 512.0f,
            .triangle_budget = 200000u,
        });
    ASSERT_TRUE(schedule_1k.valid());
    ASSERT_TRUE(schedule_2k.valid());

    // A different field resolution yields a DIFFERENT schedule key — consumers
    // of it re-key and rebuild.
    EXPECT_FALSE(schedule_1k.output == schedule_2k.output);

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    auto& schedules = assets.clipmap_lattice_schedules();
    const ClipmapLatticeScheduleData* data_1k =
        schedules.get_clipmap_lattice_schedule_data(
            schedules.get_clipmap_lattice_schedule(schedule_1k));
    const ClipmapLatticeScheduleData* data_2k =
        schedules.get_clipmap_lattice_schedule_data(
            schedules.get_clipmap_lattice_schedule(schedule_2k));
    ASSERT_NE(data_1k, nullptr);
    ASSERT_NE(data_2k, nullptr);

    // ...and the resolved schedules genuinely differ: twice the texels over the
    // same world extent halves the finest cell.
    EXPECT_FLOAT_EQ(data_1k->cell_size, 1024.0f / 1024.0f);
    EXPECT_FLOAT_EQ(data_2k->cell_size, 1024.0f / 2048.0f);
    // Both still cover the requested horizon.
    EXPECT_GE(data_1k->achieved_horizon, 512.0f);
    EXPECT_GE(data_2k->achieved_horizon, 512.0f);
}

// The module rejects an incomplete recipe before registering anything, and the
// compiler rejects a height field that never resolves.
TEST(ClipmapLatticeScheduleAssetModule, RejectsMissingOrInvalidField)
{
    const wz::fs::Path root =
        test_root("wozzits_clipmap_lattice_schedule_reject_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    // No field -> invalid.
    const auto no_field =
        assets.clipmap_lattice_schedules().create_clipmap_lattice_schedule({
            .name = "clipmap_lattice_schedule/no_field",
            .field_key = {},
            .world_extent = 1000.0f,
            .horizon = 1000.0f,
            .triangle_budget = 200000u,
        });
    EXPECT_FALSE(no_field.valid());

    // No name -> invalid: name is an identity input to the key factory, so an
    // empty one would collide with every other unnamed schedule over the same
    // field and dials.
    const auto field = assets.scalar_fields().create_procedural_scalar_field({
        .name = "clipmap_lattice_schedule/reject_height",
        .width = 64,
        .height = 1,
        .depth = 1,
        .generator = ScalarFieldGenerator::GradientX,
    });
    ASSERT_TRUE(field.valid());

    const auto no_name =
        assets.clipmap_lattice_schedules().create_clipmap_lattice_schedule({
            .name = {},
            .field_key = field.output,
        });
    EXPECT_FALSE(no_name.valid());

    // A field key that names nothing in the DAG: the schedule registers, but the
    // unsatisfiable dependency makes the graph fail rather than silently
    // compiling a schedule with no resolution behind it.
    wz::asset::AssetKey bogus_field{};
    bogus_field.content_hash = { 0xDEADull, 0xBEEFull };
    const auto dangling =
        assets.clipmap_lattice_schedules().create_clipmap_lattice_schedule({
            .name = "clipmap_lattice_schedule/dangling",
            .field_key = bogus_field,
            .world_extent = 1000.0f,
            .horizon = 1000.0f,
            .triangle_budget = 200000u,
        });
    ASSERT_TRUE(dangling.valid());
    EXPECT_FALSE(assets.commit() && assets.resolve_all().ok());
}
