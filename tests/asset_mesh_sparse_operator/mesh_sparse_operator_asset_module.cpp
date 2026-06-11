#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/key_factories/mesh_sparse_operator.h>
#include <gpu/gpu.h>

#include <cmath>
#include <string>
#include <vector>

namespace
{
    wz::engine::assets::EngineAssetLibrary make_assets(
        wz::gpu::Device& device,
        wz::Logger& logger,
        const char* suffix,
        bool disk_cache_enabled)
    {
        const wz::fs::Path root =
            wz::fs::join(
                wz::fs::temp_directory_path(),
                std::string("wozzits_mesh_sparse_operator_tests_") + suffix);

        EXPECT_EQ(
            wz::fs::create_directories(root),
            wz::fs::FileError::None);

        return wz::engine::assets::EngineAssetLibrary(
            device,
            logger,
            root,
            wz::engine::assets::EngineAssetCacheSettings{
                .root = root,
                .enabled = disk_cache_enabled,
            });
    }

    const wz::engine::assets::MeshSparseOperatorData* resolve_operator(
        wz::engine::assets::EngineAssetLibrary& assets,
        const wz::engine::assets::MeshSparseOperatorAsset& asset)
    {
        EXPECT_TRUE(assets.commit());
        EXPECT_TRUE(assets.resolve_all().ok());

        const auto handle =
            assets.mesh_sparse_operators().get_sparse_operator(asset);
        EXPECT_TRUE(handle.valid());
        if (!handle.valid()) {
            return nullptr;
        }
        return assets.mesh_sparse_operators().get_sparse_operator_data(
            handle);
    }

    bool sparsity_has_entry(
        const wz::engine::assets::MeshSparseOperatorData& data,
        uint32_t row,
        uint32_t col)
    {
        for (uint32_t e = data.row_offsets[row];
            e < data.row_offsets[row + 1u];
            ++e)
        {
            if (data.col_indices[e] == col) {
                return true;
            }
        }
        return false;
    }

    float row_weight_sum(
        const wz::engine::assets::MeshSparseOperatorData& data,
        uint32_t row)
    {
        float sum = 0.0f;
        for (uint32_t e = data.row_offsets[row];
            e < data.row_offsets[row + 1u];
            ++e)
        {
            sum += data.weights[e];
        }
        return sum;
    }

    // NeighborWeights application: Lf[i] = f[i] - sum_j w_ij * f[j].
    std::vector<float> apply_laplacian(
        const wz::engine::assets::MeshSparseOperatorData& data,
        const std::vector<float>& field)
    {
        std::vector<float> out(field.size(), 0.0f);
        for (uint32_t row = 0; row < data.row_count; ++row) {
            float smooth = 0.0f;
            for (uint32_t e = data.row_offsets[row];
                e < data.row_offsets[row + 1u];
                ++e)
            {
                smooth += data.weights[e] * field[data.col_indices[e]];
            }
            out[row] = field[row] - smooth;
        }
        return out;
    }
}

