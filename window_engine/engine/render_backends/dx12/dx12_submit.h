// file: render_backends/dx12/dx12_submit.h

#pragma once

#include <render/frame/render_frame.h>
#include <gpu/gpu.h>

namespace wz::render::backend::dx12
{
    struct Context;
    // opaque DX12 state (device, PSOs, root signatures, etc.)

    Context* create(wz::gpu::Device& device);

    void destroy(Context* ctx);

    // THIS is the important function
    void submit(Context* ctx, const RenderFrame& frame);
}