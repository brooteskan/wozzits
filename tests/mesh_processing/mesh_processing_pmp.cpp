#include <gtest/gtest.h>

#include <engine/mesh_processing/mesh_processing.h>

namespace
{
    wz::engine::assets::MeshData make_grid_mesh(
        uint32_t width,
        uint32_t height)
    {
        wz::engine::assets::MeshData mesh{};
        mesh.has_normals = true;
        mesh.has_uv0 = true;

        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                wz::engine::assets::MeshVertex vertex{};
                vertex.position[0] = static_cast<float>(x);
                vertex.position[1] =
                    0.1f * static_cast<float>((x + y) % 3u);
                vertex.position[2] = static_cast<float>(y);
                vertex.normal[1] = 1.0f;
                vertex.uv[0] =
                    static_cast<float>(x) / static_cast<float>(width - 1u);
                vertex.uv[1] =
                    static_cast<float>(y) / static_cast<float>(height - 1u);
                mesh.vertices.push_back(vertex);
            }
        }

        const auto index_of =
            [width](uint32_t x, uint32_t y) noexcept -> uint32_t {
                return y * width + x;
        };

        for (uint32_t y = 0; y + 1u < height; ++y) {
            for (uint32_t x = 0; x + 1u < width; ++x) {
                const uint32_t a = index_of(x, y);
                const uint32_t b = index_of(x + 1u, y);
                const uint32_t c = index_of(x, y + 1u);
                const uint32_t d = index_of(x + 1u, y + 1u);

                mesh.indices.push_back(a);
                mesh.indices.push_back(c);
                mesh.indices.push_back(b);

                mesh.indices.push_back(b);
                mesh.indices.push_back(c);
                mesh.indices.push_back(d);
            }
        }

        return mesh;
    }
}

TEST(MeshProcessingPMP, DecimateMeshReducesGridWithoutProducingHoles)
{
    const wz::engine::assets::MeshData source = make_grid_mesh(16u, 16u);
    ASSERT_TRUE(source.valid());

    const auto result =
        wz::engine::mesh_processing::decimate_mesh(
            source,
            wz::engine::mesh_processing::MeshDecimationDesc{
                .target_ratio = 0.35f,
                .preserve_boundary = true,
            });

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(result.mesh.valid());
    EXPECT_LT(result.mesh.vertex_count(), source.vertex_count());
    EXPECT_LT(result.mesh.index_count(), source.index_count());
    EXPECT_EQ(result.mesh.index_count() % 3u, 0u);
    EXPECT_TRUE(result.mesh.has_normals);
}
