#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/engine_asset_key_factory.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/assets/key_factories/file_carrier.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <gpu/gpu.h>
#include <window/window2.h>

#include <filesystem>
#include <fstream>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
    namespace fs = std::filesystem;

    struct TempResourceDir
    {
        fs::path root;

        TempResourceDir()
        {
            root = fs::temp_directory_path() /
                ("wozzits_asset_library_test_" + std::to_string(::GetCurrentProcessId()));

            fs::remove_all(root);
            fs::create_directories(root / "shaders" / "triangle");
        }

        ~TempResourceDir()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        fs::path shader_dir() const
        {
            return root / "shaders" / "triangle";
        }

        wz::fs::Path wz_root() const
        {
            return root.string();
        }
    };

    static void write_text_file(const fs::path& path, const std::string& text)
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.good()) << "failed to open " << path.string();
        out << text;
    }

    static void write_triangle_shaders(const TempResourceDir& dir)
    {
        write_text_file(
            dir.shader_dir() / "triangle_vs.hlsl",
            R"(
cbuffer TransformCB : register(b0)
{
    float4x4 view_proj;
};

struct VSIn
{
    float3 pos : POSITION;
};

struct PSIn
{
    float4 pos : SV_POSITION;
};

PSIn main(VSIn input)
{
    PSIn output;
    output.pos = mul(view_proj, float4(input.pos, 1.0f));
    return output;
}
)"
);

        write_text_file(
            dir.shader_dir() / "triangle_ps.hlsl",
            R"(
struct PSIn
{
    float4 pos : SV_POSITION;
};

float4 main(PSIn input) : SV_TARGET
{
    return float4(1.0f, 0.2f, 0.1f, 1.0f);
}
)"
);
    }

    wz::asset::AssetNode make_file_carrier_node(
        wz::asset::SchemaID schema,
        wz::asset::AssetType type,
        const std::string& canonical_path,
        const fs::path& full_path)
    {
        wz::asset::AssetNode node{};
        node.type = type;
        node.schema = schema;
        node.stage = wz::asset::AssetStage::Source;
        node.residency = wz::asset::ResidencyIntent::CompileOnly;
        node.meta = wz::engine::assets::internal::FileSourceDesc{
            .full_path = full_path.string(),
            .canonical_path = canonical_path,
        };
        return node;
    }

    const wz::asset::AssetGraphDraftNode* find_draft_node_by_key(
        const wz::asset::AssetGraphDraft& draft,
        const wz::asset::AssetKey& key)
    {
        for (const wz::asset::AssetGraphDraftNode& node : draft.nodes) {
            if (node.node.key == key) {
                return &node;
            }
        }
        return nullptr;
    }

    struct Phase2KeyFactoryAllowlistEntry
    {
        wz::asset::SchemaID schema;
        wz::asset::AssetType type;
    };

    bool engine_key_factory_phase2_allowlisted(
        wz::asset::SchemaID schema,
        wz::asset::AssetType type)
    {
        using namespace wz::engine::assets;

        // These real engine compilers still materialize through the generic
        // draft key path. Keep this list explicit until each recipe gets a
        // schema-aware key factory.
        constexpr Phase2KeyFactoryAllowlistEntry kAllowlist[] = {
            { kBuiltinRenderProgramSchema, kAssetTypeRenderProgram },
            { kComputePipelineSchema, kAssetTypeComputePipeline },
            { kCustomRenderProgramSchema, kAssetTypeRenderProgram },

            { kScalarFieldFromRawF32Schema, kAssetTypeScalarField },
            { kScalarFieldProceduralSchema, kAssetTypeScalarField },
            { kScalarFieldFromGaeaR32Schema, kAssetTypeScalarField },
            { kVectorFieldFromRawF32Schema, kAssetTypeVectorField },

            { kProceduralTriangleMeshSchema, kAssetTypeMesh },
            { kProceduralQuadMeshSchema, kAssetTypeMesh },
            { kProceduralCubeMeshSchema, kAssetTypeMesh },
            { kProceduralClipmapLatticeMeshSchema, kAssetTypeMesh },
            { kGLBMeshSchema, kAssetTypeMesh },
            { kMeshFromGLBSceneSchema, kAssetTypeMesh },
            { kPlaceholderMeshSchema, kAssetTypeMesh },
            { kMeshDecimationSchema, kAssetTypeMesh },
            { kMeshDerivedFieldExplicitSchema, kAssetTypeMeshDerivedField },
            { kMeshWaveletAnalysisSchema, kAssetTypeMeshDerivedField },
            { kBehaviorFieldPlaceholderSchema, kAssetTypeMeshDerivedField },
            { kMeshComputeDerivedFieldSchema, kAssetTypeMeshDerivedField },
            { kMeshSparseOperatorSchema, kAssetTypeMeshSparseOperator },
            { kGpuSparseMeshFromMeshSchema, kAssetTypeGpuSparseMesh },
            { kBuiltinMeshDerivedFieldSchema, kAssetTypeMeshDerivedField },
            { kMeshSparseApplyFieldSchema, kAssetTypeMeshDerivedField },
            { kMeshSparseDiffusionBandsSchema, kAssetTypeMeshDerivedField },
            { kMeshFieldLevelMaskSchema, kAssetTypeMeshDerivedField },
            { kMeshClusterHierarchySchema, kAssetTypeMeshClusterHierarchy },
            { kMeshClusterHierarchyPreviewMeshSchema, kAssetTypeMesh },
            { kDebugTriangleStrideMeshSchema, kAssetTypeMesh },

            { kGaussianSplatFromPLYSchema, kAssetTypeGaussianSplatCloud },
            { kGaussianSplatFromFieldSchema, kAssetTypeGaussianSplatCloud },
            {
                kGaussianSplatColorLODSchema,
                kAssetTypeGaussianSplatColorLOD,
            },
            {
                kGaussianSplatTerrainSurfaceFromHeightFieldSchema,
                kAssetTypeGaussianSplatCloud,
            },
            { kTerrainSplatFromGaeaR32Schema, kAssetTypeGaussianSplatCloud },
            {
                kProceduralGaussianSplatCloudSchema,
                kAssetTypeGaussianSplatCloud,
            },

            { kMeshRenderStyleSchema, kAssetTypeMeshRenderStyle },
            { kMeshWireframeRenderableSchema, kAssetTypeRenderable },
            { kGaussianSplatDebugRenderableSchema, kAssetTypeRenderable },
            { kScalarFieldDebugRenderableSchema, kAssetTypeRenderable },
            { kTerrainDebugRenderableSchema, kAssetTypeRenderable },
            { kTerrainSurfaceRenderableSchema, kAssetTypeRenderable },
            { kMeshStyledRenderableSchema, kAssetTypeRenderable },
            { kRhiPullMeshRenderableSchema, kAssetTypeRenderable },
            { kGpuSparseMeshRenderableSchema, kAssetTypeRenderable },
            { kClipmapLandscapeRenderableSchema, kAssetTypeRenderable },
            {
                kGaussianSplatCloudRhiRenderableSchema,
                kAssetTypeRenderable,
            },
            { kSceneFromJSONSchema, kAssetTypeScene },
            { kSceneFromGLBSchema, kAssetTypeScene },

            { kTerrainFromHeightFieldSchema, kAssetTypeTerrain },
            { kTerrainFromMeshSchema, kAssetTypeTerrain },
            { kTerrainVisualProxySchema, kAssetTypeTerrainVisualProxy },
            { kCollisionFromMeshSchema, kAssetTypeCollisionAsset },
            { kCollisionFromTerrainSchema, kAssetTypeCollisionAsset },
            { kCollisionFromHeightFieldSchema, kAssetTypeCollisionAsset },

            { kPlacementSchema, kAssetTypePlacement },

            { kJSONDocumentSchema, kAssetTypeJSONDocument },
            { kTOMLDocumentSchema, kAssetTypeTOMLDocument },
            { kCSVTableSchema, kAssetTypeCSVTable },

            { kInlineDataTableSchema, kAssetTypeDataTable },
            {
                kDiagnosticTableResampleTimeSeriesSchema,
                kAssetTypeDiagnosticResampledTimeSeries,
            },
            { kCSVExportSchema, kAssetTypeCSVExport },
            {
                kDiagnosticResampledTimeSeriesToDataTableSchema,
                kAssetTypeDataTable,
            },
            {
                kDiagnosticTimeframeSummarySchema,
                kAssetTypeDiagnosticTimeframeSummary,
            },
            {
                kDiagnosticTimeframeSummaryToDataTableSchema,
                kAssetTypeDataTable,
            },

            { kDirectLightSchema, kAssetTypeDirectLight },
            { kAmbientLightingSchema, kAssetTypeAmbientLighting },
            { kHDRIEnvironmentSchema, kAssetTypeEnvironmentMap },
        };

        for (const Phase2KeyFactoryAllowlistEntry& entry : kAllowlist) {
            if (entry.schema == schema && entry.type == type) {
                return true;
            }
        }

        return false;
    }

    // Minimal real GPU fixture.
    //
    // These tests compile HLSL through the real GPU path, so they need a window
    // and device. Keep them in the window/gpu test group, not in a pure asset
    // library unit-test group.
    struct AssetLibraryGpuFixture : public ::testing::Test
    {
        wz::Logger logger;
        wz::window::WindowHandle window{};
        wz::gpu::Device device{};

        void SetUp() override
        {
            wz::logging::init_logger(logger, {});

            wz::window::WindowDesc desc{};
            desc.title = "asset_library_test";
            desc.width = 64;
            desc.height = 64;
            desc.resizable = false;

            window = wz::window::create_window(desc);
            ASSERT_TRUE(window.native);

            device = wz::gpu::create_device(window);
            ASSERT_TRUE(device.impl);
        }

        void TearDown() override
        {
            if (device.impl)
                wz::gpu::destroy_device(device);

            if (window.native)
                wz::window::destroy_window(window);
        }
    };
}

