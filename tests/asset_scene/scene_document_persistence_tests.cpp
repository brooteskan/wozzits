// tests/asset_scene/scene_document_persistence_tests.cpp
//
// The scene library owns the one wz::fs-backed reader/writer for scene documents
// (issue #299): update_scene_document (read-modify-write, change-gated) and
// write_scene_document (fresh write). These pin the behaviours the four scene-
// document writers now depend on:
//   - a failed write is reported as a non-None FileError, never swallowed the way
//     the old raw std::ofstream's destructor-flush could (including for a small
//     document that fit the stream buffer -- the historical false-positive);
//   - a 0-change update leaves the file's exact bytes untouched (the idempotence
//     wz_host_set_scene_file_behavior_config relies on);
//   - a missing or corrupt existing file is reported (NotFound / InvalidPath)
//     WITHOUT calling the edit, so create-if-missing callers can branch on it;
//   - a real round trip preserves the document's non-node data.

#include <engine/assets/scene/scene_json_export.h>

#include <external/json/json_parser.h>
#include <external/json/json_writer.h>

#include <file/filesystem.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
    using wz::engine::assets::update_scene_document;
    using wz::engine::assets::write_scene_document;

    wz::json::JSONDocument parse(const std::string& text)
    {
        wz::json::JSONParseResult parsed = wz::json::parse_json_string(text);
        EXPECT_TRUE(parsed.ok) << parsed.error.message;
        return std::move(parsed.document);
    }

    std::string scratch_path(const char* name)
    {
        return wz::fs::join(wz::fs::temp_directory_path(), name);
    }
}

// A directory is a deterministic unwritable target: CreateFileW for GENERIC_WRITE
// on a directory fails, so write_scene_document must surface a non-None FileError
// rather than report the truncated/absent write as success.
TEST(SceneDocumentPersistence, ReportsAFailedWrite)
{
    const std::string dir = scratch_path("wz_scene_persist_unwritable_dir");
    wz::fs::create_directories(dir);
    ASSERT_TRUE(wz::fs::is_directory(dir));

    const wz::json::JSONDocument doc = parse(R"({"nodes":[]})");
    EXPECT_NE(write_scene_document(dir, doc), wz::fs::FileError::None);

    wz::fs::remove_directory(dir);
}

// The specific false-positive #299 calls out: the old raw std::ofstream buffered
// a small scene (below the ~4 KiB MSVC filebuf), so good() passed and the dirty
// flag was cleared BEFORE the failing flush in the filebuf destructor -- a tiny
// document that never reached disk was reported saved. The wz::fs writer checks
// the CreateFileW/WriteFile result, so even a few bytes to an unwritable target
// must report a non-None error.
TEST(SceneDocumentPersistence, SmallDocumentWriteFailureIsNotReportedAsSuccess)
{
    const std::string dir =
        scratch_path("wz_scene_persist_small_unwritable_dir");
    wz::fs::create_directories(dir);
    ASSERT_TRUE(wz::fs::is_directory(dir));

    const wz::json::JSONDocument tiny = parse("{}");
    ASSERT_LT(wz::json::serialize_json(tiny).size(), 4096u)
        << "the document must be below the historical stream-buffer size for "
           "this to exercise the buffered-flush false-positive";

    EXPECT_NE(write_scene_document(dir, tiny), wz::fs::FileError::None);

    wz::fs::remove_directory(dir);
}

// A fresh write followed by an in-place update: the update must read the file
// back, apply the edit, and preserve every non-node member it did not touch.
TEST(SceneDocumentPersistence, WriteThenUpdatePreservesUnrelatedData)
{
    const std::string path = scratch_path("wz_scene_persist_roundtrip.json");
    wz::fs::remove_file(path);  // clean slate for a repeatable run

    const wz::json::JSONDocument doc = parse(
        R"({"schema":"wozzits.scene.v0","nodes":[],"keep":"DATA"})");
    ASSERT_EQ(write_scene_document(path, doc), wz::fs::FileError::None);

    wz::engine::assets::SceneEditorCameraMetadata cam;
    cam.position[0] = 1.0f;
    cam.position[1] = 2.0f;
    cam.position[2] = 3.0f;
    const wz::fs::FileError err = update_scene_document(
        path,
        [&](wz::json::JSONDocument& document) {
            wz::engine::assets::set_scene_document_editor_camera(document, cam);
            return true;
        });
    EXPECT_EQ(err, wz::fs::FileError::None);

    const wz::fs::FileResult<std::string> text = wz::fs::read_file_text(path);
    ASSERT_TRUE(static_cast<bool>(text));
    EXPECT_NE(text.value.find("wozzits.scene.v0"), std::string::npos);
    EXPECT_NE(text.value.find("\"keep\""), std::string::npos);

    const wz::json::JSONDocument reloaded = parse(text.value);
    const auto read_cam =
        wz::engine::assets::read_scene_document_editor_camera(reloaded);
    ASSERT_TRUE(read_cam.has_value());
    EXPECT_FLOAT_EQ(read_cam->position[0], 1.0f);
    EXPECT_FLOAT_EQ(read_cam->position[2], 3.0f);

    wz::fs::remove_file(path);
}

