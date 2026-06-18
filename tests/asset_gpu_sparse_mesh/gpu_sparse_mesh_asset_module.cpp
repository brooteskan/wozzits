#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/key_factories/gpu_sparse_mesh.h>
#include <engine/assets/key_factories/mesh_sparse_operator.h>
#include <gpu/gpu.h>

#include <string>

namespace
{
    wz::engine::assets::EngineAssetLibrary make_assets(
        wz::gpu::Device& device,
        wz::Logger& logger,
        const char* suffix)
    {
        const wz::fs::Path root =
            wz::fs::join(
                wz::fs::temp_directory_path(),
                std::string("wozzits_gpu_sparse_mesh_tests_") + suffix);

        EXPECT_EQ(
            wz::fs::create_directories(root),
            wz::fs::FileError::None);

        return wz::engine::assets::EngineAssetLibrary(
            device,
            logger,
            root);
    }
}

TEST(GpuSparseMeshAssetModule, ResolvesSourceMeshViewAndOperator)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets(device, logger, "source_mesh");

    const auto mesh = assets.meshes().create_procedural_mesh({
        .kind = wz::engine::assets::ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());

    const wz::engine::assets::GpuSparseMeshDesc desc{
        .name = "quad sparse mesh",
        .source_mesh = mesh,
    };
    const auto sparse_mesh =
        assets.gpu_sparse_meshes().create_gpu_sparse_mesh(desc);
    ASSERT_TRUE(sparse_mesh.valid());
    EXPECT_EQ(
        sparse_mesh.output,
        wz::engine::assets::make_gpu_sparse_mesh_key(mesh.output, desc));

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto handle =
        assets.gpu_sparse_meshes().get_gpu_sparse_mesh(sparse_mesh);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.gpu_sparse_meshes().get_gpu_sparse_mesh_data(handle);
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->valid());
    EXPECT_EQ(data->source_mesh_key, mesh.output);
    EXPECT_EQ(data->vertex_count, 4u);
    EXPECT_EQ(data->index_count, 6u);
    EXPECT_EQ(data->source_triangle_count, 2u);

    const wz::engine::assets::MeshSparseOperatorDesc operator_desc{
        .source_mesh = mesh,
        .kind = desc.operator_kind,
        .domain = desc.operator_domain,
    };
    EXPECT_EQ(
        data->sparse_operator_key,
        wz::engine::assets::make_mesh_sparse_operator_key(
            mesh.output,
            operator_desc));

    const auto operator_handle =
        assets.mesh_sparse_operators().get_sparse_operator({
            .output = data->sparse_operator_key,
        });
    ASSERT_TRUE(operator_handle.valid());

    const auto* op =
        assets.mesh_sparse_operators().get_sparse_operator_data(
            operator_handle);
    ASSERT_NE(op, nullptr);
    EXPECT_TRUE(op->valid());
    EXPECT_EQ(op->row_count, data->vertex_count);
}

TEST(GpuSparseMeshAssetModule, KeyIncludesOperatorRecipe)
{
    const wz::asset::AssetKey mesh_key{
        .content_hash = { .lo = 1, .hi = 2 },
        .schema_hash = { .lo = 3, .hi = 4 },
        .compiler_hash = { .lo = 5, .hi = 6 },
        .deps_hash = { .lo = 7, .hi = 8 },
    };

    wz::engine::assets::GpuSparseMeshDesc base{
        .source_mesh = { .output = mesh_key },
    };
    wz::engine::assets::GpuSparseMeshDesc changed = base;
    changed.operator_domain = wz::engine::assets::MeshOperatorDomain::Face;

    EXPECT_EQ(
        wz::engine::assets::make_gpu_sparse_mesh_key(mesh_key, base),
        wz::engine::assets::make_gpu_sparse_mesh_key(mesh_key, base));
    EXPECT_NE(
        wz::engine::assets::make_gpu_sparse_mesh_key(mesh_key, base),
        wz::engine::assets::make_gpu_sparse_mesh_key(mesh_key, changed));
}
