#pragma once

#include <wozzits/rhi/draw_list_tag.h>
#include <wozzits/rhi/gpu_resource.h>
#include <wozzits/rhi/gpu_resource_registry.h>
#include <wozzits/rhi/render_program_registry.h>
#include <wozzits/rhi/shader_module.h>
#include <wozzits/rhi/shader_resource_group_layout.h>

namespace wz::engine::rendering
{
    struct RhiContext
    {
        explicit RhiContext(wz::rhi::GpuBackend& backend)
            : resources(backend)
        {
        }

        wz::rhi::GpuResourceRegistry        resources;
        wz::rhi::RenderProgramRegistry      programs;
        wz::rhi::ShaderModuleRegistry       shaders;
        wz::rhi::DescriptorSemanticRegistry descriptor_semantics;
        wz::rhi::ConstantSemanticRegistry   constant_semantics;
        wz::rhi::DrawListTagRegistry        passes;
        wz::rhi::ResourceVariantRegistry    resource_variants;
    };
}