TEST(EngineAssetLibrary, DefaultShaderPairAssetIsInvalid)
{
    wz::engine::assets::ShaderPairAsset asset{};
    EXPECT_FALSE(asset.valid());
}

TEST_F(AssetLibraryGpuFixture, CreateShaderPairReturnsValidAssetKeys)
{
    TempResourceDir resources;
    write_triangle_shaders(resources);

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    auto pair = assets.shaders().create_shader_pair({
        .name = "triangle",
        .vertex_path = "shaders/triangle/triangle_vs.hlsl",
        .pixel_path = "shaders/triangle/triangle_ps.hlsl",
        });

    EXPECT_TRUE(pair.valid());
}

TEST_F(AssetLibraryGpuFixture, CacheRootIsConfigurable)
{
    TempResourceDir resources;
    const fs::path cache_root = resources.root / ".wozzits" / "cache";

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root(),
        wz::engine::assets::EngineAssetCacheSettings{
            .root = cache_root.string(),
            .enabled = true,
        },
    };

    EXPECT_EQ(assets.cache_root(), cache_root.string());
    EXPECT_TRUE(fs::is_directory(cache_root));
}

TEST_F(AssetLibraryGpuFixture, CommitSucceedsAfterShaderPairRegistration)
{
    TempResourceDir resources;
    write_triangle_shaders(resources);

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    auto pair = assets.shaders().create_shader_pair({
        .name = "triangle",
        .vertex_path = "shaders/triangle/triangle_vs.hlsl",
        .pixel_path = "shaders/triangle/triangle_ps.hlsl",
        });

    ASSERT_TRUE(pair.valid());
    EXPECT_TRUE(assets.commit());
}

