#include <engine/behavior/behavior_gpu_compute_executor.h>

#include <algorithm>
#include <cstring>

namespace wz::engine::behavior
{
    namespace
    {
        const BehaviorGpuKernelBinding* find_kernel(
            std::span<const BehaviorGpuKernelBinding> kernels,
            const std::string& name)
        {
            const auto it = std::find_if(
                kernels.begin(),
                kernels.end(),
                [&name](const BehaviorGpuKernelBinding& kernel)
                {
                    return kernel.name == name;
                });
            return it == kernels.end() ? nullptr : &*it;
        }

        const BehaviorGpuPortValue* find_port(
            const BehaviorGpuComputeJob& job,
            const std::string& name)
        {
            const auto it = std::find_if(
                job.ports.begin(),
                job.ports.end(),
                [&name](const BehaviorGpuPortValue& port)
                {
                    return port.name == name;
                });
            return it == job.ports.end() ? nullptr : &*it;
        }

        bool port_matches_binding(
            const BehaviorGpuPortValue& port,
            const BehaviorGpuKernelPortBinding& binding)
        {
            return port.kind == binding.port_kind
                && port.direction == binding.direction;
        }

        bool write_root_constant(
            const BehaviorGpuPortValue& port,
            const BehaviorGpuKernelPortBinding& binding,
            std::vector<uint32_t>& root_constants)
        {
            if (binding.root_constant_dwords == 0u
                || binding.root_constant_offset
                    + binding.root_constant_dwords > root_constants.size()
                || binding.root_constant_dwords > 4u)
            {
                return false;
            }

            uint32_t source[4]{};
            if (port.kind == WZ_GPU_PORT_U32) {
                std::memcpy(source, port.u32, sizeof(source));
            }
            else if (port.kind == WZ_GPU_PORT_F32) {
                std::memcpy(source, port.f32, sizeof(source));
            }
            else {
                return false;
            }

            for (uint32_t i = 0; i < binding.root_constant_dwords; ++i) {
                root_constants[binding.root_constant_offset + i] = source[i];
            }
            return true;
        }
    }

    BehaviorGpuDispatchReport dispatch_behavior_gpu_compute_jobs(
        wz::gpu::Device& device,
        std::span<const BehaviorGpuComputeJob> jobs,
        std::span<const BehaviorGpuKernelBinding> kernels)
    {
        BehaviorGpuDispatchReport report{};
        report.submitted = static_cast<uint32_t>(jobs.size());

        for (const BehaviorGpuComputeJob& job : jobs) {
            const BehaviorGpuKernelBinding* kernel =
                find_kernel(kernels, job.kernel);
            if (!kernel || !kernel->pipeline.valid()) {
                ++report.failed;
                continue;
            }

            bool ok = true;
            // Bootstrap executor supports input SRVs, output UAVs, and scalar
            // root constants. INPUT_OUTPUT buffers are intentionally deferred
            // until lifetime/reuse semantics are less temporary.
            std::vector<uint32_t> root_constants(
                kernel->root_constant_dwords,
                0u);
            std::vector<wz::gpu::GPUHandle> buffers;
            std::vector<wz::gpu::ComputeDispatchBinding> dispatch_bindings;
            struct OutputBuffer
            {
                std::string name;
                WzGpuPortKind kind = WZ_GPU_PORT_NONE;
                uint32_t element_count = 0u;
                uint32_t stride_bytes = 0u;
                wz::gpu::GPUHandle buffer{};
            };
            std::vector<OutputBuffer> output_buffers;

            for (const BehaviorGpuKernelPortBinding& binding :
                kernel->ports)
            {
                const BehaviorGpuPortValue* port =
                    find_port(job, binding.name);
                if (!port || !port_matches_binding(*port, binding)) {
                    ok = false;
                    break;
                }

                if (binding.target
                    == BehaviorGpuKernelPortTarget::RootConstant)
                {
                    ok = write_root_constant(
                        *port,
                        binding,
                        root_constants);
                    if (!ok) {
                        break;
                    }
                    continue;
                }

                if (port->kind != WZ_GPU_PORT_STRUCTURED_BUFFER
                    || port->element_count == 0u
                    || port->stride_bytes == 0u)
                {
                    ok = false;
                    break;
                }

                wz::gpu::GPUHandle buffer{};
                if (port->direction == WZ_GPU_PORT_INPUT) {
                    if (port->initial_data.empty()) {
                        ok = false;
                        break;
                    }
                    buffer = wz::gpu::create_structured_buffer(device, {
                        .element_count = port->element_count,
                        .stride_bytes = port->stride_bytes,
                        .initial_data = port->initial_data.data(),
                        .initial_data_bytes = port->initial_data.size(),
                    });
                }
                else if (port->direction == WZ_GPU_PORT_OUTPUT) {
                    std::vector<std::byte> zeroes(
                        static_cast<size_t>(port->element_count)
                            * port->stride_bytes,
                        std::byte{ 0 });
                    buffer = wz::gpu::create_rw_structured_buffer(device, {
                        .element_count = port->element_count,
                        .stride_bytes = port->stride_bytes,
                        .initial_data = zeroes.data(),
                        .initial_data_bytes = zeroes.size(),
                    });
                    if (buffer.valid()) {
                        output_buffers.push_back({
                            .name = port->name,
                            .kind = port->kind,
                            .element_count = port->element_count,
                            .stride_bytes = port->stride_bytes,
                            .buffer = buffer,
                        });
                    }
                }
                else {
                    ok = false;
                    break;
                }

                if (!buffer.valid()) {
                    ok = false;
                    break;
                }

                buffers.push_back(buffer);
                dispatch_bindings.push_back({
                    .kind = binding.binding_kind,
                    .shader_register = binding.shader_register,
                    .register_space = binding.register_space,
                    .buffer = buffer,
                });
            }

            if (ok) {
                ok = wz::gpu::dispatch_compute(device, {
                    .pipeline = kernel->pipeline,
                    .bindings = dispatch_bindings,
                    .root_constants = root_constants,
                    .group_count_x = job.group_count_x,
                    .group_count_y = job.group_count_y,
                    .group_count_z = job.group_count_z,
                });
            }

            if (ok) {
                for (const OutputBuffer& output : output_buffers) {
                    report.readbacks.push_back({
                        .work = job.work,
                        .port_name = output.name,
                        .kind = output.kind,
                        .element_count = output.element_count,
                        .stride_bytes = output.stride_bytes,
                        .bytes = wz::gpu::readback_buffer(
                            device,
                            output.buffer),
                    });
                }
                ++report.dispatched;
                report.completed_work.push_back(job.work);
            }
            else {
                ++report.failed;
                report.failed_work.push_back(job.work);
            }

            for (wz::gpu::GPUHandle buffer : buffers) {
                (void)wz::gpu::release_compute_buffer(device, buffer);
            }
        }

        return report;
    }

