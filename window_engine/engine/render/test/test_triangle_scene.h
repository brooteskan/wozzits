#pragma once

#include <render/frame/render_frame.h>
#include <scene/compile/scene_compiler.h>

namespace wz::gpu::dx12
{
    struct TriangleTestFrame
    {
        wz::render::RenderFrameStorage frame_storage{};
        wz::scene::ViewData view{};
    };

    TriangleTestFrame build_triangle_test_frame();
}