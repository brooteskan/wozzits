#include "scene_asset_module_test_support.h"

#include <engine/assets/gltf/gltf_importer.h>
#include <engine/assets/key_factories/mesh.h>
#include <engine/assets/key_factories/scene.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/schema_ids.h>

TEST(SceneAssetModule, ResolvesSceneFromJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_asset_resolve_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(root, "test_scene.json", kSingleNodeSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "test_scene",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok()) << "resolve failures: " << report.failures.size();

    const auto handle = assets.scenes().get_scene(scene_asset);
    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.handle.type, kAssetTypeScene);

    const auto* data = assets.scenes().get_scene_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->name, "single_object_scene");
    EXPECT_EQ(data->nodes.size(), 2u);
    EXPECT_EQ(data->nodes[0].id, "root");
    EXPECT_EQ(data->nodes[1].id, "debug_object");
    EXPECT_TRUE(data->nodes[1].renderable.has_value());
}

TEST(SceneAssetModule, ResolvesSceneFromGLTFHierarchy)
{
    const char* gltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [
    { "name": "tank_scene", "nodes": [0] }
  ],
  "nodes": [
    {
      "name": "tank_body",
      "translation": [1, 0, 2],
      "children": [1]
    },
    {
      "name": "turret",
      "translation": [0, 3, 0]
    }
  ]
})";

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_gltf_hierarchy_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const auto rel_path =
        write_scene_json(root, "tank_hierarchy.gltf", gltf);

    const auto scene_asset =
        assets.scenes().create_scene_from_glb({
            .name = "tank_import",
            .path = rel_path,
        });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok()) << "resolve failures: " << report.failures.size();

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);

    ASSERT_EQ(data->nodes.size(), 2u);
    EXPECT_EQ(data->name, "tank_scene");

    const auto* body = find_scene_node(*data, "tank_body");
    ASSERT_NE(body, nullptr);
    EXPECT_FALSE(body->parent_id.has_value());
    EXPECT_FLOAT_EQ(body->local.translation[0], 1.0f);
    EXPECT_FLOAT_EQ(body->local.translation[2], 2.0f);

    const auto* turret = find_scene_node(*data, "turret");
    ASSERT_NE(turret, nullptr);
    ASSERT_TRUE(turret->parent_id.has_value());
    EXPECT_EQ(*turret->parent_id, "tank_body");
    EXPECT_FLOAT_EQ(turret->local.translation[1], 3.0f);
}

TEST(SceneAssetModule, ResolvesGLBSceneMeshAsRenderableAsset)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        WZ_TEST_FIXTURE_DIR };

    using namespace wz::engine::assets;

    const auto scene_asset =
        assets.scenes().create_scene_from_glb({
            .name = "cube_import",
            .path = "gltf/cube.glb",
        });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok()) << "resolve failures: " << report.failures.size();

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);

    ASSERT_EQ(data->nodes.size(), 1u);
    EXPECT_EQ(data->nodes[0].id, "Cube");
    // Issue #195: deviceless GLB import attaches NO renderable — the 0x706
    // replacement needs a provisioned render program whose shaders cannot
    // compile without a GPU device. The node comes through as structure; the
    // with-device renderable flow is covered by the wozzits_app GLB suites.
    EXPECT_FALSE(data->nodes[0].renderable_asset.has_value());
    EXPECT_FALSE(data->nodes[0].renderable.has_value());
}

