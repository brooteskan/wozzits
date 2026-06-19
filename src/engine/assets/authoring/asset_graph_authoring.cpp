// src/engine/assets/authoring/asset_graph_authoring.cpp

#include <engine/assets/authoring/asset_graph_authoring.h>

#include <asset/param_defaults.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/engine_asset_library_internal.h>

#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wz::engine::assets::authoring
{
    namespace
    {
        // The authored "source_path" parameter value, or empty.
        std::string source_path_from_params(const wz::asset::ParamBlock& params)
        {
            const auto it = params.values.find("source_path");
            if (it == params.values.end()) {
                return {};
            }
            if (const auto* path = std::get_if<std::string>(&it->second)) {
                return *path;
            }
            return {};
        }

        // Strip surrounding whitespace and a single layer of matching quotes;
        // if the text instead embeds a quoted absolute path, return that.
        std::string trim_quoted_path(std::string_view raw)
        {
            while (!raw.empty()
                && std::isspace(static_cast<unsigned char>(raw.front())))
            {
                raw.remove_prefix(1);
            }
            while (!raw.empty()
                && std::isspace(static_cast<unsigned char>(raw.back())))
            {
                raw.remove_suffix(1);
            }
            if (raw.size() >= 2
                && ((raw.front() == '"' && raw.back() == '"')
                    || (raw.front() == '\'' && raw.back() == '\'')))
            {
                raw.remove_prefix(1);
                raw.remove_suffix(1);
            }
            else {
                const size_t first_quote = raw.find('"');
                const size_t last_quote = raw.rfind('"');
                if (first_quote != std::string_view::npos
                    && last_quote != first_quote)
                {
                    const std::string embedded{ raw.substr(
                        first_quote + 1u,
                        last_quote - first_quote - 1u) };
                    if (wz::fs::is_absolute(embedded)) {
                        return embedded;
                    }
                }
            }
            return std::string(raw);
        }

        bool compiler_has_file_path_parameter(
            const wz::asset::AssetCompiler* compiler)
        {
            if (!compiler) {
                return false;
            }
            for (const wz::asset::ParamDecl& decl : compiler->parameters) {
                if (decl.type == wz::asset::ParamType::FilePath) {
                    return true;
                }
            }
            return false;
        }

        // Resolve every FilePath parameter's authored value to an absolute path.
        void resolve_param_file_paths(
            wz::asset::ParamBlock& block,
            const wz::asset::AssetCompiler& compiler,
            const GraphAuthoringContext& ctx)
        {
            for (const wz::asset::ParamDecl& decl : compiler.parameters) {
                if (decl.type != wz::asset::ParamType::FilePath) {
                    continue;
                }
                const auto it = block.values.find(std::string(decl.name));
                if (it == block.values.end()) {
                    continue;
                }
                auto* path = std::get_if<std::string>(&it->second);
                if (!path || path->empty()) {
                    continue;
                }
                const std::string trimmed = trim_quoted_path(*path);
                *path = ctx.resolve_file ? ctx.resolve_file(trimmed) : trimmed;
            }
        }
    }

    wz::asset::AssetNode make_source_asset_node(
        const GraphAuthoringContext& ctx,
        wz::asset::SchemaID schema,
        wz::asset::AssetType type,
        wz::asset::ParamBlock params,
        std::optional<wz::fs::Path> source_file)
    {
        const wz::asset::AssetCompiler* compiler =
            ctx.registry.find(schema, type);

        wz::asset::ParamBlock block = std::move(params);
        if (compiler) {
            wz::asset::ensure_param_block_defaults(block, *compiler);
        }

        const bool is_file_carrier = compiler_has_file_path_parameter(compiler);

        wz::asset::AssetNode node{};
        node.key = {};
        node.type = type;
        node.schema = schema;
        node.stage = wz::asset::AssetStage::Source;
        node.residency = is_file_carrier
            ? wz::asset::ResidencyIntent::CompileOnly
            : wz::asset::ResidencyIntent::EditorResident;
        node.kind = wz::asset::AssetNodeKind::Asset;
        node.payload = std::vector<uint8_t>{};

        // Only a file-carrier compiler (one declaring a FilePath parameter) gets
        // FileSourceDesc meta. A source_file handed to a non-file/param compiler
        // is ignored: the resolved param block below is that node's meta instead.
        if (source_file && is_file_carrier) {
            internal::FileSourceDesc file{};
            std::string path = trim_quoted_path(*source_file);
            const std::string from_param =
                trim_quoted_path(source_path_from_params(block));
            if (!from_param.empty()) {
                path = from_param;
            }
            file.canonical_path = detail::canonical_asset_path(path);
            file.full_path = ctx.resolve_file ? ctx.resolve_file(path) : path;
            node.meta = std::move(file);
        }
        else if (compiler && !compiler->parameters.empty()) {
            resolve_param_file_paths(block, *compiler, ctx);
            node.meta = std::move(block);
        }

        return node;
    }

    wz::asset::AssetGraphDraftNodeId add_source_asset_node(
        wz::asset::AssetGraphDraft& draft,
        const GraphAuthoringContext& ctx,
        wz::asset::SchemaID schema,
        wz::asset::AssetType type,
        wz::asset::ParamBlock params,
        std::optional<wz::fs::Path> source_file)
    {
        if (type == wz::asset::AssetType::Unknown) {
            return wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        }

        return wz::asset::add_asset_graph_draft_node(
            draft,
            make_source_asset_node(
                ctx,
                schema,
                type,
                std::move(params),
                std::move(source_file)),
            wz::asset::AssetGraphDraftNodeState::Created);
    }
}