TEST_F(AssetLibraryGpuFixture, CommitAssetGraphDraftAddsCarrierAndReloadsDraft)
{
    TempResourceDir resources;

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    wz::asset::AssetGraphDraft draft{};
    wz::asset::load_asset_graph_draft_from_registered_assets(
        draft,
        assets.system().registered_assets());

    const std::string canonical_path = "data/raw.bin";
    const wz::asset::AssetKey expected_key =
        wz::engine::assets::make_file_key(
            canonical_path,
            wz::engine::assets::kRawFileSchema);
    const wz::asset::AssetGraphDraftNodeId file_node =
        add_asset_graph_draft_node(
        draft,
        make_file_carrier_node(
            wz::engine::assets::kRawFileSchema,
            wz::engine::assets::kAssetTypeRawFile,
            canonical_path,
            resources.root / canonical_path));

    auto report = assets.commit_asset_graph_draft(draft);

    ASSERT_TRUE(report.success());
    EXPECT_EQ(report.registration_count, 1u);
    ASSERT_EQ(report.registrations.size(), 1u);
    EXPECT_EQ(report.registrations[0].node.key, expected_key);
    EXPECT_TRUE(assets.system().is_registered(expected_key));
    ASSERT_EQ(draft.nodes.size(), 1u);
    EXPECT_EQ(draft.nodes[0].id, file_node);
    EXPECT_EQ(
        draft.nodes[0].state,
        wz::asset::AssetGraphDraftNodeState::Existing);
    EXPECT_EQ(draft.nodes[0].node.key, expected_key);
}

