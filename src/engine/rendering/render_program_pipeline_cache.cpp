// src/engine/rendering/render_program_pipeline_cache.cpp

#include <engine/rendering/render_program_pipeline_cache.h>
#include <gpu/dx12/dx12_internal.h>

#include <algorithm>
#include <optional>

namespace
{
    // Shared lookup predicate for the three get_* accessors below.
    template <typename Entries>
    auto find_entry(const Entries& entries,
                    wz::asset::ResourceHandle render_program)
    {
        return std::find_if(entries.begin(), entries.end(),
            [&](const auto& e) {
                return e.render_program.id    == render_program.id &&
                       e.render_program.epoch == render_program.epoch &&
                       e.render_program.type  == render_program.type;
            });
    }
}

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

        // ── Compute realized binding layout ───────────────────────────────────
        //
        // Mirrors the root parameter assignment in create_root_signature_from_data:
        //   params[0 .. root_constants.size()-1] = root constant params (in order)
        //   params[root_constants.size() .. ]    = one table per visibility group
        //
        // Groups are built in first-occurrence order, same as the factory.

        RealizedRenderProgramBindingLayout layout;

        // Root constants: one entry per declared binding, assigned param indices
        // starting at 0.
        for (size_t i = 0; i < data->root_constants.size(); ++i)
        {
            const auto& rc = data->root_constants[i];
            layout.root_constants.push_back({
                .root_parameter_index = static_cast<uint32_t>(i),
                .value_count          = rc.value_count,
                .shader_register      = rc.shader_register,
                .register_space       = rc.register_space,
            });
        }

        // Descriptor bindings: group by visibility (first-occurrence order).
        // Track groups as (visibility, [binding_indices]).
        struct VisGroup { wz::engine::assets::ShaderVisibility vis; std::vector<size_t> indices; };
        std::vector<VisGroup> groups;
        for (size_t i = 0; i < data->descriptor_bindings.size(); ++i)
        {
            auto v = data->descriptor_bindings[i].visibility;
            auto it = std::find_if(groups.begin(), groups.end(),
                [v](const VisGroup& g){ return g.vis == v; });
            if (it != groups.end())
                it->indices.push_back(i);
            else
                groups.push_back({ v, { i } });
        }

        uint32_t next_root_param =
            static_cast<uint32_t>(data->root_constants.size());
        uint32_t heap_slot = 0;

        for (const auto& group : groups)
        {
            const uint32_t table_root_param = next_root_param++;

            layout.desc_tables.push_back({
                .root_parameter_index = table_root_param,
                .heap_start_slot      = heap_slot,
                .slot_count           = static_cast<uint32_t>(group.indices.size()),
            });

            uint32_t table_offset = 0;
            for (size_t bi : group.indices)
            {
                layout.descriptors.push_back({
                    .semantic                = data->descriptor_bindings[bi].semantic,
                    .root_parameter_index    = table_root_param,
                    .descriptor_table_offset = heap_slot + table_offset,
                });
                ++table_offset;
            }
            heap_slot += static_cast<uint32_t>(group.indices.size());
        }

        entries_.push_back({ render_program, pipeline, data->binding_model,
                             std::move(layout) });
        return true;
    }

    wz::gpu::GPUHandle RenderProgramPipelineCache::get(
        wz::asset::ResourceHandle render_program) const noexcept
    {
        const auto it = find_entry(entries_, render_program);
        return it != entries_.end() ? it->pipeline : wz::gpu::GPUHandle{};
    }

    std::optional<wz::engine::assets::RenderBindingModel>
    RenderProgramPipelineCache::get_binding_model(
        wz::asset::ResourceHandle render_program) const noexcept
    {
        const auto it = find_entry(entries_, render_program);
        if (it == entries_.end())
            return std::nullopt;
        return it->binding_model;
    }

    const RealizedRenderProgramBindingLayout*
    RenderProgramPipelineCache::get_binding_layout(
        wz::asset::ResourceHandle render_program) const noexcept
    {
        const auto it = find_entry(entries_, render_program);
        return it != entries_.end() ? &it->binding_layout : nullptr;
    }

    void RenderProgramPipelineCache::clear() noexcept
    {
        entries_.clear();
    }
}
