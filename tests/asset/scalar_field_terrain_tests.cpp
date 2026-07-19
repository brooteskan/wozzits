// tests/asset/scalar_field_terrain_tests.cpp
//
// The procedural TERRAIN scalar field recipe (kScalarFieldTerrainSchema):
// fractal noise shaped into a landscape, with a radial basin that flattens the
// middle so a far layer can ring a near one without erupting through it.
//
// Two contracts carry most of the weight here, and most of these tests exist to
// hold one of them:
//
//   1. Output is normalised to EXACTLY [0, 1] — the same range a Gaea .r32
//      arrives with. That is what makes a procedural placeholder and a
//      hand-authored field interchangeable under one Placement, with peaks
//      landing at extent[1] metres either way and nothing to re-tune on swap.
//
//   2. Every authored dial reaches the generator AND is folded into the asset
//      key. A dial missing from the decoder is silently dead; a dial missing
//      from the key is worse — nudging it re-resolves straight back to the
//      cached old field, so the terrain visibly refuses to change and nothing
//      in the log says why.
//
// Device-free throughout: a null device, a fresh temp root per test.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/key_factories/scalar_field_terrain.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/editor/asset_graph_schema_registry.h>

#include <asset/compiler.h>
#include <file/filesystem.h>
#include <logging/logger.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace wz::engine::assets::test {

namespace stdfs = std::filesystem;

namespace {

    // Small enough that a test compiles a dozen fields in well under a second,
    // large enough that the basin covers a meaningful number of samples.
    constexpr uint32_t kTestResolution = 64;

    TerrainScalarFieldDesc base_desc(std::string name)
    {
        TerrainScalarFieldDesc desc{};
        desc.name = std::move(name);
        desc.resolution = kTestResolution;
        return desc;
    }

    // The ParamBlock the editor would write for a given desc. Every dial the
    // compiler's decoder reads appears here, so a rename on either side breaks
    // the round-trip test rather than quietly reverting a dial to its default.
    wz::asset::ParamBlock params_from_desc(const TerrainScalarFieldDesc& desc)
    {
        wz::asset::ParamBlock params;
        params.values["name"] = desc.name;
        params.values["resolution"] = static_cast<int64_t>(desc.resolution);
        params.values["ridge_count"] = static_cast<double>(desc.ridge_count);
        params.values["ridginess"] = static_cast<double>(desc.ridginess);
        params.values["roughness"] = static_cast<double>(desc.roughness);
        params.values["detail"] = static_cast<int64_t>(desc.detail);
        params.values["seed"] = static_cast<int64_t>(desc.seed);
        params.values["basin_radius"] = static_cast<double>(desc.basin_radius);
        params.values["basin_falloff"] =
            static_cast<double>(desc.basin_falloff);
        params.values["basin_depth"] = static_cast<double>(desc.basin_depth);
        params.values["domain_kind"] =
            static_cast<int64_t>(desc.domain_kind);
        return params;
    }

    wz::asset::AssetKey key_from_desc(const TerrainScalarFieldDesc& desc)
    {
        return make_terrain_scalar_field_key(
            desc.name,
            desc.resolution,
            desc.ridge_count,
            desc.ridginess,
            desc.roughness,
            desc.detail,
            desc.seed,
            desc.basin_radius,
            desc.basin_falloff,
            desc.basin_depth,
            static_cast<uint8_t>(desc.format),
            static_cast<uint8_t>(desc.domain_kind));
    }

    // Mean over the disc r <= frac, where r is the field-relative radius the
    // basin dials are expressed in (1.0 = edge midpoint). Averaging rather than
    // probing one sample keeps the basin assertions from depending on whatever
    // the noise happens to do at a single texel.
    float mean_within_radius(
        const std::vector<float>& values, uint32_t n, float frac)
    {
        const float centre = 0.5f * static_cast<float>(n - 1);
        double sum = 0.0;
        uint32_t hits = 0;
        for (uint32_t y = 0; y < n; ++y) {
            for (uint32_t x = 0; x < n; ++x) {
                const float dx = static_cast<float>(x) - centre;
                const float dy = static_cast<float>(y) - centre;
                const float r = std::sqrt(dx * dx + dy * dy) / centre;
                if (r <= frac) {
                    sum += values[x + y * n];
                    ++hits;
                }
            }
        }
        return (hits > 0) ? static_cast<float>(sum / hits) : 0.0f;
    }

