#include "scene_asset_module_test_support.h"

#include <engine/assets/scene/scene_compilers.h>
#include <engine/assets/scene/scene_json_export.h>
#include <engine/assets/scene/scenelet_authoring.h>

#include <external/json/json_parser.h>
#include <external/json/json_writer.h>

#include <string>

namespace
{
    using wz::engine::assets::make_minimal_scenelet;
    using wz::engine::assets::SceneAssetData;

    // Serialize a freshly minted scenelet exactly the way wz_host_create_scenelet
    // does: build the data, hand it to the ONE exporter, write the text.
    std::string minted_scenelet_text(const std::string& name)
    {
        return wz::json::serialize_json(
            wz::engine::assets::export_scene_to_json_document(
                make_minimal_scenelet(name)));
    }
}

TEST(SceneletAuthoring, MintsASingleNamedRootNode)
{
    const SceneAssetData scene = make_minimal_scenelet("tank");

    EXPECT_EQ(scene.name, "tank");
    ASSERT_EQ(scene.nodes.size(), 1u);

    const auto& root = scene.nodes.front();
    EXPECT_EQ(root.name, "tank");
    EXPECT_FALSE(root.parent_id.has_value());  // a root carries nullopt
    EXPECT_TRUE(root.visible);
    EXPECT_TRUE(root.active);

    // Identity transform: the scenelet is placed purely by its spawn transform.
    EXPECT_FLOAT_EQ(root.local.translation[0], 0.0f);
    EXPECT_FLOAT_EQ(root.local.translation[1], 0.0f);
    EXPECT_FLOAT_EQ(root.local.translation[2], 0.0f);
    EXPECT_FLOAT_EQ(root.local.rotation_quat[3], 1.0f);
    EXPECT_FLOAT_EQ(root.local.scale[0], 1.0f);
    EXPECT_FLOAT_EQ(root.local.scale[1], 1.0f);
    EXPECT_FLOAT_EQ(root.local.scale[2], 1.0f);

    // Nothing else is invented: a fresh scenelet is empty, not a starter kit.
    EXPECT_TRUE(scene.lights.empty());
    EXPECT_FALSE(root.renderable.has_value());
}

// THE point of issue #271. A minted scenelet must be readable by the engine's
// OWN parser -- previously the editor hand-wrote this document in C#, so the
// only thing binding the writer to the reader was that someone had typed the
// same key names on both sides, with no test linking them.
TEST(SceneletAuthoring, MintedDocumentRoundTripsThroughTheSceneParser)
{
    wz::Logger logger;

    const wz::json::JSONParseResult parsed =
        wz::json::parse_json_string(minted_scenelet_text("tank"));
    ASSERT_TRUE(parsed.ok) << parsed.error.message;

    const auto reparsed =
        wz::engine::assets::internal::parse_scene_data_from_json(
            parsed.document, logger);
    ASSERT_TRUE(reparsed.has_value());

    EXPECT_EQ(reparsed->name, "tank");
    ASSERT_EQ(reparsed->nodes.size(), 1u);
    EXPECT_EQ(reparsed->nodes.front().name, "tank");
    EXPECT_FALSE(reparsed->nodes.front().parent_id.has_value());
    EXPECT_TRUE(reparsed->nodes.front().visible);
    EXPECT_TRUE(reparsed->nodes.front().active);

    // valid() is what the scene compiler gates on: a document that parses but
    // carries no nodes would be accepted here and rejected downstream.
    EXPECT_TRUE(reparsed->valid());
}

TEST(SceneletAuthoring, RejectsNamesThatCouldEscapeTheSceneletsFolder)
{
    using wz::engine::assets::is_valid_scenelet_name;

    EXPECT_TRUE(is_valid_scenelet_name("tank"));
    EXPECT_TRUE(is_valid_scenelet_name("tank_mk2"));
    EXPECT_TRUE(is_valid_scenelet_name("tank mk2"));

    EXPECT_FALSE(is_valid_scenelet_name(""));
    EXPECT_FALSE(is_valid_scenelet_name("   "));
    EXPECT_FALSE(is_valid_scenelet_name("../escape"));
    EXPECT_FALSE(is_valid_scenelet_name("sub/tank"));
    EXPECT_FALSE(is_valid_scenelet_name("sub\\tank"));
    EXPECT_FALSE(is_valid_scenelet_name("C:tank"));
    EXPECT_FALSE(is_valid_scenelet_name("tank?"));
    EXPECT_FALSE(is_valid_scenelet_name("tank*"));
    EXPECT_FALSE(is_valid_scenelet_name(std::string("tank\nx")));
}

// The folder convention has ONE owner: register_scenelet_prefabs (which builds
// the catalog) and wz_host_create_scenelet (which writes the file) both go
// through these, so the minting verb cannot put a scenelet where the catalog
// will not look for it.
TEST(SceneletAuthoring, DerivesTheSceneletsFolderFromTheSceneFile)
{
    using wz::engine::assets::scenelet_relative_path;
    using wz::engine::assets::scenelets_folder_for_scene;

    // Composed with wz::fs::join, exactly as register_scenelet_prefabs composes
    // the catalog paths -- asserting the STRUCTURE (scene dir + "scenelets" +
    // "<name>.scene.json") rather than a platform separator, so the verb and the
    // catalog stay expressed in the same terms.
    EXPECT_EQ(scenelets_folder_for_scene("projects/demo/scene.json"),
              wz::fs::join("projects/demo", "scenelets"));
    EXPECT_EQ(scenelet_relative_path("projects/demo/scene.json", "tank"),
              wz::fs::join(
                  wz::fs::join("projects/demo", "scenelets"),
                  "tank.scene.json"));

    // A scene sitting at the resource root yields a bare "scenelets".
    EXPECT_EQ(scenelets_folder_for_scene("scene.json"), "scenelets");
    EXPECT_EQ(scenelet_relative_path("scene.json", "tank"),
              wz::fs::join("scenelets", "tank.scene.json"));
}
