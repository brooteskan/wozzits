// gpu/dx12/dx12_terrain_surface_mesh.cpp

#include <gpu/dx12/dx12_terrain_surface_mesh.h>
#include <gpu/dx12/dx12_internal.h>

#include <engine/assets/gaussian_splat/terrain_reconstruction.h>

#include <file/filesystem.h>

#include <d3dcompiler.h>

#include <cmath>
#include <cstring>

namespace wz::gpu::dx12
{

// ─── Pimpl storage ───────────────────────────────────────────────────────

struct TerrainSurfaceMeshRenderer::Impl
{
    ID3D12RootSignature* root_sig  = nullptr;
    ID3D12PipelineState* solid_pso = nullptr;
    ID3D12PipelineState* wire_pso  = nullptr;

    ID3D12Resource* vertex_buffer = nullptr;
    ID3D12Resource* index_buffer  = nullptr;
    uint32_t vertex_count = 0;
    uint32_t index_count  = 0;

    bool is_pipeline_ready = false;
    bool is_mesh_ready     = false;

    void release()
    {
        if (vertex_buffer) { vertex_buffer->Release(); vertex_buffer = nullptr; }
        if (index_buffer)  { index_buffer->Release();  index_buffer  = nullptr; }
        if (solid_pso)     { solid_pso->Release();     solid_pso     = nullptr; }
        if (wire_pso)      { wire_pso->Release();      wire_pso      = nullptr; }
        if (root_sig)      { root_sig->Release();      root_sig      = nullptr; }
        vertex_count = index_count = 0;
        is_pipeline_ready = is_mesh_ready = false;
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
    if (bytes.error != wz::fs::FileError::None) return false;

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

TerrainSurfaceMeshRenderer::TerrainSurfaceMeshRenderer()
    : impl_(std::make_unique<Impl>()) {}

TerrainSurfaceMeshRenderer::~TerrainSurfaceMeshRenderer()
{
    if (impl_) impl_->release();
}


// ─── Init ────────────────────────────────────────────────────────────────

bool TerrainSurfaceMeshRenderer::init(Device& device)
{
    namespace dx = internal;
    auto& sr = *impl_;
    if (sr.is_pipeline_ready) return true;

    ID3D12Device* dev = dx::get_device(device);
    if (!dev) return false;

    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_blob = nullptr;

    if (!compile_shader(
            "resources/shaders/gaussian_splat/"
            "terrain_surface_recon_vs.hlsl",
            "surface_recon_vs", "vs_5_0", &vs_blob))
        return false;

    if (!compile_shader(
            "resources/shaders/gaussian_splat/"
            "terrain_surface_recon_ps.hlsl",
            "surface_recon_ps", "ps_5_0", &ps_blob))
    { vs_blob->Release(); return false; }

    // Root signature: 24 root constants at b0 (all stages).
    D3D12_ROOT_PARAMETER root_param{};
    root_param.ParameterType            =
        D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_param.ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    root_param.Constants.Num32BitValues = 24;
    root_param.Constants.ShaderRegister = 0;
    root_param.Constants.RegisterSpace  = 0;

    D3D12_ROOT_SIGNATURE_DESC rs_desc{};
    rs_desc.NumParameters = 1;
    rs_desc.pParameters   = &root_param;
    rs_desc.Flags         =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* sig_blob = nullptr;
    ID3DBlob* sig_err  = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(
        &rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig_blob, &sig_err);
    if (FAILED(hr))
    {
        if (sig_err) {
            OutputDebugStringA(static_cast<const char*>(
                sig_err->GetBufferPointer()));
            sig_err->Release();
        }
        vs_blob->Release(); ps_blob->Release();
        return false;
    }
    if (sig_err) { sig_err->Release(); sig_err = nullptr; }

    hr = dev->CreateRootSignature(
        0, sig_blob->GetBufferPointer(), sig_blob->GetBufferSize(),
        IID_PPV_ARGS(&sr.root_sig));
    sig_blob->Release();
    if (FAILED(hr))
    { vs_blob->Release(); ps_blob->Release(); return false; }

    // Input layout matching TerrainReconVertex.
    D3D12_INPUT_ELEMENT_DESC input_elems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
           0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
          12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
          24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT,       0,
          36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // Solid-fill PSO.
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = sr.root_sig;
        desc.VS = { vs_blob->GetBufferPointer(),
                    vs_blob->GetBufferSize() };
        desc.PS = { ps_blob->GetBufferPointer(),
                    ps_blob->GetBufferSize() };
        desc.InputLayout = { input_elems, _countof(input_elems) };
        desc.PrimitiveTopologyType =
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;

        desc.BlendState        = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState =
            CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable    = TRUE;
        desc.DepthStencilState.DepthWriteMask =
            D3D12_DEPTH_WRITE_MASK_ALL;
        desc.DepthStencilState.DepthFunc =
            D3D12_COMPARISON_FUNC_LESS;
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

        desc.NumRenderTargets  = 1;
        desc.RTVFormats[0]     = dx::get_backbuffer_format();
        desc.SampleMask        = UINT_MAX;
        desc.SampleDesc.Count  = 1;

        hr = dev->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&sr.solid_pso));
        if (FAILED(hr))
        {
            vs_blob->Release(); ps_blob->Release();
            sr.release();
            return false;
        }
    }