TEST_F(AssetLibraryGpuFixture, CommitAssetGraphDraftViaAddSourceAssetNode)
{
    TempResourceDir resources;

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    wz::asset::AssetGraphDraft draft{};
    wz::asset::load_asset_graph_draft_from_registered_assets(
        draft,
        assets.system().registered_assets());

    const std::string canonical_path = "data/raw.bin";
    const wz::asset::AssetKey expected_key =
        wz::engine::assets::make_file_key(
            canonical_path,
            wz::engine::assets::kRawFileSchema);

    // Build the node through the new authoring helper + the library's wired
    // context, then prove it against the real commit API (not just materialize).
    const auto ctx = assets.graph_authoring_context();
    const wz::asset::AssetGraphDraftNodeId file_node =
        wz::engine::assets::authoring::add_source_asset_node(
            draft,
            ctx,
            wz::engine::assets::kRawFileSchema,
            wz::engine::assets::kAssetTypeRawFile,
            /*params=*/{},
            wz::fs::Path{ canonical_path });
    ASSERT_NE(file_node, wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE);

    auto report = assets.commit_asset_graph_draft(draft);

    ASSERT_TRUE(report.success());
    EXPECT_EQ(report.registration_count, 1u);
    ASSERT_EQ(report.registrations.size(), 1u);
    EXPECT_EQ(report.registrations[0].node.key, expected_key);
    EXPECT_TRUE(assets.system().is_registered(expected_key));
    ASSERT_EQ(draft.nodes.size(), 1u);
    EXPECT_EQ(draft.nodes[0].id, file_node);
    EXPECT_EQ(
        draft.nodes[0].state,
        wz::asset::AssetGraphDraftNodeState::Existing);
    EXPECT_EQ(draft.nodes[0].node.key, expected_key);
}

TEST_F(AssetLibraryGpuFixture, CommitAssetGraphDraftReloadsAfterRemoval)
{
    TempResourceDir resources;

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    wz::asset::AssetGraphDraft draft{};
    const std::string raw_path = "data/raw.bin";
    const std::string text_path = "data/text.txt";
    const wz::asset::AssetKey raw_key =
        wz::engine::assets::make_file_key(
            raw_path,
            wz::engine::assets::kRawFileSchema);
    const wz::asset::AssetKey text_key =
        wz::engine::assets::make_file_key(
            text_path,
            wz::engine::assets::kTextFileSchema);
    add_asset_graph_draft_node(
        draft,
        make_file_carrier_node(
            wz::engine::assets::kRawFileSchema,
            wz::engine::assets::kAssetTypeRawFile,
            raw_path,
            resources.root / raw_path));
    const wz::asset::AssetGraphDraftNodeId text_node =
        add_asset_graph_draft_node(
        draft,
        make_file_carrier_node(
            wz::engine::assets::kTextFileSchema,
            wz::engine::assets::kAssetTypeTextFile,
            text_path,
            resources.root / text_path));

    ASSERT_TRUE(assets.commit_asset_graph_draft(draft).success());
    ASSERT_TRUE(assets.system().is_registered(raw_key));
    ASSERT_TRUE(assets.system().is_registered(text_key));

    const wz::asset::AssetGraphDraftNode* raw_node =
        find_draft_node_by_key(draft, raw_key);
    ASSERT_NE(raw_node, nullptr);
    ASSERT_TRUE(wz::asset::remove_asset_graph_draft_node(draft, raw_node->id));

    auto report = assets.commit_asset_graph_draft(draft);

    ASSERT_TRUE(report.success());
    EXPECT_EQ(report.registration_count, 1u);
    EXPECT_FALSE(assets.system().is_registered(raw_key));
    EXPECT_TRUE(assets.system().is_registered(text_key));
    ASSERT_EQ(draft.nodes.size(), 1u);
    EXPECT_EQ(draft.nodes[0].id, text_node);
    EXPECT_EQ(draft.nodes[0].node.key, text_key);
    EXPECT_EQ(
        draft.nodes[0].state,
        wz::asset::AssetGraphDraftNodeState::Existing);
}

