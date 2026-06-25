// tests/asset_mesh/mesh_asset_module.cpp

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/mesh/clipmap_lattice_mesh.h>
#include <engine/assets/scalar_field_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <asset/compiler.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace
{
    wz::engine::assets::EngineAssetLibrary make_assets(
        wz::gpu::Device& device,
        wz::Logger& logger)
    {
        const wz::fs::Path root =
            wz::fs::join(
                wz::fs::temp_directory_path(),
                "wozzits_mesh_asset_tests");

        EXPECT_EQ(
            wz::fs::create_directories(root),
            wz::fs::FileError::None);

        return wz::engine::assets::EngineAssetLibrary(
            device,
            logger,
            root);
    }

    bool mesh_referenced_vertices_are_connected(
        const wz::engine::assets::MeshData& mesh)
    {
        if (!mesh.valid()) {
            return false;
        }

        std::vector<std::vector<uint32_t>> adjacency(mesh.vertex_count());
        std::vector<bool> referenced(mesh.vertex_count(), false);
        for (uint32_t i = 0; i + 2u < mesh.index_count(); i += 3u) {
            const uint32_t a = mesh.indices[i + 0u];
            const uint32_t b = mesh.indices[i + 1u];
            const uint32_t c = mesh.indices[i + 2u];
            if (a >= mesh.vertex_count()
                || b >= mesh.vertex_count()
                || c >= mesh.vertex_count())
            {
                return false;
            }
            referenced[a] = true;
            referenced[b] = true;
            referenced[c] = true;
            adjacency[a].push_back(b);
            adjacency[a].push_back(c);
            adjacency[b].push_back(a);
            adjacency[b].push_back(c);
            adjacency[c].push_back(a);
            adjacency[c].push_back(b);
        }

        uint32_t start = mesh.vertex_count();
        for (uint32_t v = 0; v < mesh.vertex_count(); ++v) {
            if (referenced[v]) {
                start = v;
                break;
            }
        }
        if (start == mesh.vertex_count()) {
            return false;
        }

        std::vector<bool> visited(mesh.vertex_count(), false);
        std::vector<uint32_t> stack{ start };
        visited[start] = true;
        while (!stack.empty()) {
            const uint32_t v = stack.back();
            stack.pop_back();
            for (const uint32_t next : adjacency[v]) {
                if (!visited[next]) {
                    visited[next] = true;
                    stack.push_back(next);
                }
            }
        }

        for (uint32_t v = 0; v < mesh.vertex_count(); ++v) {
            if (referenced[v] && !visited[v]) {
                return false;
            }
        }
        return true;
    }

    std::vector<std::byte> float_field_bytes(
        std::initializer_list<float> values)
    {
        std::vector<std::byte> bytes(values.size() * sizeof(float));
        std::memcpy(bytes.data(), values.begin(), bytes.size());
        return bytes;
    }
}

TEST(MeshAssetModule, ResolvesProceduralTriangleMesh)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "triangle",
        .kind = wz::engine::assets::ProceduralMeshKind::Triangle,
        });

    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 1u);

    const auto handle = assets.meshes().get_mesh(mesh);

    ASSERT_TRUE(handle.valid());

    const auto* data = assets.meshes().get_mesh_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->vertex_count(), 3u);
    EXPECT_EQ(data->index_count(), 3u);
    EXPECT_TRUE(data->has_normals);
    EXPECT_TRUE(data->has_uv0);
}

TEST(MeshAssetModule, ResolvesProceduralQuadMesh)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = wz::engine::assets::ProceduralMeshKind::Quad,
        });

    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 1u);

    const auto handle = assets.meshes().get_mesh(mesh);

    ASSERT_TRUE(handle.valid());

    const auto* data = assets.meshes().get_mesh_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->vertex_count(), 4u);
    EXPECT_EQ(data->index_count(), 6u);
    EXPECT_TRUE(data->has_normals);
    EXPECT_TRUE(data->has_uv0);
}

TEST(MeshAssetModule, ResolvesProceduralCubeMesh)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "cube",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });

    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 1u);

    const auto handle = assets.meshes().get_mesh(mesh);

    ASSERT_TRUE(handle.valid());

    const auto* data = assets.meshes().get_mesh_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->vertex_count(), 8u);
    EXPECT_EQ(data->index_count(), 36u);
}

