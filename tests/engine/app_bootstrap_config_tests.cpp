#include <gtest/gtest.h>

#include <engine/app/app_bootstrap_config.h>

#include <file/filesystem.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

// Seam 1 of issue #334: the bundle bootstrap config the standalone runtime reads
// on a no-argument launch. These exercise the parse -> resolved-options step in
// isolation (no device, no window), which is exactly what makes the seam small
// and independently testable.

namespace
{
    namespace fs = std::filesystem;

    // A throwaway directory that stands in for a bundle root: the config and its
    // (notional) referenced files resolve against it, and it is removed on teardown.
    struct TempBundleDir
    {
        fs::path root;

        TempBundleDir()
        {
            const auto suffix = std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count());
            root = fs::temp_directory_path()
                / ("wozzits_app_bootstrap_tests_" + suffix);
            std::error_code ec;
            fs::remove_all(root, ec);
            fs::create_directories(root);
        }

        ~TempBundleDir()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        [[nodiscard]] std::string base() const { return root.string(); }
    };

    void write_config(const fs::path& base_dir, const std::string& text)
    {
        fs::create_directories(base_dir);
        std::ofstream file(base_dir / "wozzits_app.json", std::ios::binary);
        ASSERT_TRUE(file.good());
        file << text;
    }
}

TEST(AppBootstrapConfig, ResolvesRelativePathsAgainstBaseDir)
{
    TempBundleDir bundle;
    write_config(
        bundle.root,
        R"json({
  "schema": "wozzits.app.v1",
  "formatVersion": 1,
  "resource_root": "resources",
  "asset_graph": "project/assets.graph.json",
  "scene": "project/scene.json",
  "behavior_modules": "behavior"
})json");

    const auto result =
        wz::app::load_app_bootstrap_config(bundle.base());

    ASSERT_TRUE(result.valid()) << result.error;
    EXPECT_EQ(result.config.schema, "wozzits.app.v1");
    EXPECT_EQ(result.config.format_version, 1u);
    EXPECT_EQ(
        result.config.resource_root,
        wz::fs::join(bundle.base(), "resources"));
    EXPECT_EQ(
        result.config.asset_graph,
        wz::fs::join(bundle.base(), "project/assets.graph.json"));
    EXPECT_EQ(
        result.config.scene,
        wz::fs::join(bundle.base(), "project/scene.json"));
    EXPECT_EQ(
        result.config.behavior_modules,
        wz::fs::join(bundle.base(), "behavior"));
}

TEST(AppBootstrapConfig, MissingFileReportsMissingNotInvalid)
{
    TempBundleDir bundle;  // created empty, no wozzits_app.json written

    const auto result =
        wz::app::load_app_bootstrap_config(bundle.base());

    EXPECT_TRUE(result.missing());
    EXPECT_FALSE(result.valid());
    // Missing is the developer / CLI-driven case, so the caller can fall back to
    // CLI args; it is distinct from a present-but-broken config.
    EXPECT_EQ(
        result.status,
        wz::app::AppBootstrapConfigStatus::Missing);
}

TEST(AppBootstrapConfig, DefaultsResourceRootAndOmitsBehaviorModules)
{
    TempBundleDir bundle;
    write_config(
        bundle.root,
        R"json({
  "schema": "wozzits.app.v1",
  "formatVersion": 1,
  "asset_graph": "assets.graph.json",
  "scene": "scene.json"
})json");

    const auto result =
        wz::app::load_app_bootstrap_config(bundle.base());

    ASSERT_TRUE(result.valid()) << result.error;
    // resource_root defaults to <base_dir>/resources (the bundle layout).
    EXPECT_EQ(
        result.config.resource_root,
        wz::fs::join(bundle.base(), "resources"));
    // behavior_modules omitted => empty => built-in behaviors only.
    EXPECT_TRUE(result.config.behavior_modules.empty());
    // No cache block => cache off (root empty), not sealed.
    EXPECT_TRUE(result.config.cache.root.empty());
    EXPECT_FALSE(result.config.cache.sealed);
}