TEST_F(AssetLibraryGpuFixture, CommitAssetGraphDraftLeavesDraftOnMaterializeFailure)
{
    TempResourceDir resources;

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    wz::asset::AssetNode node{};
    node.type = wz::asset::AssetType::Mesh;
    node.schema = wz::asset::SchemaID{ 0xFADEu };
    node.stage = wz::asset::AssetStage::Source;

    wz::asset::AssetGraphDraft draft{};
    const wz::asset::AssetGraphDraftNodeId id =
        add_asset_graph_draft_node(draft, node);

    auto report = assets.commit_asset_graph_draft(draft);

    EXPECT_FALSE(report.success());
    EXPECT_EQ(
        report.status,
        wz::engine::assets::EngineAssetLibrary::AssetGraphDraftCommitReport::
            Status::MaterializeFailed);
    EXPECT_TRUE(assets.system().registered_assets().empty());
    EXPECT_NE(wz::asset::find_asset_graph_draft_node(draft, id), nullptr);
}

TEST_F(AssetLibraryGpuFixture, CommitAssetGraphDraftReportsReplaceFailure)
{
    TempResourceDir resources;

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    wz::asset::AssetGraphDraft draft{};
    const std::string canonical_path = "data/duplicate.bin";
    const wz::asset::AssetKey duplicate_key =
        wz::engine::assets::make_file_key(
            canonical_path,
            wz::engine::assets::kRawFileSchema);

    const wz::asset::AssetGraphDraftNodeId first =
        add_asset_graph_draft_node(
            draft,
            make_file_carrier_node(
                wz::engine::assets::kRawFileSchema,
                wz::engine::assets::kAssetTypeRawFile,
                canonical_path,
                resources.root / canonical_path));
    const wz::asset::AssetGraphDraftNodeId second =
        add_asset_graph_draft_node(
            draft,
            make_file_carrier_node(
                wz::engine::assets::kRawFileSchema,
                wz::engine::assets::kAssetTypeRawFile,
                canonical_path,
                resources.root / canonical_path));

    auto report = assets.commit_asset_graph_draft(draft);

    EXPECT_FALSE(report.success());
    EXPECT_EQ(
        report.status,
        wz::engine::assets::EngineAssetLibrary::AssetGraphDraftCommitReport::
            Status::ReplaceFailed);
    EXPECT_EQ(report.registration_count, 2u);
    ASSERT_EQ(report.registrations.size(), 2u);
    EXPECT_EQ(report.registrations[0].node.key, duplicate_key);
    EXPECT_EQ(report.registrations[1].node.key, duplicate_key);
    EXPECT_TRUE(assets.system().registered_assets().empty());

    const wz::asset::AssetGraphDraftNode* first_node =
        wz::asset::find_asset_graph_draft_node(draft, first);
    const wz::asset::AssetGraphDraftNode* second_node =
        wz::asset::find_asset_graph_draft_node(draft, second);
    ASSERT_NE(first_node, nullptr);
    ASSERT_NE(second_node, nullptr);
    EXPECT_EQ(first_node->state, wz::asset::AssetGraphDraftNodeState::Created);
    EXPECT_EQ(second_node->state, wz::asset::AssetGraphDraftNodeState::Created);
    EXPECT_EQ(first_node->node.key, duplicate_key);
    EXPECT_EQ(second_node->node.key, duplicate_key);
}

TEST_F(AssetLibraryGpuFixture, EveryRegisteredCompilerIsHandledOrAllowlisted)
{
    TempResourceDir resources;

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    for (const auto& entry : assets.system().registry().compilers()) {
        const wz::asset::AssetCompiler& compiler = entry.second;
        const bool handled = wz::engine::assets::engine_key_factory_handles(
            compiler.input_schema,
            compiler.output_type);
        const bool allowlisted = engine_key_factory_phase2_allowlisted(
            compiler.input_schema,
            compiler.output_type);
        EXPECT_TRUE(handled || allowlisted)
            << "schema=" << compiler.input_schema.value
            << " type="
            << static_cast<uint32_t>(
                static_cast<uint16_t>(compiler.output_type));
    }
}

