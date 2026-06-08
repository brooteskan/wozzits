#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/key_factories/mesh_derived_field.h>

#include <cstring>

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
                std::string("wozzits_mesh_derived_field_tests_") + suffix);

        EXPECT_EQ(
            wz::fs::create_directories(root),
            wz::fs::FileError::None);

        return wz::engine::assets::EngineAssetLibrary(
            device,
            logger,
            root);
    }

    std::vector<std::byte> float_values(std::initializer_list<float> values)
    {
        std::vector<std::byte> bytes(values.size() * sizeof(float));
        std::memcpy(bytes.data(), values.begin(), bytes.size());
        return bytes;
    }

    std::vector<std::byte> uint_values(std::initializer_list<uint32_t> values)
    {
        std::vector<std::byte> bytes(values.size() * sizeof(uint32_t));
        std::memcpy(bytes.data(), values.begin(), bytes.size());
        return bytes;
    }

    float read_float(
        const std::vector<std::byte>& bytes,
        uint32_t byte_offset,
        uint32_t element)
    {
        float out = 0.0f;
        std::memcpy(
            &out,
            bytes.data() + byte_offset + element * sizeof(float),
            sizeof(float));
        return out;
    }
}

TEST(MeshDerivedFieldAssetModule, DefaultAssetAndHandleAreInvalid)
{
    wz::engine::assets::MeshDerivedFieldAsset asset{};
    wz::engine::assets::MeshDerivedFieldHandle handle{};

    EXPECT_FALSE(asset.valid());
    EXPECT_FALSE(handle.valid());
}

TEST(MeshDerivedFieldAssetModule, ResolvesExplicitVertexField)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets(device, logger, "vertex_field");

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());

    const auto field = assets.mesh_derived_fields().create_explicit_field({
        .name = "quad_vertex_mask",
        .source_mesh = mesh,
        .domain = MeshDerivedFieldDomain::Vertex,
        .element_count = 4u,
        .channels = {
            MeshDerivedFieldChannelDesc{
                .channel_id = 0x100u,
                .value_type = MeshDerivedFieldValueType::Float1,
                .values = float_values({ 0.0f, 0.25f, 0.75f, 1.0f }),
            },
        },
    });
    ASSERT_TRUE(field.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle =
        assets.mesh_derived_fields().get_mesh_derived_field(field);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->source_mesh_key, mesh.output);
    EXPECT_EQ(data->domain, MeshDerivedFieldDomain::Vertex);
    EXPECT_EQ(data->element_count, 4u);
    ASSERT_EQ(data->channels.size(), 1u);
    EXPECT_EQ(data->channels[0].channel_id, 0x100u);
    EXPECT_EQ(data->channels[0].byte_offset, 0u);
    EXPECT_EQ(data->channels[0].byte_count, 4u * sizeof(float));
    EXPECT_FLOAT_EQ(read_float(data->values, 0u, 2u), 0.75f);

    const auto mesh_handle = assets.meshes().get_mesh(mesh);
    ASSERT_TRUE(mesh_handle.valid());
    const auto* mesh_data = assets.meshes().get_mesh_data(mesh_handle);
    ASSERT_NE(mesh_data, nullptr);
    EXPECT_EQ(data->source_topology_hash, compute_mesh_topology_hash(*mesh_data));
}

TEST(MeshDerivedFieldAssetModule, PreservesFaceDomainAndSOAChannels)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets(device, logger, "face_field");

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());

    const auto field = assets.mesh_derived_fields().create_explicit_field({
        .name = "quad_face_masks",
        .source_mesh = mesh,
        .domain = MeshDerivedFieldDomain::Face,
        .element_count = 2u,
        .channels = {
            MeshDerivedFieldChannelDesc{
                .channel_id = 0x100u,
                .value_type = MeshDerivedFieldValueType::Float1,
                .values = float_values({ 0.4f, 0.9f }),
            },
            MeshDerivedFieldChannelDesc{
                .channel_id = 0x101u,
                .value_type = MeshDerivedFieldValueType::UInt1,
                .values = uint_values({ 7u, 8u }),
            },
        },
    });
    ASSERT_TRUE(field.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto handle =
        assets.mesh_derived_fields().get_mesh_derived_field(field);
    ASSERT_TRUE(handle.valid());
    const auto* data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(handle);
    ASSERT_NE(data, nullptr);

    EXPECT_EQ(data->domain, MeshDerivedFieldDomain::Face);
    EXPECT_EQ(data->element_count, 2u);
    ASSERT_EQ(data->channels.size(), 2u);
    EXPECT_EQ(data->channels[0].byte_offset, 0u);
    EXPECT_EQ(data->channels[0].byte_count, 2u * sizeof(float));
    EXPECT_EQ(data->channels[1].byte_offset, 2u * sizeof(float));
    EXPECT_EQ(data->channels[1].byte_count, 2u * sizeof(uint32_t));
    EXPECT_EQ(
        data->values.size(),
        2u * sizeof(float) + 2u * sizeof(uint32_t));
}

