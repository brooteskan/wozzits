// src/gpu/dx12/dx12_gaussian_splat.cpp

#include <gpu/dx12/dx12_internal.h>

#include "dx12_device_internal.h"

#include <engine/assets/gaussian_splat/gaussian_splat_cloud.h>
#include <engine/assets/gaussian_splat/gaussian_splat_color_lod.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>

namespace wz::gpu::dx12::internal
{
    namespace
    {
        bool create_upload_buffer(
            ID3D12Device* device,
            const void* src,
            uint64_t byte_count,
            ID3D12Resource** out_resource)
        {
            assert(out_resource);
            *out_resource = nullptr;

            if (!device || !src || byte_count == 0)
                return false;

            const D3D12_HEAP_PROPERTIES heap_props =
                CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

            const D3D12_RESOURCE_DESC resource_desc =
                CD3DX12_RESOURCE_DESC::Buffer(byte_count);

            ID3D12Resource* resource = nullptr;

            const HRESULT hr = device->CreateCommittedResource(
                &heap_props,
                D3D12_HEAP_FLAG_NONE,
                &resource_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&resource));

            if (FAILED(hr))
                return false;

            void* mapped = nullptr;

            const D3D12_RANGE read_range{
                0,
                0,
            };

            const HRESULT map_hr = resource->Map(
                0,
                &read_range,
                &mapped);

            if (FAILED(map_hr) || !mapped) {
                resource->Release();
                return false;
            }

            std::memcpy(mapped, src, static_cast<size_t>(byte_count));

            resource->Unmap(
                0,
                nullptr);

            *out_resource = resource;
            return true;
        }

        // Pack a linear-RGB triplet + a [0,1] confidence into a single
        // RGBA8 word.  R is in the low byte, A is in the high byte, matching
        // the HLSL unpack helper `unpack_rgba8`.
        inline uint32_t pack_rgba8(
            float r, float g, float b, float a) noexcept
        {
            auto byte = [](float v) -> uint32_t
            {
                if (v <= 0.0f) return 0u;
                if (v >= 1.0f) return 255u;
                return static_cast<uint32_t>(v * 255.0f + 0.5f);
            };
            return  byte(r)
                | (byte(g) << 8)
                | (byte(b) << 16)
                | (byte(a) << 24);
        }

