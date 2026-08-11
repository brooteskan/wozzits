// src/tools/wozzits_export/main.cpp
//
// wozzits_export — headless standalone-bundle exporter (issue #295, Seam 3.2).
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
#include <engine/app/editor_runtime.h>  // kRuntimeNoDeviceExitCode
#include <engine/assets/engine_asset_library_internal.h>  // internal::FileSourceDesc
#include <engine/assets/scene/asset_graph_json.h>
#include <engine/behavior/behavior_plugin_abi.h>      // WZ_BEHAVIOR_ABI_VERSION
#include <engine/behavior/behavior_plugin_adapter.h>  // BehaviorPluginHost (verify)
#include <engine/behavior/behavior_registry.h>
#include <engine/bundle/bundle_closure.h>
#include <engine/project/project_runtime_launch.h>

#include <external/json/json_parser.h>
#include <file/filesystem.h>

#include <process.h>  // _spawnv / _P_WAIT (spawn the runtime to warm/verify)

#include <any>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    struct Options
    {
        std::string project;   // project root (holds .wozzits/project.json)
        std::string out;       // bundle output directory
        std::string app_exe;   // runtime exe to bundle (default: a sibling)
        std::string behavior_modules;  // behavior-DLL dir to ship (default: the
                                       // manifest's; override to point at RELEASE
                                       // DLLs — the exporter ships, not builds)
        bool seal = false;     // bake a sealed cache + strip cache-served sources
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
            else if (arg == "--behavior-modules" && i + 1 < argc) {
                out.behavior_modules = argv[++i];
            }
            else if (arg == "--seal") {
                out.seal = true;
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

    // Artefacts that must NOT ship in a runtime bundle: the editor manifest folder
    // (.wozzits), the graph-layout sidecar (*.editor.json), and the project's
    // behavior source/build tree — the runtime needs only the compiled DLLs, which
    // are shipped separately into a clean bundle/behavior/ (release-capable).
    bool is_excluded(const fs::path& relative, const std::string& behavior_dir)
    {
        if (!behavior_dir.empty()
            && relative.begin() != relative.end()
            && *relative.begin() == fs::path(behavior_dir))
        {
            return true;
        }
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
    // skipping editor-only cruft + the behavior source/build tree. Returns the
    // file count, or -1 on error.
    long copy_project_tree(
        const Options& o,
        const fs::path& project_root,
        const fs::path& bundle_root,
        const std::string& behavior_dir,
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
            if (is_excluded(relative, behavior_dir)) {
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

    // Copy just the behavior-module DLLs from `source_dir` into a clean
    // bundle/behavior/ (a flat dir the config points at), and report the count.
    // Returns the number shipped, or -1 on error. An empty / missing source_dir
    // ships nothing (built-in behaviors only).
    long ship_behavior_modules(
        const Options& o,
        const fs::path& source_dir,
        const fs::path& bundle_root,
        std::string& error)
    {
        std::error_code ec;
        if (source_dir.empty() || !fs::is_directory(source_dir, ec)) {
            return 0;
        }
        const fs::path dst_dir = bundle_root / "behavior";
        fs::create_directories(dst_dir, ec);
        long shipped = 0;
        for (auto it = fs::directory_iterator(source_dir, ec);
             !ec && it != fs::directory_iterator();
             it.increment(ec))
        {
            if (!it->is_regular_file(ec) || it->path().extension() != ".dll") {
                continue;
            }
            fs::copy_file(
                it->path(),
                dst_dir / it->path().filename(),
                fs::copy_options::overwrite_existing, ec);
            if (ec) {
                error = "failed to ship behavior DLL "
                    + it->path().string() + ": " + ec.message();
                return -1;
            }
            ++shipped;
        }
        log_line(o,
            "shipped " + std::to_string(shipped) + " behavior DLL(s) from "
            + source_dir.string());
        return shipped;
    }

    const char* load_status_name(
        wz::engine::behavior::BehaviorPluginHost::DynamicLoadStatus status)
    {
        using S = wz::engine::behavior::BehaviorPluginHost::DynamicLoadStatus;
        switch (status) {
        case S::Loaded: return "loaded";
        case S::InvalidPath: return "invalid_path";
        case S::LoadFailed: return "load_failed";
        case S::CopyFailed: return "copy_failed";
        case S::MissingRegisterSymbol: return "missing_register_symbol";
        case S::RegistrationFailed: return "registration_failed";
        case S::UnsupportedPlatform: return "unsupported_platform";
        }
        return "unknown";
    }

    // Export-time ABI verify (issue #295, Seam 3.5): dry-run LOAD each shipped
    // behavior DLL exactly as the runtime will (LoadLibrary + wz_register_behaviors
    // at the exporter's WZ_BEHAVIOR_ABI_VERSION, which equals the shipped exe's
    // since both are one build). A DLL built against a different ABI fails to
    // register (registration_failed) — caught HERE as a hard export error instead
    // of silently vanishing at runtime. No GPU needed.
    bool verify_behavior_dlls(
        const Options& o,
        const fs::path& behavior_dir,
        std::string& error)
    {
        std::error_code ec;
        if (!fs::is_directory(behavior_dir, ec)) {
            return true;  // nothing shipped => nothing to verify
        }
        wz::engine::behavior::BehaviorRegistry registry;
        wz::engine::behavior::BehaviorPluginHost host;
        long verified = 0;
        for (auto it = fs::directory_iterator(behavior_dir, ec);
             !ec && it != fs::directory_iterator();
             it.increment(ec))
        {
            if (!it->is_regular_file(ec) || it->path().extension() != ".dll") {
                continue;
            }
            const auto result =
                host.load_dynamic_module(registry, it->path(), nullptr);
            if (!result.ok()) {
                error = "behavior DLL failed export-time ABI verify: "
                    + it->path().filename().string() + " (status="
                    + load_status_name(result.status)
                    + (result.detail.empty() ? "" : ", " + result.detail)
                    + ") — it is likely built against a WZ_BEHAVIOR_ABI_VERSION "
                      "other than the runtime's ("
                    + std::to_string(WZ_BEHAVIOR_ABI_VERSION) + ")";
                return false;
            }
            ++verified;
        }
        log_line(o,
            "verified " + std::to_string(verified)
            + " behavior DLL(s) against ABI "
            + std::to_string(WZ_BEHAVIOR_ABI_VERSION));
        return true;
    }

    // Whether `p` resolves to a location inside `root` (i.e. not escaping it).
    bool path_is_inside(const fs::path& p, const fs::path& root)
    {
        std::error_code ec;
        const fs::path rel = fs::relative(p, root, ec);
        if (ec || rel.empty()) {
            return false;
        }
        return rel.generic_string().rfind("..", 0) != 0;  // does not escape root
    }

    // Log the closure: each source's disposition (copy/strip) and whether it is
    // external (lives outside the project tree, so it will be relocated into the
    // bundle's resources/ subtree).
    void report_closure(
        const Options& o,
        const wz::engine::bundle::BundleClosure& closure,
        const fs::path& project_root)
    {
        size_t copy = 0;
        size_t strip = 0;
        size_t external = 0;
        for (const wz::engine::bundle::BundleSourceRef& ref : closure.sources) {
            const bool is_copy =
                ref.disposition == wz::engine::bundle::BundleFileDisposition::Copy;
            (is_copy ? copy : strip) += 1;
            const bool inside =
                path_is_inside(fs::path(ref.resolved_path), project_root);
            if (!inside) {
                ++external;
            }
            log_line(o,
                std::string(is_copy ? "  copy  " : "  strip ")
                + std::string(ref.resolved_path)
                + (inside ? "" : "   [external -> resources/]"));
        }
        log_line(o,
            "closure: " + std::to_string(closure.sources.size())
            + " sources (" + std::to_string(copy) + " copy, "
            + std::to_string(strip) + " strip, "
            + std::to_string(external) + " external)");
    }

    // Short stable hex tag for a directory path (case-folded, normalized). External
    // sources from the same source dir share one bundle subdir, so files with the
    // same basename from different dirs never collide.
    std::string dir_hash_tag(const fs::path& dir)
    {
        const std::string norm = dir.lexically_normal().generic_string();
        uint64_t h = 14695981039346656037ull;
        for (const char c : norm) {
            h ^= static_cast<unsigned char>(
                std::tolower(static_cast<unsigned char>(c)));
            h *= 1099511628211ull;
        }
        char buf[17];
        std::snprintf(
            buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
        return buf;
    }

    // Point a source-carrying node at a new (bundle-relative) path, in whichever
    // meta form it uses (ParamBlock source_path/directory, or FileSourceDesc).
    void set_node_source_path(
        wz::asset::AssetGraphDraft& draft,
        wz::asset::AssetGraphDraftNodeId node_id,
        bool is_directory,
        const std::string& new_path)
    {
        wz::asset::AssetGraphDraftNode* node =
            wz::asset::find_asset_graph_draft_node(draft, node_id);
        if (!node) {
            return;
        }
        if (auto* params =
                std::any_cast<wz::asset::ParamBlock>(&node->node.meta))
        {
            params->values[is_directory ? "directory" : "source_path"] = new_path;
        }
        else if (auto* file =
                     std::any_cast<wz::engine::assets::internal::FileSourceDesc>(
                         &node->node.meta))
        {
            file->full_path = new_path;
        }
    }

    // Copy every source the graph references from OUTSIDE the project tree into a
    // bundle resources/ subtree and rewrite the graph to point at the copies, so a
    // project that references shared engine shaders (etc.) still bundles into a
    // self-contained, relocatable folder. Records original->bundle-relative
    // mappings in `rewrites` (so a sealed strip can find an external source's
    // bundle location), and re-serializes the graph to `bundle_graph_path` when
    // anything changed. Local sources are left untouched (already carried by the
    // project-tree copy).
    bool relocate_external_sources(
        const Options& o,
        wz::asset::AssetGraphDraft& draft,
        const wz::engine::bundle::BundleClosure& closure,
        const fs::path& project_root,
        const fs::path& bundle_root,
        const fs::path& bundle_graph_path,
        std::map<std::string, std::string>& rewrites,
        std::string& error)
    {
        std::error_code ec;
        bool changed = false;
        for (const wz::engine::bundle::BundleSourceRef& ref : closure.sources) {
            const fs::path source(ref.resolved_path);
            if (path_is_inside(source, project_root)) {
                continue;  // local — copied verbatim with the project tree
            }

            std::string dest_rel;
            const auto seen = rewrites.find(ref.resolved_path);
            if (seen != rewrites.end()) {
                dest_rel = seen->second;  // same external file already relocated
            }
            else {
                dest_rel = "resources/" + dir_hash_tag(source.parent_path())
                    + "/" + source.filename().generic_string();
                const fs::path dest = bundle_root / dest_rel;
                fs::create_directories(dest.parent_path(), ec);
                if (ref.is_directory) {
                    fs::copy(source, dest,
                        fs::copy_options::recursive
                            | fs::copy_options::overwrite_existing,
                        ec);
                }
                else {
                    fs::copy_file(source, dest,
                        fs::copy_options::overwrite_existing, ec);
                }
                if (ec) {
                    error = "failed to relocate external source "
                        + source.string() + " -> " + dest.string() + ": "
                        + ec.message();
                    return false;
                }
                rewrites[ref.resolved_path] = dest_rel;
                log_line(o,
                    "  relocated " + source.string() + " -> " + dest_rel);
            }

            set_node_source_path(draft, ref.node, ref.is_directory, dest_rel);
            changed = true;
        }

        if (changed) {
            // Clear every node key so the runtime re-derives them from the
            // rewritten params (bind materializes keys; the writer omits empty
            // keys). The editor-only layout is intentionally dropped.
            for (wz::asset::AssetGraphDraftNode& node : draft.nodes) {
                node.node.key = wz::asset::AssetKey{};
            }
            std::ofstream out(bundle_graph_path, std::ios::binary);
            if (!out) {
                error = "cannot rewrite bundle graph: "
                    + bundle_graph_path.string();
                return false;
            }
            out << wz::engine::assets::save_asset_graph_draft_to_v2_json(draft);
            if (!out) {
                error = "failed writing bundle graph: "
                    + bundle_graph_path.string();
                return false;
            }
            log_line(o,
                "rewrote graph for " + std::to_string(rewrites.size())
                + " external source(s)");
        }
        return true;
    }

    // Run the bundled runtime as a child process; returns its exit code (or -1 on
    // spawn failure). Args are passed as a vector (no shell), so paths with spaces
    // are safe. Keeping the GPU run in its own process leaves wozzits_export a pure
    // CPU orchestrator and reuses the shipped runtime exactly.
    int run_bundled_app(
        const std::string& exe,
        const std::vector<std::string>& args)
    {
        std::vector<const char*> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(exe.c_str());
        for (const std::string& arg : args) {
            argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr);
        const intptr_t rc = _spawnv(_P_WAIT, exe.c_str(), argv.data());
        return rc < 0 ? -1 : static_cast<int>(rc);
    }

    bool cache_has_entries(const fs::path& cache_root)
    {
        std::error_code ec;
        const fs::path assets = cache_root / "assets";
        if (!fs::is_directory(assets, ec)) {
            return false;
        }
        for (auto it = fs::recursive_directory_iterator(assets, ec);
             !ec && it != fs::recursive_directory_iterator();
             it.increment(ec))
        {
            if (it->is_regular_file(ec)) {
                return true;
            }
        }
        return false;
    }

    // Seal the bundle: warm the baked cache by running the runtime once (cache on,
    // so the cacheable compilers store_cached), strip the sources the closure
    // marked cache-served, rewrite the config sealed, and verify the stripped
    // bundle still runs from the cache alone. GPU-dependent (the warm + verify
    // runs render a frame), matching the issue's local-only bake constraint.
    // Returns a process-style status: 0 ok, kRuntimeNoDeviceExitCode when no GPU
    // is available (so a caller/CTest can SKIP rather than fail), 1 on real error.
    int seal_bundle(
        const Options& o,
        const fs::path& bundle_root,
        const fs::path& project_root,
        const wz::engine::bundle::BundleClosure& closure,
        const std::map<std::string, std::string>& external_rewrites,
        wz::app::AppBootstrapConfigDoc& doc,
        std::string& error)
    {
        const std::string exe = (bundle_root / "wozzits_app_v1.exe").string();
        const fs::path cache_root = bundle_root / "cache";

        // 1. Warm — cache ON, NOT sealed, so a miss still compiles from source and
        //    the compiler stores the product. Reads the co-located (unsealed)
        //    config for the graph/scene/resource/behavior paths.
        log_line(o, "warming baked cache (running the runtime, cache on)…");
        const int warm_rc = run_bundled_app(
            exe, { "--cache-root", cache_root.string(), "--frames", "1" });
        if (warm_rc == wz::app::kRuntimeNoDeviceExitCode) {
            error = "sealing requires a GPU: the cache-warm run reported no device";
            return wz::app::kRuntimeNoDeviceExitCode;
        }
        if (warm_rc != 0) {
            error = "cache-warm run failed (exit " + std::to_string(warm_rc) + ")";
            return 1;
        }
        if (!cache_has_entries(cache_root)) {
            error = "cache-warm produced no entries under " + cache_root.string();
            return 1;
        }

        // 2. Strip the sources whose products the sealed cache now serves. An
        //    external source lives at its relocated bundle path; a local one at
        //    its project-relative path.
        std::error_code ec;
        size_t stripped = 0;
        for (const wz::fs::Path& strip : closure.strip_paths()) {
            fs::path target;
            const auto ext = external_rewrites.find(std::string(strip));
            if (ext != external_rewrites.end()) {
                target = bundle_root / ext->second;
            }
            else {
                const fs::path relative =
                    fs::relative(fs::path(strip), project_root, ec);
                if (ec || relative.empty()) {
                    continue;
                }
                target = bundle_root / relative;
            }
            if (fs::remove(target, ec)) {
                ++stripped;
                log_line(o, "  stripped " + target.string());
            }
        }
        log_line(o,
            "stripped " + std::to_string(stripped) + " cache-served source(s)");

        // 3. Rewrite the config sealed (cache on + fatal-on-miss).
        doc.cache_root = "cache";
        doc.cache_sealed = true;
        if (!wz::app::write_app_bootstrap_config(
                bundle_root.string(), doc, error))
        {
            return 1;
        }

        // 4. Verify: the sealed bundle renders from the cache with sources gone.
        log_line(o, "verifying sealed bundle (sources stripped)…");
        const int seal_rc = run_bundled_app(exe, { "--frames", "1" });
        if (seal_rc == wz::app::kRuntimeNoDeviceExitCode) {
            log_line(o, "  (sealed-run verification skipped: no GPU device)");
            return 0;
        }
        if (seal_rc != 0) {
            error =
                "sealed bundle failed to run from the cache after stripping (exit "
                + std::to_string(seal_rc) + ")";
            return 1;
        }
        return 0;
    }
}

int main(int argc, char** argv)
{
    Options options;
    std::string error;
    if (!parse_options(argc, argv, options, error)) {
        std::cerr
            << "usage: wozzits_export --project <dir> --out <bundle-dir> "
               "[--app-exe <path>] [--behavior-modules <dir>] [--seal] "
               "[--no-log]\n"
               "  --behavior-modules  dir of behavior DLLs to ship (default: the "
               "manifest's; point at RELEASE DLLs here)\n"
               "  --seal  bake a read-only cache and strip the cache-served "
               "sources (needs a GPU locally)\n"
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

    // 3. Run the closure walker on the REAL graph and report copy/strip/external.
    const wz::engine::bundle::BundleClosure closure =
        wz::engine::bundle::compute_bundle_closure(
            draft, launch.launch.resource_root);
    report_closure(options, closure, project_root);

    // 4. Assemble the bundle: authored tree, runtime exe, bootstrap config.
    std::error_code ec;
    const fs::path bundle_root(options.out);
    fs::create_directories(bundle_root, ec);
    if (ec) {
        std::cerr << "wozzits_export: cannot create bundle dir: "
                  << options.out << ": " << ec.message() << '\n';
        return 1;
    }

    // Behavior modules: ship a clean bundle/behavior/ of just the DLLs. The
    // source is --behavior-modules (point at RELEASE DLLs) or, by default, the
    // manifest's folder; the project's behavior source/build tree is excluded
    // from the verbatim copy (only the DLLs are needed at runtime).
    const fs::path behavior_source = !options.behavior_modules.empty()
        ? fs::path(options.behavior_modules)
        : fs::path(launch.launch.manifest.behavior_module_folder);
    std::string behavior_project_rel;
    if (!launch.launch.manifest.behavior_project_folder.empty()) {
        behavior_project_rel = fs::relative(
            fs::path(launch.launch.manifest.behavior_project_folder),
            project_root, ec).generic_string();
    }

    const long copied = copy_project_tree(
        options, project_root, bundle_root, behavior_project_rel, error);
    if (copied < 0) {
        std::cerr << "wozzits_export: " << error << '\n';
        return 1;
    }
    log_line(options,
        "copied " + std::to_string(copied) + " project files");

    const long behavior_dlls =
        ship_behavior_modules(options, behavior_source, bundle_root, error);
    if (behavior_dlls < 0) {
        std::cerr << "wozzits_export: " << error << '\n';
        return 1;
    }
    if (behavior_dlls > 0
        && !verify_behavior_dlls(options, bundle_root / "behavior", error))
    {
        std::cerr << "wozzits_export: " << error << '\n';
        return 1;
    }

    // Relocate any sources the graph references from OUTSIDE the project tree
    // (e.g. shared engine shaders) into the bundle's resources/ subtree and
    // rewrite the graph to point at them, so the bundle is self-contained.
    std::map<std::string, std::string> external_rewrites;
    const fs::path bundle_graph_path = bundle_root
        / fs::relative(fs::path(launch.launch.asset_graph_path), project_root, ec);
    if (!relocate_external_sources(
            options, draft, closure, project_root, bundle_root,
            bundle_graph_path, external_rewrites, error))
    {
        std::cerr << "wozzits_export: " << error << '\n';
        return 1;
    }

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
    if (behavior_dlls > 0) {
        doc.behavior_modules = "behavior";  // the clean flat dir shipped above
        // Stamp the ABI the DLLs were just verified against, so the runtime
        // rejects a mismatched exe at launch (Seam 3.5).
        doc.behavior_abi_version = WZ_BEHAVIOR_ABI_VERSION;
    }
    // Write the config UNSEALED first: it names the graph/scene/resource/behavior
    // paths the --seal warm run reads, and is the final config for a plain
    // (unsealed) export. Sealing rewrites it with the cache block afterwards.
    if (!wz::app::write_app_bootstrap_config(bundle_root.string(), doc, error)) {
        std::cerr << "wozzits_export: " << error << '\n';
        return 1;
    }

    if (options.seal) {
        const int seal_status = seal_bundle(
            options, bundle_root, project_root, closure, external_rewrites,
            doc, error);
        if (seal_status == wz::app::kRuntimeNoDeviceExitCode) {
            // No GPU to bake the cache: surface the runtime's no-device code so a
            // CTest treats it as SKIPPED rather than a real failure.
            std::cerr << "wozzits_export: " << error << '\n';
            return wz::app::kRuntimeNoDeviceExitCode;
        }
        if (seal_status != 0) {
            std::cerr << "wozzits_export: " << error << '\n';
            return 1;
        }
    }

    log_line(options,
        std::string(options.seal ? "sealed bundle" : "bundle")
        + " written to " + bundle_root.string());
    return 0;
}
