#include <gtest/gtest.h>

#include <engine/bundle/bundle_closure.h>

#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <asset/draft.h>
#include <file/filesystem.h>

#include <string>
#include <utility>

// Seam 3.1 of issue #295: the bundle resource-closure walker. These exercise the
// Copy-vs-Strip classifier over synthetic graphs (no device, no file I/O), which
// is exactly what makes it headlessly testable. The strip rule must mirror the
// runtime resolve: a source feeding a cache-served product is pruned (Strip); a
// source compiled at load (shaders, audio dirs) must ship (Copy).

using wz::asset::AssetGraphDraft;
using wz::asset::AssetGraphDraftNodeId;
using wz::asset::AssetGraphDraftNodeState;
using wz::asset::AssetNode;
using wz::asset::AssetType;
using wz::asset::ParamBlock;
using wz::asset::SchemaID;
using wz::engine::bundle::BundleClosure;
using wz::engine::bundle::BundleFileDisposition;
using wz::engine::bundle::BundleSourceRef;
using wz::engine::bundle::compute_bundle_closure;

namespace
{
    namespace ids = wz::engine::assets;

    // A synthetic schema for intermediates / sinks (shaders, renderables).
    // is_disk_cacheable keys on specific (schema,type) pairs, so any schema with a
    // non-cached type reads as "compiles at load".
    constexpr SchemaID kSyntheticSchema{ 0xB0000001ull };

    AssetGraphDraftNodeId add_node(
        AssetGraphDraft& draft,
        AssetGraphDraftNodeId id,
        SchemaID schema,
        AssetType type,
        ParamBlock params = {})
    {
        AssetNode node{};
        node.schema = schema;
        node.type = type;
        if (!params.values.empty()) {
            node.meta = std::move(params);
        }
        return wz::asset::add_asset_graph_draft_node_with_id(
            draft, std::move(node), id, AssetGraphDraftNodeState::Existing);
    }

    ParamBlock source_path_params(std::string path)
    {
        ParamBlock pb;
        pb.values["source_path"] = std::move(path);
        return pb;
    }

    const BundleSourceRef* find_ref(
        const BundleClosure& closure,
        AssetGraphDraftNodeId id)
    {
        for (const BundleSourceRef& ref : closure.sources) {
            if (ref.node == id) {
                return &ref;
            }
        }
        return nullptr;
    }
}

TEST(BundleClosure, StripsCarrierFeedingCacheServedProduct)
{
    // .r32 carrier -> cache-served scalar field -> live sink. The scalar field is
    // served from the sealed cache, so its .r32 prerequisite is pruned at runtime
    // and never read -> Strip.
    AssetGraphDraft draft;
    const auto raw = add_node(
        draft, 1, ids::kRawFileSchema, ids::kAssetTypeRawFile,
        source_path_params("heightfield/x.r32"));
    const auto field = add_node(
        draft, 2, ids::kScalarFieldFromGaeaR32Schema, ids::kAssetTypeScalarField);
    const auto sink = add_node(draft, 3, kSyntheticSchema, AssetType::Texture);
    wz::asset::connect_asset_graph_draft_nodes(draft, raw, field, 0);
    wz::asset::connect_asset_graph_draft_nodes(draft, field, sink, 0);

    const BundleClosure closure = compute_bundle_closure(draft, "R");

    const BundleSourceRef* ref = find_ref(closure, raw);
    ASSERT_NE(ref, nullptr);
    EXPECT_EQ(ref->disposition, BundleFileDisposition::Strip);
    EXPECT_FALSE(ref->reached);  // pruned by the cache hit on the scalar field
    EXPECT_EQ(ref->resolved_path, wz::fs::join("R", "heightfield/x.r32"));
    EXPECT_TRUE(closure.copy_paths().empty());
    EXPECT_EQ(closure.strip_paths().size(), 1u);
}

TEST(BundleClosure, CopiesShaderSourceCompiledAtLoad)
{
    // .hlsl carrier -> shader (not cache-served) -> live sink. Shaders compile
    // from source every run, so the .hlsl bytes are read at load -> Copy.
    AssetGraphDraft draft;
    const auto hlsl = add_node(
        draft, 1, ids::kHLSLFileSchema, AssetType::ShaderSource,
        source_path_params("shaders/a.hlsl"));
    const auto shader = add_node(draft, 2, kSyntheticSchema, AssetType::Shader);
    const auto sink = add_node(draft, 3, kSyntheticSchema, AssetType::Texture);
    wz::asset::connect_asset_graph_draft_nodes(draft, hlsl, shader, 0);
    wz::asset::connect_asset_graph_draft_nodes(draft, shader, sink, 0);

    const BundleClosure closure = compute_bundle_closure(draft, "R");

    const BundleSourceRef* ref = find_ref(closure, hlsl);
    ASSERT_NE(ref, nullptr);
    EXPECT_EQ(ref->disposition, BundleFileDisposition::Copy);
    EXPECT_TRUE(ref->reached);
    ASSERT_EQ(closure.copy_paths().size(), 1u);
    EXPECT_EQ(closure.copy_paths().front(), wz::fs::join("R", "shaders/a.hlsl"));
}

