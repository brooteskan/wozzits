// src/tools/wozzits_export/main.cpp
//
// wozzits_export — headless standalone-bundle exporter (issue #334, Seam 3.2).
//
//   wozzits_export --project <dir> --out <bundle-dir> [--app-exe <path>] [--no-log]
//
// Resolves a project (its .wozzits/project.json), loads the asset graph, runs the
// resource-closure walker to REPORT which sources a sealed bundle could strip,
// then assembles a self-contained, double-clickable bundle:
//
//   <out>/
//     wozzits_app_v1.exe     # the runtime (copied from --app-exe / a sibling)
//     wozzits_app.json       # co-located bootstrap config, relative paths
//     <project files…>       # the authored project tree (minus editor-only cruft)
//
// This seam produces a WORKING (unsealed) bundle: it copies the full authored
// tree — including the heavy sources — and leaves the cache off, so everything
// compiles from source at load. Sealing the baked cache and dropping the stripped
// sources the walker identifies here is Seam 3.3; release behavior DLLs + an ABI
// stamp are 3.4/3.5. The strip/copy report is printed so 3.3 can act on it.
//
// Editor-independent by construction: it is a data-driven engine tool, no editor
// in the loop (the authoring litmus). Exit codes: 0 ok, 1 runtime error, 2 usage.

#include <engine/app/app_bootstrap_config.h>
#include <engine/assets/scene/asset_graph_json.h>
#include <engine/bundle/bundle_closure.h>
#include <engine/project/project_runtime_launch.h>

#include <external/json/json_parser.h>
#include <file/filesystem.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>

namespace
{
    namespace fs = std::filesystem;

    struct Options
    {
        std::string project;   // project root (holds .wozzits/project.json)
        std::string out;       // bundle output directory
        std::string app_exe;   // runtime exe to bundle (default: a sibling)
        bool log = true;
    };

    bool parse_options(int argc, char** argv, Options& out, std::string& error)
    {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--project" && i + 1 < argc) {
                out.project = argv[++i];
            }
            else if (arg == "--out" && i + 1 < argc) {
                out.out = argv[++i];
            }
            else if (arg == "--app-exe" && i + 1 < argc) {
                out.app_exe = argv[++i];
            }
            else if (arg == "--no-log") {
                out.log = false;
            }
            else {
                error = "unknown or incomplete option: " + arg;
                return false;
            }
        }
        if (out.project.empty() || out.out.empty()) {
            error = "both --project <dir> and --out <bundle-dir> are required";
            return false;
        }
        return true;
    }

    void log_line(const Options& o, const std::string& message)
    {
        if (o.log) {
            std::cout << "[export] " << message << '\n';
        }
    }

    std::string read_text_file(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }
        return std::string(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
    }

    // Editor-only artefacts that must NOT ship in a runtime bundle: the editor
    // manifest folder (.wozzits) and the graph-layout sidecar (*.editor.json).
    bool is_excluded(const fs::path& relative)
    {
        for (const fs::path& part : relative) {
            if (part == ".wozzits") {
                return true;
            }
        }
        const std::string name = relative.filename().string();
        static constexpr std::string_view kEditorSuffix = ".editor.json";
        return name.size() >= kEditorSuffix.size()
            && name.compare(
                   name.size() - kEditorSuffix.size(),
                   kEditorSuffix.size(),
                   kEditorSuffix) == 0;
    }

    // Copy the authored project tree into the bundle, preserving structure and
    // skipping editor-only cruft. Returns the file count, or -1 on error.
    long copy_project_tree(
        const Options& o,
        const fs::path& project_root,
        const fs::path& bundle_root,
        std::string& error)
    {
        std::error_code ec;
        long copied = 0;
        for (auto it = fs::recursive_directory_iterator(project_root, ec);
             !ec && it != fs::recursive_directory_iterator();
             it.increment(ec))
        {
            const fs::path& src = it->path();
            const fs::path relative = fs::relative(src, project_root, ec);
            if (ec) {
                error = "cannot relativize: " + src.string();
                return -1;
            }
            if (is_excluded(relative)) {
                if (it->is_directory(ec)) {
                    it.disable_recursion_pending();  // skip the whole subtree
                }
                continue;
            }
            const fs::path dst = bundle_root / relative;
            if (it->is_directory(ec)) {
                fs::create_directories(dst, ec);
                continue;
            }
            fs::create_directories(dst.parent_path(), ec);
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                error = "failed to copy " + src.string() + " -> " + dst.string()
                    + ": " + ec.message();
                return -1;
            }
            ++copied;
        }
        if (ec) {
            error = "failed to walk project tree: " + ec.message();
            return -1;
        }
        return copied;
    }

    // Report the closure and warn about sources that live OUTSIDE the project
    // tree (this seam copies only the project tree, so an external reference —
    // e.g. into the shared resources/ tree — would not be bundled; the general
    // path-rewriting case is a later remainder). Returns false if any copy source
    // is outside the tree, which would make the bundle incomplete.
    bool report_and_check_closure(
        const Options& o,
        const wz::engine::bundle::BundleClosure& closure,
        const fs::path& project_root,
        std::string& error)
    {
        size_t copy = 0;
        size_t strip = 0;
        bool external = false;
        for (const wz::engine::bundle::BundleSourceRef& ref : closure.sources) {
            const bool is_copy =
                ref.disposition == wz::engine::bundle::BundleFileDisposition::Copy;
            (is_copy ? copy : strip) += 1;

            std::error_code ec;
            const fs::path rel =
                fs::relative(fs::path(ref.resolved_path), project_root, ec);
            const bool inside =
                !ec && !rel.empty() && rel.native().rfind(L"..", 0) != 0;
            if (o.log) {
                log_line(o,
                    std::string(is_copy ? "  copy  " : "  strip ")
                    + std::string(ref.resolved_path)
                    + (inside ? "" : "   [OUTSIDE PROJECT]"));
            }
            if (is_copy && !inside) {
                external = true;
            }
        }
        log_line(o,
            "closure: " + std::to_string(closure.sources.size())
            + " sources (" + std::to_string(copy) + " copy, "
            + std::to_string(strip) + " strip)");
        if (external) {
            error =
                "the project references copy-sources outside its own tree; "
                "external-resource bundling (path rewriting) is not yet "
                "supported — relocate the project to be self-contained first";
            return false;
        }
        return true;
    }
}