    uint32_t post_behavior_gpu_compute_events(
        BehaviorGpuComputeBuffer& buffer,
        std::span<const BehaviorGpuComputeJob> jobs,
        const BehaviorGpuDispatchReport& report)
    {
        uint32_t posted = 0u;

        for (const WzGpuWorkId completed_work : report.completed_work) {
            const auto job_it = std::find_if(
                jobs.begin(),
                jobs.end(),
                [completed_work](const BehaviorGpuComputeJob& job)
                {
                    return job.work.value == completed_work.value;
                });
            if (job_it == jobs.end()) {
                continue;
            }

            const BehaviorGpuComputeJob& job = *job_it;
            const auto output_count =
                static_cast<uint32_t>(
                    std::count_if(
                        report.readbacks.begin(),
                        report.readbacks.end(),
                        [&job](const BehaviorGpuOutputReadback& readback)
                        {
                            return readback.work.value == job.work.value;
                        }));
            std::vector<BehaviorGpuPortValue> outputs;
            outputs.reserve(output_count);
            for (const BehaviorGpuOutputReadback& readback :
                report.readbacks)
            {
                if (readback.work.value != job.work.value) {
                    continue;
                }
                outputs.push_back(BehaviorGpuPortValue{
                    .name = readback.port_name,
                    .kind = readback.kind,
                    .direction = WZ_GPU_PORT_OUTPUT,
                    .element_count = readback.element_count,
                    .stride_bytes = readback.stride_bytes,
                    .initial_data = readback.bytes,
                });
            }
            buffer.add_event(
                job.entity,
                WZ_EVENT_GPU_COMPUTE_COMPLETED,
                WzGpuComputeEventPayload{
                    .work = job.work,
                    .status = WZ_GPU_COMPUTE_STATUS_COMPLETED,
                    .request_tag = job.request_tag,
                    .output_count = output_count,
                },
                std::move(outputs));
            ++posted;
        }

        for (const WzGpuWorkId failed_work : report.failed_work) {
            const auto it = std::find_if(
                jobs.begin(),
                jobs.end(),
                [failed_work](const BehaviorGpuComputeJob& job)
                {
                    return job.work.value == failed_work.value;
                });
            if (it == jobs.end()) {
                continue;
            }

            buffer.add_event(
                it->entity,
                WZ_EVENT_GPU_COMPUTE_FAILED,
                WzGpuComputeEventPayload{
                    .work = it->work,
                    .status = WZ_GPU_COMPUTE_STATUS_FAILED,
                    .request_tag = it->request_tag,
                    .output_count = 0u,
                });
            ++posted;
        }

        return posted;
    }
}