// Piece 1 of the asset-graph "Subtree from asset" path (issue #213): a
// graph-authored "Scene from GLB" node supplies only a source file + scene_index
// (no per-mesh renderable bindings, unlike the imperative create_scene_from_glb).
// It must still compile into the GLB's bare node hierarchy so a scene node can
// reference it; mesh nodes come through WITHOUT a renderable (attached later via
// the asset graph) instead of failing with "references unregistered mesh N".
TEST(SceneAssetModule, CompilesGraphAuthoredGLBAsStructureWithoutBindings)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        WZ_TEST_FIXTURE_DIR };

    using namespace wz::engine::assets;

    // Register only the GLB file carrier — NOT the per-mesh meshes/renderables —
    // mirroring a "Scene from GLB" asset-graph node whose sole input is the
    // source_file (+ scene_index param).
    const wz::asset::AssetKey file_key = assets.files().register_file_node(
        "gltf/test-mesh-a.glb", kRawFileSchema, kAssetTypeRawFile);
    ASSERT_FALSE(file_key == wz::asset::AssetKey{});

    const wz::asset::AssetKey scene_key =
        make_scene_from_glb_key(file_key, 0u, wz::asset::Hash{});

    wz::asset::AssetNode node;
    node.key = scene_key;
    node.type = kAssetTypeScene;
    node.schema = kSceneFromGLBSchema;
    node.stage = wz::asset::AssetStage::Source;
    node.payload = std::vector<uint8_t>{};
    // Empty mesh_renderables = the graph-authored shape (no bindings).
    node.meta = SceneFromGLBCompileDesc{ .scene_index = 0u };
    ASSERT_TRUE(
        assets.system().register_asset(std::move(node), { file_key }));

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok()) << "resolve failures: " << report.failures.size();

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(SceneAsset{ .output = scene_key }));
    ASSERT_NE(data, nullptr);

    // The full GLB hierarchy is produced as bare structure: body -> turret -> gun,
    // every node WITHOUT a renderable (no bindings were supplied).
    ASSERT_EQ(data->nodes.size(), 3u);
    for (const auto& scene_node : data->nodes) {
        EXPECT_FALSE(scene_node.renderable_asset.has_value())
            << "node '" << scene_node.id
            << "' unexpectedly has a renderable binding";
    }

    const auto* body = find_scene_node(*data, "body");
    ASSERT_NE(body, nullptr);
    EXPECT_FALSE(body->parent_id.has_value());

    const auto* turret = find_scene_node(*data, "turret");
    ASSERT_NE(turret, nullptr);
    ASSERT_TRUE(turret->parent_id.has_value());
    EXPECT_EQ(*turret->parent_id, "body");

    const auto* gun = find_scene_node(*data, "gun");
    ASSERT_NE(gun, nullptr);
    ASSERT_TRUE(gun->parent_id.has_value());
    EXPECT_EQ(*gun->parent_id, "turret");
}

TEST(SceneAssetModule, ResolvesTankGLBHierarchyFixture)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        WZ_TEST_FIXTURE_DIR };

    using namespace wz::engine::assets;

    const auto scene_asset =
        assets.scenes().create_scene_from_glb({
            .name = "tank1_import",
            .path = "gltf/test-mesh-a.glb",
        });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok()) << "resolve failures: " << report.failures.size();

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);

    ASSERT_EQ(data->nodes.size(), 3u);

    // Issue #195: deviceless GLB import attaches NO renderables (the 0x706
    // replacement needs a provisioned render program, whose shaders cannot
    // compile without a GPU device). Hierarchy + transforms are the contract.
    const auto* body = find_scene_node(*data, "body");
    ASSERT_NE(body, nullptr);
    EXPECT_FALSE(body->parent_id.has_value());
    EXPECT_FALSE(body->renderable_asset.has_value());

    const auto* turret = find_scene_node(*data, "turret");
    ASSERT_NE(turret, nullptr);
    ASSERT_TRUE(turret->parent_id.has_value());
    EXPECT_EQ(*turret->parent_id, "body");
    EXPECT_FALSE(turret->renderable_asset.has_value());

    const auto* gun = find_scene_node(*data, "gun");
    ASSERT_NE(gun, nullptr);
    ASSERT_TRUE(gun->parent_id.has_value());
    EXPECT_EQ(*gun->parent_id, "turret");
    EXPECT_FALSE(gun->renderable_asset.has_value());

    EXPECT_FLOAT_EQ(body->local.scale[0], 1.6399999856948853f);
    EXPECT_FLOAT_EQ(turret->local.translation[1], 1.6363635063171387f);
    EXPECT_FLOAT_EQ(gun->local.rotation_quat[2], 0.5323767066001892f);
}


