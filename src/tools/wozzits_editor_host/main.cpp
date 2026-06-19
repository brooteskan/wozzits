#include <engine/project/project_manifest.h>

#include <cstdint>
#include <iostream>
#include <string>

namespace
{
    struct Options
    {
        std::string command;
        wz::fs::Path project_root;
        wz::fs::Path resource_root;
        std::string name;
    };

    std::string json_string(const std::string& text)
    {
        std::string out;
        out.push_back('"');
        for (const char ch : text) {
            switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                out.push_back(ch);
                break;
            }
        }
        out.push_back('"');
        return out;
    }

    const char* status_text(
        wz::engine::project::ProjectManifestProbeStatus status)
    {
        switch (status) {
        case wz::engine::project::ProjectManifestProbeStatus::Missing:
            return "missing";
        case wz::engine::project::ProjectManifestProbeStatus::Invalid:
            return "invalid";
        case wz::engine::project::ProjectManifestProbeStatus::Valid:
            return "valid";
        }
        return "invalid";
    }

    void write_manifest_json(
        std::ostream& out,
        const wz::engine::project::ProjectManifest& manifest)
    {
        out << "{"
            << "\"root\":" << json_string(manifest.root) << ","
            << "\"manifestPath\":" << json_string(manifest.manifest_path)
            << ","
            << "\"schema\":" << json_string(manifest.schema) << ","
            << "\"formatVersion\":" << manifest.format_version << ","
            << "\"name\":" << json_string(manifest.name) << ","
            << "\"rhiRenderPath\":"
            << (manifest.rhi_render_path ? "true" : "false") << ","
            << "\"scenePath\":" << json_string(manifest.scene_path) << ","
            << "\"assetGraphPath\":"
            << json_string(manifest.asset_graph_path) << ","
            << "\"behaviorProjectFolder\":"
            << json_string(manifest.behavior_project_folder) << ","
            << "\"behaviorModuleFolder\":"
            << json_string(manifest.behavior_module_folder)
            << "}";
    }

    void write_response_json(
        bool ok,
        const std::string& status,
        bool created,
        const std::string& error,
        const wz::engine::project::ProjectManifest& manifest)
    {
        std::cout << "{"
                  << "\"ok\":" << (ok ? "true" : "false") << ","
                  << "\"status\":" << json_string(status) << ","
                  << "\"created\":" << (created ? "true" : "false") << ","
                  << "\"error\":" << json_string(error) << ","
                  << "\"project\":";
        write_manifest_json(std::cout, manifest);
        std::cout << "}\n";
    }

    void write_log(const char* level, const std::string& message)
    {
        std::cout << '[' << level << "] " << message << '\n';
        std::cout.flush();
    }

    bool parse_options(int argc, char** argv, Options& out, std::string& error)
    {
        if (argc < 3) {
            error =
                "usage: wozzits_editor_host <probe|create|serve> <project-root> "
                "[--resource-root <path>] [--name <name>]";
            return false;
        }

        out.command = argv[1];
        out.project_root = argv[2];

        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--resource-root" && i + 1 < argc) {
                out.resource_root = argv[++i];
            }
            else if (arg == "--name" && i + 1 < argc) {
                out.name = argv[++i];
            }
            else {
                error = "unknown or incomplete option: " + arg;
                return false;
            }
        }

        if (out.command != "probe" && out.command != "create"
            && out.command != "serve") {
            error = "unknown command: " + out.command;
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
        write_response_json(
            false,
            "invalid",
            false,
            error,
            wz::engine::project::ProjectManifest{});
        return 2;
    }

    if (options.command == "probe") {
        const auto result = wz::engine::project::probe_project_manifest(
            wz::engine::project::ProjectManifestLoadDesc{
                .project_root = options.project_root,
                .resource_root = options.resource_root,
            });
        write_response_json(
            result.valid(),
            status_text(result.status),
            false,
            result.error,
            result.manifest);
        return 0;
    }

    if (options.command == "serve") {
        write_log("info", "wozzits_editor_host starting");
        write_log("info", "project root: " + options.project_root);

        const auto result = wz::engine::project::probe_project_manifest(
            wz::engine::project::ProjectManifestLoadDesc{
                .project_root = options.project_root,
                .resource_root = options.resource_root,
            });

        if (!result.valid()) {
            write_log("error", result.error);
            return 1;
        }

        write_log("info", "project loaded: " + result.manifest.name);
        write_log("info", "ready");

        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "shutdown" || line == "exit" || line == "quit") {
                write_log("info", "shutdown requested");
                return 0;
            }

            if (!line.empty()) {
                write_log("warn", "unknown command: " + line);
            }
        }

        write_log("info", "stdin closed; exiting");
        return 0;
    }

    const auto result = wz::engine::project::create_project_manifest(
        wz::engine::project::ProjectManifestCreateDesc{
            .project_root = options.project_root,
            .resource_root = options.resource_root,
            .name = options.name,
        });
    write_response_json(
        result.ok,
        result.ok ? "valid" : "invalid",
        result.created,
        result.error,
        result.manifest);
    return result.ok ? 0 : 1;
}