        DX12GaussianSplatVertex make_gpu_splat_vertex(
            const wz::engine::assets::GaussianSplat& splat,
            const wz::engine::assets::GaussianSplatColorLODData* lod,
            uint32_t splat_index)
        {
            // 3DGS decoding: PLY stores raw/encoded values that must be
            // transformed before display.
            constexpr float SH_C0 = 0.28209479177387814f;

            DX12GaussianSplatVertex out{};

            out.position[0] = splat.position[0];
            out.position[1] = splat.position[1];
            out.position[2] = splat.position[2];

            // Opacity: stored as logit, decode via sigmoid.
            out.opacity = 1.0f / (1.0f + std::exp(-splat.opacity));

            // Scale: stored as log, decode each axis to world-space size.
            out.scale[0] = std::exp(splat.scale[0]);
            out.scale[1] = std::exp(splat.scale[1]);
            out.scale[2] = std::exp(splat.scale[2]);

            // Rotation: PLY importer stores rot_0=w, rot_1=x, rot_2=y, rot_3=z.
            // Send to shader as x,y,z,w.
            float qw = splat.rotation[0];
            float qx = splat.rotation[1];
            float qy = splat.rotation[2];
            float qz = splat.rotation[3];

            const float q_len_sq = qx * qx + qy * qy + qz * qz + qw * qw;

            if (q_len_sq > 0.0f)
            {
                const float inv_len = 1.0f / std::sqrt(q_len_sq);
                qx *= inv_len;
                qy *= inv_len;
                qz *= inv_len;
                qw *= inv_len;
            }
            else
            {
                qx = 0.0f;
                qy = 0.0f;
                qz = 0.0f;
                qw = 1.0f;
            }

            out.rotation[0] = qx;
            out.rotation[1] = qy;
            out.rotation[2] = qz;
            out.rotation[3] = qw;

            // Color: SH DC coefficient → linear display/debug RGB.
            // Clamp to [0, 1] to guard against out-of-range SH values.
            //out.color[0] = std::clamp(0.5f + SH_C0 * splat.color_dc[0], 0.0f, 1.0f);
            //out.color[1] = std::clamp(0.5f + SH_C0 * splat.color_dc[1], 0.0f, 1.0f);
            //out.color[2] = std::clamp(0.5f + SH_C0 * splat.color_dc[2], 0.0f, 1.0f);
            out.color[0] = 0.5f + SH_C0 * splat.color_dc[0];
            out.color[1] = 0.5f + SH_C0 * splat.color_dc[1];
            out.color[2] = 0.5f + SH_C0 * splat.color_dc[2];

            // LOD packed slot.
            //   With LOD data: pack neighborhood RGB + confidence.
            //   Without LOD:   pack base color + confidence = 0 so the
            //                  shader sees a safe fallback (no LOD blend).
            if (lod
                && lod->valid()
                && splat_index < lod->splat_count())
            {
                const float lr = lod->neighborhood_color[3 * splat_index + 0];
                const float lg = lod->neighborhood_color[3 * splat_index + 1];
                const float lb = lod->neighborhood_color[3 * splat_index + 2];
                const float lc = lod->lod_confidence[splat_index];
                out.lod_color_confidence_rgba8 = pack_rgba8(lr, lg, lb, lc);
            }
            else
            {
                out.lod_color_confidence_rgba8 = pack_rgba8(
                    out.color[0], out.color[1], out.color[2], 0.0f);
            }

            return out;
        }
    }

    DX12GaussianSplatCloudTable::DX12GaussianSplatCloudTable()
    {
        slots_.push_back({});
    }

    GPUHandle DX12GaussianSplatCloudTable::add(
        DX12GaussianSplatCloudResource cloud)
    {
        if (!cloud.valid_for_vertex_instanced())
            return {};

        const uint32_t id = static_cast<uint32_t>(slots_.size());

        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.cloud = cloud;

        slots_.push_back(slot);

        return GPUHandle{
            .id = id,
            .epoch = slot.epoch,
            .type = GPUResourceType::GaussianSplatCloud,
        };
    }

    const DX12GaussianSplatCloudResource* DX12GaussianSplatCloudTable::get(
        GPUHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.type != GPUResourceType::GaussianSplatCloud)
            return nullptr;

        if (handle.id >= slots_.size())
            return nullptr;

        const Slot& slot = slots_[handle.id];

        if (!slot.occupied)
            return nullptr;

        if (slot.epoch != handle.epoch)
            return nullptr;

        return &slot.cloud;
    }

    namespace
    {
        void release_splat_cloud_resource(DX12GaussianSplatCloudResource& cloud)
        {
            if (cloud.vertex_buffer) {
                cloud.vertex_buffer->Release();
                cloud.vertex_buffer = nullptr;
            }
            if (cloud.sorted_index_buffer) {
                cloud.sorted_index_buffer->Unmap(0, nullptr);
                cloud.sorted_index_buffer->Release();
                cloud.sorted_index_buffer = nullptr;
                cloud.sorted_index_map    = nullptr;
            }
            cloud.srv_table = {};
            cloud.vertex_view = {};
            cloud.splat_count = 0;
        }

        void release_splat_cloud_resource(
            DX12GaussianSplatCloudResource& cloud,
            DX12DescriptorAllocator& allocator)
        {
            allocator.release(cloud.srv_table);
            release_splat_cloud_resource(cloud);
        }
    }

    bool DX12GaussianSplatCloudTable::release(
        GPUHandle handle,
        DX12DescriptorAllocator& allocator)
    {
        if (!handle.valid())
            return false;

        if (handle.type != GPUResourceType::GaussianSplatCloud)
            return false;

        if (handle.id == 0 || handle.id >= slots_.size())
            return false;

        Slot& slot = slots_[handle.id];

        if (!slot.occupied || slot.epoch != handle.epoch)
            return false;

        release_splat_cloud_resource(slot.cloud, allocator);
        slot.occupied = false;
        ++slot.epoch;
        if (slot.epoch == 0)
            slot.epoch = 1;
        return true;
    }

    void DX12GaussianSplatCloudTable::destroy(
        DX12DescriptorAllocator& allocator)
    {
        for (Slot& slot : slots_) {
            if (!slot.occupied)
                continue;

            release_splat_cloud_resource(slot.cloud, allocator);
            slot.occupied = false;
            ++slot.epoch;
        }

        slots_.clear();
        slots_.push_back({});
    }

    GPUHandle upload_gaussian_splat_cloud_dx12(
        Device& device,
        const wz::engine::assets::GaussianSplatCloudData& cloud,
        const wz::engine::assets::GaussianSplatColorLODData* lod)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        assert(impl);

        if (!cloud.valid())
            return {};

        if (!impl->device)
            return {};

        if (cloud.splats.empty())
            return {};

        // Validate optional LOD: it must be either absent, or sized to match
        // the source cloud.  Mismatched LOD data is treated as absent so the
        // upload still succeeds with the safe fallback packing.
        const wz::engine::assets::GaussianSplatColorLODData* effective_lod = lod;
        if (effective_lod
            && effective_lod->splat_count() != cloud.splats.size())
        {
            effective_lod = nullptr;
        }

        std::vector<DX12GaussianSplatVertex> vertices;
        vertices.reserve(cloud.splats.size());

        for (size_t i = 0; i < cloud.splats.size(); ++i) {
            vertices.push_back(make_gpu_splat_vertex(
                cloud.splats[i],
                effective_lod,
                static_cast<uint32_t>(i)));
        }

        const uint64_t vertex_bytes =
            static_cast<uint64_t>(vertices.size()) *
            static_cast<uint64_t>(sizeof(DX12GaussianSplatVertex));

        DX12GaussianSplatCloudResource resource{};
        resource.splat_count = static_cast<uint32_t>(vertices.size());

        if (!create_upload_buffer(
            impl->device,
            vertices.data(),
            vertex_bytes,
            &resource.vertex_buffer))
        {
            return {};
        }

        resource.vertex_view.BufferLocation =
            resource.vertex_buffer->GetGPUVirtualAddress();
        resource.vertex_view.SizeInBytes =
            static_cast<UINT>(vertex_bytes);
        resource.vertex_view.StrideInBytes =
            static_cast<UINT>(sizeof(DX12GaussianSplatVertex));

        // ── Identity sorted-index buffer (t1) ────────────────────────────────
        //
        // Allocate a persistently-mapped upload-heap uint32_t[N] buffer and
        // initialise it to the identity permutation [0, 1, 2, ..., N-1].
        // The sort follow-on replaces the contents before each draw; for now
        // the identity order matches the vertex-instanced path exactly.
        {
            const uint64_t index_bytes =
                static_cast<uint64_t>(resource.splat_count) * sizeof(uint32_t);

            const D3D12_HEAP_PROPERTIES heap_props =
                CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            const D3D12_RESOURCE_DESC index_desc =
                CD3DX12_RESOURCE_DESC::Buffer(index_bytes);

            HRESULT hr = impl->device->CreateCommittedResource(
                &heap_props,
                D3D12_HEAP_FLAG_NONE,
                &index_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&resource.sorted_index_buffer));

            if (SUCCEEDED(hr) && resource.sorted_index_buffer)
            {
                const D3D12_RANGE read_range{ 0, 0 };
                void* mapped = nullptr;
                hr = resource.sorted_index_buffer->Map(0, &read_range, &mapped);

                if (SUCCEEDED(hr) && mapped)
                {
                    resource.sorted_index_map =
                        static_cast<uint32_t*>(mapped);

                    for (uint32_t i = 0; i < resource.splat_count; ++i)
                        resource.sorted_index_map[i] = i;
                    // Leave persistently mapped — upload-heap resources
                    // remain valid for CPU writes at any time.
                }
                else
                {
                    resource.sorted_index_buffer->Release();
                    resource.sorted_index_buffer = nullptr;
                }
            }
        }

        // ── Two-slot SRV descriptor table (t0 = splats, t1 = indices) ────────
        //
        // Both SRVs share one root parameter; a single SetGraphicsRootDescriptorTable
        // call covering slot 0 exposes both registers to the shader.
        if (resource.sorted_index_buffer)
        {
            resource.srv_table = impl->srv_cbv_uav_allocator.allocate(2);
            if (resource.srv_table.valid())
            {
                // slot 0 → StructuredBuffer<Splat>  t0
                impl->srv_cbv_uav_allocator.create_structured_buffer_srv(
                    resource.srv_table, 0,
                    resource.vertex_buffer,
                    resource.splat_count,
                    sizeof(DX12GaussianSplatVertex));

                // slot 1 → StructuredBuffer<uint>  t1
                impl->srv_cbv_uav_allocator.create_structured_buffer_srv(
                    resource.srv_table, 1,
                    resource.sorted_index_buffer,
                    resource.splat_count,
                    sizeof(uint32_t));
            }
        }

        return impl->gaussian_splat_clouds.add(resource);
    }

    const DX12GaussianSplatCloudResource* get_gaussian_splat_cloud(
        Device& device,
        GPUHandle handle)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        assert(impl);

        return impl->gaussian_splat_clouds.get(handle);
    }

    bool release_gaussian_splat_cloud_dx12(
        Device& device,
        GPUHandle handle)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        if (!impl)
            return false;

        return impl->gaussian_splat_clouds.release(
            handle,
            impl->srv_cbv_uav_allocator);
    }

    const wz::gpu::SplatColorLODSettings& get_lod_settings(Device& device)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        assert(impl);

        return impl->splat_color_lod_settings;
    }

    const wz::gpu::SplatCoverageSettings& get_coverage_settings(Device& device)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        assert(impl);

        return impl->splat_coverage_settings;
    }

    void update_sorted_indices(
        const DX12GaussianSplatCloudResource& cloud,
        std::span<const uint32_t>             sorted_indices)
    {
        // Empty span = "use existing t1 buffer" (identity or previously sorted).
        if (sorted_indices.empty())
            return;

        if (!cloud.sorted_index_map)
        {
            OutputDebugStringA(
                "update_sorted_indices: sorted_index_map is null — cloud not fully initialized\n");
            return;
        }

        if (sorted_indices.size() != cloud.splat_count)
        {
            // Size mismatch: caller's span doesn't match the cloud's splat count.
            // Preserve the existing buffer rather than writing partial/garbage data.
            char buf[256];
            snprintf(buf, sizeof(buf),
                "update_sorted_indices: size mismatch — "
                "sorted_indices.size()=%zu, cloud.splat_count=%u (skipped)\n",
                sorted_indices.size(), cloud.splat_count);
            OutputDebugStringA(buf);
            return;
        }

        // Write indices into the persistently-mapped upload buffer.
        // The GPU reads this buffer as t1 SortedIndices at DrawInstanced time.
        std::memcpy(cloud.sorted_index_map,
                    sorted_indices.data(),
                    sorted_indices.size() * sizeof(uint32_t));
    }
}
