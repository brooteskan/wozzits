#pragma once

// wozzits/rhi/compute_program.h
//
// Compute pipelines as registered data — the peer of render_program.h for the
// compute path. Same no-enums treatment: a compute program is a named registry
// entry, never an enum member, and its descriptor semantics are Tags.
//
// Reuses the binding declarations from render_program.h (RootConstantBinding,
// DescriptorBinding, DescriptorSemanticRegistry) — bindings are not graphics-
// specific. Compute commonly binds UAVs (DescriptorKind::UAV), which the shared
// DescriptorBinding already covers.
//
// A ComputeProgramDesc is the PIPELINE contract (shader + binding layout +
// thread-group size). Dispatch is execution-side — a compute pass in the frame
// graph records a dispatch — just as a draw is execution-side for a render
// program.

#include <wozzits/rhi/program_registry.h>
#include <wozzits/rhi/render_program.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wz::rhi
{
    struct ComputeProgramDesc
    {
        std::string name;
        std::string compute_shader;   // path/ref; resolved by rhi's shader subsystem

        // Dispatch granularity declared by the shader ([numthreads(x, y, z)]).
        uint32_t thread_group_size[3] = { 1, 1, 1 };

        std::vector<RootConstantBinding> root_constants;
        std::vector<DescriptorBinding>   descriptor_bindings;
    };

    inline constexpr size_t kMaxComputePrograms = 256;

    using ComputeProgramRegistry =
        ProgramRegistry<ComputeProgramDesc, kMaxComputePrograms>;
}