TEST(BundleClosure, StripsHeavySourceAndPreservesAbsolutePath)
{
    // Absolute .glb carrier -> cache-served mesh -> sink. Stripped, and the
    // absolute authored path passes through untouched (not re-rooted).
    AssetGraphDraft draft;
    const std::string absolute = "C:/abs/tank.glb";
    ASSERT_TRUE(wz::fs::is_absolute(absolute));
    const auto glb = add_node(
        draft, 1, ids::kBinaryBlobSchema, ids::kAssetTypeBinaryBlob,
        source_path_params(absolute));
    const auto mesh = add_node(draft, 2, ids::kGLBMeshSchema, ids::kAssetTypeMesh);
    const auto sink = add_node(draft, 3, kSyntheticSchema, AssetType::Texture);
    wz::asset::connect_asset_graph_draft_nodes(draft, glb, mesh, 0);
    wz::asset::connect_asset_graph_draft_nodes(draft, mesh, sink, 0);

    const BundleClosure closure = compute_bundle_closure(draft, "R");

    const BundleSourceRef* ref = find_ref(closure, glb);
    ASSERT_NE(ref, nullptr);
    EXPECT_EQ(ref->disposition, BundleFileDisposition::Strip);
    EXPECT_EQ(ref->resolved_path, absolute);
}

TEST(BundleClosure, CopiesAudioDirectoryImporter)
{
    // The audio importer walks a wav directory at load (AudioBank is not
    // cache-served) -> Copy the whole directory, recursive flag preserved.
    AssetGraphDraft draft;
    ParamBlock pb;
    pb.values["directory"] = std::string("wav");
    pb.values["recursive"] = true;
    const auto audio = add_node(
        draft, 1, ids::kAudioClipBankFromDirectorySchema, ids::kAssetTypeAudioBank,
        std::move(pb));
    const auto sink = add_node(draft, 2, kSyntheticSchema, AssetType::Texture);
    wz::asset::connect_asset_graph_draft_nodes(draft, audio, sink, 0);

    const BundleClosure closure = compute_bundle_closure(draft, "R");

    const BundleSourceRef* ref = find_ref(closure, audio);
    ASSERT_NE(ref, nullptr);
    EXPECT_EQ(ref->disposition, BundleFileDisposition::Copy);
    EXPECT_TRUE(ref->is_directory);
    EXPECT_TRUE(ref->recursive);
    EXPECT_EQ(ref->resolved_path, wz::fs::join("R", "wav"));
}

TEST(BundleClosure, TrimsQuotesAndResolvesRelativePaths)
{
    // A quoted source_path (Windows "Copy as path") is trimmed like the carrier,
    // then a relative path joins the resource root.
    AssetGraphDraft draft;
    const auto raw = add_node(
        draft, 1, ids::kRawFileSchema, ids::kAssetTypeRawFile,
        source_path_params("\"data/blob.bin\""));
    const auto sink = add_node(draft, 2, kSyntheticSchema, AssetType::Texture);
    wz::asset::connect_asset_graph_draft_nodes(draft, raw, sink, 0);

    const BundleClosure closure = compute_bundle_closure(draft, "R");

    const BundleSourceRef* ref = find_ref(closure, raw);
    ASSERT_NE(ref, nullptr);
    EXPECT_EQ(ref->authored_path, "data/blob.bin");
    EXPECT_EQ(ref->resolved_path, wz::fs::join("R", "data/blob.bin"));
    // A carrier feeding a non-cached sink is read at load -> Copy.
    EXPECT_EQ(ref->disposition, BundleFileDisposition::Copy);
}

TEST(BundleClosure, SharedSourceCopyWinsOverStrip)
{
    // The same file feeds BOTH a cache-served mesh (Strip) and a live consumer
    // (Copy). It must be copied, and must NOT appear in strip_paths.
    AssetGraphDraft draft;
    const auto blob_cached = add_node(
        draft, 1, ids::kBinaryBlobSchema, ids::kAssetTypeBinaryBlob,
        source_path_params("shared.bin"));
    const auto mesh = add_node(draft, 2, ids::kGLBMeshSchema, ids::kAssetTypeMesh);
    const auto sink_mesh = add_node(draft, 3, kSyntheticSchema, AssetType::Texture);
    const auto blob_live = add_node(
        draft, 4, ids::kBinaryBlobSchema, ids::kAssetTypeBinaryBlob,
        source_path_params("shared.bin"));
    const auto live_consumer =
        add_node(draft, 5, kSyntheticSchema, AssetType::Texture);
    wz::asset::connect_asset_graph_draft_nodes(draft, blob_cached, mesh, 0);
    wz::asset::connect_asset_graph_draft_nodes(draft, mesh, sink_mesh, 0);
    wz::asset::connect_asset_graph_draft_nodes(draft, blob_live, live_consumer, 0);

    const BundleClosure closure = compute_bundle_closure(draft, "R");

    EXPECT_EQ(
        find_ref(closure, blob_cached)->disposition,
        BundleFileDisposition::Strip);
    EXPECT_EQ(
        find_ref(closure, blob_live)->disposition,
        BundleFileDisposition::Copy);
    ASSERT_EQ(closure.copy_paths().size(), 1u);
    EXPECT_EQ(closure.copy_paths().front(), wz::fs::join("R", "shared.bin"));
    // Excluded from strip_paths because a live consumer still reads it.
    EXPECT_TRUE(closure.strip_paths().empty());
}
