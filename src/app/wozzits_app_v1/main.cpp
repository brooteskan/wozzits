// src/app/wozzits_app_v1/main.cpp
//
// wozzits_app_v1 — the thin runtime driver. It resolves the runtime paths from a
// co-located bootstrap config (a shipped bundle: issue #334, Seam 1) and/or
// explicit CLI args, then hands off to wz::app::run_project_runtime (the shared
// loop also used by the editor's in-process engine ABI, so the loop lives in one
// place).
//
// Resolution order: seed from <exe-dir>/wozzits_app.json when present, then let
// any CLI arg override that field. So a bundle runs by double-clicking the exe
// (no args), while a developer — and the standalone-render test — can still pass
// every path explicitly with no config present.

#include <engine/app/app_bootstrap_config.h>
#include <engine/app/editor_runtime.h>

#include <file/filesystem.h>

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>

namespace
{
    struct RuntimeOptions
    {
        wz::fs::Path resource_root{ "resources" };
        wz::fs::Path asset_graph;
        wz::fs::Path scene;
        // Folder of project-authored behavior-module DLLs. Empty => built-ins
        // only (the loader early-returns on an empty folder). Resolved against
        // the asset resource root, matching the editor's resident runtime.
        wz::fs::Path behavior_modules;
        // > 0 => render this many frames then exit with a verification exit code
        // (the standalone-render check); 0 => run interactively until closed.
        uint32_t max_frames = 0;
    };

    // Only the options the user passed on the CLI. Each is applied ON TOP of the
    // bootstrap config, so an unset field leaves the config's value (or the
    // built-in default) in place rather than clobbering it with an empty path.
    struct CliOverrides
    {
        std::optional<wz::fs::Path> resource_root;
        std::optional<wz::fs::Path> asset_graph;
        std::optional<wz::fs::Path> scene;
        std::optional<wz::fs::Path> behavior_modules;
        uint32_t max_frames = 0;
    };

    bool parse_cli(
        int argc,
        char** argv,
        CliOverrides& out,
        std::string& error)
    {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--resource-root" && i + 1 < argc) {
                out.resource_root = argv[++i];
            }
            else if (arg == "--asset-graph" && i + 1 < argc) {
                out.asset_graph = argv[++i];
            }
            else if (arg == "--scene" && i + 1 < argc) {
                out.scene = argv[++i];
            }
            else if (arg == "--behavior-modules" && i + 1 < argc) {
                out.behavior_modules = argv[++i];
            }
            else if (arg == "--frames" && i + 1 < argc) {
                const std::string value = argv[++i];
                try {
                    out.max_frames = static_cast<uint32_t>(std::stoul(value));
                }
                catch (...) {
                    error = "invalid --frames value: " + value;
                    return false;
                }
            }
            else {
                error = "unknown or incomplete option: " + arg;
                return false;
            }
        }
        return true;
    }

    void print_usage(const std::string& detail)
    {
        std::cerr
            << "usage: wozzits_app_v1 [--asset-graph <path>] [--scene <path>] "
               "[--resource-root <path>] [--behavior-modules <dir>] "
               "[--frames <n>]\n"
               "  paths default from a co-located wozzits_app.json (a shipped "
               "bundle); CLI args override it.\n"
            << detail << '\n';
    }
}

int main(int argc, char** argv)
{
    CliOverrides cli;
    std::string error;
    if (!parse_cli(argc, argv, cli, error)) {
        print_usage(error);
        return 2;
    }

    RuntimeOptions options;
    options.max_frames = cli.max_frames;

    // Seed from a co-located bootstrap config so the bundle launches with no
    // args. Resolved against the exe dir, so the whole folder is relocatable. A
    // MISSING config is fine (developer / CLI-driven runs); a PRESENT but
    // malformed one is fatal rather than silently ignored.
    const wz::fs::Path base_dir =
        wz::fs::parent_path(wz::fs::executable_path());
    const wz::app::AppBootstrapConfigResult boot =
        wz::app::load_app_bootstrap_config(base_dir);
    if (boot.valid()) {
        options.resource_root = boot.config.resource_root;
        options.asset_graph = boot.config.asset_graph;
        options.scene = boot.config.scene;
        options.behavior_modules = boot.config.behavior_modules;
    }
    else if (!boot.missing()) {
        std::cerr << "wozzits_app_v1: " << boot.error << '\n';
        return 2;
    }

    // CLI args override the config, per field.
    if (cli.resource_root) {
        options.resource_root = *cli.resource_root;
    }
    if (cli.asset_graph) {
        options.asset_graph = *cli.asset_graph;
    }
    if (cli.scene) {
        options.scene = *cli.scene;
    }
    if (cli.behavior_modules) {
        options.behavior_modules = *cli.behavior_modules;
    }

    if (options.asset_graph.empty() || options.scene.empty()) {
        print_usage(
            "no --asset-graph/--scene given and no co-located "
            "wozzits_app.json supplied them");
        return 2;
    }

    return wz::app::run_project_runtime(
        "Wozzits App v1",
        options.asset_graph,
        options.scene,
        options.resource_root,
        nullptr,
        {},  // log sink: none (logs go to stdout)
        options.behavior_modules,  // empty => built-ins only
        wz::app::RuntimeRunOptions{ .max_frames = options.max_frames });
}