    // Mean over the annulus lo <= r <= hi.
    float mean_within_annulus(
        const std::vector<float>& values, uint32_t n, float lo, float hi)
    {
        const float centre = 0.5f * static_cast<float>(n - 1);
        double sum = 0.0;
        uint32_t hits = 0;
        for (uint32_t y = 0; y < n; ++y) {
            for (uint32_t x = 0; x < n; ++x) {
                const float dx = static_cast<float>(x) - centre;
                const float dy = static_cast<float>(y) - centre;
                const float r = std::sqrt(dx * dx + dy * dy) / centre;
                if (r >= lo && r <= hi) {
                    sum += values[x + y * n];
                    ++hits;
                }
            }
        }
        return (hits > 0) ? static_cast<float>(sum / hits) : 0.0f;
    }

} // namespace


// ─── Fixture ──────────────────────────────────────────────────────────────────

class TerrainScalarFieldTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Per-test root: the recipe is disk-cached, and a root shared between
        // tests would let one test's cache answer another test's compile.
        const std::string leaf =
            std::string("wz_terrain_field_tests_")
            + ::testing::UnitTest::GetInstance()
                ->current_test_info()->name();
        temp_dir_ = wz::fs::Path{ (stdfs::temp_directory_path() / leaf).string() };
        stdfs::remove_all(temp_dir_);
        stdfs::create_directories(temp_dir_);

        library_ = std::make_unique<EngineAssetLibrary>(
            null_device_, logger_, temp_dir_);
    }

    void TearDown() override
    {
        library_.reset();
        stdfs::remove_all(temp_dir_);
    }

    // Compile through the TYPED path (the programmatic half of the two-way
    // meta read) and hand back the resolved samples.
    std::vector<float> compile_typed(const TerrainScalarFieldDesc& desc)
    {
        const auto asset =
            library_->scalar_fields().create_terrain_scalar_field(desc);
        EXPECT_TRUE(asset.valid());

        EXPECT_TRUE(library_->commit());
        EXPECT_TRUE(library_->resolve_all().ok());

        const ScalarFieldData* data =
            library_->scalar_fields().get_scalar_field_data(
                library_->scalar_fields().get_scalar_field(asset));
        if (!data) {
            ADD_FAILURE() << "terrain field did not resolve: " << desc.name;
            return {};
        }
        return data->values;
    }

    // Compile through the PARAMBLOCK path — what the editor/graph supplies.
    std::vector<float> compile_params(const TerrainScalarFieldDesc& desc)
    {
        const wz::asset::AssetKey key = key_from_desc(desc);

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypeScalarField;
        node.schema = kScalarFieldTerrainSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = params_from_desc(desc);
        EXPECT_TRUE(library_->system().register_asset(std::move(node)));

        EXPECT_TRUE(library_->commit());
        EXPECT_TRUE(library_->resolve_all().ok());

        const ScalarFieldData* data =
            library_->scalar_fields().get_scalar_field_data(
                library_->scalar_fields().get_scalar_field(
                    ScalarFieldAsset{ .output = key }));
        if (!data) {
            ADD_FAILURE() << "terrain field did not resolve: " << desc.name;
            return {};
        }
        return data->values;
    }

    wz::gpu::Device null_device_{};
    wz::Logger      logger_{};
    wz::fs::Path    temp_dir_{};
    std::unique_ptr<EngineAssetLibrary> library_;
};


// ─── The [0, 1] contract ──────────────────────────────────────────────────────

