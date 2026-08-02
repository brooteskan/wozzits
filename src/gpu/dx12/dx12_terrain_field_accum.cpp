// gpu/dx12/dx12_terrain_field_accum.cpp

#include <gpu/dx12/dx12_terrain_field_accum.h>
#include <gpu/dx12/dx12_internal.h>

#include <file/filesystem.h>

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace wz::gpu::dx12
{

// ─── Pimpl storage ───────────────────────────────────────────────────────

struct TerrainFieldAccumRenderer::Impl
{
    ID3D12Resource*         rt_color         = nullptr;
    ID3D12Resource*         rt_normal        = nullptr;
    ID3D12DescriptorHeap*   rtv_heap         = nullptr;
    ID3D12DescriptorHeap*   resolve_srv_heap = nullptr;

    ID3D12RootSignature*    accum_root_sig   = nullptr;
    ID3D12PipelineState*    accum_pso        = nullptr;

    ID3D12RootSignature*    resolve_root_sig = nullptr;
    ID3D12PipelineState*    resolve_pso      = nullptr;

    uint32_t width  = 0;
    uint32_t height = 0;
    bool     is_ready = false;

    void release()
    {
        if (rt_color)         { rt_color->Release();         rt_color = nullptr; }
        if (rt_normal)        { rt_normal->Release();        rt_normal = nullptr; }
        if (rtv_heap)         { rtv_heap->Release();         rtv_heap = nullptr; }
        if (resolve_srv_heap) { resolve_srv_heap->Release(); resolve_srv_heap = nullptr; }
        if (accum_pso)        { accum_pso->Release();        accum_pso = nullptr; }
        if (accum_root_sig)   { accum_root_sig->Release();   accum_root_sig = nullptr; }
        if (resolve_pso)      { resolve_pso->Release();      resolve_pso = nullptr; }
        if (resolve_root_sig) { resolve_root_sig->Release(); resolve_root_sig = nullptr; }
        width = height = 0;
        is_ready = false;
    }
};


// ─── Shader compile helper ───────────────────────────────────────────────

static bool compile_shader(
    const std::string& path,
    const char* debug_name,
    const char* target,
    ID3DBlob** out_blob)
{
    auto bytes = wz::fs::read_file(path);
    if (bytes.error != wz::fs::FileError::None)
        return false;

    ID3DBlob* err = nullptr;
    HRESULT r = D3DCompile(
        bytes.value.data(), bytes.value.size(),
        debug_name, nullptr, nullptr,
        "main", target, 0, 0, out_blob, &err);
    if (FAILED(r))
    {
        if (err) {
            OutputDebugStringA(static_cast<const char*>(
                err->GetBufferPointer()));
            err->Release();
        }
        return false;
    }
    if (err) err->Release();
    return true;
}


// ─── Constructor / destructor ────────────────────────────────────────────

TerrainFieldAccumRenderer::TerrainFieldAccumRenderer()
    : impl_(std::make_unique<Impl>()) {}

TerrainFieldAccumRenderer::~TerrainFieldAccumRenderer()
{
    if (impl_) impl_->release();
}


// ─── Init ────────────────────────────────────────────────────────────────

bool TerrainFieldAccumRenderer::init(Device& device, uint32_t width, uint32_t height)
{
    namespace dx = internal;
    auto& fa = *impl_;
    if (fa.is_ready) return true;

    ID3D12Device* dev = dx::get_device(device);
    if (!dev) return false;
    if (width == 0 || height == 0) return false;

    HRESULT hr;

    // ── Create accumulation RTs (RGBA16F) ──
    D3D12_RESOURCE_DESC rt_desc{};
    rt_desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rt_desc.Width              = width;
    rt_desc.Height             = height;
    rt_desc.DepthOrArraySize   = 1;
    rt_desc.MipLevels          = 1;
    rt_desc.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rt_desc.SampleDesc.Count   = 1;
    rt_desc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear_val{};
    clear_val.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    clear_val.Color[0] = clear_val.Color[1] =
        clear_val.Color[2] = clear_val.Color[3] = 0.0f;

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    hr = dev->CreateCommittedResource(
        &heap_props, D3D12_HEAP_FLAG_NONE,
        &rt_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clear_val, IID_PPV_ARGS(&fa.rt_color));
    if (FAILED(hr)) { fa.release(); return false; }

    hr = dev->CreateCommittedResource(
        &heap_props, D3D12_HEAP_FLAG_NONE,
        &rt_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clear_val, IID_PPV_ARGS(&fa.rt_normal));
    if (FAILED(hr)) { fa.release(); return false; }

    // ── RTV heap (2 descriptors) ──
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
    rtv_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.NumDescriptors = 2;
    rtv_heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = dev->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&fa.rtv_heap));
    if (FAILED(hr)) { fa.release(); return false; }

    const UINT rtv_stride = dev->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv0 =
        fa.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv1 = rtv0;
    rtv1.ptr += rtv_stride;

    dev->CreateRenderTargetView(fa.rt_color,  nullptr, rtv0);
    dev->CreateRenderTargetView(fa.rt_normal, nullptr, rtv1);

    // ── SRV heap for resolve (shader-visible, 2 descriptors) ──
    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc{};
    srv_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap_desc.NumDescriptors = 2;
    srv_heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = dev->CreateDescriptorHeap(&srv_heap_desc,
        IID_PPV_ARGS(&fa.resolve_srv_heap));
    if (FAILED(hr)) { fa.release(); return false; }

    const UINT srv_stride = dev->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srv_desc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels      = 1;
    srv_desc.Texture2D.MostDetailedMip = 0;

    D3D12_CPU_DESCRIPTOR_HANDLE srv0 =
        fa.resolve_srv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE srv1 = srv0;
    srv1.ptr += srv_stride;

    dev->CreateShaderResourceView(fa.rt_color,  &srv_desc, srv0);
    dev->CreateShaderResourceView(fa.rt_normal, &srv_desc, srv1);

    // ── Compile shaders ──
    ID3DBlob* accum_vs_blob = nullptr;
    ID3DBlob* accum_ps_blob = nullptr;

    if (!compile_shader(
            "resources/shaders/gaussian_splat/"
            "gaussian_splat_terrain_coverage_vs.hlsl",
            "field_accum_vs", "vs_5_0", &accum_vs_blob))
    { fa.release(); return false; }

    if (!compile_shader(
            "resources/shaders/gaussian_splat/"
            "gaussian_splat_terrain_field_accum_ps.hlsl",
            "field_accum_ps", "ps_5_0", &accum_ps_blob))
    { accum_vs_blob->Release(); fa.release(); return false; }

    ID3DBlob* resolve_vs_blob = nullptr;
    ID3DBlob* resolve_ps_blob = nullptr;

    if (!compile_shader(
            "resources/shaders/gaussian_splat/"
            "gaussian_splat_terrain_field_resolve_vs.hlsl",
            "field_resolve_vs", "vs_5_0", &resolve_vs_blob))
    {
        accum_vs_blob->Release();
        accum_ps_blob->Release();
        fa.release();
        return false;
    }

    if (!compile_shader(
            "resources/shaders/gaussian_splat/"
            "gaussian_splat_terrain_field_resolve_ps.hlsl",
            "field_resolve_ps", "ps_5_0", &resolve_ps_blob))
    {
        resolve_vs_blob->Release();
        accum_vs_blob->Release();
        accum_ps_blob->Release();
        fa.release();
        return false;
    }

    // ── Create accumulation root signature ──
    // Root param 0: 60 root constants (same layout as coverage shader)
    // Root param 1: descriptor table with SRVs
    {
        D3D12_ROOT_PARAMETER params[2]{};

        params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        params[0].Constants.Num32BitValues = 60;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace  = 0;

        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors     = 4;  // t0..t3 (splat buffers)
        range.BaseShaderRegister = 0;
        range.RegisterSpace      = 0;
        range.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &range;

        D3D12_ROOT_SIGNATURE_DESC rs_desc{};
        rs_desc.NumParameters = 2;
        rs_desc.pParameters   = params;
        rs_desc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob* sig_blob = nullptr;
        ID3DBlob* sig_err  = nullptr;
        hr = D3D12SerializeRootSignature(
            &rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig_blob, &sig_err);
        if (FAILED(hr))
        {
            if (sig_err) {
                OutputDebugStringA(static_cast<const char*>(
                    sig_err->GetBufferPointer()));
                sig_err->Release();
            }
            accum_vs_blob->Release();
            accum_ps_blob->Release();
            resolve_vs_blob->Release();
            resolve_ps_blob->Release();
            fa.release();
            return false;
        }
        if (sig_err) sig_err->Release();

        hr = dev->CreateRootSignature(
            0, sig_blob->GetBufferPointer(), sig_blob->GetBufferSize(),
            IID_PPV_ARGS(&fa.accum_root_sig));
        sig_blob->Release();
        if (FAILED(hr))
        {
            accum_vs_blob->Release();
            accum_ps_blob->Release();
            resolve_vs_blob->Release();
            resolve_ps_blob->Release();
            fa.release();
            return false;
        }
    }

    // ── Create accumulation PSO (multi-RT additive, depth test no write) ──
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = fa.accum_root_sig;
        desc.VS = { accum_vs_blob->GetBufferPointer(),
                    accum_vs_blob->GetBufferSize() };
        desc.PS = { accum_ps_blob->GetBufferPointer(),
                    accum_ps_blob->GetBufferSize() };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        // Additive blend on both render targets.
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        for (int i = 0; i < 2; ++i)
        {
            auto& rt = desc.BlendState.RenderTarget[i];
            rt.BlendEnable           = TRUE;
            rt.LogicOpEnable         = FALSE;
            rt.SrcBlend              = D3D12_BLEND_ONE;
            rt.DestBlend             = D3D12_BLEND_ONE;
            rt.BlendOp               = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha         = D3D12_BLEND_ONE;
            rt.DestBlendAlpha        = D3D12_BLEND_ONE;
            rt.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
            rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }

        // Depth test against coverage prepass, no write.
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable    = TRUE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

        desc.NumRenderTargets = 2;
        desc.RTVFormats[0]    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.RTVFormats[1]    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleMask       = UINT_MAX;
        desc.SampleDesc.Count = 1;

        hr = dev->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&fa.accum_pso));
        if (FAILED(hr))
        {
            char buf[128];
            sprintf_s(buf, "field accum PSO creation failed: 0x%08X\n",
                static_cast<unsigned>(hr));
            OutputDebugStringA(buf);
            accum_vs_blob->Release();
            accum_ps_blob->Release();
            resolve_vs_blob->Release();
            resolve_ps_blob->Release();
            fa.release();
            return false;
        }
    }

    accum_vs_blob->Release();
    accum_ps_blob->Release();

    // ── Create resolve root signature ──
    // Root param 0: 8 root constants (resolve_params0[4] + resolve_params1[4])
    // Root param 1: descriptor table with 2 SRV (t0, t1)
    {
        D3D12_ROOT_PARAMETER params[2]{};

        params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
        params[0].Constants.Num32BitValues = 8;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace  = 0;

        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors     = 2;
        range.BaseShaderRegister = 0;
        range.RegisterSpace      = 0;
        range.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &range;

        D3D12_ROOT_SIGNATURE_DESC rs_desc{};
        rs_desc.NumParameters = 2;
        rs_desc.pParameters   = params;
        rs_desc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob* sig_blob = nullptr;
        ID3DBlob* sig_err  = nullptr;
        hr = D3D12SerializeRootSignature(
            &rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig_blob, &sig_err);
        if (FAILED(hr))
        {
            if (sig_err) {
                OutputDebugStringA(static_cast<const char*>(
                    sig_err->GetBufferPointer()));
                sig_err->Release();
            }
            resolve_vs_blob->Release();
            resolve_ps_blob->Release();
            fa.release();
            return false;
        }
        if (sig_err) sig_err->Release();

        hr = dev->CreateRootSignature(
            0, sig_blob->GetBufferPointer(), sig_blob->GetBufferSize(),
            IID_PPV_ARGS(&fa.resolve_root_sig));
        sig_blob->Release();
        if (FAILED(hr))
        {
            resolve_vs_blob->Release();
            resolve_ps_blob->Release();
            fa.release();
            return false;
        }
    }

    // ── Create resolve PSO ──
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature        = fa.resolve_root_sig;
        desc.VS = { resolve_vs_blob->GetBufferPointer(),
                    resolve_vs_blob->GetBufferSize() };
        desc.PS = { resolve_ps_blob->GetBufferPointer(),
                    resolve_ps_blob->GetBufferSize() };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        desc.BlendState        = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DSVFormat         = DXGI_FORMAT_UNKNOWN;

        desc.NumRenderTargets  = 1;
        desc.RTVFormats[0]     = dx::get_backbuffer_format();
        desc.SampleMask        = UINT_MAX;
        desc.SampleDesc.Count  = 1;

        hr = dev->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&fa.resolve_pso));
    }

    resolve_vs_blob->Release();
    resolve_ps_blob->Release();

    if (FAILED(hr))
    {
        fa.release();
        return false;
    }

    fa.width    = width;
    fa.height   = height;
    fa.is_ready = true;
    return true;
}