TEST_F(AssetLibraryGpuFixture, ResolveShaderPairProducesValidHandles)
{
    TempResourceDir resources;
    write_triangle_shaders(resources);

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    auto pair = assets.shaders().create_shader_pair({
        .name = "triangle",
        .vertex_path = "shaders/triangle/triangle_vs.hlsl",
        .pixel_path = "shaders/triangle/triangle_ps.hlsl",
        });

    ASSERT_TRUE(pair.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_GE(report.resolved_count, 4u);
    // Expected nodes:
    // - VS file carrier
    // - PS file carrier
    // - VS shader
    // - PS shader

    auto handles = assets.shaders().get_shader_pair(pair);

    EXPECT_TRUE(handles.valid());
    EXPECT_TRUE(handles.vertex.valid());
    EXPECT_TRUE(handles.pixel.valid());
}

TEST_F(AssetLibraryGpuFixture, ResolveRuntimeUsesDemandRootsAndEvictsFileCarriers)
{
    TempResourceDir resources;
    write_triangle_shaders(resources);

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    auto pair = assets.shaders().create_shader_pair({
        .name = "triangle",
        .vertex_path = "shaders/triangle/triangle_vs.hlsl",
        .pixel_path = "shaders/triangle/triangle_ps.hlsl",
        });

    ASSERT_TRUE(pair.valid());
    ASSERT_TRUE(assets.system().register_demand_root(
        wz::asset::DemandRoot::GPURuntime,
        { pair.vertex_shader, pair.pixel_shader }));
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_runtime();
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 4u);
    EXPECT_GE(report.evicted_count, 2u);

    auto handles = assets.shaders().get_shader_pair(pair);
    EXPECT_TRUE(handles.valid());
}

TEST_F(AssetLibraryGpuFixture, ResolveRuntimeWithoutDemandRootsIsNoOp)
{
    TempResourceDir resources;

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_runtime();
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 0u);
    EXPECT_EQ(report.evicted_count, 0u);
}

TEST_F(AssetLibraryGpuFixture, MissingShaderFileDoesNotProduceValidHandles)
{
    TempResourceDir resources;

    // Only write pixel shader. Vertex shader is intentionally missing.
    write_text_file(
        resources.shader_dir() / "triangle_ps.hlsl",
        R"(
struct PSIn
{
    float4 pos : SV_POSITION;
};

float4 main(PSIn input) : SV_TARGET
{
    return float4(1.0f, 0.2f, 0.1f, 1.0f);
}
)"
);

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    auto pair = assets.shaders().create_shader_pair({
        .name = "triangle",
        .vertex_path = "shaders/triangle/missing_vs.hlsl",
        .pixel_path = "shaders/triangle/triangle_ps.hlsl",
        });

    ASSERT_TRUE(pair.valid());
    ASSERT_TRUE(assets.commit());

    assets.resolve_all();

    auto handles = assets.shaders().get_shader_pair(pair);

    EXPECT_FALSE(handles.valid());
    EXPECT_FALSE(handles.vertex.valid());
}

TEST_F(AssetLibraryGpuFixture, InvalidHlslDoesNotProduceValidShaderPair)
{
    TempResourceDir resources;

    write_text_file(
        resources.shader_dir() / "triangle_vs.hlsl",
        "this is not valid hlsl"
    );

    write_text_file(
        resources.shader_dir() / "triangle_ps.hlsl",
        R"(
float4 main() : SV_TARGET
{
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}
)"
);

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    auto pair = assets.shaders().create_shader_pair({
        .name = "bad_triangle",
        .vertex_path = "shaders/triangle/triangle_vs.hlsl",
        .pixel_path = "shaders/triangle/triangle_ps.hlsl",
        });

    ASSERT_TRUE(pair.valid());
    ASSERT_TRUE(assets.commit());

    assets.resolve_all();

    auto handles = assets.shaders().get_shader_pair(pair);

    EXPECT_FALSE(handles.valid());
    EXPECT_FALSE(handles.vertex.valid());
}