// Issue #213 (Phase 1): per-component style mapping. Two distinct overrides on
// two different mesh indices produce renderables whose styles differ from each
// other and from the base; an un-overridden mesh keeps the base style. test-mesh-a.glb
// has three distinct meshes (body/turret/gun -> indices 0,1,2); override 0 and 1.
TEST(SceneAssetModule, GLBPerComponentStyleOverrides)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, WZ_TEST_FIXTURE_DIR };

    using namespace wz::engine::assets;

    // Base style: surface disabled, recognizable green. Each override flips
    // surface on with a distinct color so the resolved style is identifiable.
    MeshRenderStyleData base{};
    base.surface.enabled = false;

    MeshRenderStyleData style_a{};
    style_a.surface.enabled = true;
    style_a.surface.color[0] = 0.90f;  // reddish
    style_a.surface.color[1] = 0.10f;
    style_a.surface.color[2] = 0.10f;

    MeshRenderStyleData style_b{};
    style_b.surface.enabled = true;
    style_b.surface.color[0] = 0.10f;
    style_b.surface.color[1] = 0.10f;
    style_b.surface.color[2] = 0.90f;  // bluish

    const auto scene_asset = assets.scenes().create_scene_from_glb({
        .name = "tank1_styled",
        .path = "gltf/test-mesh-a.glb",
        .base_style = base,
        .style_overrides = {
            { .mesh_index = 0u, .style = style_a },
            { .mesh_index = 1u, .style = style_b },
        },
    });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok()) << "resolve failures: " << report.failures.size();

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->nodes.size(), 3u);

    // Issue #195: deviceless GLB import attaches NO renderables (the 0x706
    // replacement's program cannot be provisioned without a GPU device), so
    // the per-NODE style mapping is only observable with a device (the
    // renderable recipe bakes the style there; see the wozzits_app GLB style
    // suites). What remains observable — and asserted — deviceless: the import
    // registered and resolved the base + both override style ASSETS with the
    // authored contents. create_mesh_render_style is idempotent on
    // (name, style), so re-creating returns the import's keys.
    const auto* body = find_scene_node(*data, "body");
    const auto* turret = find_scene_node(*data, "turret");
    const auto* gun = find_scene_node(*data, "gun");
    ASSERT_NE(body, nullptr);
    ASSERT_NE(turret, nullptr);
    ASSERT_NE(gun, nullptr);
    EXPECT_FALSE(body->renderable_asset.has_value());
    EXPECT_FALSE(turret->renderable_asset.has_value());
    EXPECT_FALSE(gun->renderable_asset.has_value());

    const auto base_asset =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "tank1_styled/default_mesh_style",
            .style = base,
        });
    const auto style_a_asset =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "tank1_styled/mesh_style_0",
            .style = style_a,
        });
    const auto style_b_asset =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "tank1_styled/mesh_style_1",
            .style = style_b,
        });
    ASSERT_TRUE(base_asset.valid());
    ASSERT_TRUE(style_a_asset.valid());
    ASSERT_TRUE(style_b_asset.valid());

    const MeshRenderStyleData* base_data =
        assets.mesh_render_styles().get_mesh_render_style_data(
            assets.mesh_render_styles().get_mesh_render_style(base_asset));
    const MeshRenderStyleData* a_data =
        assets.mesh_render_styles().get_mesh_render_style_data(
            assets.mesh_render_styles().get_mesh_render_style(style_a_asset));
    const MeshRenderStyleData* b_data =
        assets.mesh_render_styles().get_mesh_render_style_data(
            assets.mesh_render_styles().get_mesh_render_style(style_b_asset));
    ASSERT_NE(base_data, nullptr);
    ASSERT_NE(a_data, nullptr);
    ASSERT_NE(b_data, nullptr);
    EXPECT_FALSE(base_data->surface.enabled);
    EXPECT_TRUE(a_data->surface.enabled);
    EXPECT_GT(a_data->surface.color[0], 0.5f);   // reddish (mesh 0)
    EXPECT_TRUE(b_data->surface.enabled);
    EXPECT_GT(b_data->surface.color[2], 0.5f);   // bluish (mesh 1)
}

// Issue #213 (Phase 1): the no-override default reproduces prior behavior — a
// single uniform style across all meshes (a valid scene, no regression).
TEST(SceneAssetModule, GLBNoOverrideDefaultIsUniform)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, WZ_TEST_FIXTURE_DIR };

    using namespace wz::engine::assets;

    const auto scene_asset = assets.scenes().create_scene_from_glb({
        .name = "tank1_uniform",
        .path = "gltf/test-mesh-a.glb",
    });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok()) << "resolve failures: " << report.failures.size();

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->nodes.size(), 3u);

    // Issue #195: deviceless the nodes carry no renderables (see the styled
    // test above); the no-override default contract is that ONE default style
    // asset was registered with the built-in style: surface disabled and
    // wireframe enabled. create_mesh_render_style is idempotent on
    // (name, style), so re-creating returns the import's key.
    const auto default_asset =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "tank1_uniform/default_mesh_style",
            .style = MeshRenderStyleData{},
        });
    ASSERT_TRUE(default_asset.valid());
    const MeshRenderStyleData* s =
        assets.mesh_render_styles().get_mesh_render_style_data(
            assets.mesh_render_styles().get_mesh_render_style(default_asset));
    ASSERT_NE(s, nullptr) << "default style did not resolve";
    EXPECT_FALSE(s->surface.enabled);
    EXPECT_TRUE(s->wireframe.enabled);

    for (const char* id : { "body", "turret", "gun" }) {
        const SceneNodeAsset* node = find_scene_node(*data, id);
        ASSERT_NE(node, nullptr);
        EXPECT_FALSE(node->renderable_asset.has_value());
    }
}