// The load-bearing invariant. A terrain field and a Gaea .r32 have to mean the
// same thing under one Placement, so peaks land at extent[1] metres either way.
// Exact bounds, not approximate: the compiler rescales to fill the range rather
// than trusting the noise to reach it, which is what makes "set extent_y = 900
// and your peaks are 900 m" literally true.
TEST_F(TerrainScalarFieldTest, OutputIsNormalisedToExactlyZeroOne)
{
    const std::vector<float> values = compile_typed(base_desc("terrain/range"));
    ASSERT_EQ(values.size(),
              static_cast<size_t>(kTestResolution) * kTestResolution);

    const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
    EXPECT_FLOAT_EQ(*lo, 0.0f);
    EXPECT_FLOAT_EQ(*hi, 1.0f);

    for (const float v : values) {
        ASSERT_GE(v, 0.0f);
        ASSERT_LE(v, 1.0f);
        ASSERT_TRUE(std::isfinite(v));
    }
}

// The recorded min/max travel with the data — downstream consumers read them
// rather than rescanning, so they have to agree with what is actually stored.
TEST_F(TerrainScalarFieldTest, RecordedRangeMatchesTheSamples)
{
    auto desc = base_desc("terrain/recorded_range");
    const auto asset =
        library_->scalar_fields().create_terrain_scalar_field(desc);
    ASSERT_TRUE(asset.valid());
    ASSERT_TRUE(library_->commit());
    ASSERT_TRUE(library_->resolve_all().ok());

    const ScalarFieldData* data =
        library_->scalar_fields().get_scalar_field_data(
            library_->scalar_fields().get_scalar_field(asset));
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->valid());

    EXPECT_EQ(data->width, kTestResolution);
    EXPECT_EQ(data->height, kTestResolution);
    EXPECT_EQ(data->depth, 1u);
    EXPECT_FLOAT_EQ(data->min_value, 0.0f);
    EXPECT_FLOAT_EQ(data->max_value, 1.0f);
}


// ─── The basin ────────────────────────────────────────────────────────────────

// The dial set that makes this recipe fit for the far-horizon job rather than
// being generic noise: the middle collapses to the valley floor so the near
// layer sits above it, while the rim keeps full relief for the silhouette.
TEST_F(TerrainScalarFieldTest, BasinFlattensTheMiddleAndKeepsTheRim)
{
    auto desc = base_desc("terrain/basin_on");
    desc.basin_radius = 0.35f;
    desc.basin_falloff = 0.25f;
    desc.basin_depth = 1.0f;

    const std::vector<float> values = compile_typed(desc);
    ASSERT_FALSE(values.empty());

    // Inside basin_radius the mask is exactly zero, so those samples are the
    // field minimum — and normalisation maps the minimum to exactly 0.
    EXPECT_FLOAT_EQ(
        mean_within_radius(values, kTestResolution, 0.30f), 0.0f);

    // Beyond basin_radius + basin_falloff the mask is exactly one, so the rim
    // carries the terrain at full height.
    EXPECT_GT(
        mean_within_annulus(values, kTestResolution, 0.75f, 1.0f), 0.15f);
}

// basin_depth = 0 means "no basin", not "a subtle basin". With the mask flat at
// one the middle has to carry the same relief as anywhere else — otherwise the
// dial has a floor and an author cannot actually turn it off.
TEST_F(TerrainScalarFieldTest, ZeroBasinDepthLeavesTheMiddleInRelief)
{
    auto desc = base_desc("terrain/basin_off");
    desc.basin_depth = 0.0f;

    const std::vector<float> values = compile_typed(desc);
    ASSERT_FALSE(values.empty());

    // Normalised fBm averages near the middle of its range; anything above a
    // token amount proves the centre was not flattened.
    EXPECT_GT(mean_within_radius(values, kTestResolution, 0.30f), 0.15f);
}


// ─── Determinism ──────────────────────────────────────────────────────────────

