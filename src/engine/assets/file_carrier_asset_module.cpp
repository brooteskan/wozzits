// src/engine/assets/file_carrier_asset_module.cpp

#include <engine/assets/file_carrier_asset_module.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/key_factories/file_carrier.h>

#include <vector>

namespace wz::engine::assets
{

    FileCarrierAssetModule::FileCarrierAssetModule(
        wz::asset::AssetSystem& system,
        wz::fs::Path            resource_root)
        : system_(system)
        , resource_root_(std::move(resource_root))
    {
    }

    wz::asset::AssetKey FileCarrierAssetModule::register_file_node(
        const wz::fs::Path& path,
        wz::asset::SchemaID  schema,
        wz::asset::AssetType type)
    {
        const std::string canonical =
            detail::canonical_asset_path(path);

        const wz::asset::AssetKey key = make_file_key(canonical, schema);

        // Store the AUTHORED path, NOT one rooted at resource_root. Every
        // reader of FileSourceDesc::full_path resolves it against the library's
        // resource_root at read time (file carriers, shader compilers, key
        // derivation), and wozzits_export rewrites it to a bundle-relative path
        // when relocating sources. Rooting it here applied resource_root twice
        // ("resources/resources/projects/..."), and it also baked a
        // machine-specific prefix into the serialized graph. Quotes are still
        // stripped here so the stored path is openable once resolved.
        const wz::fs::Path full_path = strip_file_carrier_path_quotes(path);

        wz::asset::AssetNode node;
        node.key     = key;
        node.type    = type;
        node.schema  = schema;
        node.stage     = wz::asset::AssetStage::Source;
        node.residency = wz::asset::ResidencyIntent::CompileOnly;
        node.payload   = std::vector<uint8_t>{};
        node.meta      = internal::FileSourceDesc{
            .full_path      = full_path,
            .canonical_path = canonical,
        };

        // register_asset returns false when the key is already present — that is
        // intentional here: multiple asset nodes may share the same file carrier.
        system_.register_asset(std::move(node));

        return key;
    }

    wz::fs::Path strip_file_carrier_path_quotes(const wz::fs::Path& path)
    {
        // Strip a single matched surrounding pair of ASCII double-quotes.
        // Windows Explorer's "Copy as path" wraps the path in double quotes
        // (e.g. "C:\...\tank1.glb"), which is not openable as-is. Only a
        // genuine leading+trailing pair is removed; interior quotes and an
        // unbalanced single quote are left untouched.
        if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
            return path.substr(1, path.size() - 2);
        }
        return path;
    }

    wz::fs::Path resolve_file_carrier_path(
        const wz::fs::Path& resource_root,
        const wz::fs::Path& path)
    {
        const wz::fs::Path cleaned = strip_file_carrier_path_quotes(path);

        return wz::fs::is_absolute(cleaned)
            ? cleaned
            : wz::fs::join(resource_root, cleaned);
    }

    wz::fs::Path FileCarrierAssetModule::resolve_path(
        const wz::fs::Path& path) const
    {
        return resolve_file_carrier_path(resource_root_, path);
    }

} // namespace wz::engine::assets
