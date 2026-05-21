// tests/asset_gaussian_splat/gaussian_splat_color_lod_compiler.cpp
//
// Unit tests for the deterministic uniform-grid neighbor pass that
// produces GaussianSplatColorLODData.
//
// These tests exercise the core compute_gaussian_splat_color_lod function
// directly — no asset library, no compilers registered. There is one
// integration test at the bottom that drives the full asset path.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/gaussian_splat/gaussian_splat_color_lod.h>
#include <engine/assets/gaussian_splat/gaussian_splat_color_lod_compiler.h>
#include <engine/assets/key_factories/gaussian_splat_color_lod.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

#include <cmath>
#include <filesystem>

namespace wz::engine::assets::test
{
    namespace
    {
        constexpr float kSH_C0 = 0.28209479177387814f;

        // Build a splat at `pos` with the given LINEAR-RGB display color.
        // Internally inverts the SH-DC encoding so the compiler decodes
        // back to the requested display color.
        GaussianSplat make_splat(
            float x, float y, float z,
            float r, float g, float b,
            float opacity_logit = 2.1972245773f)
        {
            GaussianSplat s{};
            s.position[0] = x;
            s.position[1] = y;
            s.position[2] = z;
            s.opacity     = opacity_logit;
            s.scale[0]    = std::log(0.01f);
            s.scale[1]    = std::log(0.01f);
            s.scale[2]    = std::log(0.01f);
            s.rotation[0] = 1.0f;  // w = 1, identity
            s.color_dc[0] = (r - 0.5f) / kSH_C0;
            s.color_dc[1] = (g - 0.5f) / kSH_C0;
            s.color_dc[2] = (b - 0.5f) / kSH_C0;
            return s;
        }

        GaussianSplatCloudData make_cloud(
            std::vector<GaussianSplat> splats)
        {
            GaussianSplatCloudData c{};
            c.splats = std::move(splats);
            c.bounds.valid = false;
            return c;
        }

        // Helper accessors for parallel arrays.
        inline float r_at(const GaussianSplatColorLODData& d, uint32_t i)
        { return d.neighborhood_color[3 * i + 0]; }
        inline float g_at(const GaussianSplatColorLODData& d, uint32_t i)
        { return d.neighborhood_color[3 * i + 1]; }
        inline float b_at(const GaussianSplatColorLODData& d, uint32_t i)
        { return d.neighborhood_color[3 * i + 2]; }
    }


    // ─── 1. Neighborhood mean shifts toward majority color ────────────────

