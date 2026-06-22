#pragma once

#include <wozzits/rhi/draw_list_tag.h>
#include <wozzits/rhi/gpu_resource.h>

namespace wz::engine::rendering
{
    // Renderer-local rhi registries. The program / shader / descriptor-semantic /
    // constant-semantic registries moved to EngineGpuContext so the asset
    // compiler can register into the same instances the renderer binds (#192);
    // only the genuinely renderer-owned registries stay here: draw-list passes
    // and mesh pull-buffer resource variants (the compiler never touches these).
    struct RhiContext
    {
        wz::rhi::DrawListTagRegistry     passes;
        wz::rhi::ResourceVariantRegistry resource_variants;
    };
}
