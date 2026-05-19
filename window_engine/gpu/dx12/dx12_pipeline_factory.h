#pragma once
// gpu/dx12/dx12_pipeline_factory.h
//
// Creates DX12 root signatures and PSOs for BuiltinRenderProgram values.
//
// This is the single source of truth for all built-in pipeline state.  Both
// the legacy debug context path and the new RenderablePipelineCache path call
// these functions.  The debug context files are transitional wrappers; they
// should not contain their own PSO creation logic long-term.

#include <d3d12.h>

#include <gpu/gpu.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/render_program/render_program.h>

namespace wz::gpu::dx12::internal
{
    // Create the root signature for the given builtin program.
    // Returns nullptr on failure.
    ID3D12RootSignature* create_root_signature_for_program(
        ID3D12Device* device,
        wz::engine::assets::BuiltinRenderProgram program);

    // Create the PSO for the given builtin program using already-compiled shaders.
    // root_sig must match the one returned by create_root_signature_for_program
    // for the same program.  Returns nullptr on failure.
    ID3D12PipelineState* create_pso_for_program(
        Device& device,
        wz::engine::assets::BuiltinRenderProgram program,
        ID3D12RootSignature* root_sig,
        GPUHandle vertex_shader,
        GPUHandle pixel_shader);

    // Data-driven variants: build root signature and PSO from RenderProgramData.
    // These replace the enum-dispatch functions for new render programs.
    // Returns nullptr on failure.
    ID3D12RootSignature* create_root_signature_from_data(
        ID3D12Device* device,
        const wz::engine::assets::RenderProgramData& data);

    ID3D12PipelineState* create_pso_from_data(
        Device& device,
        const wz::engine::assets::RenderProgramData& data,
        ID3D12RootSignature* root_sig,
        GPUHandle vertex_shader,
        GPUHandle pixel_shader);
}