// Two libraries, two temp roots, one recipe. Separate roots mean separate disk
// caches, so both actually generate — this pins the GENERATOR as deterministic
// rather than just proving the cache returns what it stored.
TEST_F(TerrainScalarFieldTest, GenerationIsDeterministicAcrossLibraries)
{
    const auto desc = base_desc("terrain/deterministic");
    const std::vector<float> first = compile_typed(desc);
    ASSERT_FALSE(first.empty());

    const wz::fs::Path other_root{
        (stdfs::temp_directory_path() / "wz_terrain_field_tests_second").string()
    };
    stdfs::remove_all(other_root);
    stdfs::create_directories(other_root);
    {
        wz::Logger other_logger{};
        EngineAssetLibrary other{ null_device_, other_logger, other_root };

        const auto asset =
            other.scalar_fields().create_terrain_scalar_field(desc);
        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(other.commit());
        ASSERT_TRUE(other.resolve_all().ok());

        const ScalarFieldData* data =
            other.scalar_fields().get_scalar_field_data(
                other.scalar_fields().get_scalar_field(asset));
        ASSERT_NE(data, nullptr);
        ASSERT_EQ(data->values.size(), first.size());
        for (size_t i = 0; i < first.size(); ++i) {
            ASSERT_FLOAT_EQ(data->values[i], first[i]) << "sample " << i;
        }
    }
    stdfs::remove_all(other_root);
}

// The two halves of the two-way meta read must agree. If the ParamBlock decoder
// drops a dial it falls back to the struct default, which for a default-valued
// desc would still MATCH — so every dial here is moved off its default first.
TEST_F(TerrainScalarFieldTest, TypedAndParamBlockPathsAgree)
{
    auto desc = base_desc("terrain/two_paths");
    desc.ridge_count = 9.5f;
    desc.ridginess = 0.25f;
    desc.roughness = 0.7f;
    desc.detail = 4;
    desc.seed = 12345;
    desc.basin_radius = 0.2f;
    desc.basin_falloff = 0.4f;
    desc.basin_depth = 0.75f;

    const std::vector<float> typed = compile_typed(desc);
    ASSERT_FALSE(typed.empty());

    // Same dials, so the same key — a second library keeps the registration
    // from colliding with the one the typed path already made.
    const wz::fs::Path other_root{
        (stdfs::temp_directory_path() / "wz_terrain_field_tests_paths").string()
    };
    stdfs::remove_all(other_root);
    stdfs::create_directories(other_root);
    {
        wz::Logger other_logger{};
        EngineAssetLibrary other{ null_device_, other_logger, other_root };

        const wz::asset::AssetKey key = key_from_desc(desc);
        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypeScalarField;
        node.schema = kScalarFieldTerrainSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = params_from_desc(desc);
        ASSERT_TRUE(other.system().register_asset(std::move(node)));
        ASSERT_TRUE(other.commit());
        ASSERT_TRUE(other.resolve_all().ok());

        const ScalarFieldData* data =
            other.scalar_fields().get_scalar_field_data(
                other.scalar_fields().get_scalar_field(
                    ScalarFieldAsset{ .output = key }));
        ASSERT_NE(data, nullptr);
        ASSERT_EQ(data->values.size(), typed.size());
        for (size_t i = 0; i < typed.size(); ++i) {
            ASSERT_FLOAT_EQ(data->values[i], typed[i]) << "sample " << i;
        }
    }
    stdfs::remove_all(other_root);
}


// ─── Dial liveness ────────────────────────────────────────────────────────────

