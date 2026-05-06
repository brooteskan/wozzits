// engine/assets/engine_asset_library_files.cpp

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/key_factories/file_carrier.h>
#include <engine/assets/key_factories/hlsl_shader.h>
#include <engine/assets/key_factories/scalar_field.h>
#include <engine/assets/key_factories/scalar_field_procedural.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/shader/shader_types.h>
#include <gpu/shader.h>




namespace wz::engine::assets
{

    // ─── Private helpers ──────────────────────────────────────────────────────────

    wz::asset::AssetKey EngineAssetLibrary::register_file_node(
        const wz::fs::Path& relative_path,
        wz::asset::SchemaID  schema,
        wz::asset::AssetType type)
    {
        const std::string canonical =
            detail::canonical_asset_path(relative_path);

        const wz::asset::AssetKey key = make_file_key(canonical, schema);

        const wz::fs::Path full_path =
            wz::fs::join(resource_root_, relative_path);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = type;
        node.schema = schema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = internal::FileSourceDesc{
            .full_path = full_path,
            .canonical_path = canonical,
        };

        if (!system_.register_asset(std::move(node))) {
            logger_.error("failed to register file node: " + canonical);
            return {};
        }

        return key;
    }

}