    // Wireframe PSO.
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = sr.root_sig;
        desc.VS = { vs_blob->GetBufferPointer(),
                    vs_blob->GetBufferSize() };
        desc.PS = { ps_blob->GetBufferPointer(),
                    ps_blob->GetBufferSize() };
        desc.InputLayout = { input_elems, _countof(input_elems) };
        desc.PrimitiveTopologyType =
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        desc.BlendState        = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState =
            CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable    = TRUE;
        desc.DepthStencilState.DepthWriteMask =
            D3D12_DEPTH_WRITE_MASK_ALL;
        desc.DepthStencilState.DepthFunc =
            D3D12_COMPARISON_FUNC_LESS;
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

        desc.NumRenderTargets  = 1;
        desc.RTVFormats[0]     = dx::get_backbuffer_format();
        desc.SampleMask        = UINT_MAX;
        desc.SampleDesc.Count  = 1;

        hr = dev->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&sr.wire_pso));
    }

    vs_blob->Release();
    ps_blob->Release();

    if (FAILED(hr))
    {
        sr.release();
        return false;
    }

    sr.is_pipeline_ready = true;
    return true;
}


// ─── Release / queries ───────────────────────────────────────────────────

void TerrainSurfaceMeshRenderer::release()
{
    impl_->release();
}

bool TerrainSurfaceMeshRenderer::pipeline_ready() const noexcept
{
    return impl_->is_pipeline_ready;
}

bool TerrainSurfaceMeshRenderer::mesh_ready() const noexcept
{
    return impl_->is_mesh_ready;
}


// ─── Upload ──────────────────────────────────────────────────────────────

bool TerrainSurfaceMeshRenderer::upload(
    Device& device,
    std::span<const wz::engine::assets::TerrainReconVertex> vertices,
    std::span<const uint32_t> indices)
{
    namespace dx = internal;
    auto& sr = *impl_;

    ID3D12Device* dev = dx::get_device(device);
    if (!dev) return false;

    // Release old buffers.
    if (sr.vertex_buffer)
    { sr.vertex_buffer->Release(); sr.vertex_buffer = nullptr; }
    if (sr.index_buffer)
    { sr.index_buffer->Release();  sr.index_buffer  = nullptr; }
    sr.is_mesh_ready = false;

    using Vertex = wz::engine::assets::TerrainReconVertex;

    const UINT vb_size =
        static_cast<UINT>(vertices.size() * sizeof(Vertex));
    const UINT ib_size =
        static_cast<UINT>(indices.size() * sizeof(uint32_t));

    D3D12_HEAP_PROPERTIES upload_props{};
    upload_props.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC buf_desc{};
    buf_desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf_desc.Width            = vb_size;
    buf_desc.Height           = 1;
    buf_desc.DepthOrArraySize = 1;
    buf_desc.MipLevels        = 1;
    buf_desc.SampleDesc.Count = 1;
    buf_desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = dev->CreateCommittedResource(
        &upload_props, D3D12_HEAP_FLAG_NONE,
        &buf_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&sr.vertex_buffer));
    if (FAILED(hr)) return false;

    buf_desc.Width = ib_size;
    hr = dev->CreateCommittedResource(
        &upload_props, D3D12_HEAP_FLAG_NONE,
        &buf_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&sr.index_buffer));
    if (FAILED(hr))
    {
        sr.vertex_buffer->Release();
        sr.vertex_buffer = nullptr;
        return false;
    }

    // Map and copy vertex data.
    void* vb_ptr = nullptr;
    hr = sr.vertex_buffer->Map(0, nullptr, &vb_ptr);
    if (FAILED(hr))
    {
        sr.vertex_buffer->Release(); sr.vertex_buffer = nullptr;
        sr.index_buffer->Release();  sr.index_buffer  = nullptr;
        return false;
    }
    std::memcpy(vb_ptr, vertices.data(), vb_size);
    sr.vertex_buffer->Unmap(0, nullptr);

    // Map and copy index data.
    void* ib_ptr = nullptr;
    hr = sr.index_buffer->Map(0, nullptr, &ib_ptr);
    if (FAILED(hr))
    {
        sr.vertex_buffer->Release(); sr.vertex_buffer = nullptr;
        sr.index_buffer->Release();  sr.index_buffer  = nullptr;
        return false;
    }
    std::memcpy(ib_ptr, indices.data(), ib_size);
    sr.index_buffer->Unmap(0, nullptr);

    sr.vertex_count  = static_cast<uint32_t>(vertices.size());
    sr.index_count   = static_cast<uint32_t>(indices.size());
    sr.is_mesh_ready = true;

    return true;
}


