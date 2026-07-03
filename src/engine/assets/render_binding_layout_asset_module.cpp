#include <engine/assets/render_binding_layout_asset_module.h>
#include <engine/assets/key_factories/render_binding_layout.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    RenderBindingLayoutAssetModule::RenderBindingLayoutAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        RenderBindingLayoutTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    RenderBindingLayoutAsset
    RenderBindingLayoutAssetModule::create_render_binding_layout(
        const RenderBindingLayoutDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("render binding layout has empty name");
            return {};
        }

        RenderBindingLayoutSrg srg{};
        std::string error;
        if (!build_render_binding_layout_srg(desc.layout, srg, error)) {
            logger_.error(
                "render binding layout is invalid: " + error
                + ": " + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_render_binding_layout_key(desc.name, desc.layout);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderBindingLayout;
        node.schema = kRenderBindingLayoutSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = RenderBindingLayoutCompileDesc{
            .layout = desc.layout,
        };

        // register_asset() returns false when the key is already registered;
        // re-materializing the same content is expected to hit that, so the
        // existing key is returned rather than failing.
        (void)system_.register_asset(std::move(node));

        return RenderBindingLayoutAsset{ .output = key };
    }

    RenderBindingLayoutHandle
    RenderBindingLayoutAssetModule::get_render_binding_layout(
        const RenderBindingLayoutAsset& asset) const
    {
        if (!asset.valid()) {
            return {};
        }

        RenderBindingLayoutHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }

        if (!out.valid()) {
            logger_.error("render binding layout handle not found");
        }

        return out;
    }

    const RenderBindingLayoutData*
    RenderBindingLayoutAssetModule::get_render_binding_layout_data(
        RenderBindingLayoutHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }

        return table_.get(handle.handle);
    }
}
