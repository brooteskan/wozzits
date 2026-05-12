// tests/gpu/gpu_mesh.cpp

#include <gtest/gtest.h>

#include <gpu/mesh.h>
#include <gpu/dx12/dx12_internal.h>

#include <engine/assets/mesh/mesh.h>

namespace
{
    wz::engine::assets::MeshData make_test_triangle_mesh()
    {
        using wz::engine::assets::MeshData;
        using wz::engine::assets::MeshIndexFormat;
        using wz::engine::assets::MeshPrimitiveTopology;
        using wz::engine::assets::MeshVertex;

        MeshData mesh{};
        mesh.topology = MeshPrimitiveTopology::TriangleList;
        mesh.index_format = MeshIndexFormat::UInt32;

        MeshVertex a{};
        a.position[0] = 0.0f;
        a.position[1] = 1.0f;
        a.position[2] = 0.0f;
        a.normal[2] = 1.0f;
        a.uv[0] = 0.5f;
        a.uv[1] = 0.0f;

        MeshVertex b{};
        b.position[0] = -1.0f;
        b.position[1] = -1.0f;
        b.position[2] = 0.0f;
        b.normal[2] = 1.0f;
        b.uv[0] = 0.0f;
        b.uv[1] = 1.0f;

        MeshVertex c{};
        c.position[0] = 1.0f;
        c.position[1] = -1.0f;
        c.position[2] = 0.0f;
        c.normal[2] = 1.0f;
        c.uv[0] = 1.0f;
        c.uv[1] = 1.0f;

        mesh.vertices = { a, b, c };
        mesh.indices = { 0u, 1u, 2u };

        return mesh;
    }
}

TEST(GPUMeshUploadDesc, NullMeshIsInvalid)
{
    wz::gpu::MeshUploadDesc desc{};

    EXPECT_FALSE(desc.valid());
}

TEST(GPUMeshUploadDesc, ValidMeshDataIsValid)
{
    const auto mesh = make_test_triangle_mesh();

    wz::gpu::MeshUploadDesc desc{
        .mesh = &mesh,
    };

    EXPECT_TRUE(desc.valid());
}

TEST(GPUMeshUploadDesc, EmptyMeshDataIsInvalid)
{
    wz::engine::assets::MeshData mesh{};

    wz::gpu::MeshUploadDesc desc{
        .mesh = &mesh,
    };

    EXPECT_FALSE(desc.valid());
}

TEST(DX12MeshTable, NullHandleDoesNotResolve)
{
    wz::gpu::dx12::internal::DX12MeshTable table{};

    wz::gpu::GPUHandle handle{};

    EXPECT_FALSE(handle.valid());
    EXPECT_EQ(table.get(handle), nullptr);
}

TEST(DX12MeshTable, AddReturnsValidMeshHandle)
{
    wz::gpu::dx12::internal::DX12MeshTable table{};

    wz::gpu::dx12::internal::DX12MeshResource mesh{};
    mesh.vertex_count = 3;
    mesh.index_count = 3;

    const wz::gpu::GPUHandle handle = table.add(mesh);

    EXPECT_TRUE(handle.valid());
    EXPECT_NE(handle.id, 0u);
    EXPECT_NE(handle.epoch, 0u);
    EXPECT_EQ(handle.type, wz::gpu::GPUResourceType::Mesh);
}

TEST(DX12MeshTable, AddedMeshCanBeResolved)
{
    wz::gpu::dx12::internal::DX12MeshTable table{};

    wz::gpu::dx12::internal::DX12MeshResource mesh{};
    mesh.vertex_count = 3;
    mesh.index_count = 3;

    const wz::gpu::GPUHandle handle = table.add(mesh);
    const wz::gpu::dx12::internal::DX12MeshResource* resolved =
        table.get(handle);

    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->vertex_count, 3u);
    EXPECT_EQ(resolved->index_count, 3u);
}

TEST(DX12MeshTable, WrongTypeHandleDoesNotResolve)
{
    wz::gpu::dx12::internal::DX12MeshTable table{};

    wz::gpu::dx12::internal::DX12MeshResource mesh{};
    mesh.vertex_count = 3;
    mesh.index_count = 3;

    wz::gpu::GPUHandle handle = table.add(mesh);
    ASSERT_TRUE(handle.valid());

    handle.type = wz::gpu::GPUResourceType::Texture;

    EXPECT_EQ(table.get(handle), nullptr);
}

TEST(DX12MeshTable, OutOfRangeHandleDoesNotResolve)
{
    wz::gpu::dx12::internal::DX12MeshTable table{};

    wz::gpu::GPUHandle handle{};
    handle.id = 999u;
    handle.epoch = 1u;
    handle.type = wz::gpu::GPUResourceType::Mesh;

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(table.get(handle), nullptr);
}

TEST(DX12MeshTable, StaleEpochHandleDoesNotResolve)
{
    wz::gpu::dx12::internal::DX12MeshTable table{};

    wz::gpu::dx12::internal::DX12MeshResource mesh{};
    mesh.vertex_count = 3;
    mesh.index_count = 3;

    wz::gpu::GPUHandle handle = table.add(mesh);
    ASSERT_TRUE(handle.valid());

    handle.epoch += 1;

    EXPECT_EQ(table.get(handle), nullptr);
}

TEST(DX12MeshTable, DestroyInvalidatesExistingHandles)
{
    wz::gpu::dx12::internal::DX12MeshTable table{};

    wz::gpu::dx12::internal::DX12MeshResource mesh{};
    mesh.vertex_count = 3;
    mesh.index_count = 3;

    const wz::gpu::GPUHandle handle = table.add(mesh);
    ASSERT_NE(table.get(handle), nullptr);

    table.destroy();

    EXPECT_EQ(table.get(handle), nullptr);
}

TEST(DX12MeshTable, CanAddAfterDestroy)
{
    wz::gpu::dx12::internal::DX12MeshTable table{};

    wz::gpu::dx12::internal::DX12MeshResource first{};
    first.vertex_count = 3;
    first.index_count = 3;

    const wz::gpu::GPUHandle first_handle = table.add(first);
    ASSERT_NE(table.get(first_handle), nullptr);

    table.destroy();

    wz::gpu::dx12::internal::DX12MeshResource second{};
    second.vertex_count = 4;
    second.index_count = 6;

    const wz::gpu::GPUHandle second_handle = table.add(second);
    const wz::gpu::dx12::internal::DX12MeshResource* resolved =
        table.get(second_handle);

    ASSERT_TRUE(second_handle.valid());
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->vertex_count, 4u);
    EXPECT_EQ(resolved->index_count, 6u);
}