TEST(MeshSparseOperatorAssetModule, UniformLaplacianSatisfiesCsrInvariantsOnQuad)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets(device, logger, "quad_invariants", false);

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());

    const auto op = assets.mesh_sparse_operators().create_sparse_operator({
        .name = "quad_laplacian",
        .source_mesh = mesh,
    });
    ASSERT_TRUE(op.valid());

    const auto* data = resolve_operator(assets, op);
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->valid());

    EXPECT_EQ(data->source_mesh_key, mesh.output);
    EXPECT_EQ(data->kind, MeshSparseOperatorKind::UniformVertexLaplacian);
    EXPECT_EQ(data->domain, MeshOperatorDomain::Vertex);
    EXPECT_EQ(
        data->value_convention,
        MeshSparseOperatorValueConvention::NeighborWeights);

    // Quad triangle list {0,1,2, 0,2,3}: diagonal vertices 0 and 2 have
    // degree 3, the others degree 2.
    EXPECT_EQ(data->row_count, 4u);
    EXPECT_EQ(data->nonzero_count, 10u);
    ASSERT_EQ(data->row_offsets.size(), 5u);
    EXPECT_EQ(data->row_offsets.front(), 0u);
    EXPECT_EQ(data->row_offsets.back(), data->nonzero_count);
    const uint32_t expected_degrees[4]{ 3u, 2u, 3u, 2u };
    for (uint32_t row = 0; row < 4u; ++row) {
        EXPECT_EQ(
            data->row_offsets[row + 1u] - data->row_offsets[row],
            expected_degrees[row]);
    }

    // Sorted col indices, no self-entries, symmetric sparsity pattern.
    for (uint32_t row = 0; row < data->row_count; ++row) {
        for (uint32_t e = data->row_offsets[row];
            e < data->row_offsets[row + 1u];
            ++e)
        {
            const uint32_t col = data->col_indices[e];
            EXPECT_NE(col, row);
            EXPECT_TRUE(sparsity_has_entry(*data, col, row));
            if (e + 1u < data->row_offsets[row + 1u]) {
                EXPECT_LT(col, data->col_indices[e + 1u]);
            }
        }
    }

    // Uniform NeighborWeights rows sum to 1; mass diagonal is all 1.
    for (uint32_t row = 0; row < data->row_count; ++row) {
        EXPECT_NEAR(row_weight_sum(*data, row), 1.0f, 1.0e-6f);
    }
    ASSERT_EQ(data->vertex_mass.size(), data->row_count);
    for (const float mass : data->vertex_mass) {
        EXPECT_FLOAT_EQ(mass, 1.0f);
    }
}

TEST(MeshSparseOperatorAssetModule, NeighborWeightsLaplacianAnnihilatesConstantField)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets(device, logger, "constant_field", false);

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto op = assets.mesh_sparse_operators().create_sparse_operator({
        .name = "cube_laplacian",
        .source_mesh = mesh,
    });
    ASSERT_TRUE(op.valid());

    const auto* data = resolve_operator(assets, op);
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->row_count, 8u);

    const std::vector<float> constant(data->row_count, 0.75f);
    const std::vector<float> residual = apply_laplacian(*data, constant);
    for (const float value : residual) {
        EXPECT_NEAR(value, 0.0f, 1.0e-6f);
    }

    // A non-constant field has non-zero detail somewhere.
    std::vector<float> varying(data->row_count, 0.0f);
    varying[0] = 1.0f;
    const std::vector<float> detail = apply_laplacian(*data, varying);
    float max_abs = 0.0f;
    for (const float value : detail) {
        max_abs = (std::max)(max_abs, std::abs(value));
    }
    EXPECT_GT(max_abs, 0.0f);
}

TEST(MeshSparseOperatorAssetModule, IdentityIncludesKindDomainAndMeshDeps)
{
    using namespace wz::engine::assets;

    const MeshAsset mesh{
        .output = wz::asset::AssetKey{
            .content_hash = { 1u, 2u },
            .schema_hash = { 3u, 4u },
            .compiler_hash = { 5u, 6u },
            .deps_hash = { 7u, 8u },
        },
    };

    const MeshSparseOperatorDesc first{
        .name = "op",
        .source_mesh = mesh,
        .kind = MeshSparseOperatorKind::UniformVertexLaplacian,
    };
    MeshSparseOperatorDesc second = first;
    // Same mesh, different kind -> different keys, cacheable side by side
    // (no second kind compiles yet, so identity is checked at key level).
    MeshSparseOperatorDesc changed_kind = first;
    changed_kind.kind = static_cast<MeshSparseOperatorKind>(1);

    const auto base_key = make_mesh_sparse_operator_key(mesh.output, first);
    EXPECT_EQ(
        base_key,
        make_mesh_sparse_operator_key(mesh.output, second));
    EXPECT_NE(
        base_key,
        make_mesh_sparse_operator_key(mesh.output, changed_kind));

    wz::asset::AssetKey other_mesh_key = mesh.output;
    other_mesh_key.content_hash = { 77u, 78u };
    const auto changed_mesh_key =
        make_mesh_sparse_operator_key(other_mesh_key, first);
    EXPECT_NE(base_key, changed_mesh_key);
    EXPECT_EQ(base_key.content_hash, changed_mesh_key.content_hash);
}