// Issue #213 (engine foundation for GLB mesh extraction): a "Scene from GLB"
// output now embeds each mesh-bearing node's RAW, OBJECT-SPACE geometry
// (SceneAssetData::glb_meshes) keyed by glTF mesh_index, and records each node's
// mesh_index. The "Mesh from GLB scene" extractor then pulls one node's geometry
// out as a standalone Mesh via a SCENE input. Positive path: extract node "body"
// from test-mesh-a.glb and confirm the output Mesh matches the raw body mesh straight
// from import_glb_meshes (verbatim object-space — no node transform baked in).
TEST(SceneAssetModule, ExtractsMeshFromGLBSceneNode)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        WZ_TEST_FIXTURE_DIR };

    using namespace wz::engine::assets;

    // Build a "Scene from GLB" so its output carries the embedded geometry.
    const auto scene_asset = assets.scenes().create_scene_from_glb({
        .name = "tank1_extract",
        .path = "gltf/test-mesh-a.glb",
    });
    ASSERT_TRUE(scene_asset.valid());

    // Extract the "body" node's mesh as a standalone Mesh asset.
    const auto body_mesh = assets.meshes().create_mesh_from_glb_scene({
        .name = "tank1_body_mesh",
        .scene = scene_asset.output,
        .node_id = "body",
    });
    ASSERT_TRUE(body_mesh.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok()) << "resolve failures: " << report.failures.size();

    // The source scene embeds the per-mesh geometry and the node's mesh_index.
    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    const auto* body_node = find_scene_node(*scene_data, "body");
    ASSERT_NE(body_node, nullptr);
    ASSERT_TRUE(body_node->mesh_index.has_value());
    EXPECT_FALSE(scene_data->glb_meshes.empty());
    ASSERT_TRUE(scene_data->glb_meshes.count(*body_node->mesh_index) != 0u);

    // The extracted Mesh resolves and is valid.
    const auto handle = assets.meshes().get_mesh(body_mesh);
    ASSERT_TRUE(handle.valid());
    const MeshData* extracted = assets.meshes().get_mesh_data(handle);
    ASSERT_NE(extracted, nullptr);
    ASSERT_TRUE(extracted->valid());

    // Independently import the raw GLB meshes and compare against the body
    // node's mesh_index: the extracted geometry must match verbatim (same
    // vertex/index counts and identical first-vertex object-space position —
    // no node transform baked into the extracted mesh).
    const auto bytes = wz::fs::read_file(
        wz::fs::join(WZ_TEST_FIXTURE_DIR, "gltf/test-mesh-a.glb"));
    ASSERT_TRUE(static_cast<bool>(bytes));
    ImportedGLTFMeshSet imported{};
    ASSERT_TRUE(import_glb_meshes(
        bytes.value.data(), bytes.value.size(), GLTFImportOptions{}, imported));
    ASSERT_LT(*body_node->mesh_index, imported.meshes.size());
    const MeshData& raw_body = imported.meshes[*body_node->mesh_index].mesh;

    EXPECT_EQ(extracted->vertex_count(), raw_body.vertex_count());
    EXPECT_EQ(extracted->index_count(), raw_body.index_count());
    ASSERT_FALSE(raw_body.vertices.empty());
    ASSERT_FALSE(extracted->vertices.empty());
    EXPECT_FLOAT_EQ(
        extracted->vertices[0].position[0], raw_body.vertices[0].position[0]);
    EXPECT_FLOAT_EQ(
        extracted->vertices[0].position[1], raw_body.vertices[0].position[1]);
    EXPECT_FLOAT_EQ(
        extracted->vertices[0].position[2], raw_body.vertices[0].position[2]);
}