// Every shape dial, driven through the PARAMBLOCK path, has to change the
// field. This is the test that catches a dial dropped from the decoder or
// misspelled in the declaration: pb.get<T> ignores a param it cannot find,
// silently substitutes the default, and the dial goes dead with no diagnostic.
TEST_F(TerrainScalarFieldTest, EveryDialThroughParamBlockChangesTheField)
{
    const auto baseline_desc = base_desc("terrain/dial_baseline");
    const std::vector<float> baseline = compile_params(baseline_desc);
    ASSERT_FALSE(baseline.empty());

    const auto differs_from_baseline =
        [&](const std::vector<float>& other, const char* dial) {
            ASSERT_EQ(other.size(), baseline.size()) << dial;
            bool any_different = false;
            for (size_t i = 0; i < baseline.size(); ++i) {
                if (std::fabs(other[i] - baseline[i]) > 1e-6f) {
                    any_different = true;
                    break;
                }
            }
            EXPECT_TRUE(any_different)
                << "dial '" << dial << "' did not reach the generator";
        };

    // Each variant gets its own name so the key differs even if — as this test
    // is checking — the dial itself failed to reach the key factory.
    {
        auto d = base_desc("terrain/dial_ridge_count");
        d.ridge_count = 17.0f;
        differs_from_baseline(compile_params(d), "ridge_count");
    }
    {
        auto d = base_desc("terrain/dial_ridginess");
        d.ridginess = 0.05f;
        differs_from_baseline(compile_params(d), "ridginess");
    }
    {
        auto d = base_desc("terrain/dial_roughness");
        d.roughness = 0.95f;
        differs_from_baseline(compile_params(d), "roughness");
    }
    {
        auto d = base_desc("terrain/dial_detail");
        d.detail = 2;
        differs_from_baseline(compile_params(d), "detail");
    }
    {
        auto d = base_desc("terrain/dial_seed");
        d.seed = 99991;
        differs_from_baseline(compile_params(d), "seed");
    }
    {
        auto d = base_desc("terrain/dial_basin_radius");
        d.basin_radius = 0.8f;
        differs_from_baseline(compile_params(d), "basin_radius");
    }
    {
        auto d = base_desc("terrain/dial_basin_falloff");
        d.basin_falloff = 0.9f;
        differs_from_baseline(compile_params(d), "basin_falloff");
    }
    {
        auto d = base_desc("terrain/dial_basin_depth");
        d.basin_depth = 0.0f;
        differs_from_baseline(compile_params(d), "basin_depth");
    }
}

// resolution is the one dial whose effect is visible in the field's shape
// rather than its samples, so it gets its own check.
TEST_F(TerrainScalarFieldTest, ResolutionThroughParamBlockSizesTheField)
{
    auto desc = base_desc("terrain/resolution");
    desc.resolution = 32;

    const std::vector<float> values = compile_params(desc);
    EXPECT_EQ(values.size(), 32u * 32u);
}


// ─── Key identity ─────────────────────────────────────────────────────────────

// Every dial must re-key. A dial folded into the generator but NOT into the key
// is the worst of the failure modes: the editor writes the new value, the graph
// resolves straight back to the cached old field, and the terrain refuses to
// change with nothing in the log to explain it.
TEST(TerrainScalarFieldKeyTests, EveryDialContributesToIdentity)
{
    TerrainScalarFieldDesc base{};
    base.name = "terrain/key";
    const wz::asset::AssetKey baseline = key_from_desc(base);

    const auto rekeys = [&](TerrainScalarFieldDesc d, const char* dial) {
        EXPECT_FALSE(key_from_desc(d) == baseline)
            << "dial '" << dial << "' is not folded into the asset key";
    };

    { auto d = base; d.resolution = 2048;    rekeys(d, "resolution"); }
    { auto d = base; d.ridge_count = 7.0f;   rekeys(d, "ridge_count"); }
    { auto d = base; d.ridginess = 0.61f;    rekeys(d, "ridginess"); }
    { auto d = base; d.roughness = 0.51f;    rekeys(d, "roughness"); }
    { auto d = base; d.detail = 7;           rekeys(d, "detail"); }
    { auto d = base; d.seed = 1;             rekeys(d, "seed"); }
    { auto d = base; d.basin_radius = 0.36f; rekeys(d, "basin_radius"); }
    { auto d = base; d.basin_falloff = 0.26f;rekeys(d, "basin_falloff"); }
    { auto d = base; d.basin_depth = 0.99f;  rekeys(d, "basin_depth"); }
    { auto d = base; d.domain_kind =
        ScalarFieldDomainKind::BakedComputation; rekeys(d, "domain_kind"); }

    // name is an identity input too — two terrains sharing every dial must
    // still be distinct assets, or the second registration silently fails.
    { auto d = base; d.name = "terrain/other"; rekeys(d, "name"); }
}