TEST(AppBootstrapConfig, ParsesSealedCacheBlockResolvingRoot)
{
    // Seam 2 of issue #334: the bundle's baked-cache block. A relative root
    // resolves against the bundle dir like every other path, and sealed rides
    // through as a plain bool.
    TempBundleDir bundle;
    write_config(
        bundle.root,
        R"json({
  "schema": "wozzits.app.v1",
  "formatVersion": 1,
  "asset_graph": "assets.graph.json",
  "scene": "scene.json",
  "cache": {
    "root": "cache",
    "sealed": true
  }
})json");

    const auto result =
        wz::app::load_app_bootstrap_config(bundle.base());

    ASSERT_TRUE(result.valid()) << result.error;
    EXPECT_EQ(
        result.config.cache.root,
        wz::fs::join(bundle.base(), "cache"));
    EXPECT_TRUE(result.config.cache.sealed);
}

TEST(AppBootstrapConfig, CacheBlockWithoutSealedDefaultsToUnsealed)
{
    // A cache block that names a root but omits `sealed` serves from the cache
    // yet still recompiles from source on a miss (the dev warm-cache case), so
    // sealed must default false, not inherit the presence of the block.
    TempBundleDir bundle;
    write_config(
        bundle.root,
        R"json({
  "schema": "wozzits.app.v1",
  "formatVersion": 1,
  "asset_graph": "assets.graph.json",
  "scene": "scene.json",
  "cache": {
    "root": "cache"
  }
})json");

    const auto result =
        wz::app::load_app_bootstrap_config(bundle.base());

    ASSERT_TRUE(result.valid()) << result.error;
    EXPECT_EQ(
        result.config.cache.root,
        wz::fs::join(bundle.base(), "cache"));
    EXPECT_FALSE(result.config.cache.sealed);
}

TEST(AppBootstrapConfig, KeepsAbsolutePathsAsAuthored)
{
    TempBundleDir bundle;
    const std::string absolute_graph =
        (bundle.root / "elsewhere" / "assets.graph.json").generic_string();

    write_config(
        bundle.root,
        std::string{
            R"json({
  "schema": "wozzits.app.v1",
  "formatVersion": 1,
  "asset_graph": ")json" }
            + absolute_graph
            + R"json(",
  "scene": "scene.json"
})json");

    const auto result =
        wz::app::load_app_bootstrap_config(bundle.base());

    ASSERT_TRUE(result.valid()) << result.error;
    // An absolute authored path is used as-is (not re-rooted under base_dir).
    EXPECT_EQ(result.config.asset_graph, absolute_graph);
    EXPECT_EQ(result.config.scene, wz::fs::join(bundle.base(), "scene.json"));
}

TEST(AppBootstrapConfig, RejectsUnsupportedSchema)
{
    TempBundleDir bundle;
    write_config(
        bundle.root,
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "asset_graph": "assets.graph.json",
  "scene": "scene.json"
})json");

    const auto result =
        wz::app::load_app_bootstrap_config(bundle.base());

    EXPECT_EQ(result.status, wz::app::AppBootstrapConfigStatus::Invalid);
    EXPECT_FALSE(result.error.empty());
}

TEST(AppBootstrapConfig, RejectsUnsupportedFormatVersion)
{
    TempBundleDir bundle;
    write_config(
        bundle.root,
        R"json({
  "schema": "wozzits.app.v1",
  "formatVersion": 2,
  "asset_graph": "assets.graph.json",
  "scene": "scene.json"
})json");

    const auto result =
        wz::app::load_app_bootstrap_config(bundle.base());

    EXPECT_EQ(result.status, wz::app::AppBootstrapConfigStatus::Invalid);
    EXPECT_FALSE(result.error.empty());
}