// Negative: extracting an unknown node id fails the compile (the mesh asset
// never resolves), leaving resolve_all reporting the failure.
TEST(SceneAssetModule, MeshFromGLBSceneUnknownNodeFails)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        WZ_TEST_FIXTURE_DIR };

    using namespace wz::engine::assets;

    const auto scene_asset = assets.scenes().create_scene_from_glb({
        .name = "tank1_extract_unknown",
        .path = "gltf/test-mesh-a.glb",
    });
    ASSERT_TRUE(scene_asset.valid());

    const auto bad_mesh = assets.meshes().create_mesh_from_glb_scene({
        .name = "tank1_nope_mesh",
        .scene = scene_asset.output,
        .node_id = "does_not_exist",
    });
    ASSERT_TRUE(bad_mesh.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    EXPECT_FALSE(report.ok());

    // The bad extractor produced no usable mesh.
    const auto handle = assets.meshes().get_mesh(bad_mesh);
    EXPECT_FALSE(handle.valid());

    // Issue #212: the failure carries the compiler's human-readable reason so
    // the editor can surface it on the failing node. The extractor reports the
    // missing node id verbatim.
    bool saw_detail = false;
    for (const auto& failure : report.failures) {
        if (failure.detail.find("does_not_exist") != std::string::npos) {
            EXPECT_NE(
                failure.detail.find("not found in source scene"),
                std::string::npos)
                << "detail was: " << failure.detail;
            saw_detail = true;
        }
    }
    EXPECT_TRUE(saw_detail)
        << "no resolve failure carried the extractor's reason detail";
}

// Negative: a group/structure node (no mesh) cannot be extracted. Uses an
// inline glTF hierarchy whose "tank_body" node carries a transform + child but
// no mesh, so the extractor fails with "no mesh (group/structure node)".
TEST(SceneAssetModule, MeshFromGLBSceneGroupNodeFails)
{
    const char* gltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [
    { "name": "group_scene", "nodes": [0] }
  ],
  "nodes": [
    {
      "name": "tank_body",
      "translation": [1, 0, 2],
      "children": [1]
    },
    {
      "name": "turret",
      "translation": [0, 3, 0]
    }
  ]
})";

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_mesh_from_glb_group_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    const auto rel_path = write_scene_json(root, "group_hierarchy.gltf", gltf);

    const auto scene_asset = assets.scenes().create_scene_from_glb({
        .name = "group_import",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());

    const auto group_mesh = assets.meshes().create_mesh_from_glb_scene({
        .name = "group_body_mesh",
        .scene = scene_asset.output,
        .node_id = "tank_body",
    });
    ASSERT_TRUE(group_mesh.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    EXPECT_FALSE(report.ok());

    const auto handle = assets.meshes().get_mesh(group_mesh);
    EXPECT_FALSE(handle.valid());
}

// Issue #213: FileCarrierAssetModule::resolve_path strips a single matched
// surrounding pair of ASCII double-quotes before resolving. Windows Explorer's
// "Copy as path" wraps a path in double-quotes ("C:\...\test-mesh-a.glb"), which then
// can't be opened. A quote-wrapped path must resolve to the SAME result as the
// unwrapped one; interior quotes and a lone unbalanced quote are left untouched.
TEST(SceneAssetModule, ResolvePathStripsSurroundingQuotes)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        WZ_TEST_FIXTURE_DIR };

    auto& files = assets.files();

    // An already-absolute path is returned verbatim, so a quote-wrapped form must
    // resolve to exactly the bare path (the surrounding pair removed).
#ifdef _WIN32
    const wz::fs::Path bare = "C:\\models\\test-mesh-a.glb";
#else
    const wz::fs::Path bare = "/models/test-mesh-a.glb";
#endif
    const wz::fs::Path quoted = "\"" + bare + "\"";
    EXPECT_EQ(files.resolve_path(quoted), files.resolve_path(bare));
    EXPECT_EQ(files.resolve_path(quoted), bare);

    // A relative quote-wrapped path resolves identically to the unwrapped one
    // (both joined against the resource root).
    EXPECT_EQ(
        files.resolve_path("\"gltf/test-mesh-a.glb\""),
        files.resolve_path("gltf/test-mesh-a.glb"));

    // Only a matched surrounding pair is stripped: a lone leading quote and
    // interior quotes are preserved (the path is altered only at both ends).
    EXPECT_EQ(files.resolve_path("\"only_leading"), files.resolve_path("\"only_leading"));
    EXPECT_NE(files.resolve_path("\"only_leading"), files.resolve_path("only_leading"));
}