TEST(MeshSparseOperatorAssetModule, RejectsUnsupportedDomain)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets(device, logger, "bad_domain", false);

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());

    EXPECT_FALSE(assets.mesh_sparse_operators().create_sparse_operator({
        .name = "face_domain",
        .source_mesh = mesh,
        .domain = MeshOperatorDomain::Face,
    }).valid());
    EXPECT_FALSE(assets.mesh_sparse_operators().create_sparse_operator({
        .name = "no_mesh",
        .source_mesh = {},
    }).valid());
}

TEST(MeshSparseOperatorAssetModule, SecondResolveIsDiskCacheHit)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    const char* suffix = "disk_cache_hit";

    std::vector<uint32_t> first_offsets;
    std::vector<uint32_t> first_cols;
    std::vector<float> first_weights;
    {
        auto assets = make_assets(device, logger, suffix, true);
        const auto mesh = assets.meshes().create_procedural_mesh({
            .name = "cube",
            .kind = ProceduralMeshKind::Cube,
        });
        ASSERT_TRUE(mesh.valid());
        const auto op =
            assets.mesh_sparse_operators().create_sparse_operator({
                .name = "cube_laplacian",
                .source_mesh = mesh,
            });
        ASSERT_TRUE(op.valid());

        const auto* data = resolve_operator(assets, op);
        ASSERT_NE(data, nullptr);
        first_offsets = data->row_offsets;
        first_cols = data->col_indices;
        first_weights = data->weights;
    }

    // Demand-resolve from the disk cache: a hit short-circuits the compile,
    // so the source mesh never needs to be compiled.
    wz::Logger cached_logger;
    wz::gpu::Device cached_device{};
    auto cached_assets =
        make_assets(cached_device, cached_logger, suffix, true);
    const auto cached_mesh = cached_assets.meshes().create_procedural_mesh({
        .name = "cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(cached_mesh.valid());
    const auto cached_op =
        cached_assets.mesh_sparse_operators().create_sparse_operator({
            .name = "cube_laplacian",
            .source_mesh = cached_mesh,
        });
    ASSERT_TRUE(cached_op.valid());
    ASSERT_TRUE(cached_assets.system().register_demand_root(
        wz::asset::DemandRoot::GPURuntime,
        { cached_op.output }));

    ASSERT_TRUE(cached_assets.commit());
    const auto report = cached_assets.resolve_demanded(
        wz::asset::ResolvePolicy::CachePreferred);
    ASSERT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 1u);
    EXPECT_FALSE(cached_assets.meshes().get_mesh(cached_mesh).valid());

    const auto* data =
        cached_assets.mesh_sparse_operators().get_sparse_operator_data(
            cached_assets.mesh_sparse_operators().get_sparse_operator(
                cached_op));
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->row_offsets, first_offsets);
    EXPECT_EQ(data->col_indices, first_cols);
    EXPECT_EQ(data->weights, first_weights);
}

TEST(MeshSparseOperatorAssetModule, DegenerateTrianglesAndIsolatedVerticesAreFinite)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets(device, logger, "degenerate", false);

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    // Replace the second triangle with a fully degenerate one: vertex 3
    // becomes isolated, and the degenerate triangle must contribute no
    // edges (in particular no self-edges).
    const auto* const_mesh_data =
        assets.meshes().get_mesh_data(assets.meshes().get_mesh(mesh));
    ASSERT_NE(const_mesh_data, nullptr);
    auto* mesh_data = const_cast<MeshData*>(const_mesh_data);
    mesh_data->indices = { 0u, 1u, 2u, 2u, 2u, 2u };

    const auto op = assets.mesh_sparse_operators().create_sparse_operator({
        .name = "degenerate_laplacian",
        .source_mesh = mesh,
    });
    ASSERT_TRUE(op.valid());

    const auto* data = resolve_operator(assets, op);
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->valid());
    EXPECT_EQ(data->row_count, 4u);
    EXPECT_EQ(data->nonzero_count, 6u);

    // Vertex 3 is isolated: empty row, finite (zero) row sum. Connected
    // rows still sum to 1, and every weight is finite.
    EXPECT_EQ(data->row_offsets[4u] - data->row_offsets[3u], 0u);
    for (uint32_t row = 0; row < 3u; ++row) {
        EXPECT_NEAR(row_weight_sum(*data, row), 1.0f, 1.0e-6f);
    }
    for (const float weight : data->weights) {
        EXPECT_TRUE(std::isfinite(weight));
    }
}
