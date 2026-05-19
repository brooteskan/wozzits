#include <engine/assets/render_program/render_program_asset_module.h>
#include <engine/assets/key_factories/render_program.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <gpu/dx12/dx12_internal.h>

#include <asset/system.h>

namespace wz::engine::assets
{
    namespace
    {
        wz::asset::AssetKey make_render_program_key(
            const BuiltinRenderProgramDesc& desc)
        {
            return make_builtin_render_program_key(
                desc.name,
                desc.program,
                desc.vertex_shader,
                desc.pixel_shader);
        }
    }

    RenderProgramAssetModule::RenderProgramAssetModule(
        wz::asset::AssetSystem& system,
        RenderProgramTable& table)
        : system_(&system)
        , table_(&table)
    {
    }

    RenderProgramAsset RenderProgramAssetModule::create_builtin(
        BuiltinRenderProgramDesc desc)
    {
        if (!system_)
            return {};

        if (desc.name.empty())
            return {};

        if (desc.program == BuiltinRenderProgram::Count)
            return {};

        if (desc.vertex_shader == wz::asset::AssetKey{} ||
            desc.pixel_shader  == wz::asset::AssetKey{})
            return {};

        const wz::asset::AssetKey key = make_render_program_key(desc);

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypeRenderProgram;
        node.schema = kBuiltinRenderProgramSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = std::move(desc);

        const auto& d = std::any_cast<const BuiltinRenderProgramDesc&>(node.meta);
        if (!system_->register_asset(node, { d.vertex_shader, d.pixel_shader }))
            return {};

        return RenderProgramAsset{ key };
    }

    wz::asset::ResourceHandle RenderProgramAssetModule::get_render_program(
        RenderProgramAsset asset) const
    {
        if (!system_ || !asset.valid())
            return {};

        const auto* compiled = system_->find_compiled(asset.key);
        if (!compiled)
            return {};

        return compiled->handle;
    }

    const RenderProgramData* RenderProgramAssetModule::get_render_program_data(
        wz::asset::ResourceHandle handle) const
    {
        return table_ ? table_->get(handle) : nullptr;
    }

    bool RenderProgramAssetModule::realize_pipeline(
        wz::gpu::Device&   device,
        RenderProgramAsset asset)
    {
        if (!table_ || !asset.valid())
            return false;

        const wz::asset::ResourceHandle handle = get_render_program(asset);
        if (!handle.valid())
            return false;

        RenderProgramData* data = table_->get(handle);
        if (!data)
            return false;

        if (data->pipeline_handle.valid())
            return true;  // already realized

        data->pipeline_handle =
            wz::gpu::dx12::internal::create_graphics_pipeline_from_data(
                device, *data, data->vertex_shader, data->pixel_shader);

        return data->pipeline_handle.valid();
    }

    RenderProgramTable& RenderProgramAssetModule::table()
    {
        return *table_;
    }

    const RenderProgramTable& RenderProgramAssetModule::table() const
    {
        return *table_;
    }
}