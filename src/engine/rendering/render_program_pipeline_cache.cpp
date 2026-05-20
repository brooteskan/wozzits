// src/engine/rendering/render_program_pipeline_cache.cpp

#include <engine/rendering/render_program_pipeline_cache.h>
#include <gpu/dx12/dx12_internal.h>

namespace wz::engine::rendering
{
    bool RenderProgramPipelineCache::realize(
        wz::gpu::Device&                               device,
        const wz::engine::assets::RenderProgramTable&  table,
        wz::asset::ResourceHandle                      render_program)
    {
        if (!render_program.valid())
            return false;

        if (get(render_program).valid())
            return true;  // already realized

        const wz::engine::assets::RenderProgramData* data =
            table.get(render_program);

        if (!data || !data->valid())
            return false;

        const wz::gpu::GPUHandle pipeline =
            wz::gpu::dx12::internal::create_graphics_pipeline_from_data(
                device, *data, data->vertex_shader, data->pixel_shader);

        if (!pipeline.valid())
            return false;

        entries_.push_back({ render_program, pipeline, data->binding_model });
        return true;
    }

    wz::gpu::GPUHandle RenderProgramPipelineCache::get(
        wz::asset::ResourceHandle render_program) const noexcept
    {
        for (const Entry& e : entries_)
        {
            if (e.render_program.id    == render_program.id &&
                e.render_program.epoch == render_program.epoch &&
                e.render_program.type  == render_program.type)
            {
                return e.pipeline;
            }
        }
        return {};
    }

    wz::engine::assets::RenderBindingModel
    RenderProgramPipelineCache::get_binding_model(
        wz::asset::ResourceHandle render_program) const noexcept
    {
        for (const Entry& e : entries_)
        {
            if (e.render_program.id    == render_program.id &&
                e.render_program.epoch == render_program.epoch &&
                e.render_program.type  == render_program.type)
            {
                return e.binding_model;
            }
        }
        return wz::engine::assets::RenderBindingModel::MeshIA;
    }

    void RenderProgramPipelineCache::clear() noexcept
    {
        entries_.clear();
    }
}