// ─── Release ─────────────────────────────────────────────────────────────

void TerrainFieldAccumRenderer::release()
{
    impl_->release();
}

bool TerrainFieldAccumRenderer::ready() const noexcept
{
    return impl_->is_ready;
}


// ─── Render ──────────────────────────────────────────────────────────────

void TerrainFieldAccumRenderer::render(
    Device& device,
    std::span<const FieldAccumSplatDraw> draws,
    const float view_projection[16],
    const SplatCoverageSettings& coverage,
    const TerrainFieldAccumSettings& settings)
{
    namespace dx = internal;
    auto& fa = *impl_;
    if (!fa.is_ready) return;

    ID3D12GraphicsCommandList* cmd = dx::get_command_list(device);
    if (!cmd) return;

    ID3D12Device* dev = dx::get_device(device);
    if (!dev) return;

    const UINT vp_w = fa.width;
    const UINT vp_h = fa.height;

    // ── Transition accumulation RTs to RENDER_TARGET ──
    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource   = fa.rt_color;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1] = barriers[0];
    barriers[1].Transition.pResource   = fa.rt_normal;
    cmd->ResourceBarrier(2, barriers);

    // ── Clear accum RTs ──
    const UINT rtv_stride = dev->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv0 =
        fa.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv1 = rtv0;
    rtv1.ptr += rtv_stride;

    const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    cmd->ClearRenderTargetView(rtv0, clear_color, 0, nullptr);
    cmd->ClearRenderTargetView(rtv1, clear_color, 0, nullptr);

    // ── Bind accum RTs + existing DSV ──
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[2] = { rtv0, rtv1 };
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dx::get_dsv(device);
    cmd->OMSetRenderTargets(2, rtv_handles, FALSE, &dsv);
    // See DX12Device::depth_target_bound -- every bind in this layer sets it.
    dx::set_depth_target_bound(device, true);

    // Viewport + scissor
    D3D12_VIEWPORT viewport{};
    viewport.Width    = static_cast<float>(vp_w);
    viewport.Height   = static_cast<float>(vp_h);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    cmd->RSSetViewports(1, &viewport);

    D3D12_RECT scissor{ 0, 0,
        static_cast<LONG>(vp_w), static_cast<LONG>(vp_h) };
    cmd->RSSetScissorRects(1, &scissor);

    // ── Accumulation draw ──
    auto* srv_heap = dx::get_srv_cbv_uav_heap(device);
    if (!srv_heap) return;

    cmd->SetDescriptorHeaps(1, &srv_heap);
    cmd->SetGraphicsRootSignature(fa.accum_root_sig);
    cmd->SetPipelineState(fa.accum_pso);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    for (const auto& draw : draws)
    {
        const auto* cloud = dx::get_gaussian_splat_cloud(
            device, draw.splat_cloud);
        if (!cloud || !cloud->valid_for_splat_pull()) continue;

        // Build constants — same layout as coverage (60 dwords).
        float constants[60] = {};
        for (int i = 0; i < 16; ++i) constants[i]      = draw.world[i];
        for (int i = 0; i < 16; ++i) constants[16 + i] = view_projection[i];
        constants[32] = static_cast<float>(vp_w);
        constants[33] = static_cast<float>(vp_h);
        constants[34] = 0.01f;
        constants[35] = 0.0f;

        // Coverage kernel params [48..59]
        constants[48] = 0.0f;   // mode — unused by accum PS
        constants[49] = 0.0f;   // threshold — unused
        constants[50] = 0.0f;   // opacity_scale — unused
        constants[51] = static_cast<float>(
            static_cast<uint32_t>(coverage.kernel_mode));

        constants[52] = coverage.radius_scale;
        constants[53] = coverage.inner_radius;
        constants[54] = coverage.outer_radius;
        constants[55] = coverage.gaussian_falloff;

        constants[56] = 0.0f;
        constants[57] = 0.0f;
        constants[58] = settings.weight_scale;
        constants[59] = 0.0f;

        const uint32_t instance_count =
            draw.instance_count > 0
                ? draw.instance_count
                : cloud->splat_count;

        cmd->SetGraphicsRoot32BitConstants(0, 60, constants, 0);
        cmd->SetGraphicsRootDescriptorTable(
            1, cloud->srv_table.gpu_at(0));

        cmd->DrawInstanced(4, instance_count, 0, 0);
    }

    // ── Transition accum RTs to PIXEL_SHADER_RESOURCE ──
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmd->ResourceBarrier(2, barriers);

    // ── Re-bind backbuffer as RT (no depth for resolve) ──
    D3D12_CPU_DESCRIPTOR_HANDLE bb_rtv = dx::get_current_rtv(device);
    cmd->OMSetRenderTargets(1, &bb_rtv, FALSE, nullptr);
    dx::set_depth_target_bound(device, false);
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);

    // ── Resolve pass (fullscreen triangle) ──
    auto* resolve_heap = fa.resolve_srv_heap;
    cmd->SetDescriptorHeaps(1, &resolve_heap);
    cmd->SetGraphicsRootSignature(fa.resolve_root_sig);
    cmd->SetPipelineState(fa.resolve_pso);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Push resolve constants.
    float resolve_consts[8]{};
    resolve_consts[0] = settings.ambient;
    resolve_consts[1] = settings.diffuse_strength;
    // Normalize light direction on CPU.
    {
        float lx = settings.light_dir[0];
        float ly = settings.light_dir[1];
        float lz = settings.light_dir[2];
        float len = std::sqrt(lx*lx + ly*ly + lz*lz);
        if (len > 1e-6f) { lx /= len; ly /= len; lz /= len; }
        resolve_consts[4] = lx;
        resolve_consts[5] = ly;
        resolve_consts[6] = lz;
    }
    resolve_consts[7] = static_cast<float>(settings.debug_mode);

    cmd->SetGraphicsRoot32BitConstants(0, 8, resolve_consts, 0);
    cmd->SetGraphicsRootDescriptorTable(
        1, fa.resolve_srv_heap->GetGPUDescriptorHandleForHeapStart());

    cmd->DrawInstanced(3, 1, 0, 0);

    // Re-bind the main SRV heap so subsequent draws work.
    cmd->SetDescriptorHeaps(1, &srv_heap);
}

}  // namespace wz::gpu::dx12
