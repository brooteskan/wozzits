#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/key_factories/gpu_sparse_mesh.h>
#include <engine/assets/key_factories/mesh_sparse_operator.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/rendering/engine_gpu_context.h>
#include <gpu/compute.h>
#include <gpu/gpu.h>
#include <window/window2.h>

#include <wozzits/rhi/gpu_resource_registry.h>

#include <array>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

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

    wz::engine::assets::EngineAssetLibrary make_assets(
        wz::engine::rendering::EngineGpuContext& gpu,
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
            gpu,
            logger,
            root);
    }

    template <typename T, size_t N>
    void expect_readback_bytes_eq(
        const std::vector<std::byte>& bytes,
        const std::array<T, N>& expected)
    {
        ASSERT_EQ(bytes.size(), sizeof(T) * N);
        EXPECT_EQ(
            std::memcmp(bytes.data(), expected.data(), sizeof(T) * N),
            0);
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

TEST(GpuSparseMeshAssetModule, ResolvePublishesPullBuffersIntoRhiRegistry)
{
    wz::window::WindowDesc window_desc{};
    window_desc.title = "gpu_sparse_mesh_registry_publish_test";
    window_desc.width = 64;
    window_desc.height = 64;
    window_desc.resizable = false;

    wz::window::WindowHandle window =
        wz::window::create_window(window_desc);
    if (!window.valid()) {
        GTEST_SKIP() << "no window available for on-device registry test";
    }

    wz::gpu::Device device = wz::gpu::create_device(window);
    if (!device.valid()) {
        wz::window::destroy_window(window);
        GTEST_SKIP() << "no GPU device available for on-device registry test";
    }

    {
        wz::engine::rendering::EngineGpuContext gpu(device);
        wz::Logger logger;
        auto assets = make_assets(gpu, logger, "rhi_registry_publish");

        const auto mesh = assets.meshes().create_procedural_mesh({
            .kind = wz::engine::assets::ProceduralMeshKind::Quad,
        });
        ASSERT_TRUE(mesh.valid());

        const wz::engine::assets::GpuSparseMeshDesc desc{
            .name = "quad sparse mesh registry publish",
            .source_mesh = mesh,
        };
        const auto sparse_mesh =
            assets.gpu_sparse_meshes().create_gpu_sparse_mesh(desc);
        ASSERT_TRUE(sparse_mesh.valid());

        ASSERT_TRUE(assets.commit());
        ASSERT_TRUE(assets.resolve_all().ok());

        const wz::rhi::ResourceIdentity positions_identity{
            wz::engine::assets::rhi_asset_identity(
                sparse_mesh.output,
                "pull_positions"),
            {},
        };
        const wz::rhi::ResourceIdentity indices_identity{
            wz::engine::assets::rhi_asset_identity(
                sparse_mesh.output,
                "pull_indices"),
            {},
        };

        const wz::rhi::GpuResourceHandle positions =
            gpu.resources.find(positions_identity);
        const wz::rhi::GpuResourceHandle indices =
            gpu.resources.find(indices_identity);
        ASSERT_TRUE(positions.valid());
        ASSERT_TRUE(indices.valid());

        const wz::rhi::GpuResource* positions_resource =
            gpu.resources.get(positions);
        const wz::rhi::GpuResource* indices_resource =
            gpu.resources.get(indices);
        ASSERT_NE(positions_resource, nullptr);
        ASSERT_NE(indices_resource, nullptr);
        EXPECT_EQ(positions_resource->desc.identity, positions_identity);
        EXPECT_EQ(indices_resource->desc.identity, indices_identity);
        EXPECT_EQ(positions_resource->desc.usage,
            wz::rhi::ResourceUsage_Sampled);
        EXPECT_EQ(indices_resource->desc.usage,
            wz::rhi::ResourceUsage_Sampled);
        EXPECT_EQ(positions_resource->desc.cpu_access,
            wz::rhi::ResourceCpuAccess::WriteOnce);
        EXPECT_EQ(indices_resource->desc.cpu_access,
            wz::rhi::ResourceCpuAccess::WriteOnce);
        EXPECT_EQ(positions_resource->desc.stride_bytes, 3u * sizeof(float));
        EXPECT_EQ(indices_resource->desc.stride_bytes, sizeof(uint32_t));

        const wz::gpu::GPUHandle positions_gpu =
            gpu.backend.gpu_handle_for(positions_resource->backend);
        const wz::gpu::GPUHandle indices_gpu =
            gpu.backend.gpu_handle_for(indices_resource->backend);
        ASSERT_TRUE(positions_gpu.valid());
        ASSERT_TRUE(indices_gpu.valid());

        expect_readback_bytes_eq(
            wz::gpu::readback_buffer(device, positions_gpu),
            std::array<float, 12>{
                -1.0f,  1.0f, 0.0f,
                -1.0f, -1.0f, 0.0f,
                 1.0f, -1.0f, 0.0f,
                 1.0f,  1.0f, 0.0f,
            });
        expect_readback_bytes_eq(
            wz::gpu::readback_buffer(device, indices_gpu),
            std::array<uint32_t, 6>{ 0u, 1u, 2u, 0u, 2u, 3u });

        // Per-vertex normals ride a parallel "pull_normals" buffer (the quad's
        // four vertices all face +Z).
        const wz::rhi::ResourceIdentity normals_identity{
            wz::engine::assets::rhi_asset_identity(
                sparse_mesh.output,
                "pull_normals"),
            {},
        };
        const wz::rhi::GpuResourceHandle normals =
            gpu.resources.find(normals_identity);
        ASSERT_TRUE(normals.valid());
        const wz::rhi::GpuResource* normals_resource =
            gpu.resources.get(normals);
        ASSERT_NE(normals_resource, nullptr);
        EXPECT_EQ(normals_resource->desc.identity, normals_identity);
        EXPECT_EQ(normals_resource->desc.usage,
            wz::rhi::ResourceUsage_Sampled);
        EXPECT_EQ(normals_resource->desc.stride_bytes, 3u * sizeof(float));

        const wz::gpu::GPUHandle normals_gpu =
            gpu.backend.gpu_handle_for(normals_resource->backend);
        ASSERT_TRUE(normals_gpu.valid());
        expect_readback_bytes_eq(
            wz::gpu::readback_buffer(device, normals_gpu),
            std::array<float, 12>{
                0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 1.0f,
            });

        gpu.resources.release(positions);
        gpu.resources.release(indices);
        gpu.resources.release(normals);
        gpu.resources.collect(UINT64_MAX);
    }

    wz::gpu::destroy_device(device);
    wz::window::destroy_window(window);
}