// ─── Draw ────────────────────────────────────────────────────────────────

void TerrainSurfaceMeshRenderer::draw(
    Device& device,
    const float view_projection[16],
    const TerrainSurfaceMeshSettings& settings)
{
    namespace dx = internal;
    auto& sr = *impl_;

    if (!sr.is_pipeline_ready || !sr.is_mesh_ready) return;
    if (sr.index_count == 0) return;

    ID3D12GraphicsCommandList* cmd = dx::get_command_list(device);
    if (!cmd) return;

    const UINT vp_w = dx::get_width(device);
    const UINT vp_h = dx::get_height(device);

    // Bind backbuffer + DSV.
    D3D12_CPU_DESCRIPTOR_HANDLE bb_rtv = dx::get_current_rtv(device);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv    = dx::get_dsv(device);
    cmd->OMSetRenderTargets(1, &bb_rtv, FALSE, &dsv);
    // See DX12Device::depth_target_bound -- every bind in this layer sets it.
    dx::set_depth_target_bound(device, true);

    D3D12_VIEWPORT viewport{};
    viewport.Width    = static_cast<float>(vp_w);
    viewport.Height   = static_cast<float>(vp_h);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    cmd->RSSetViewports(1, &viewport);

    D3D12_RECT scissor{ 0, 0,
        static_cast<LONG>(vp_w), static_cast<LONG>(vp_h) };
    cmd->RSSetScissorRects(1, &scissor);

    cmd->SetGraphicsRootSignature(sr.root_sig);
    cmd->SetPipelineState(
        settings.wireframe ? sr.wire_pso : sr.solid_pso);
    cmd->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Root constants: view_proj[16] + params0[4] + params1[4].
    float constants[24] = {};
    for (int i = 0; i < 16; ++i)
        constants[i] = view_projection[i];

    // params0: light_dir.xyz, debug_mode.
    {
        float lx = settings.light_dir[0];
        float ly = settings.light_dir[1];
        float lz = settings.light_dir[2];
        const float len = std::sqrt(lx*lx + ly*ly + lz*lz);
        if (len > 1e-6f)
        { lx /= len; ly /= len; lz /= len; }
        constants[16] = lx;
        constants[17] = ly;
        constants[18] = lz;
    }
    constants[19] = static_cast<float>(settings.debug_mode);

    // params1: ambient, diffuse_strength, height_min, height_max.
    constants[20] = settings.ambient;
    constants[21] = settings.diffuse_strength;
    constants[22] = settings.height_min;
    constants[23] = settings.height_max;

    cmd->SetGraphicsRoot32BitConstants(0, 24, constants, 0);

    // Vertex/index buffer views.
    using Vertex = wz::engine::assets::TerrainReconVertex;

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation =
        sr.vertex_buffer->GetGPUVirtualAddress();
    vbv.SizeInBytes    = sr.vertex_count * sizeof(Vertex);
    vbv.StrideInBytes  = sizeof(Vertex);
    cmd->IASetVertexBuffers(0, 1, &vbv);

    D3D12_INDEX_BUFFER_VIEW ibv{};
    ibv.BufferLocation =
        sr.index_buffer->GetGPUVirtualAddress();
    ibv.SizeInBytes    = sr.index_count * sizeof(uint32_t);
    ibv.Format         = DXGI_FORMAT_R32_UINT;
    cmd->IASetIndexBuffer(&ibv);

    cmd->DrawIndexedInstanced(sr.index_count, 1, 0, 0, 0);
}

}  // namespace wz::gpu::dx12