int main(int argc, char** argv)
{
    Options options;
    std::string error;
    if (!parse_options(argc, argv, options, error)) {
        std::cerr
            << "usage: wozzits_export --project <dir> --out <bundle-dir> "
               "[--app-exe <path>] [--no-log]\n"
            << error << '\n';
        return 2;
    }

    // 1. Resolve the project (manifest -> graph/scene/resource-root paths).
    const wz::engine::project::ProjectRuntimeLaunchResult launch =
        wz::engine::project::load_project_runtime_launch(
            { .project_root = options.project, .resource_root = {} });
    if (!launch.ok) {
        std::cerr << "wozzits_export: " << launch.error << '\n';
        return 1;
    }
    const fs::path project_root =
        fs::path(launch.launch.resource_root).empty()
            ? fs::path(options.project)
            : fs::path(launch.launch.resource_root);

    // 2. Load the asset graph into a draft (no compile / no device).
    const std::string graph_text =
        read_text_file(fs::path(launch.launch.asset_graph_path));
    if (graph_text.empty()) {
        std::cerr << "wozzits_export: cannot read asset graph: "
                  << launch.launch.asset_graph_path << '\n';
        return 1;
    }
    const wz::json::JSONParseResult parsed =
        wz::json::parse_json_string(graph_text);
    if (!parsed.ok || !parsed.document.root) {
        std::cerr << "wozzits_export: invalid asset graph json\n";
        return 1;
    }
    wz::asset::AssetGraphDraft draft;
    if (!wz::engine::assets::load_asset_graph_draft_from_v2_json(
            *parsed.document.root, draft, error))
    {
        std::cerr << "wozzits_export: " << error << '\n';
        return 1;
    }

    // 3. Run the closure walker on the REAL graph and report copy/strip.
    const wz::engine::bundle::BundleClosure closure =
        wz::engine::bundle::compute_bundle_closure(
            draft, launch.launch.resource_root);
    if (!report_and_check_closure(options, closure, project_root, error)) {
        std::cerr << "wozzits_export: " << error << '\n';
        return 1;
    }

    // 4. Assemble the bundle: authored tree, runtime exe, bootstrap config.
    std::error_code ec;
    const fs::path bundle_root(options.out);
    fs::create_directories(bundle_root, ec);
    if (ec) {
        std::cerr << "wozzits_export: cannot create bundle dir: "
                  << options.out << ": " << ec.message() << '\n';
        return 1;
    }

    const long copied =
        copy_project_tree(options, project_root, bundle_root, error);
    if (copied < 0) {
        std::cerr << "wozzits_export: " << error << '\n';
        return 1;
    }
    log_line(options,
        "copied " + std::to_string(copied) + " project files");

    // The runtime exe: --app-exe, else a sibling of this tool.
    fs::path app_exe = options.app_exe.empty()
        ? fs::path(wz::fs::parent_path(wz::fs::executable_path()))
              / "wozzits_app_v1.exe"
        : fs::path(options.app_exe);
    if (!fs::exists(app_exe, ec)) {
        std::cerr << "wozzits_export: runtime exe not found: "
                  << app_exe.string()
                  << " (pass --app-exe <path>)\n";
        return 1;
    }
    fs::copy_file(
        app_exe,
        bundle_root / "wozzits_app_v1.exe",
        fs::copy_options::overwrite_existing,
        ec);
    if (ec) {
        std::cerr << "wozzits_export: failed to copy runtime exe: "
                  << ec.message() << '\n';
        return 1;
    }

    // The bootstrap config: paths are project-relative and the tree preserves the
    // project layout, so they resolve against the bundle root (resource_root ".").
    wz::app::AppBootstrapConfigDoc doc;
    doc.resource_root = ".";
    doc.asset_graph =
        fs::relative(fs::path(launch.launch.asset_graph_path), project_root, ec)
            .generic_string();
    doc.scene =
        fs::relative(fs::path(launch.launch.scene_path), project_root, ec)
            .generic_string();
    if (!launch.launch.manifest.behavior_module_folder.empty()) {
        doc.behavior_modules =
            fs::relative(
                fs::path(launch.launch.manifest.behavior_module_folder),
                project_root, ec)
                .generic_string();
    }
    // Cache stays OFF this seam (unsealed bundle); the seal + strip is Seam 3.3.
    if (!wz::app::write_app_bootstrap_config(bundle_root.string(), doc, error)) {
        std::cerr << "wozzits_export: " << error << '\n';
        return 1;
    }

    log_line(options, "bundle written to " + bundle_root.string());
    return 0;
}
