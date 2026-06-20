// src/app/wozzits_app_v1/main.cpp
//
// wozzits_app_v1 — the thin runtime driver. It parses explicit runtime paths
// and hands off to wz::app::run_project_runtime (the shared loop also used by
// the editor's in-process engine ABI, so the loop lives in one place).

#include <engine/app/editor_runtime.h>

#include <iostream>
#include <string>

namespace
{
    struct RuntimeOptions
    {
        wz::fs::Path resource_root{ "resources" };
        wz::fs::Path asset_graph;
        wz::fs::Path scene;
    };

    bool parse_options(
        int argc,
        char** argv,
        RuntimeOptions& out,
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
            else {
                error = "unknown or incomplete option: " + arg;
                return false;
            }
        }

        if (out.asset_graph.empty()) {
            error = "missing required option: --asset-graph <path>";
            return false;
        }
        if (out.scene.empty()) {
            error = "missing required option: --scene <path>";
            return false;
        }
        return true;
    }
}

int main(int argc, char** argv)
{
    RuntimeOptions options;
    std::string error;
    if (!parse_options(argc, argv, options, error)) {
        std::cerr
            << "usage: wozzits_app_v1 --asset-graph <path> --scene <path> "
               "[--resource-root <path>]\n"
            << error << '\n';
        return 2;
    }

    return wz::app::run_project_runtime(
        "Wozzits App v1",
        options.asset_graph,
        options.scene,
        options.resource_root,
        nullptr);
}