TEST(MeshDerivedFieldAssetModule, RejectsElementCountMismatch)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    auto assets = make_assets(device, logger, "mismatch");

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "triangle",
        .kind = ProceduralMeshKind::Triangle,
    });
    ASSERT_TRUE(mesh.valid());

    const auto field = assets.mesh_derived_fields().create_explicit_field({
        .name = "bad_vertex_mask",
        .source_mesh = mesh,
        .domain = MeshDerivedFieldDomain::Vertex,
        .element_count = 4u,
        .channels = {
            MeshDerivedFieldChannelDesc{
                .channel_id = 0x100u,
                .value_type = MeshDerivedFieldValueType::Float1,
                .values = float_values({ 0.0f, 0.25f, 0.75f, 1.0f }),
            },
        },
    });
    ASSERT_TRUE(field.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    EXPECT_FALSE(report.ok());
    ASSERT_EQ(report.failures.size(), 1u);
    EXPECT_EQ(report.failures[0].key, field.output);
    EXPECT_FALSE(
        assets.mesh_derived_fields().get_mesh_derived_field(field).valid());
}

TEST(MeshDerivedFieldAssetModule, DeterministicIdentityIncludesChannels)
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
    const ExplicitMeshDerivedFieldDesc first{
        .name = "a",
        .source_mesh = mesh,
        .domain = MeshDerivedFieldDomain::Face,
        .element_count = 2u,
        .channels = {
            MeshDerivedFieldChannelDesc{
                .channel_id = 0x100u,
                .value_type = MeshDerivedFieldValueType::Float1,
                .values = float_values({ 0.2f, 0.8f }),
            },
        },
    };
    ExplicitMeshDerivedFieldDesc second = first;
    ExplicitMeshDerivedFieldDesc changed = first;
    changed.channels[0].channel_id = 0x101u;

    EXPECT_EQ(
        make_explicit_mesh_derived_field_key(mesh.output, first),
        make_explicit_mesh_derived_field_key(mesh.output, second));
    EXPECT_NE(
        make_explicit_mesh_derived_field_key(mesh.output, first),
        make_explicit_mesh_derived_field_key(mesh.output, changed));
}

TEST(MeshDerivedFieldAssetModule, TopologyHashIgnoresVertexPositions)
{
    using namespace wz::engine::assets;

    MeshData a{};
    a.vertices.resize(3);
    a.indices = { 0u, 1u, 2u };

    MeshData b = a;
    b.vertices[0].position[0] = 10.0f;
    b.vertices[1].position[1] = 20.0f;

    MeshData c = a;
    c.indices = { 0u, 2u, 1u };

    EXPECT_EQ(compute_mesh_topology_hash(a), compute_mesh_topology_hash(b));
    EXPECT_NE(compute_mesh_topology_hash(a), compute_mesh_topology_hash(c));
}

TEST(MeshDerivedFieldAssetModule, MeshDomainElementCountsMatchQuad)
{
    using namespace wz::engine::assets;

    MeshData quad{};
    quad.vertices.resize(4);
    quad.indices = { 0u, 1u, 2u, 0u, 2u, 3u };

    EXPECT_EQ(
        mesh_domain_element_count(quad, MeshDerivedFieldDomain::Vertex),
        4u);
    EXPECT_EQ(
        mesh_domain_element_count(quad, MeshDerivedFieldDomain::Face),
        2u);
    EXPECT_EQ(
        mesh_domain_element_count(quad, MeshDerivedFieldDomain::Corner),
        6u);
    EXPECT_EQ(
        mesh_domain_element_count(quad, MeshDerivedFieldDomain::Edge),
        5u);
}
