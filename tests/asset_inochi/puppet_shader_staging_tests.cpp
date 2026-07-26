// tests/asset_inochi/puppet_shader_staging_tests.cpp
//
// #283: stage_puppet_shaders used to be write-if-missing, so a project whose
// shaders were staged by an earlier build kept the OLD ones forever after an
// engine shader change. The failure was silent -- a smaller shader cbuffer
// against a larger root signature is legal in D3D12 -- and it bit the #274,
// #275 and #276/#277 work in turn. The policy is now overwrite-on-differ:
// content that matches is left completely alone (no write, no mtime churn),
// content that has drifted is re-staged from the embedded source of truth.
//
// Device-free: staging is pure filesystem work.

#include <gtest/gtest.h>

#include <engine/assets/puppet_program.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>

namespace
{
    namespace ea = wz::engine::assets;
    namespace fs = std::filesystem;

    fs::path unique_temp_root()
    {
        return fs::temp_directory_path()
            / ("wozzits_puppet_staging_"
               + std::to_string(static_cast<unsigned long long>(
                     std::chrono::steady_clock::now()
                         .time_since_epoch()
                         .count())));
    }
}

TEST(PuppetShaderStaging, StagesRestagesStaleAndLeavesMatchingFilesAlone)
{
    wz::Logger logger;
    const fs::path root = unique_temp_root();
    fs::remove_all(root);

    const std::function<wz::fs::Path(const wz::fs::Path&)> resolve =
        [&](const wz::fs::Path& relative) -> wz::fs::Path
    {
        return (root / relative).string();
    };

    // First run of a fresh project: both shaders appear.
    ASSERT_TRUE(ea::stage_puppet_shaders(logger, resolve));

    const wz::fs::Path vs_path = resolve(ea::kPuppetVertexShaderProjectPath);
    const wz::fs::Path ps_path = resolve(ea::kPuppetPixelShaderProjectPath);
    const wz::fs::FileResult<std::string> vs = wz::fs::read_file_text(vs_path);
    const wz::fs::FileResult<std::string> ps = wz::fs::read_file_text(ps_path);
    ASSERT_TRUE(vs);
    ASSERT_TRUE(ps);
    EXPECT_FALSE(vs.value.empty());
    EXPECT_FALSE(ps.value.empty());

    // A matching staged file is not rewritten at all. Proven by stamping a
    // distinctive mtime and requiring it to survive -- content equality alone
    // would not catch a needless rewrite, and a rewrite would invalidate the
    // shader cache every run.
    const fs::file_time_type stamp =
        fs::last_write_time(fs::path(vs_path)) - std::chrono::hours(48);
    fs::last_write_time(fs::path(vs_path), stamp);
    ASSERT_TRUE(ea::stage_puppet_shaders(logger, resolve));
    EXPECT_EQ(fs::last_write_time(fs::path(vs_path)), stamp)
        << "an up-to-date staged shader was rewritten";

    // A project carrying a STALE staged copy is re-staged from the embedded
    // source -- the case that silently rendered with the previous shader.
    ASSERT_EQ(
        wz::fs::write_file_text(vs_path, "// stale shader from an older build\n"),
        wz::fs::FileError::None);
    ASSERT_TRUE(ea::stage_puppet_shaders(logger, resolve));
    const wz::fs::FileResult<std::string> restaged =
        wz::fs::read_file_text(vs_path);
    ASSERT_TRUE(restaged);
    EXPECT_EQ(restaged.value, vs.value)
        << "a stale staged shader survived staging (#283)";

    // The sibling it shares a directory with was untouched throughout.
    const wz::fs::FileResult<std::string> ps_after =
        wz::fs::read_file_text(ps_path);
    ASSERT_TRUE(ps_after);
    EXPECT_EQ(ps_after.value, ps.value);

    fs::remove_all(root);
}
