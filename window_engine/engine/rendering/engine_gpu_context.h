#pragma once

#include <engine/rendering/rhi_gpu_backend.h>

#include <gpu/gpu.h>

#include <wozzits/rhi/gpu_resource_registry.h>

namespace wz::engine::rendering
{
    struct EngineGpuContext
    {
        explicit EngineGpuContext(wz::gpu::Device& in_device)
            : device(in_device)
            , backend(in_device)
            , resources(backend)
        {
        }

        wz::gpu::Device&             device;
        EngineGpuBackend             backend;
        wz::rhi::GpuResourceRegistry resources;
    };
}
