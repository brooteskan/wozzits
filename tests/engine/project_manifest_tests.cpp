#include <gtest/gtest.h>

#include <engine/project/project_manifest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    struct TempProjectRoot
    {
        fs::path root;

        TempProjectRoot()
        {
            const auto suffix = std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count());
            root = fs::temp_directory_path()
                / ("wozzits_project_manifest_tests_" + suffix);
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        ~TempProjectRoot()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
    };

    void write_text_file(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        ASSERT_TRUE(file.good()) << "failed to open " << path.string();
        file << text;
    }

    fs::path manifest_path(const fs::path& project_root)
    {
        return project_root / ".wozzits" / "project.json";
    }
}

TEST(ProjectManifest, LoadsRelativeProjectRootFromResourceRoot)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "projects" / "sample";
    const std::string absolute_module =
        (temp.root / "external" / "module").generic_string();

    write_text_file(
        manifest_path(project_root),
        std::string{
            R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Sample",
  "rhi_render_path": true,
  "scene": "scenes/main.scene.json",
  "asset_graph": "assets.graph.json",
  "behavior_project_folder": "behavior",
  "behavior_module_folder": ")json" }
            + absolute_module
            + R"json("
})json");

    const auto loaded = wz::engine::project::load_project_manifest(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = "projects/sample",
            .resource_root = temp.root.string(),
        });

    ASSERT_TRUE(loaded.ok) << loaded.error;
    EXPECT_EQ(loaded.manifest.root, "projects/sample");
    EXPECT_EQ(
        loaded.manifest.manifest_path,
        wz::engine::project::project_manifest_path("projects/sample"));
    EXPECT_EQ(loaded.manifest.schema, "wozzits.project.v1");
    EXPECT_EQ(loaded.manifest.format_version, 1u);
    EXPECT_EQ(loaded.manifest.name, "Sample");
    EXPECT_TRUE(loaded.manifest.rhi_render_path);
    EXPECT_EQ(
        loaded.manifest.scene_path,
        wz::fs::join("projects/sample", "scenes/main.scene.json"));
    EXPECT_EQ(
        loaded.manifest.asset_graph_path,
        wz::fs::join("projects/sample", "assets.graph.json"));
    EXPECT_EQ(
        loaded.manifest.behavior_project_folder,
        wz::fs::join("projects/sample", "behavior"));
    EXPECT_EQ(loaded.manifest.behavior_module_folder, absolute_module);
}

TEST(ProjectManifest, MinimalManifestAllowsEmptyEditorProject)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "empty_project";

    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Empty"
})json");

    const auto loaded = wz::engine::project::load_project_manifest(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });

    ASSERT_TRUE(loaded.ok) << loaded.error;
    EXPECT_EQ(loaded.manifest.root, project_root.string());
    EXPECT_EQ(loaded.manifest.name, "Empty");
    EXPECT_FALSE(loaded.manifest.rhi_render_path);
    EXPECT_TRUE(loaded.manifest.scene_path.empty());
    EXPECT_TRUE(loaded.manifest.asset_graph_path.empty());
    EXPECT_TRUE(loaded.manifest.behavior_project_folder.empty());
    EXPECT_TRUE(loaded.manifest.behavior_module_folder.empty());
}
