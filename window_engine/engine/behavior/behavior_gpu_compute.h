#pragma once

// engine/behavior/behavior_gpu_compute.h

#include <engine/behavior/behavior_plugin_abi.h>

#include <scene/scene_ecs.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace wz::engine::behavior
{
    struct BehaviorGpuPortValue
    {
        std::string name;
        WzGpuPortKind kind = WZ_GPU_PORT_NONE;
        WzGpuPortDirection direction = 0u;
        uint32_t element_count = 0u;
        uint32_t stride_bytes = 0u;
        std::vector<std::byte> initial_data;
        WzGpuResourceRef resource{};
        uint32_t u32[4]{};
        float f32[4]{};
    };

    struct BehaviorGpuComputeJob
    {
        WzGpuWorkId work{};
        wz::scene::RuntimeEntityId entity =
            wz::scene::INVALID_RUNTIME_ENTITY;
        std::string kernel;
        std::vector<BehaviorGpuPortValue> ports;
        uint32_t group_count_x = 1u;
        uint32_t group_count_y = 1u;
        uint32_t group_count_z = 1u;
        WzGpuDispatchDomain dispatch_domain = WZ_GPU_DISPATCH_DOMAIN_AUTO;
        uint64_t request_tag = 0u;
    };

    struct BehaviorGpuComputeEvent
    {
        WzBehaviorEventKind kind = WZ_EVENT_NONE;
        wz::scene::RuntimeEntityId entity =
            wz::scene::INVALID_RUNTIME_ENTITY;
        WzGpuComputeEventPayload payload{};
        std::vector<BehaviorGpuPortValue> outputs;
    };

    struct BehaviorGpuComputeBuffer
    {
        std::vector<BehaviorGpuComputeJob> jobs;
        std::vector<BehaviorGpuComputeEvent> events;
        uint64_t next_work_id = 1u;

        void clear_jobs() { jobs.clear(); }
        void clear_events() { events.clear(); }
        void clear()
        {
            clear_jobs();
            clear_events();
        }

        void add_event(
            wz::scene::RuntimeEntityId entity,
            WzBehaviorEventKind kind,
            WzGpuComputeEventPayload payload,
            std::vector<BehaviorGpuPortValue> outputs = {})
        {
            if (kind != WZ_EVENT_GPU_COMPUTE_COMPLETED
                && kind != WZ_EVENT_GPU_COMPUTE_FAILED)
            {
                return;
            }
            events.push_back({
                .kind = kind,
                .entity = entity,
                .payload = payload,
                .outputs = std::move(outputs),
            });
        }

        [[nodiscard]] bool submit(
            wz::scene::RuntimeEntityId entity,
            const WzGpuComputeJobDesc& desc,
            WzGpuWorkId* out_work)
        {
            // group_count_x == 0 requests an engine-derived dispatch size
            // from mesh-resolved port element counts (see
            // wz_gpu_set_groups_from_mesh).
            if (!desc.kernel || desc.kernel[0] == '\0'
                || !desc.ports
                || desc.port_count == 0u
                || desc.port_count > 64u
                || desc.group_count_y == 0u
                || desc.group_count_z == 0u)
            {
                return false;
            }

            BehaviorGpuComputeJob job{};
            job.work.value = next_work_id++;
            job.entity = entity;
            job.kernel = desc.kernel;
            job.group_count_x = desc.group_count_x;
            job.group_count_y = desc.group_count_y;
            job.group_count_z = desc.group_count_z;
            job.dispatch_domain = desc.dispatch_domain;
            job.request_tag = desc.request_tag;
            job.ports.reserve(desc.port_count);

            for (uint32_t i = 0; i < desc.port_count; ++i) {
                const WzGpuPortValue& src = desc.ports[i];
                if (!src.name || src.name[0] == '\0'
                    || src.kind == WZ_GPU_PORT_NONE
                    || src.direction == 0u)
                {
                    return false;
                }
                if (src.kind == WZ_GPU_PORT_STRUCTURED_BUFFER) {
                    // Mesh-resolved ports may leave element_count at 0; the
                    // executor fills it from the entity's mesh.
                    const bool engine_sized =
                        src.resource.value
                            == WZ_GPU_RESOURCE_REF_MESH_VERTEX_POSITIONS
                        || src.resource.value
                            == WZ_GPU_RESOURCE_REF_MESH_INDICES
                        || src.resource.value
                            == WZ_GPU_RESOURCE_REF_MESH_SPARSE_OPERATOR
                        || src.resource.value
                            == WZ_GPU_RESOURCE_REF_MESH_DERIVED_FIELD_CHANNEL
                        || src.resource.value
                            == WZ_GPU_RESOURCE_REF_MESH_FIELD_VISUALIZATION;
                    if ((src.element_count == 0u && !engine_sized)
                        || src.stride_bytes == 0u
                        || (!src.initial_data
                            && src.initial_data_bytes != 0u))
                    {
                        return false;
                    }
                }

                BehaviorGpuPortValue port{};
                port.name = src.name;
                port.kind = src.kind;
                port.direction = src.direction;
                port.element_count = src.element_count;
                port.stride_bytes = src.stride_bytes;
                port.resource = src.resource;
                std::memcpy(port.u32, src.u32, sizeof(port.u32));
                std::memcpy(port.f32, src.f32, sizeof(port.f32));
                if (src.initial_data && src.initial_data_bytes > 0u) {
                    const auto* bytes =
                        static_cast<const std::byte*>(src.initial_data);
                    port.initial_data.assign(
                        bytes,
                        bytes + src.initial_data_bytes);
                }
                job.ports.push_back(std::move(port));
            }

            if (out_work) {
                *out_work = job.work;
            }
            jobs.push_back(std::move(job));
            return true;
        }
    };
}