// The change-gate: an edit that reports no change must leave the file's bytes
// EXACTLY as they were -- no reformat, no rewrite. This is what keeps
// wz_host_set_scene_file_behavior_config idempotent (a scenelet that is also the
// open scene must not be written twice). The on-disk bytes are deliberately
// compact so a re-serialize (pretty, 2-space) would change them, letting the test
// distinguish "skipped the write" from "rewrote identical content".
TEST(SceneDocumentPersistence, ZeroChangesLeavesBytesUntouched)
{
    const std::string path = scratch_path("wz_scene_persist_idempotent.json");
    const std::string original = R"({"keep":"UNTOUCHED","nodes":[]})";
    ASSERT_EQ(wz::fs::write_file_text(path, original), wz::fs::FileError::None);

    const wz::fs::FileError err = update_scene_document(
        path, [](wz::json::JSONDocument&) { return false; });
    EXPECT_EQ(err, wz::fs::FileError::None);

    const wz::fs::FileResult<std::string> after = wz::fs::read_file_text(path);
    ASSERT_TRUE(static_cast<bool>(after));
    EXPECT_EQ(after.value, original) << "a 0-change update rewrote the file";

    wz::fs::remove_file(path);
}

// The other side of the gate: an edit that reports a change is written back
// (serialized fresh, so the compact original is reformatted), and unrelated data
// survives the rewrite.
TEST(SceneDocumentPersistence, AChangeIsWrittenBack)
{
    const std::string path = scratch_path("wz_scene_persist_rewrite.json");
    const std::string original = R"({"keep":"UNTOUCHED","nodes":[]})";
    ASSERT_EQ(wz::fs::write_file_text(path, original), wz::fs::FileError::None);

    const wz::fs::FileError err = update_scene_document(
        path,
        [](wz::json::JSONDocument& document) {
            wz::engine::assets::set_scene_document_editor_camera(
                document, wz::engine::assets::SceneEditorCameraMetadata{});
            return true;
        });
    EXPECT_EQ(err, wz::fs::FileError::None);

    const wz::fs::FileResult<std::string> after = wz::fs::read_file_text(path);
    ASSERT_TRUE(static_cast<bool>(after));
    EXPECT_NE(after.value, original) << "a change must be written back";
    EXPECT_NE(after.value.find("\"keep\""), std::string::npos)
        << "unrelated data must survive the rewrite";
    EXPECT_NE(after.value.find("scene_editor_metadata"), std::string::npos);

    wz::fs::remove_file(path);
}

// A missing file is reported (NotFound) and the edit is NOT run, so a
// create-if-missing caller (save_scene's first save) can branch on it and the
// update never fabricates a file behind the caller's back.
TEST(SceneDocumentPersistence, UpdateReportsMissingFileWithoutCallingEdit)
{
    const std::string path = scratch_path("wz_scene_persist_absent.json");
    wz::fs::remove_file(path);
    ASSERT_FALSE(wz::fs::exists(path));

    bool edit_called = false;
    const wz::fs::FileError err = update_scene_document(
        path,
        [&](wz::json::JSONDocument&) {
            edit_called = true;
            return true;
        });

    EXPECT_EQ(err, wz::fs::FileError::NotFound);
    EXPECT_FALSE(edit_called) << "edit must not run when the file is unreadable";
    EXPECT_FALSE(wz::fs::exists(path)) << "update must not create a missing file";
}

// A file that exists but is not valid JSON is reported as InvalidPath (distinct
// from a genuine read error), again without calling the edit -- so save_scene can
// treat a corrupt file the way it treats an absent one (emit fresh) rather than
// silently patch nothing.
TEST(SceneDocumentPersistence, UpdateReportsCorruptFileAsInvalidPath)
{
    const std::string path = scratch_path("wz_scene_persist_corrupt.json");
    ASSERT_EQ(wz::fs::write_file_text(path, "{ this is not valid json"),
              wz::fs::FileError::None);

    bool edit_called = false;
    const wz::fs::FileError err = update_scene_document(
        path,
        [&](wz::json::JSONDocument&) {
            edit_called = true;
            return true;
        });

    EXPECT_EQ(err, wz::fs::FileError::InvalidPath);
    EXPECT_FALSE(edit_called);

    wz::fs::remove_file(path);
}
