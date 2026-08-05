#pragma once

// engine/app/app_bootstrap_config.h
//
// The bundle bootstrap config (issue #334, Seam 1). A shipped app bundle places
// a `wozzits_app.json` next to `wozzits_app_v1.exe`; the runtime reads it on a
// no-argument launch so the bundle runs by double-clicking the exe, with no
// editor host in the loop deriving CLI paths.
//
// Every authored path in the config is RELATIVE and is resolved against the
// config's base directory (the exe dir in a bundle), so the whole folder is
// relocatable. This is deliberately a distinct, minimal schema rather than the
// editor's `.wozzits/project.json` manifest: the bundle config names the
// (release) behavior-module DLL folder directly and lives at the bundle root,
// decoupled from the editor's CMake `behavior_project_folder`. It shares the
// manifest module's relative-path resolver (resolve_project_authored_path).

#include <file/filesystem.h>

#include <cstdint>
#include <string>

namespace wz::app
{
    // File name of the bundle bootstrap config, co-located with the runtime exe.
    inline constexpr const char* kAppBootstrapConfigFileName = "wozzits_app.json";
    inline constexpr const char* kAppBootstrapConfigSchema = "wozzits.app.v1";
    inline constexpr uint32_t kAppBootstrapConfigFormatVersion = 1u;

    // Runtime paths resolved from a bootstrap config: every path is made absolute
    // against the config's base directory. `behavior_modules` is empty when the
    // config omits it (=> built-in behaviors only, matching the runtime's
    // empty-folder contract). `resource_root` defaults to <base_dir>/resources
    // (the bundle layout) when the config omits it.
    struct AppBootstrapConfig
    {
        std::string schema;
        uint32_t format_version = 0;
        wz::fs::Path resource_root;
        wz::fs::Path asset_graph;
        wz::fs::Path scene;
        wz::fs::Path behavior_modules;
    };

    enum class AppBootstrapConfigStatus
    {
        // No config file at base_dir. Not an error on its own: a developer run
        // (and the standalone-render test) launches the exe with explicit CLI
        // paths and no co-located config, so the caller falls back to CLI args.
        Missing,
        // Present but unreadable, not valid JSON, wrong schema/version, or missing
        // a required field. A present-but-broken bundle config is fatal rather
        // than silently ignored.
        Invalid,
        Valid,
    };

    struct AppBootstrapConfigResult
    {
        AppBootstrapConfigStatus status = AppBootstrapConfigStatus::Invalid;
        AppBootstrapConfig config;
        std::string error;

        [[nodiscard]] bool valid() const noexcept
        {
            return status == AppBootstrapConfigStatus::Valid;
        }

        [[nodiscard]] bool missing() const noexcept
        {
            return status == AppBootstrapConfigStatus::Missing;
        }
    };

    // Path of the bootstrap config co-located with `base_dir`.
    wz::fs::Path app_bootstrap_config_path(const wz::fs::Path& base_dir);

    // Read <base_dir>/wozzits_app.json, resolving asset_graph / scene /
    // resource_root / behavior_modules against base_dir. asset_graph and scene
    // are required (a bundle cannot render without them); resource_root defaults
    // to <base_dir>/resources and behavior_modules is optional. A missing file
    // yields Missing (not an error); anything malformed yields Invalid + error.
    AppBootstrapConfigResult load_app_bootstrap_config(
        const wz::fs::Path& base_dir);
}
