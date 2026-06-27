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
        wz::Logger&             logger,
        wz::fs::Path            resource_root)
        : system_(system)
        , logger_(logger)
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

        const wz::fs::Path full_path = resolve_path(path);

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

    wz::fs::Path FileCarrierAssetModule::resolve_path(
        const wz::fs::Path& path) const
    {
        // Strip a single matched surrounding pair of ASCII double-quotes before
        // resolving. Windows Explorer's "Copy as path" wraps the path in double
        // quotes (e.g. "C:\...\tank1.glb"), which is not openable as-is. Only a
        // genuine leading+trailing pair is removed; interior quotes and an
        // unbalanced single quote are left untouched.
        wz::fs::Path cleaned = path;
        if (cleaned.size() >= 2
            && cleaned.front() == '"'
            && cleaned.back() == '"')
        {
            cleaned = cleaned.substr(1, cleaned.size() - 2);
        }

        return wz::fs::is_absolute(cleaned)
            ? cleaned
            : wz::fs::join(resource_root_, cleaned);
    }

} // namespace wz::engine::assets