TEST(AppBootstrapConfig, RequiresAssetGraphAndScene)
{
    TempBundleDir bundle;

    // Missing scene: Invalid, and the error names the offending field.
    write_config(
        bundle.root,
        R"json({
  "schema": "wozzits.app.v1",
  "formatVersion": 1,
  "asset_graph": "assets.graph.json"
})json");

    const auto no_scene =
        wz::app::load_app_bootstrap_config(bundle.base());
    EXPECT_EQ(no_scene.status, wz::app::AppBootstrapConfigStatus::Invalid);
    EXPECT_NE(no_scene.error.find("scene"), std::string::npos);

    // Missing asset_graph: same, naming asset_graph.
    write_config(
        bundle.root,
        R"json({
  "schema": "wozzits.app.v1",
  "formatVersion": 1,
  "scene": "scene.json"
})json");

    const auto no_graph =
        wz::app::load_app_bootstrap_config(bundle.base());
    EXPECT_EQ(no_graph.status, wz::app::AppBootstrapConfigStatus::Invalid);
    EXPECT_NE(no_graph.error.find("asset_graph"), std::string::npos);
}

TEST(AppBootstrapConfig, WriterRoundTripsThroughLoader)
{
    // Seam 3 of issue #334: the exporter writes wozzits_app.json; loading it back
    // must reconstruct the same resolved options (a sealed cache bundle here).
    TempBundleDir bundle;

    wz::app::AppBootstrapConfigDoc doc;
    doc.resource_root = ".";
    doc.asset_graph = "assets.graph.json";
    doc.scene = "scene.json";
    doc.behavior_modules = "behavior/build/clang-release";
    doc.cache_root = "cache";
    doc.cache_sealed = true;

    std::string error;
    ASSERT_TRUE(wz::app::write_app_bootstrap_config(bundle.base(), doc, error))
        << error;

    const auto result = wz::app::load_app_bootstrap_config(bundle.base());
    ASSERT_TRUE(result.valid()) << result.error;
    EXPECT_EQ(result.config.resource_root, wz::fs::join(bundle.base(), "."));
    EXPECT_EQ(
        result.config.asset_graph,
        wz::fs::join(bundle.base(), "assets.graph.json"));
    EXPECT_EQ(result.config.scene, wz::fs::join(bundle.base(), "scene.json"));
    EXPECT_EQ(
        result.config.behavior_modules,
        wz::fs::join(bundle.base(), "behavior/build/clang-release"));
    EXPECT_EQ(result.config.cache.root, wz::fs::join(bundle.base(), "cache"));
    EXPECT_TRUE(result.config.cache.sealed);
}

TEST(AppBootstrapConfig, WriterOmitsEmptyOptionalFields)
{
    // A minimal doc (no behavior modules, no cache) must still load, with the
    // cache off and behavior_modules empty.
    TempBundleDir bundle;

    wz::app::AppBootstrapConfigDoc doc;
    doc.asset_graph = "assets.graph.json";
    doc.scene = "scene.json";

    std::string error;
    ASSERT_TRUE(wz::app::write_app_bootstrap_config(bundle.base(), doc, error))
        << error;

    const auto result = wz::app::load_app_bootstrap_config(bundle.base());
    ASSERT_TRUE(result.valid()) << result.error;
    EXPECT_TRUE(result.config.behavior_modules.empty());
    EXPECT_TRUE(result.config.cache.root.empty());
    EXPECT_FALSE(result.config.cache.sealed);
}

TEST(AppBootstrapConfig, RejectsMalformedJson)
{
    TempBundleDir bundle;
    write_config(bundle.root, "{ this is not json");

    const auto result =
        wz::app::load_app_bootstrap_config(bundle.base());

    EXPECT_EQ(result.status, wz::app::AppBootstrapConfigStatus::Invalid);
    EXPECT_FALSE(result.error.empty());
}