// A terrain field must never alias a procedural one, whatever the dials say.
TEST(TerrainScalarFieldKeyTests, SchemaSeparatesTerrainFromOtherRecipes)
{
    TerrainScalarFieldDesc desc{};
    desc.name = "terrain/schema";
    const wz::asset::AssetKey key = key_from_desc(desc);

    EXPECT_EQ(
        key.schema_hash.lo,
        detail::hash_u64(kScalarFieldTerrainSchema.value).lo);
    EXPECT_NE(
        key.schema_hash.lo,
        detail::hash_u64(kScalarFieldProceduralSchema.value).lo);

    // The schema id itself is a stable identity contract — pin it so an
    // accidental renumber is caught rather than silently orphaning every
    // cached terrain in every project.
    EXPECT_EQ(kScalarFieldTerrainSchema.value, 0xF11ECA55E7000204ull);
}


// ─── Declarations ─────────────────────────────────────────────────────────────

// The other half of the declared-params contract. The liveness test above
// proves the compiler READS the right names; this proves the editor is TOLD
// those names exist. An undeclared param is stored as a string, pb.get<T>
// ignores strings, and the dial goes silently dead.
TEST(TerrainScalarFieldKeyTests, DeclaresEveryParamItReads)
{
    const wz::asset::CompilerRegistry registry =
        wz::engine::editor::build_asset_graph_schema_registry();

    const wz::asset::AssetCompiler* compiler =
        registry.find(kScalarFieldTerrainSchema, kAssetTypeScalarField);
    ASSERT_NE(compiler, nullptr);

    // Generated from dials alone — no upstream data to derive from.
    EXPECT_TRUE(compiler->input_ports.empty());

    const auto declares =
        [compiler](std::string_view name, wz::asset::ParamType type) {
            return std::any_of(
                compiler->parameters.begin(),
                compiler->parameters.end(),
                [&](const wz::asset::ParamDecl& decl) {
                    return decl.name == name && decl.type == type;
                });
        };

    EXPECT_TRUE(declares("name", wz::asset::ParamType::String));
    EXPECT_TRUE(declares("resolution", wz::asset::ParamType::Int));
    EXPECT_TRUE(declares("ridge_count", wz::asset::ParamType::Float));
    EXPECT_TRUE(declares("ridginess", wz::asset::ParamType::Float));
    EXPECT_TRUE(declares("roughness", wz::asset::ParamType::Float));
    EXPECT_TRUE(declares("detail", wz::asset::ParamType::Int));
    EXPECT_TRUE(declares("seed", wz::asset::ParamType::Int));
    EXPECT_TRUE(declares("basin_radius", wz::asset::ParamType::Float));
    EXPECT_TRUE(declares("basin_falloff", wz::asset::ParamType::Float));
    EXPECT_TRUE(declares("basin_depth", wz::asset::ParamType::Float));
    EXPECT_TRUE(declares("domain_kind", wz::asset::ParamType::Enum));
}

