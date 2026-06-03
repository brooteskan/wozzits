#include "scene_asset_module_test_support.h"

TEST(SceneDescriptorValidation, RejectsMissingInputMap)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "bad_input_receiver",
  "nodes": [{
    "id": "n",
    "input_receiver": {}
  }]
})";
    EXPECT_FALSE(scene_json_compiles("missing_input_map", json));
}

TEST(SceneDescriptorValidation, RejectsEmptyInputMap)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "empty_input_map",
  "nodes": [{
    "id": "n",
    "input_receiver": { "input_map": "" }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("empty_input_map", json));
}

TEST(SceneDescriptorValidation, RejectsNegativeMoveSpeed)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "neg_move_speed",
  "nodes": [{
    "id": "n",
    "flying_camera_controller": { "move_speed": -1.0 }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("neg_move_speed", json));
}

TEST(SceneDescriptorValidation, RejectsNegativeLookSpeed)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "neg_look_speed",
  "nodes": [{
    "id": "n",
    "flying_camera_controller": { "look_speed": -0.001 }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("neg_look_speed", json));
}

TEST(SceneDescriptorValidation, RejectsNegativeBoostMultiplier)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "neg_boost",
  "nodes": [{
    "id": "n",
    "flying_camera_controller": { "boost_multiplier": -2.0 }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("neg_boost", json));
}

TEST(SceneDescriptorValidation, RejectsNegativeRollSpeed)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "neg_roll",
  "nodes": [{
    "id": "n",
    "flying_camera_controller": { "roll_speed": -0.5 }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("neg_roll", json));
}

TEST(SceneDescriptorValidation, RejectsEmptyEventChannels)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "empty_channels",
  "nodes": [{
    "id": "n",
    "event_listener": { "channels": [] }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("empty_channels", json));
}

TEST(SceneDescriptorValidation, RejectsGroundBoundaryMissingBounds)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "ground_boundary_missing_bounds",
  "nodes": [{
    "id": "surface",
    "ground_boundary": { "min": [-1, 0, -1] }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("ground_boundary_missing_bounds", json));
}

TEST(SceneDescriptorValidation, RejectsGroundBoundaryInvertedBounds)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "ground_boundary_inverted_bounds",
  "nodes": [{
    "id": "surface",
    "ground_boundary": {
      "min": [5, 0, -1],
      "max": [-5, 0, 1]
    }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("ground_boundary_inverted_bounds", json));
}

TEST(SceneDescriptorValidation, RejectsAllBlankEventChannels)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "blank_channels",
  "nodes": [{
    "id": "n",
    "event_listener": { "channels": ["", ""] }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("blank_channels", json));
}

TEST(SceneDescriptorValidation, AcceptsZeroSpeedValues)
{
    // Zero is a valid edge case — means "no movement" until overridden.
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "zero_speeds",
  "nodes": [{
    "id": "n",
    "flying_camera_controller": {
      "move_speed": 0.0,
      "look_speed": 0.0,
      "boost_multiplier": 0.0,
      "roll_speed": 0.0
    }
  }]
})";
    EXPECT_TRUE(scene_json_compiles("zero_speeds", json));
}

