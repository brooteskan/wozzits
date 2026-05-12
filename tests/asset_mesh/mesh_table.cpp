// tests/asset/mesh_tests.cpp

#include <gtest/gtest.h>

#include <engine/assets/mesh/mesh.h>

namespace
{
    using wz::asset::ResourceHandle;

    using wz::engine::assets::MeshData;
    using wz::engine::assets::MeshIndexFormat;
    using wz::engine::assets::MeshPrimitiveTopology;
    using wz::engine::assets::MeshTable;
    using wz::engine::assets::MeshVertex;

    MeshData make_test_triangle_mesh()
    {
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

TEST(MeshData, DefaultConstructedMeshIsInvalid)
{
    MeshData mesh{};

    EXPECT_FALSE(mesh.valid());
    EXPECT_EQ(mesh.vertex_count(), 0u);
    EXPECT_EQ(mesh.index_count(), 0u);
}

TEST(MeshData, TriangleMeshIsValid)
{
    MeshData mesh = make_test_triangle_mesh();

    EXPECT_TRUE(mesh.valid());
    EXPECT_EQ(mesh.vertex_count(), 3u);
    EXPECT_EQ(mesh.index_count(), 3u);
    EXPECT_EQ(mesh.topology, MeshPrimitiveTopology::TriangleList);
    EXPECT_EQ(mesh.index_format, MeshIndexFormat::UInt32);
}

TEST(MeshData, MeshWithVerticesButNoIndicesIsInvalid)
{
    MeshData mesh = make_test_triangle_mesh();
    mesh.indices.clear();

    EXPECT_FALSE(mesh.valid());
    EXPECT_EQ(mesh.vertex_count(), 3u);
    EXPECT_EQ(mesh.index_count(), 0u);
}

TEST(MeshData, MeshWithNonTriangleIndexCountIsInvalid)
{
    MeshData mesh = make_test_triangle_mesh();
    mesh.indices.push_back(0u);

    EXPECT_FALSE(mesh.valid());
    EXPECT_EQ(mesh.vertex_count(), 3u);
    EXPECT_EQ(mesh.index_count(), 4u);
}

TEST(MeshTable, NullHandleDoesNotResolve)
{
    MeshTable table{};

    ResourceHandle null_handle{};

    EXPECT_FALSE(null_handle.valid());
    EXPECT_EQ(table.get(null_handle), nullptr);
}

TEST(MeshTable, AddReturnsValidHandle)
{
    MeshTable table{};

    ResourceHandle handle = table.add(make_test_triangle_mesh());

    EXPECT_TRUE(handle.valid());
    EXPECT_NE(handle.id, 0u);
}

TEST(MeshTable, AddedMeshCanBeResolved)
{
    MeshTable table{};

    ResourceHandle handle = table.add(make_test_triangle_mesh());
    const MeshData* mesh = table.get(handle);

    ASSERT_NE(mesh, nullptr);
    EXPECT_TRUE(mesh->valid());
    EXPECT_EQ(mesh->vertex_count(), 3u);
    EXPECT_EQ(mesh->index_count(), 3u);

    EXPECT_FLOAT_EQ(mesh->vertices[0].position[0], 0.0f);
    EXPECT_FLOAT_EQ(mesh->vertices[0].position[1], 1.0f);
    EXPECT_FLOAT_EQ(mesh->vertices[0].position[2], 0.0f);

    EXPECT_EQ(mesh->indices[0], 0u);
    EXPECT_EQ(mesh->indices[1], 1u);
    EXPECT_EQ(mesh->indices[2], 2u);
}

TEST(MeshTable, InvalidOutOfRangeHandleDoesNotResolve)
{
    MeshTable table{};

    ResourceHandle handle{};
    handle.id = 999u;
    handle.epoch = 1u;

    EXPECT_TRUE(handle.valid());
    EXPECT_EQ(table.get(handle), nullptr);
}

TEST(MeshTable, StaleHandleAfterDestroyDoesNotResolve)
{
    MeshTable table{};

    ResourceHandle handle = table.add(make_test_triangle_mesh());
    ASSERT_NE(table.get(handle), nullptr);

    table.destroy();

    EXPECT_EQ(table.get(handle), nullptr);
}

TEST(MeshTable, CanAddAfterDestroy)
{
    MeshTable table{};

    ResourceHandle first = table.add(make_test_triangle_mesh());
    ASSERT_NE(table.get(first), nullptr);

    table.destroy();

    ResourceHandle second = table.add(make_test_triangle_mesh());
    const MeshData* mesh = table.get(second);

    EXPECT_TRUE(second.valid());
    ASSERT_NE(mesh, nullptr);
    EXPECT_TRUE(mesh->valid());
    EXPECT_EQ(mesh->vertex_count(), 3u);
    EXPECT_EQ(mesh->index_count(), 3u);
}

TEST(MeshTable, MutableAccessForTestsReturnsStoredMesh)
{
    MeshTable table{};

    ResourceHandle handle = table.add(make_test_triangle_mesh());
    MeshData* mesh = table.get_mutable_for_tests(handle);

    ASSERT_NE(mesh, nullptr);
    ASSERT_EQ(mesh->vertices.size(), 3u);

    mesh->vertices[0].position[0] = 42.0f;

    const MeshData* resolved = table.get(handle);
    ASSERT_NE(resolved, nullptr);
    EXPECT_FLOAT_EQ(resolved->vertices[0].position[0], 42.0f);
}