    TEST(GaussianSplatColorLODCompute, MajorityColorDominatesMean)
    {
        // Two red splats and six blue splats packed close together
        // (all within one cell of a 0.10 grid).
        std::vector<GaussianSplat> splats;
        splats.push_back(make_splat(0.00f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f));  // 0: red
        splats.push_back(make_splat(0.01f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f));  // 1: red
        splats.push_back(make_splat(0.02f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f));  // 2..7: blue
        splats.push_back(make_splat(0.03f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
        splats.push_back(make_splat(0.04f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
        splats.push_back(make_splat(0.05f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
        splats.push_back(make_splat(0.06f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
        splats.push_back(make_splat(0.07f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
        const auto cloud = make_cloud(std::move(splats));

        GaussianSplatColorLODCompileDesc desc{};
        desc.neighbor_radius_world = 0.20f;
        desc.gaussian_sigma_world  = 0.10f;
        desc.include_self          = true;
        desc.use_opacity_weight    = true;

        const auto data = compute_gaussian_splat_color_lod(cloud, desc);

        ASSERT_TRUE(data.valid());
        ASSERT_EQ(data.splat_count(), 8u);

        // Red splat (#0) should have a neighborhood color where BLUE > RED:
        // its neighbors are mostly blue, only #1 shares its red.
        EXPECT_GT(b_at(data, 0), r_at(data, 0))
            << "Red splat #0 expected blue > red in neighborhood color "
            << "(R=" << r_at(data, 0) << " B=" << b_at(data, 0) << ")";

        // Blue splats #2..#7 should be mostly blue.
        EXPECT_GT(b_at(data, 4), r_at(data, 4));
    }


    // ─── 2. Confidence polarity ───────────────────────────────────────────

    TEST(GaussianSplatColorLODCompute, ConfidencePolarity)
    {
        // Build TWO clusters in separate cells so they don't influence each
        // other.  Cell size will be 0.05; clusters are at x=0 and x=1.0.
        //
        // Cluster A (uniform): 4 white splats at x=0.
        // Cluster B (varied):  4 splats at x=1.0 with high color variance.
        std::vector<GaussianSplat> splats;
        // Uniform white cluster
        for (int i = 0; i < 4; ++i)
            splats.push_back(make_splat(
                0.005f * static_cast<float>(i), 0.0f, 0.0f,
                1.0f, 1.0f, 1.0f));
        // High-variance cluster: red, green, blue, white
        splats.push_back(make_splat(1.000f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f));
        splats.push_back(make_splat(1.005f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f));
        splats.push_back(make_splat(1.010f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
        splats.push_back(make_splat(1.015f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f));
        const auto cloud = make_cloud(std::move(splats));

        GaussianSplatColorLODCompileDesc desc{};
        desc.neighbor_radius_world = 0.05f;
        desc.gaussian_sigma_world  = 0.025f;
        desc.include_self          = true;
        desc.use_opacity_weight    = true;
        desc.color_variance_gain   = 4.0f;

        const auto data = compute_gaussian_splat_color_lod(cloud, desc);
        ASSERT_TRUE(data.valid());

        // Uniform cluster: confidence should be high (close to 1) — all
        // members agree.
        const float conf_uniform = data.lod_confidence[1];

        // High-variance cluster: confidence should be lower than the
        // uniform cluster's confidence.
        const float conf_varied = data.lod_confidence[5];

        EXPECT_GT(conf_uniform, conf_varied)
            << "Uniform-color neighborhood should have HIGHER confidence than "
            << "varied-color neighborhood (uniform=" << conf_uniform
            << " varied=" << conf_varied << ")";

        EXPECT_GE(conf_uniform, 0.95f)
            << "Uniform cluster confidence expected near 1.0, got "
            << conf_uniform;
    }


    // ─── 3. include_self = false ──────────────────────────────────────────

    TEST(GaussianSplatColorLODCompute, IncludeSelfFalseExcludesSelfColor)
    {
        // One red splat surrounded by three blue splats.  With include_self
        // = false, the red splat's neighborhood should be PURE blue (no red
        // contamination from the splat itself).
        std::vector<GaussianSplat> splats;
        splats.push_back(make_splat(0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 0.0f));  // 0: red
        splats.push_back(make_splat(0.01f,  0.0f, 0.0f, 0.0f, 0.0f, 1.0f));  // 1: blue
        splats.push_back(make_splat(0.02f,  0.0f, 0.0f, 0.0f, 0.0f, 1.0f));  // 2: blue
        splats.push_back(make_splat(0.03f,  0.0f, 0.0f, 0.0f, 0.0f, 1.0f));  // 3: blue
        const auto cloud = make_cloud(std::move(splats));

        GaussianSplatColorLODCompileDesc desc{};
        desc.neighbor_radius_world = 0.10f;
        desc.gaussian_sigma_world  = 0.05f;
        desc.use_opacity_weight    = true;

        // include_self = true: red splat keeps some red.
        desc.include_self = true;
        const auto with_self = compute_gaussian_splat_color_lod(cloud, desc);
        ASSERT_TRUE(with_self.valid());
        const float r_with    = r_at(with_self, 0);
        const float b_with    = b_at(with_self, 0);

        // include_self = false: red splat should have ~0 red contribution.
        desc.include_self = false;
        const auto without_self = compute_gaussian_splat_color_lod(cloud, desc);
        ASSERT_TRUE(without_self.valid());
        const float r_without = r_at(without_self, 0);
        const float b_without = b_at(without_self, 0);

        EXPECT_LT(r_without, r_with)
            << "include_self=false should reduce red contribution at the red "
            << "splat (with=" << r_with << " without=" << r_without << ")";

        EXPECT_GT(b_without, b_with - 1e-6f)
            << "include_self=false should keep or increase blue contribution "
            << "(with=" << b_with << " without=" << b_without << ")";

        // And red component without self should be near zero (only blue
        // neighbors contribute).
        EXPECT_LT(r_without, 0.05f);
    }


    // ─── 4. Determinism ───────────────────────────────────────────────────

    TEST(GaussianSplatColorLODCompute, IsDeterministic)
    {
        // Build a cloud where index order differs from spatial proximity so
        // any accidental reordering changes the output.
        std::vector<GaussianSplat> splats;
        splats.push_back(make_splat(0.000f, 0.0f, 0.0f, 0.9f, 0.1f, 0.1f));
        splats.push_back(make_splat(0.020f, 0.0f, 0.0f, 0.1f, 0.9f, 0.1f));
        splats.push_back(make_splat(0.010f, 0.0f, 0.0f, 0.1f, 0.1f, 0.9f));
        splats.push_back(make_splat(0.030f, 0.0f, 0.0f, 0.8f, 0.2f, 0.4f));
        splats.push_back(make_splat(0.005f, 0.0f, 0.0f, 0.2f, 0.5f, 0.8f));
        const auto cloud = make_cloud(std::move(splats));

        GaussianSplatColorLODCompileDesc desc{};
        desc.neighbor_radius_world = 0.05f;
        desc.gaussian_sigma_world  = 0.025f;

        const auto a = compute_gaussian_splat_color_lod(cloud, desc);
        const auto b = compute_gaussian_splat_color_lod(cloud, desc);

        ASSERT_TRUE(a.valid());
        ASSERT_TRUE(b.valid());
        ASSERT_EQ(a.splat_count(), b.splat_count());

        // Bit-exact equality is the determinism contract.
        for (uint32_t i = 0; i < a.splat_count(); ++i)
        {
            EXPECT_FLOAT_EQ(r_at(a, i), r_at(b, i));
            EXPECT_FLOAT_EQ(g_at(a, i), g_at(b, i));
            EXPECT_FLOAT_EQ(b_at(a, i), b_at(b, i));
            EXPECT_FLOAT_EQ(a.lod_confidence[i], b.lod_confidence[i]);
        }
    }


    // ─── 5. Compile stats are populated ───────────────────────────────────

    TEST(GaussianSplatColorLODCompute, StatsArePopulated)
    {
        std::vector<GaussianSplat> splats;
        for (int i = 0; i < 16; ++i)
            splats.push_back(make_splat(
                0.005f * static_cast<float>(i), 0.0f, 0.0f,
                0.5f, 0.5f, 0.5f));
        const auto cloud = make_cloud(std::move(splats));

        GaussianSplatColorLODCompileDesc desc{};
        desc.neighbor_radius_world = 0.02f;
        desc.gaussian_sigma_world  = 0.01f;

        GaussianSplatColorLODCompileStats stats{};
        const auto data = compute_gaussian_splat_color_lod(cloud, desc, &stats);

        ASSERT_TRUE(data.valid());
        EXPECT_EQ(stats.splat_count, 16u);
        EXPECT_GT(stats.occupied_cell_count, 0u);
        EXPECT_GT(stats.max_neighbor_count,  0u);
        EXPECT_GE(stats.compile_time_ms,     0.0);
    }


    // ─── 6. Integration: asset library resolves the LOD asset ─────────────

    namespace stdfs = std::filesystem;

    TEST(GaussianSplatColorLODAssetModule, ResolvesFromProceduralCloud)
    {
        const wz::fs::Path root{
            (stdfs::temp_directory_path()
                / "wz_splat_color_lod_module_tests").string() };
        stdfs::create_directories(root);

        wz::Logger logger;
        wz::gpu::Device null_device{};

        EngineAssetLibrary assets(null_device, logger, root);

        // Source cloud: small procedural debug sphere.
        const auto cloud = assets.gaussian_splats().create_procedural_cloud({
            .name        = "lod_test_cloud",
            .count       = 32,
            .radius      = 1.0f,
            .splat_scale = 0.05f,
            });
        ASSERT_TRUE(cloud.valid());

        // Derived LOD asset.
        GaussianSplatColorLODCompileDesc lod_desc{};
        lod_desc.neighbor_radius_world = 0.5f;
        lod_desc.gaussian_sigma_world  = 0.25f;

        const auto lod = assets.gaussian_splat_color_lods().create_from_cloud({
            .name         = "lod_test_lod",
            .source_cloud = cloud,
            .compile      = lod_desc,
            });
        ASSERT_TRUE(lod.valid());

        ASSERT_TRUE(assets.commit());
        const auto report = assets.resolve_all();
        EXPECT_TRUE(report.ok());
        EXPECT_EQ(report.resolved_count, 2u);  // cloud + lod

        const auto handle = assets.gaussian_splat_color_lods().get_lod(lod);
        ASSERT_TRUE(handle.valid());
        EXPECT_EQ(handle.handle.type, kAssetTypeGaussianSplatColorLOD);

        const auto* data =
            assets.gaussian_splat_color_lods().get_lod_data(handle);
        ASSERT_NE(data, nullptr);
        EXPECT_TRUE(data->valid());
        EXPECT_EQ(data->splat_count(), 32u);
    }
}