// Three declarations of one default — the authoring desc, the compile desc, and
// what the editor seeds a fresh node with. Nothing but a test stops them
// drifting apart, and when they do, "a terrain nobody has touched" quietly
// means two different things depending on which path built it.
TEST(TerrainScalarFieldKeyTests, EveryDefaultDeclarationAgrees)
{
    const TerrainScalarFieldDesc        authoring{};
    const TerrainScalarFieldCompileDesc compile{};

    EXPECT_EQ(authoring.resolution, compile.resolution);
    EXPECT_FLOAT_EQ(authoring.ridge_count, compile.ridge_count);
    EXPECT_FLOAT_EQ(authoring.ridginess, compile.ridginess);
    EXPECT_FLOAT_EQ(authoring.roughness, compile.roughness);
    EXPECT_EQ(authoring.detail, compile.detail);
    EXPECT_EQ(authoring.seed, compile.seed);
    EXPECT_FLOAT_EQ(authoring.basin_radius, compile.basin_radius);
    EXPECT_FLOAT_EQ(authoring.basin_falloff, compile.basin_falloff);
    EXPECT_FLOAT_EQ(authoring.basin_depth, compile.basin_depth);

    const wz::asset::CompilerRegistry registry =
        wz::engine::editor::build_asset_graph_schema_registry();
    const wz::asset::AssetCompiler* compiler =
        registry.find(kScalarFieldTerrainSchema, kAssetTypeScalarField);
    ASSERT_NE(compiler, nullptr);

    for (const wz::asset::ParamDecl& decl : compiler->parameters) {
        const double d = decl.default_num;
        if (decl.name == "resolution") {
            EXPECT_EQ(static_cast<uint32_t>(d), authoring.resolution);
        } else if (decl.name == "ridge_count") {
            EXPECT_FLOAT_EQ(static_cast<float>(d), authoring.ridge_count);
        } else if (decl.name == "ridginess") {
            EXPECT_FLOAT_EQ(static_cast<float>(d), authoring.ridginess);
        } else if (decl.name == "roughness") {
            EXPECT_FLOAT_EQ(static_cast<float>(d), authoring.roughness);
        } else if (decl.name == "detail") {
            EXPECT_EQ(static_cast<uint32_t>(d), authoring.detail);
        } else if (decl.name == "seed") {
            EXPECT_EQ(static_cast<uint32_t>(d), authoring.seed);
        } else if (decl.name == "basin_radius") {
            EXPECT_FLOAT_EQ(static_cast<float>(d), authoring.basin_radius);
        } else if (decl.name == "basin_falloff") {
            EXPECT_FLOAT_EQ(static_cast<float>(d), authoring.basin_falloff);
        } else if (decl.name == "basin_depth") {
            EXPECT_FLOAT_EQ(static_cast<float>(d), authoring.basin_depth);
        }
    }
}


// ─── Rejections ───────────────────────────────────────────────────────────────

// name is an identity input to the key factory, so an unnamed terrain would
// alias every other unnamed terrain sharing its dials. Rejected before anything
// is registered.
TEST_F(TerrainScalarFieldTest, RejectsMissingName)
{
    TerrainScalarFieldDesc desc{};
    desc.name.clear();
    EXPECT_FALSE(
        library_->scalar_fields().create_terrain_scalar_field(desc).valid());
}

// Zero octaves would produce an entirely flat field, which reads as a broken
// asset rather than an authored choice — so it fails the compile loudly instead
// of being clamped behind the author's back.
TEST_F(TerrainScalarFieldTest, RejectsZeroDetail)
{
    auto desc = base_desc("terrain/no_detail");
    desc.detail = 0;

    const auto asset =
        library_->scalar_fields().create_terrain_scalar_field(desc);
    ASSERT_TRUE(asset.valid());
    ASSERT_TRUE(library_->commit());

    library_->resolve_all();
    EXPECT_EQ(
        library_->scalar_fields().get_scalar_field_data(
            library_->scalar_fields().get_scalar_field(asset)),
        nullptr);
}

// A one-sample field has no interior and no basin geometry to speak of.
TEST_F(TerrainScalarFieldTest, RejectsDegenerateResolution)
{
    auto desc = base_desc("terrain/one_sample");
    desc.resolution = 1;

    const auto asset =
        library_->scalar_fields().create_terrain_scalar_field(desc);
    ASSERT_TRUE(asset.valid());
    ASSERT_TRUE(library_->commit());

    library_->resolve_all();
    EXPECT_EQ(
        library_->scalar_fields().get_scalar_field_data(
            library_->scalar_fields().get_scalar_field(asset)),
        nullptr);
}

} // namespace wz::engine::assets::test
