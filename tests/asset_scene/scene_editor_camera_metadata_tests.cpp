// tests/asset_scene/scene_editor_camera_metadata_tests.cpp
//
// The editor viewport's free-fly camera is persisted in the scene file's
// top-level "scene_editor_metadata" block so a project reopens looking from
// where it was left. These tests lock the pure (de)serialization seam
// (set_/read_scene_document_editor_camera) that WozzitsApp_v1::save_scene /
// load_scene drive: a round trip, preservation of sibling data, and the
// absent / partial-block fallbacks.

#include <engine/assets/scene/scene_json_export.h>

#include <external/json/json_parser.h>
#include <external/json/json_read_helpers.h>
#include <external/json/json_writer.h>

#include <gtest/gtest.h>

namespace
{
    using wz::engine::assets::SceneEditorCameraMetadata;
    using wz::engine::assets::read_scene_document_editor_camera;
    using wz::engine::assets::set_scene_document_editor_camera;

    // Serialize `document`, reparse it, and return the reparsed result — the
    // exact disk round trip save_scene + load_scene perform.
    wz::json::JSONParseResult round_trip(const wz::json::JSONDocument& document)
    {
        return wz::json::parse_json_string(wz::json::serialize_json(document));
    }

    wz::json::JSONDocument parse(const char* text)
    {
        wz::json::JSONParseResult parsed =
            wz::json::parse_json_string(std::string{ text });
        EXPECT_TRUE(parsed.ok);
        return std::move(parsed.document);
    }
}

TEST(SceneEditorCameraMetadata, RoundTripsThroughSceneDocument)
{
    wz::json::JSONDocument document = parse(R"({
      "schema": "wozzits.scene.v0",
      "name": "scene",
      "nodes": []
    })");

    SceneEditorCameraMetadata in;
    in.position[0] = -392.5f;
    in.position[1] = 258.1875f;
    in.position[2] = 91.75f;
    in.orientation[0] = -0.15275568f;
    in.orientation[1] = -0.82532024f;
    in.orientation[2] = 0.28689712f;
    in.orientation[3] = -0.46173829f;
    in.move_speed = 8.0f;
    in.look_speed = 0.0005f;
    in.boost_multiplier = 4.0f;
    in.roll_speed = 1.5f;

    set_scene_document_editor_camera(document, in);

    const wz::json::JSONParseResult reparsed = round_trip(document);
    ASSERT_TRUE(reparsed.ok);

    const auto out = read_scene_document_editor_camera(reparsed.document);
    ASSERT_TRUE(out.has_value());
    for (int i = 0; i < 3; ++i) {
        EXPECT_FLOAT_EQ(out->position[i], in.position[i]);
    }
    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(out->orientation[i], in.orientation[i]);
    }
    EXPECT_FLOAT_EQ(out->move_speed, in.move_speed);
    EXPECT_FLOAT_EQ(out->look_speed, in.look_speed);
    EXPECT_FLOAT_EQ(out->boost_multiplier, in.boost_multiplier);
    EXPECT_FLOAT_EQ(out->roll_speed, in.roll_speed);
}

