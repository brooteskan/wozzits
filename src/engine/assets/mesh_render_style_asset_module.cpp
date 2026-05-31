#include <engine/assets/mesh_render_style_asset_module.h>

#include <engine/assets/key_factories/mesh_render_style.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    MeshRenderStyleAssetModule::MeshRenderStyleAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        MeshRenderStyleTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    MeshRenderStyleAsset
    MeshRenderStyleAssetModule::create_mesh_render_style(
        const MeshRenderStyleDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("mesh render style has empty name");
            return {};
        }

        if (!desc.style.valid()) {
            logger_.error("mesh render style is invalid: " + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_mesh_render_style_key(desc.name, desc.style);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeMeshRenderStyle;
        node.schema = kMeshRenderStyleSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = MeshRenderStyleCompileDesc{
            .style = desc.style,
        };

        if (!system_.register_asset(std::move(node))) {
            return MeshRenderStyleAsset{ .output = key };
        }

        return MeshRenderStyleAsset{ .output = key };
    }

    MeshRenderStyleHandle
    MeshRenderStyleAssetModule::get_mesh_render_style(
        const MeshRenderStyleAsset& asset) const
    {
        if (!asset.valid()) {
            return {};
        }

        MeshRenderStyleHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }

        if (!out.valid()) {
            logger_.error("mesh render style handle not found");
        }

        return out;
    }

    const MeshRenderStyleData*
    MeshRenderStyleAssetModule::get_mesh_render_style_data(
        MeshRenderStyleHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }

        return table_.get(handle.handle);
    }
}
