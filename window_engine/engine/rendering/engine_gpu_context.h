#pragma once

#include <engine/rendering/rhi_gpu_backend.h>

#include <gpu/gpu.h>

#include <wozzits/rhi/compute_program.h>
#include <wozzits/rhi/constants_layout.h>
#include <wozzits/rhi/gpu_resource_registry.h>
#include <wozzits/rhi/render_program_registry.h>
#include <wozzits/rhi/shader_module.h>
#include <wozzits/rhi/shader_resource_group_layout.h>

namespace wz::engine::rendering
{
    // Shared GPU + rhi context, owned by AppContext and injected into BOTH the
    // asset library (the producer — the render-program compiler registers rhi
    // programs/shaders here during resolve) and RhiSceneRenderer (the consumer —
    // it binds them). The program/shader/semantic registries live here, not in
    // renderer-private RhiContext, so producer and consumer share one Tag space;
    // the GpuResourceRegistry was hoisted the same way in #190. The PSO cache,
    // draw passes, and resource variants stay renderer-owned (RhiContext).
    struct EngineGpuContext
    {
        explicit EngineGpuContext(wz::gpu::Device& in_device)
            : device(in_device)
            , backend(in_device)
            , resources(backend)
        {
        }

        // Drain the registry while the backend + device are still alive: this
        // body runs BEFORE the members are destroyed (reverse order: resources,
        // then backend). Without it, resident GPU resources are reclaimed only
        // because destroy_device sweeps the dx12 tables right after -- fine for
        // the process-lifetime context today, but a leak the moment a context is
        // rebuilt against a surviving device. GpuBackend has no virtual dtor and
        // is a plain member, so nothing else frees them.
        ~EngineGpuContext()
        {
            resources.destroy_all();
        }

        wz::gpu::Device&             device;
        EngineGpuBackend             backend;
        wz::rhi::GpuResourceRegistry resources;

        // rhi programs/shaders/semantics — shared between the asset compiler
        // (registers during resolve) and the renderer (binds). Must not be
        // renderer-private: the desc's descriptor/constant Tags must come from
        // the same registry both sides use, or the bindings silently mismatch.
        wz::rhi::RenderProgramRegistry      programs;
        wz::rhi::ComputeProgramRegistry     compute_programs;
        wz::rhi::ShaderModuleRegistry       shaders;
        wz::rhi::DescriptorSemanticRegistry descriptor_semantics;
        wz::rhi::ConstantSemanticRegistry   constant_semantics;
    };
}