TEST(SceneEditorCameraMetadata, UpsertPreservesNodesAndMetadataSiblings)
{
    // A pre-existing metadata block with a sibling "note" and an old camera, plus
    // other top-level scene data (nodes, defaults) that must all survive.
    wz::json::JSONDocument document = parse(R"({
      "schema": "wozzits.scene.v0",
      "name": "scene",
      "nodes": [ { "id": "root" } ],
      "defaults": { "active_camera": "1" },
      "scene_editor_metadata": {
        "schema": "wozzits.scene_editor_metadata.v1",
        "version": 1,
        "note": "keep me",
        "camera": { "position": [1, 2, 3] }
      }
    })");

    SceneEditorCameraMetadata cam;
    cam.position[0] = 10.0f;
    cam.position[1] = 20.0f;
    cam.position[2] = 30.0f;
    set_scene_document_editor_camera(document, cam);

    const wz::json::JSONParseResult reparsed = round_trip(document);
    ASSERT_TRUE(reparsed.ok);
    const wz::json::JSONValue& root = *reparsed.document.root;

    // The camera was replaced with the new pose...
    const auto out = read_scene_document_editor_camera(reparsed.document);
    ASSERT_TRUE(out.has_value());
    EXPECT_FLOAT_EQ(out->position[0], 10.0f);
    EXPECT_FLOAT_EQ(out->position[2], 30.0f);

    // ...while every sibling — nodes, defaults, and the metadata "note" — stayed.
    const wz::json::JSONValue* nodes = wz::json::find_member(root, "nodes");
    ASSERT_NE(nodes, nullptr);
    EXPECT_EQ(nodes->kind, wz::json::JSONValueKind::Array);
    ASSERT_EQ(nodes->array_values.size(), 1u);

    const wz::json::JSONValue* defaults =
        wz::json::find_member(root, "defaults");
    ASSERT_NE(defaults, nullptr);
    EXPECT_EQ(wz::json::read_string(*defaults, "active_camera"), "1");

    const wz::json::JSONValue* metadata =
        wz::json::find_member(root, "scene_editor_metadata");
    ASSERT_NE(metadata, nullptr);
    EXPECT_EQ(wz::json::read_string(*metadata, "note"), "keep me");
}

TEST(SceneEditorCameraMetadata, UpsertCreatesMetadataBlockWhenAbsent)
{
    wz::json::JSONDocument document = parse(R"({
      "schema": "wozzits.scene.v0",
      "name": "scene",
      "nodes": []
    })");

    SceneEditorCameraMetadata cam;
    cam.position[1] = 5.0f;
    set_scene_document_editor_camera(document, cam);

    const wz::json::JSONParseResult reparsed = round_trip(document);
    ASSERT_TRUE(reparsed.ok);

    const wz::json::JSONValue* metadata =
        wz::json::find_member(*reparsed.document.root, "scene_editor_metadata");
    ASSERT_NE(metadata, nullptr);
    EXPECT_EQ(
        wz::json::read_string(*metadata, "schema"),
        "wozzits.scene_editor_metadata.v1");

    const auto out = read_scene_document_editor_camera(reparsed.document);
    ASSERT_TRUE(out.has_value());
    EXPECT_FLOAT_EQ(out->position[1], 5.0f);
}

TEST(SceneEditorCameraMetadata, ReadReturnsNulloptWhenBlockAbsent)
{
    const wz::json::JSONDocument document = parse(R"({
      "schema": "wozzits.scene.v0",
      "name": "scene",
      "nodes": []
    })");
    EXPECT_FALSE(read_scene_document_editor_camera(document).has_value());
}

TEST(SceneEditorCameraMetadata, PartialCameraBlockKeepsFieldDefaults)
{
    // Only a position is authored: orientation + tuning keep struct defaults,
    // so a hand-authored or older partial block still loads a usable camera.
    const wz::json::JSONDocument document = parse(R"({
      "schema": "wozzits.scene.v0",
      "scene_editor_metadata": {
        "camera": { "position": [7, 8, 9] }
      }
    })");

    const auto out = read_scene_document_editor_camera(document);
    ASSERT_TRUE(out.has_value());
    EXPECT_FLOAT_EQ(out->position[0], 7.0f);
    EXPECT_FLOAT_EQ(out->position[2], 9.0f);

    const SceneEditorCameraMetadata defaults;
    EXPECT_FLOAT_EQ(out->orientation[3], defaults.orientation[3]);
    EXPECT_FLOAT_EQ(out->move_speed, defaults.move_speed);
    EXPECT_FLOAT_EQ(out->boost_multiplier, defaults.boost_multiplier);
    EXPECT_FLOAT_EQ(out->roll_speed, defaults.roll_speed);
}