TEST(MeshAssetModule, ResolvesDecimatedMeshAsset)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto source = assets.meshes().create_procedural_mesh({
        .name = "cube_source",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(source.valid());

    const auto decimated = assets.meshes().create_decimated_mesh({
        .name = "cube_decimated",
        .source_mesh = source,
        .target_vertex_count = 6u,
        .preserve_boundary = true,
        });
    ASSERT_TRUE(decimated.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto source_handle = assets.meshes().get_mesh(source);
    const auto decimated_handle = assets.meshes().get_mesh(decimated);
    ASSERT_TRUE(source_handle.valid());
    ASSERT_TRUE(decimated_handle.valid());

    const auto* source_data = assets.meshes().get_mesh_data(source_handle);
    const auto* decimated_data =
        assets.meshes().get_mesh_data(decimated_handle);

    ASSERT_NE(source_data, nullptr);
    ASSERT_NE(decimated_data, nullptr);
    EXPECT_TRUE(decimated_data->valid());
    EXPECT_LE(decimated_data->vertex_count(), source_data->vertex_count());
    EXPECT_LE(decimated_data->index_count(), source_data->index_count());
}

TEST(MeshAssetModule, ResolvesDebugTriangleStrideMeshAsset)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto source = assets.meshes().create_procedural_mesh({
        .name = "quad_source",
        .kind = wz::engine::assets::ProceduralMeshKind::Quad,
        });
    ASSERT_TRUE(source.valid());

    const auto strided =
        assets.meshes().create_debug_triangle_stride_mesh({
            .name = "quad_stride",
            .source_mesh = source,
            .stride = 2u,
            .phase = 0u,
        });
    ASSERT_TRUE(strided.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto source_handle = assets.meshes().get_mesh(source);
    const auto strided_handle = assets.meshes().get_mesh(strided);
    ASSERT_TRUE(source_handle.valid());
    ASSERT_TRUE(strided_handle.valid());

    const auto* source_data = assets.meshes().get_mesh_data(source_handle);
    const auto* strided_data = assets.meshes().get_mesh_data(strided_handle);

    ASSERT_NE(source_data, nullptr);
    ASSERT_NE(strided_data, nullptr);
    EXPECT_TRUE(strided_data->valid());
    EXPECT_EQ(source_data->index_count(), 6u);
    EXPECT_EQ(strided_data->index_count(), 3u);
    EXPECT_EQ(strided_data->vertex_count(), 3u);
}

TEST(MeshAssetModule, ResolvesIdentityMeshClusterHierarchyAsset)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto source = assets.meshes().create_procedural_mesh({
        .name = "cube_source",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(source.valid());

    const auto hierarchy =
        assets.mesh_cluster_hierarchies().create_mesh_cluster_hierarchy({
            .name = "cube_identity_hierarchy",
            .source_mesh = source,
            .method =
                wz::engine::assets::MeshClusterHierarchyBuildMethod::Identity,
        });
    ASSERT_TRUE(hierarchy.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto source_handle = assets.meshes().get_mesh(source);
    const auto hierarchy_handle =
        assets.mesh_cluster_hierarchies().get_mesh_cluster_hierarchy(
            hierarchy);
    ASSERT_TRUE(source_handle.valid());
    ASSERT_TRUE(hierarchy_handle.valid());

    const auto* source_data = assets.meshes().get_mesh_data(source_handle);
    const auto* hierarchy_data =
        assets.mesh_cluster_hierarchies()
            .get_mesh_cluster_hierarchy_data(hierarchy_handle);

    ASSERT_NE(source_data, nullptr);
    ASSERT_NE(hierarchy_data, nullptr);
    ASSERT_TRUE(hierarchy_data->valid());
    EXPECT_EQ(hierarchy_data->source_mesh_key, source.output);
    EXPECT_EQ(
        hierarchy_data->method,
        wz::engine::assets::MeshClusterHierarchyBuildMethod::Identity);
    ASSERT_EQ(hierarchy_data->level_count(), 1u);

    const auto& level = hierarchy_data->levels[0];
    EXPECT_EQ(level.level_index, 0u);
    EXPECT_EQ(level.cluster_count, 1u);
    EXPECT_EQ(level.vertex_count, source_data->vertex_count());
    EXPECT_EQ(level.triangle_count, source_data->index_count() / 3u);
    EXPECT_FLOAT_EQ(level.conservative_error, 0.0f);
    EXPECT_EQ(level.preview_mesh.vertex_count(), source_data->vertex_count());
    EXPECT_EQ(level.preview_mesh.index_count(), source_data->index_count());
}

TEST(MeshAssetModule, ResolvesGraphCoarsenMeshClusterHierarchyAsset)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto source = assets.meshes().create_procedural_mesh({
        .name = "cube_source",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(source.valid());

    const auto hierarchy =
        assets.mesh_cluster_hierarchies().create_mesh_cluster_hierarchy({
            .name = "cube_graph_hierarchy",
            .source_mesh = source,
            .method =
                wz::engine::assets::MeshClusterHierarchyBuildMethod::
                    GraphCoarsen,
        });
    ASSERT_TRUE(hierarchy.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto source_handle = assets.meshes().get_mesh(source);
    const auto hierarchy_handle =
        assets.mesh_cluster_hierarchies().get_mesh_cluster_hierarchy(
            hierarchy);
    ASSERT_TRUE(source_handle.valid());
    ASSERT_TRUE(hierarchy_handle.valid());

    const auto* source_data = assets.meshes().get_mesh_data(source_handle);
    const auto* hierarchy_data =
        assets.mesh_cluster_hierarchies()
            .get_mesh_cluster_hierarchy_data(hierarchy_handle);

    ASSERT_NE(source_data, nullptr);
    ASSERT_NE(hierarchy_data, nullptr);
    ASSERT_TRUE(hierarchy_data->valid());
    EXPECT_EQ(
        hierarchy_data->method,
        wz::engine::assets::MeshClusterHierarchyBuildMethod::GraphCoarsen);
    ASSERT_GE(hierarchy_data->level_count(), 2u);
    EXPECT_EQ(
        hierarchy_data->levels[0].triangle_count,
        source_data->index_count() / 3u);
    EXPECT_LT(
        hierarchy_data->levels[1].triangle_count,
        hierarchy_data->levels[0].triangle_count);
    EXPECT_LT(
        hierarchy_data->levels[1].vertex_count,
        hierarchy_data->levels[0].vertex_count);
    EXPECT_GT(hierarchy_data->levels[1].conservative_error, 0.0f);
    EXPECT_TRUE(mesh_referenced_vertices_are_connected(
        hierarchy_data->levels[1].preview_mesh));
}

TEST(MeshAssetModule, GraphCoarsenMeshClusterHierarchyUsesRegionMask)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto source = assets.meshes().create_procedural_mesh({
        .name = "cube_source",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(source.valid());

    const auto mask_field =
        assets.mesh_derived_fields().create_explicit_field({
            .name = "partial_vertex_mask",
            .source_mesh = source,
            .domain = wz::engine::assets::MeshDerivedFieldDomain::Vertex,
            .element_count = 8u,
            .channels = {
                wz::engine::assets::MeshDerivedFieldChannelDesc{
                    .channel_id = 0x3300u,
                    .value_type =
                        wz::engine::assets::MeshDerivedFieldValueType::
                            Float1,
                    .values = float_field_bytes({
                        1.0f, 1.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 0.0f, 0.0f,
                    }),
                },
            },
        });
    ASSERT_TRUE(mask_field.valid());

    const auto unmasked =
        assets.mesh_cluster_hierarchies().create_mesh_cluster_hierarchy({
            .name = "cube_unmasked_graph_hierarchy",
            .source_mesh = source,
            .method =
                wz::engine::assets::MeshClusterHierarchyBuildMethod::
                    GraphCoarsen,
        });
    ASSERT_TRUE(unmasked.valid());

    const auto masked =
        assets.mesh_cluster_hierarchies().create_mesh_cluster_hierarchy({
            .name = "cube_masked_graph_hierarchy",
            .source_mesh = source,
            .method =
                wz::engine::assets::MeshClusterHierarchyBuildMethod::
                    GraphCoarsen,
            .region_mask =
                wz::engine::assets::MeshClusterHierarchyRegionMaskDesc{
                    .field = mask_field,
                    .mask = wz::engine::assets::MeshMaskRenderStyleData{
                        .enabled = true,
                        .domain =
                            wz::engine::assets::MeshMaskDomain::Vertex,
                        .projection_mode =
                            wz::engine::assets::MeshMaskProjectionMode::
                                Direct,
                        .overlap_mode =
                            wz::engine::assets::MeshMaskOverlapMode::
                                Priority,
                        .rules = {
                            wz::engine::assets::MeshMaskRule{
                                .input_channel_id = 0x3300u,
                                .lo = 0.5f,
                                .hi = 1.5f,
                            },
                        },
                    },
                },
        });
    ASSERT_TRUE(masked.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto unmasked_handle =
        assets.mesh_cluster_hierarchies().get_mesh_cluster_hierarchy(
            unmasked);
    const auto masked_handle =
        assets.mesh_cluster_hierarchies().get_mesh_cluster_hierarchy(
            masked);
    ASSERT_TRUE(unmasked_handle.valid());
    ASSERT_TRUE(masked_handle.valid());

    const auto* unmasked_data =
        assets.mesh_cluster_hierarchies()
            .get_mesh_cluster_hierarchy_data(unmasked_handle);
    const auto* masked_data =
        assets.mesh_cluster_hierarchies()
            .get_mesh_cluster_hierarchy_data(masked_handle);
    ASSERT_NE(unmasked_data, nullptr);
    ASSERT_NE(masked_data, nullptr);
    ASSERT_GE(unmasked_data->level_count(), 2u);
    ASSERT_GE(masked_data->level_count(), 2u);

    EXPECT_LT(
        masked_data->levels[1].vertex_count,
        masked_data->levels[0].vertex_count);
    EXPECT_GT(
        masked_data->levels[1].vertex_count,
        unmasked_data->levels[1].vertex_count);
}

TEST(MeshAssetModule, ResolvesLaplacianGraphCellsMeshClusterHierarchyAsset)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto source = assets.meshes().create_procedural_mesh({
        .name = "cube_source",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(source.valid());

    const auto hierarchy =
        assets.mesh_cluster_hierarchies().create_mesh_cluster_hierarchy({
            .name = "cube_graph_cells_hierarchy",
            .source_mesh = source,
            .method =
                wz::engine::assets::MeshClusterHierarchyBuildMethod::
                    LaplacianGraphCells,
        });
    ASSERT_TRUE(hierarchy.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto source_handle = assets.meshes().get_mesh(source);
    const auto hierarchy_handle =
        assets.mesh_cluster_hierarchies().get_mesh_cluster_hierarchy(
            hierarchy);
    ASSERT_TRUE(source_handle.valid());
    ASSERT_TRUE(hierarchy_handle.valid());

    const auto* source_data = assets.meshes().get_mesh_data(source_handle);
    const auto* hierarchy_data =
        assets.mesh_cluster_hierarchies()
            .get_mesh_cluster_hierarchy_data(hierarchy_handle);

    ASSERT_NE(source_data, nullptr);
    ASSERT_NE(hierarchy_data, nullptr);
    ASSERT_TRUE(hierarchy_data->valid());
    EXPECT_EQ(
        hierarchy_data->method,
        wz::engine::assets::MeshClusterHierarchyBuildMethod::
            LaplacianGraphCells);
    ASSERT_GE(hierarchy_data->level_count(), 2u);
    EXPECT_EQ(
        hierarchy_data->levels[0].triangle_count,
        source_data->index_count() / 3u);
    EXPECT_LT(
        hierarchy_data->levels[1].triangle_count,
        hierarchy_data->levels[0].triangle_count);
    EXPECT_LT(
        hierarchy_data->levels[1].vertex_count,
        hierarchy_data->levels[0].vertex_count);
    EXPECT_GT(hierarchy_data->levels[1].conservative_error, 0.0f);
    EXPECT_TRUE(mesh_referenced_vertices_are_connected(
        hierarchy_data->levels[1].preview_mesh));
}

TEST(MeshAssetModule, ResolvesMeshClusterHierarchyPreviewMesh)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto source = assets.meshes().create_procedural_mesh({
        .name = "cube_source",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(source.valid());

    const auto hierarchy =
        assets.mesh_cluster_hierarchies().create_mesh_cluster_hierarchy({
            .name = "cube_identity_hierarchy",
            .source_mesh = source,
            .method =
                wz::engine::assets::MeshClusterHierarchyBuildMethod::Identity,
        });
    ASSERT_TRUE(hierarchy.valid());

    const auto preview =
        assets.mesh_cluster_hierarchies()
            .create_mesh_cluster_hierarchy_preview_mesh({
                .name = "cube_identity_preview_level_0",
                .hierarchy = hierarchy,
                .level_index = 0u,
            });
    ASSERT_TRUE(preview.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 3u);

    const auto source_handle = assets.meshes().get_mesh(source);
    const auto preview_handle = assets.meshes().get_mesh(preview);
    ASSERT_TRUE(source_handle.valid());
    ASSERT_TRUE(preview_handle.valid());

    const auto* source_data = assets.meshes().get_mesh_data(source_handle);
    const auto* preview_data = assets.meshes().get_mesh_data(preview_handle);

    ASSERT_NE(source_data, nullptr);
    ASSERT_NE(preview_data, nullptr);
    EXPECT_TRUE(preview_data->valid());
    EXPECT_EQ(preview_data->vertex_count(), source_data->vertex_count());
    EXPECT_EQ(preview_data->index_count(), source_data->index_count());
}

// The TYPED (explicit/bypass) authoring path: create_clipmap_lattice_mesh
// supplies level_count / base_resolution / cell_size directly and carries no
// dependency, so the compiler feeds them straight into the generator with no
// derivation. (The editor / ParamBlock path that derives from a height field is
// covered by DerivesClipmapLatticeMeshFromHeightField below.)
TEST(MeshAssetModule, ResolvesClipmapLatticeMesh)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto lattice = assets.meshes().create_clipmap_lattice_mesh({
        .name = "clipmap_lattice",
        .level_count = 4u,
        .base_resolution = 8u,
        .cell_size = 1.0f,
        });
    ASSERT_TRUE(lattice.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 1u);

    const auto handle = assets.meshes().get_mesh(lattice);
    ASSERT_TRUE(handle.valid());

    const auto* data = assets.meshes().get_mesh_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_TRUE(data->has_normals);
    EXPECT_TRUE(data->has_uv0);

    // Each LOD level owns its own vertices (keyed by level), so the footprint is
    // deliberately split into per-level components: adjacent levels share
    // boundary POSITIONS but not vertices, and the seams are closed in the
    // clipmap VS by the geomorph (#207), not by a stitched mesh. A whole-mesh
    // connectivity check therefore no longer applies; the dedicated
    // clipmap_lattice_mesh tests cover within-level connectivity and the
    // deliberate per-level un-sharing.

    // Matches the analytic level-0 + ring triangle count: plain rings with no
    // seam-stitch fans (the prior +2*m per ring), since the levels are stitched
    // in-shader, not in geometry.
    const uint32_t m = 8u;
    const uint32_t L = 4u;
    const uint32_t expected_tris =
        2u * m * m + (L - 1u) * ((3u * m * m) / 2u);
    EXPECT_EQ(data->index_count(), expected_tris * 3u);
}

// The EDITOR / asset-graph (ParamBlock) authoring path: the clipmap lattice
// mesh compiler reads the height field's resolution N, computes the finest cell
// size s = world_extent / N, and derives the geometric lattice via
// resolve_clipmap_lattice(horizon, triangle_budget, s). This drives the
// compiler directly with a ParamBlock + a height-field ScalarField dependency
// (the typed desc is intentionally absent) and proves the derived
// level_count / base_resolution / cell_size — hence the mesh's exact triangle
// count — match the pure resolver fed s = world_extent / N.
TEST(MeshAssetModule, DerivesClipmapLatticeMeshFromHeightField)
{
    namespace ea = wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    // A square height field of known resolution N. width is used as N.
    constexpr uint32_t kN = 64u;
    const auto height = assets.scalar_fields().create_procedural_scalar_field({
        .name = "clipmap/height",
        .width = kN,
        .height = kN,
        .generator = ea::ScalarFieldGenerator::RadialGradient,
        });
    ASSERT_TRUE(height.valid());

    // Author the clipmap lattice mesh the way the editor does: a ParamBlock of
    // physical params plus a height-field dependency. No typed desc.
    constexpr float kWorldExtent = 256.0f;
    constexpr float kHorizon = 96.0f;
    constexpr int64_t kTriangleBudget = 50000;

    wz::asset::ParamBlock params;
    params.values["world_extent"] = static_cast<double>(kWorldExtent);
    params.values["horizon"] = static_cast<double>(kHorizon);
    params.values["triangle_budget"] = kTriangleBudget;

    // A unique key for the authored node; the field dep is folded into
    // deps_hash so resolution wires it as dep_handles[0]. The compiler
    // dispatches on schema, so the key contents otherwise only need to be
    // unique. Mirrors the editor graph-draft key shape (schema + compiler +
    // dep fold).
    const wz::asset::AssetKey mesh_key{
        .content_hash = ea::detail::hash_u64(
            ea::detail::mix64(
                ea::kProceduralClipmapLatticeMeshSchema.value, 0x6c61747469ull)),
        .schema_hash = ea::detail::hash_u64(
            ea::kProceduralClipmapLatticeMeshSchema.value),
        .compiler_hash = ea::detail::hash_u64(ea::kMeshCompilerVersion),
        .deps_hash = ea::detail::key_to_dep_hash(height.output),
    };

    wz::asset::AssetNode node{};
    node.key = mesh_key;
    node.type = ea::kAssetTypeMesh;
    node.schema = ea::kProceduralClipmapLatticeMeshSchema;
    node.stage = wz::asset::AssetStage::Source;
    node.meta = params;

    ASSERT_TRUE(assets.system().register_asset(
        std::move(node), { height.output }));

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const ea::MeshAsset lattice{ .output = mesh_key };
    const auto handle = assets.meshes().get_mesh(lattice);
    ASSERT_TRUE(handle.valid());

    const auto* data = assets.meshes().get_mesh_data(handle);
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->valid());

    // The expectation, computed from the SAME pure function the compiler calls,
    // fed s = world_extent / N. This proves the compiler read N, computed s, and
    // resolved correctly.
    const float expected_s = kWorldExtent / static_cast<float>(kN);
    const ea::ResolvedClipmapLattice expected =
        ea::resolve_clipmap_lattice(
            kHorizon,
            static_cast<uint64_t>(kTriangleBudget),
            expected_s);

    const ea::ClipmapLatticeParams& p = expected.params;
    // cell_size must be exactly s (the finest cell == one texel).
    EXPECT_FLOAT_EQ(p.cell_size, expected_s);

    // The generator's exact triangle count for the resolved (m, L): the same
    // analytic formula ResolvesClipmapLatticeMesh uses, here with the DERIVED
    // m and L instead of hand-authored ones.
    const uint32_t mm = p.base_resolution;
    const uint32_t ll = p.level_count;
    const uint64_t expected_tris =
        static_cast<uint64_t>(2u) * mm * mm
        + static_cast<uint64_t>(ll - 1u) * ((3ull * mm * mm) / 2ull);
    EXPECT_EQ(data->index_count(), expected_tris * 3u);
}

TEST(MeshAssetModule, ResolvesDefaultPlaceholderMesh)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto mesh = assets.meshes().create_placeholder_mesh();

    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 1u);

    const auto handle = assets.meshes().get_mesh(mesh);

    ASSERT_TRUE(handle.valid());

    const auto* data = assets.meshes().get_mesh_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->vertex_count(), 8u);
    EXPECT_EQ(data->index_count(), 36u);
    EXPECT_TRUE(data->has_normals);
    EXPECT_FALSE(data->has_uv0);
}

TEST(MeshAssetModule, ResolvesAllProceduralMeshKinds)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto triangle = assets.meshes().create_procedural_mesh({
        .name = "triangle",
        .kind = wz::engine::assets::ProceduralMeshKind::Triangle,
        });

    const auto quad = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = wz::engine::assets::ProceduralMeshKind::Quad,
        });

    const auto cube = assets.meshes().create_procedural_mesh({
        .name = "cube",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });

    ASSERT_TRUE(triangle.valid());
    ASSERT_TRUE(quad.valid());
    ASSERT_TRUE(cube.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 3u);

    const auto triangle_handle = assets.meshes().get_mesh(triangle);
    const auto quad_handle = assets.meshes().get_mesh(quad);
    const auto cube_handle = assets.meshes().get_mesh(cube);

    ASSERT_TRUE(triangle_handle.valid());
    ASSERT_TRUE(quad_handle.valid());
    ASSERT_TRUE(cube_handle.valid());

    const auto* triangle_data =
        assets.meshes().get_mesh_data(triangle_handle);
    const auto* quad_data =
        assets.meshes().get_mesh_data(quad_handle);
    const auto* cube_data =
        assets.meshes().get_mesh_data(cube_handle);

    ASSERT_NE(triangle_data, nullptr);
    ASSERT_NE(quad_data, nullptr);
    ASSERT_NE(cube_data, nullptr);

    EXPECT_EQ(triangle_data->vertex_count(), 3u);
    EXPECT_EQ(triangle_data->index_count(), 3u);

    EXPECT_EQ(quad_data->vertex_count(), 4u);
    EXPECT_EQ(quad_data->index_count(), 6u);

    EXPECT_EQ(cube_data->vertex_count(), 8u);
    EXPECT_EQ(cube_data->index_count(), 36u);
}
