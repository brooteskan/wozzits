#include <engine/behavior/behavior_gpu_compute_executor.h>

#include <engine/assets/compute_pipeline/hlsl_binding_extract.h>
#include <engine/assets/key_factories/mesh_sparse_operator.h>
#include <engine/assets/scene/scene_instance.h>
#include <gpu/mesh_field_visualization.h>
#include <file/filesystem.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_set>

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
            if (port.kind != binding.port_kind
                || port.direction != binding.direction)
            {
                return false;
            }
            if (binding.port_kind == WZ_GPU_PORT_STRUCTURED_BUFFER
                && binding.stride_bytes != 0u
                && port.stride_bytes != binding.stride_bytes)
            {
                return false;
            }
            return true;
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

        void set_error(std::string* error, std::string message)
        {
            if (error) {
                *error = std::move(message);
            }
        }

        struct MeshFieldPublishTarget
        {
            wz::asset::AssetKey field_asset{};
            uint32_t channel_id = 0u;
            uint32_t element_count = 0u;

            bool valid() const noexcept
            {
                return field_asset != wz::asset::AssetKey{}
                    && channel_id != 0u
                    && element_count != 0u;
            }
        };

        struct BehaviorGpuPublishContext
        {
            wz::engine::assets::EngineAssetLibrary* assets = nullptr;
            const wz::engine::assets::SceneInstance* scene = nullptr;
        };

        MeshFieldPublishTarget find_mesh_field_publish_target(
            const BehaviorGpuPublishContext* context,
            const BehaviorGpuComputeJob& job,
            uint32_t channel_override,
            std::string& failure)
        {
            using wz::engine::assets::MeshDerivedFieldAsset;

            if (!context || !context->assets || !context->scene) {
                failure = "no publish context (scene-aware dispatch overload "
                    "required)";
                return {};
            }

            bool saw_entity_target = false;
            for (const auto& target :
                context->scene->mesh_field_visualization_targets)
            {
                if (target.node != job.entity) {
                    continue;
                }
                saw_entity_target = true;

                const uint32_t channel_id =
                    channel_override != 0u
                        ? channel_override
                        : target.component.channel_id;
                const MeshDerivedFieldAsset field_asset{
                    .output = target.component.field_asset,
                };
                const auto field_handle =
                    context->assets->mesh_derived_fields()
                        .get_mesh_derived_field(field_asset);
                const auto* field =
                    context->assets->mesh_derived_fields()
                        .get_mesh_derived_field_data(field_handle);
                if (!field || !field->valid()) {
                    failure = "target mesh derived field is not resolved";
                    continue;
                }
                if (field->domain
                    != wz::engine::assets::MeshDerivedFieldDomain::Vertex)
                {
                    failure = "target mesh derived field is not vertex "
                        "domain";
                    continue;
                }

                const auto it = std::find_if(
                    field->channels.begin(),
                    field->channels.end(),
                    [channel_id](const auto& channel)
                    {
                        return channel.channel_id == channel_id;
                    });
                if (it == field->channels.end()
                    || it->value_type
                        != wz::engine::assets
                            ::MeshDerivedFieldValueType::Float1)
                {
                    failure = "target field has no Float1 channel "
                        + std::to_string(channel_id);
                    continue;
                }

                failure.clear();
                return MeshFieldPublishTarget{
                    .field_asset = target.component.field_asset,
                    .channel_id = channel_id,
                    .element_count = field->element_count,
                };
            }

            if (!saw_entity_target) {
                failure = "entity has no mesh field visualization target";
            }
            return {};
        }

        // Resolve the mesh backing the job's entity through its mesh field
        // visualization target (the target field records its source mesh).
        // Used to fill engine-owned mesh data ports. The mesh key is the
        // resident mesh-data table key — the table itself is keyed by the
        // source mesh asset, not by visualization state; this entity-to-mesh
        // hop through the visualization target is the remaining short-term
        // bridge.
        const wz::engine::assets::MeshData* find_entity_mesh_data(
            const BehaviorGpuPublishContext* context,
            const BehaviorGpuComputeJob& job,
            wz::asset::AssetKey* out_mesh_key = nullptr)
        {
            using wz::engine::assets::MeshDerivedFieldAsset;

            if (!context || !context->assets || !context->scene) {
                return nullptr;
            }

            for (const auto& target :
                context->scene->mesh_field_visualization_targets)
            {
                if (target.node != job.entity) {
                    continue;
                }

                const MeshDerivedFieldAsset field_asset{
                    .output = target.component.field_asset,
                };
                const auto field_handle =
                    context->assets->mesh_derived_fields()
                        .get_mesh_derived_field(field_asset);
                const auto* field =
                    context->assets->mesh_derived_fields()
                        .get_mesh_derived_field_data(field_handle);
                if (!field || !field->valid()) {
                    continue;
                }

                const wz::engine::assets::MeshAsset mesh_asset{
                    .output = field->source_mesh_key,
                };
                const auto mesh_handle =
                    context->assets->meshes().get_mesh(mesh_asset);
                const auto* mesh =
                    context->assets->meshes().get_mesh_data(mesh_handle);
                if (mesh && mesh->valid()) {
                    if (out_mesh_key) {
                        *out_mesh_key = field->source_mesh_key;
                    }
                    return mesh;
                }
            }

            return nullptr;
        }

        // Resolve the compiled sparse operator for the job's entity: entity
        // -> mesh (via the visualization-target bridge) -> deterministic
        // operator key (mesh key + kind + vertex domain). The operator must
        // already be compiled; this path never builds one.
        const wz::engine::assets::MeshSparseOperatorData*
        find_entity_sparse_operator(
            const BehaviorGpuPublishContext* context,
            const BehaviorGpuComputeJob& job,
            uint32_t kind_ordinal,
            wz::asset::AssetKey* out_operator_key)
        {
            using namespace wz::engine::assets;

            wz::asset::AssetKey mesh_key{};
            if (!find_entity_mesh_data(context, job, &mesh_key)) {
                return nullptr;
            }

            const MeshSparseOperatorDesc desc{
                .source_mesh = MeshAsset{ .output = mesh_key },
                .kind = static_cast<MeshSparseOperatorKind>(kind_ordinal),
                .domain = MeshOperatorDomain::Vertex,
            };
            const wz::asset::AssetKey operator_key =
                make_mesh_sparse_operator_key(mesh_key, desc);
            if (out_operator_key) {
                *out_operator_key = operator_key;
            }

            const auto handle =
                context->assets->mesh_sparse_operators()
                    .get_sparse_operator(
                        MeshSparseOperatorAsset{ .output = operator_key });
            const auto* data =
                context->assets->mesh_sparse_operators()
                    .get_sparse_operator_data(handle);
            return data && data->valid() ? data : nullptr;
        }

        const char* mesh_field_domain_name(
            wz::engine::assets::MeshDerivedFieldDomain domain) noexcept
        {
            using wz::engine::assets::MeshDerivedFieldDomain;
            switch (domain) {
            case MeshDerivedFieldDomain::Vertex:
                return "Vertex";
            case MeshDerivedFieldDomain::Edge:
                return "Edge";
            case MeshDerivedFieldDomain::Face:
                return "Face";
            case MeshDerivedFieldDomain::Corner:
                return "Corner";
            }
            return "Unknown";
        }

        const char* mesh_field_value_type_name(
            wz::engine::assets::MeshDerivedFieldValueType type) noexcept
        {
            using wz::engine::assets::MeshDerivedFieldValueType;
            switch (type) {
            case MeshDerivedFieldValueType::Float1:
                return "Float1";
            case MeshDerivedFieldValueType::Float2:
                return "Float2";
            case MeshDerivedFieldValueType::Float3:
                return "Float3";
            case MeshDerivedFieldValueType::Float4:
                return "Float4";
            case MeshDerivedFieldValueType::UInt1:
                return "UInt1";
            }
            return "Unknown";
        }

        bool find_job_sparse_operator_kind(
            const BehaviorGpuComputeJob& job,
            uint32_t& out_kind)
        {
            for (const BehaviorGpuPortValue& candidate : job.ports) {
                if (candidate.resource.value
                        == WZ_GPU_RESOURCE_REF_MESH_SPARSE_OPERATOR
                    || candidate.resource.value
                        == WZ_GPU_RESOURCE_REF_MESH_SPARSE_OPERATOR_INFO)
                {
                    out_kind = candidate.u32[1];
                    return true;
                }
            }
            return false;
        }

        struct MeshDerivedFieldInputSignal
        {
            wz::asset::AssetKey field_asset{};
            const wz::engine::assets::MeshDerivedFieldData* field = nullptr;
            const wz::engine::assets::MeshDerivedFieldChannel* channel =
                nullptr;
            wz::gpu::GPUHandle resident_resource{};

            bool valid() const noexcept
            {
                return field_asset != wz::asset::AssetKey{}
                    && field
                    && channel;
            }
        };

        MeshDerivedFieldInputSignal find_mesh_derived_field_input_signal(
            const BehaviorGpuPublishContext* context,
            const BehaviorGpuComputeJob& job,
            const BehaviorGpuPortValue& port,
            std::string& failure)
        {
            using namespace wz::engine::assets;

            if (!context || !context->assets || !context->scene) {
                failure = "mesh derived field input unavailable: "
                    "scene-aware dispatch overload required";
                return {};
            }
            if (port.stride_bytes != sizeof(float)) {
                failure = "mesh derived field input stride must be 4 "
                    "bytes for Float1";
                return {};
            }
            if (port.u32[0] == 0u) {
                failure = "mesh derived field input channel id is zero";
                return {};
            }
            if (port.u32[1] != WZ_GPU_MESH_FIELD_VALUE_FLOAT1) {
                failure = "mesh derived field input supports Float1 only "
                    "in v0";
                return {};
            }
            if (port.u32[2] != WZ_GPU_MESH_FIELD_COMPONENT_ALL
                && port.u32[2] != WZ_GPU_MESH_FIELD_COMPONENT_X)
            {
                failure = "mesh derived field input Float1 component mode "
                    "must be all or x";
                return {};
            }
            if (port.u32[3] != WZ_GPU_SPARSE_APPLY_RESIDUAL) {
                failure = "mesh derived field input supports residual "
                    "apply mode only in v0";
                return {};
            }

            uint32_t operator_kind = 0u;
            if (!find_job_sparse_operator_kind(job, operator_kind)) {
                failure = "mesh derived field input requires a mesh sparse "
                    "operator port or info port for domain/count validation";
                return {};
            }

            wz::asset::AssetKey operator_key{};
            const MeshSparseOperatorData* op = find_entity_sparse_operator(
                context,
                job,
                operator_kind,
                &operator_key);
            if (!op) {
                failure = "mesh sparse operator unavailable: no compiled "
                    "operator for the entity's mesh (resolve the operator "
                    "asset first)";
                return {};
            }
            if (op->domain != MeshOperatorDomain::Vertex) {
                failure = "mesh derived field input domain mismatch: "
                    "operator domain "
                    + std::string(mesh_field_domain_name(op->domain))
                    + " is not Vertex";
                return {};
            }

            bool saw_entity_target = false;
            for (const auto& target :
                context->scene->mesh_field_visualization_targets)
            {
                if (target.node != job.entity) {
                    continue;
                }
                saw_entity_target = true;

                const MeshDerivedFieldAsset field_asset{
                    .output = target.component.field_asset,
                };
                const auto field_handle =
                    context->assets->mesh_derived_fields()
                        .get_mesh_derived_field(field_asset);
                const MeshDerivedFieldData* field =
                    context->assets->mesh_derived_fields()
                        .get_mesh_derived_field_data(field_handle);
                if (!field || !field->valid()) {
                    failure = "input mesh derived field is not resolved";
                    continue;
                }
                if (field->source_mesh_key != op->source_mesh_key) {
                    failure = "input mesh derived field source mesh does "
                        "not match sparse operator source mesh";
                    continue;
                }
                if (field->domain != MeshDerivedFieldDomain::Vertex) {
                    failure = "input mesh derived field domain mismatch: "
                        "expected Vertex, got "
                        + std::string(mesh_field_domain_name(field->domain));
                    continue;
                }
                if (field->element_count != op->row_count) {
                    failure = "sparse operator row count "
                        + std::to_string(op->row_count)
                        + " does not match input field element count "
                        + std::to_string(field->element_count);
                    continue;
                }

                const auto channel_it = std::find_if(
                    field->channels.begin(),
                    field->channels.end(),
                    [&port](const MeshDerivedFieldChannel& channel)
                    {
                        return channel.channel_id == port.u32[0];
                    });
                if (channel_it == field->channels.end()) {
                    failure = "input mesh derived field has no channel "
                        + std::to_string(port.u32[0]);
                    continue;
                }
                if (channel_it->value_type
                    != MeshDerivedFieldValueType::Float1)
                {
                    failure = "input mesh derived field channel "
                        + std::to_string(port.u32[0])
                        + " type mismatch: expected Float1, got "
                        + std::string(mesh_field_value_type_name(
                            channel_it->value_type));
                    continue;
                }

                failure.clear();
                return MeshDerivedFieldInputSignal{
                    .field_asset = target.component.field_asset,
                    .field = field,
                    .channel = &*channel_it,
                    .resident_resource =
                        context->assets->gpu_resident_fields()
                            .find(
                                target.component.field_asset,
                                port.u32[0]),
                };
            }

            if (!saw_entity_target) {
                failure = "entity has no mesh field visualization target";
            }
            return {};
        }

        // Publish one flagged output buffer as the resident mesh field for
        // the job's entity. Prefers refreshing the existing resident
        // resource in place (which keeps handles captured by resolved
        // renderables valid); falls back to creating a new resource for the
        // first publish. Returns false and fills `failure` on any mismatch.
        bool publish_mesh_field_output(
            wz::gpu::Device& device,
            const BehaviorGpuPublishContext* context,
            const BehaviorGpuComputeJob& job,
            uint32_t channel_override,
            wz::gpu::GPUHandle output_buffer,
            uint32_t element_count,
            uint32_t stride_bytes,
            std::string& failure)
        {
            const MeshFieldPublishTarget target =
                find_mesh_field_publish_target(
                    context,
                    job,
                    channel_override,
                    failure);
            if (!target.valid()) {
                if (failure.empty()) {
                    failure = "no usable publish target";
                }
                return false;
            }
            if (stride_bytes != sizeof(float)) {
                failure = "output stride must be sizeof(float), got "
                    + std::to_string(stride_bytes);
                return false;
            }
            if (element_count != target.element_count) {
                failure = "output element count "
                    + std::to_string(element_count)
                    + " does not match field element count "
                    + std::to_string(target.element_count);
                return false;
            }

            auto& resident_fields = context->assets->gpu_resident_fields();
            const wz::gpu::GPUHandle existing =
                resident_fields.find(target.field_asset, target.channel_id);
            if (existing.valid()
                && wz::gpu::update_mesh_field_visualization_from_gpu_source(
                    device,
                    existing,
                    output_buffer,
                    0u,
                    element_count,
                    stride_bytes))
            {
                return true;
            }

            const wz::gpu::GPUHandle mesh_field =
                wz::gpu::create_mesh_field_visualization_from_gpu_source(
                    device,
                    output_buffer,
                    0u,
                    element_count,
                    stride_bytes);
            if (!mesh_field.valid()) {
                failure = "failed to create mesh field visualization from "
                    "output buffer";
                return false;
            }

            if (!resident_fields.replace(
                    context->assets->mesh_field_compute(),
                    wz::engine::assets::GpuResidentFieldEntry{
                        .field_key = target.field_asset,
                        .channel_id = target.channel_id,
                        .gpu_resource = mesh_field,
                    }))
            {
                wz::gpu::release_mesh_field_visualization(
                    device,
                    mesh_field);
                failure = "failed to register resident mesh field";
                return false;
            }
            return true;
        }

        WzGpuPortKind behavior_port_kind(
            wz::engine::assets::SceneComputeKernelPortKind kind)
        {
            using wz::engine::assets::SceneComputeKernelPortKind;

            switch (kind) {
            case SceneComputeKernelPortKind::StructuredBuffer:
                return WZ_GPU_PORT_STRUCTURED_BUFFER;
            case SceneComputeKernelPortKind::U32:
                return WZ_GPU_PORT_U32;
            case SceneComputeKernelPortKind::F32:
                return WZ_GPU_PORT_F32;
            }
            return WZ_GPU_PORT_NONE;
        }

        WzGpuPortDirection behavior_port_direction(
            wz::engine::assets::SceneComputeKernelPortDirection direction)
        {
            using wz::engine::assets::SceneComputeKernelPortDirection;

            switch (direction) {
            case SceneComputeKernelPortDirection::Input:
                return WZ_GPU_PORT_INPUT;
            case SceneComputeKernelPortDirection::Output:
                return WZ_GPU_PORT_OUTPUT;
            }
            return 0u;
        }

        wz::gpu::ComputeBindingKind gpu_binding_kind(
            wz::engine::assets::ComputeBindingKind kind)
        {
            using AssetKind = wz::engine::assets::ComputeBindingKind;
            using GpuKind = wz::gpu::ComputeBindingKind;

            switch (kind) {
            case AssetKind::StructuredBufferSRV:
                return GpuKind::StructuredBufferSRV;
            case AssetKind::StructuredBufferUAV:
                return GpuKind::StructuredBufferUAV;
            case AssetKind::ByteAddressBufferSRV:
                return GpuKind::ByteAddressBufferSRV;
            case AssetKind::ByteAddressBufferUAV:
                return GpuKind::ByteAddressBufferUAV;
            }
            return GpuKind::StructuredBufferSRV;
        }

        wz::gpu::ComputeBindingKind behavior_binding_kind(
            wz::engine::assets::SceneComputeKernelBindingKind kind)
        {
            using wz::gpu::ComputeBindingKind;
            using wz::engine::assets::SceneComputeKernelBindingKind;

            switch (kind) {
            case SceneComputeKernelBindingKind::SRV:
                return ComputeBindingKind::StructuredBufferSRV;
            case SceneComputeKernelBindingKind::UAV:
                return ComputeBindingKind::StructuredBufferUAV;
            }
            return ComputeBindingKind::StructuredBufferSRV;
        }

        BehaviorGpuKernelPortBinding make_port_binding(
            const wz::engine::assets::SceneComputeKernelPortAsset& port)
        {
            using wz::engine::assets::SceneComputeKernelPortKind;

            BehaviorGpuKernelPortBinding binding{};
            binding.name = port.name;
            binding.port_kind = behavior_port_kind(port.kind);
            binding.direction = behavior_port_direction(port.direction);

            if (port.kind == SceneComputeKernelPortKind::StructuredBuffer) {
                binding.target =
                    BehaviorGpuKernelPortTarget::BufferBinding;
                binding.binding_kind = behavior_binding_kind(
                    port.binding_kind);
                binding.shader_register = port.shader_register;
                binding.register_space = port.register_space;
                binding.stride_bytes = port.stride_bytes;
            }
            else {
                binding.target =
                    BehaviorGpuKernelPortTarget::RootConstant;
                binding.root_constant_offset = port.root_constant_offset;
                binding.root_constant_dwords = port.root_constant_dwords;
            }

            return binding;
        }

        BehaviorGpuKernelPortBinding make_port_binding(
            const wz::engine::assets::HlslBindingPort& port)
        {
            using wz::engine::assets::HlslBindingPortTarget;

            BehaviorGpuKernelPortBinding binding{};
            binding.name = port.name;
            binding.port_kind = port.port_kind;
            binding.direction = port.direction;

            if (port.target == HlslBindingPortTarget::Buffer) {
                binding.target =
                    BehaviorGpuKernelPortTarget::BufferBinding;
                binding.binding_kind = gpu_binding_kind(port.binding_kind);
                binding.shader_register = port.shader_register;
                binding.register_space = port.register_space;
                binding.stride_bytes = port.stride_bytes;
            }
            else {
                binding.target =
                    BehaviorGpuKernelPortTarget::RootConstant;
                binding.root_constant_offset = port.root_constant_offset;
                binding.root_constant_dwords = port.root_constant_dwords;
            }

            return binding;
        }

        std::vector<BehaviorGpuKernelPortBinding> derive_port_bindings(
            const wz::engine::assets::SceneComputeKernelAsset& kernel,
            const wz::engine::assets::EngineAssetLibrary& assets,
            std::string* error)
        {
            using namespace wz::engine::assets;

            if (!kernel.ports.empty()) {
                std::vector<BehaviorGpuKernelPortBinding> out;
                out.reserve(kernel.ports.size());
                for (const SceneComputeKernelPortAsset& port : kernel.ports) {
                    out.push_back(make_port_binding(port));
                }
                return out;
            }

            const std::string shader_path =
                wz::fs::is_absolute(kernel.hlsl_path)
                    ? kernel.hlsl_path
                    : wz::fs::join(assets.resource_root(), kernel.hlsl_path);
            const HlslBindingExtraction extraction =
                extract_hlsl_bindings_from_file(shader_path);
            if (!extraction.ok()) {
                set_error(
                    error,
                    "compute kernel '" + kernel.kernel_id
                    + "' shader binding extraction failed: "
                    + extraction.diagnostics.front());
                return {};
            }

            std::vector<BehaviorGpuKernelPortBinding> out;
            out.reserve(extraction.ports.size());
            for (const HlslBindingPort& port : extraction.ports) {
                out.push_back(make_port_binding(port));
            }
            return out;
        }

        const BehaviorGpuKernelContract* find_contract(
            std::span<const BehaviorGpuKernelContract> contracts,
            const std::string& kernel_id)
        {
            const auto it = std::find_if(
                contracts.begin(),
                contracts.end(),
                [&kernel_id](const BehaviorGpuKernelContract& contract)
                {
                    return contract.kernel_id == kernel_id;
                });
            return it == contracts.end() ? nullptr : &*it;
        }

        bool validate_contract(
            const BehaviorGpuKernelBinding& binding,
            const BehaviorGpuKernelContract* contract,
            std::string* error)
        {
            if (!contract) {
                return true;
            }

            for (const BehaviorGpuKernelPortContract& expected :
                contract->ports)
            {
                const std::string expected_name =
                    wz::engine::assets::normalize_hlsl_port_name(
                        expected.name);
                const auto it = std::find_if(
                    binding.ports.begin(),
                    binding.ports.end(),
                    [&expected_name](
                        const BehaviorGpuKernelPortBinding& actual)
                    {
                        return actual.name == expected_name;
                    });
                if (it == binding.ports.end()) {
                    set_error(
                        error,
                        "GPU kernel contract for '" + binding.name
                        + "' expects missing port '" + expected.name + "'");
                    return false;
                }
                if (it->port_kind != expected.kind
                    || it->direction != expected.direction)
                {
                    set_error(
                        error,
                        "GPU kernel contract for '" + binding.name
                        + "' has incompatible port '" + expected.name + "'");
                    return false;
                }
                if (expected.kind == WZ_GPU_PORT_STRUCTURED_BUFFER
                    && expected.stride_bytes != 0u
                    && it->stride_bytes != 0u
                    && expected.stride_bytes != it->stride_bytes)
                {
                    set_error(
                        error,
                        "GPU kernel contract for '" + binding.name
                        + "' has stride mismatch for port '"
                        + expected.name + "'");
                    return false;
                }
            }

            return true;
        }
    }

    const BehaviorGpuKernelBinding* BehaviorGpuKernelLibrary::find(
        const std::string& name) const
    {
        return find_kernel(kernels, name);
    }

    namespace
    {
        BehaviorGpuDispatchReport dispatch_behavior_gpu_compute_jobs_impl(
        wz::gpu::Device& device,
        std::span<const BehaviorGpuComputeJob> jobs,
        std::span<const BehaviorGpuKernelBinding> kernels,
        const BehaviorGpuPublishContext* publish_context)
    {
        BehaviorGpuDispatchReport report{};
        report.submitted = static_cast<uint32_t>(jobs.size());

        for (const BehaviorGpuComputeJob& job : jobs) {
            const BehaviorGpuKernelBinding* kernel =
                find_kernel(kernels, job.kernel);
            if (!kernel || !kernel->pipeline.valid()) {
                ++report.failed;
                report.failed_work.push_back(job.work);
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
                WzGpuResourceRef resource{};
                uint32_t u32[4]{};
                wz::gpu::GPUHandle buffer{};
            };
            std::vector<OutputBuffer> output_buffers;

            // Largest element count the engine resolved for mesh-bound
            // ports; used to derive the dispatch group count when the job
            // leaves it at 0.
            uint32_t engine_resolved_elements = 0u;

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
                    BehaviorGpuPortValue constant_port = *port;
                    if (port->resource.value
                        == WZ_GPU_RESOURCE_REF_MESH_VERTEX_COUNT)
                    {
                        const auto* mesh =
                            find_entity_mesh_data(publish_context, job);
                        if (!mesh) {
                            report.publish_failures.push_back({
                                .work = job.work,
                                .port_name = port->name,
                                .reason = "mesh vertex count unavailable: "
                                    "entity has no resolvable mesh field "
                                    "visualization target",
                            });
                            ok = false;
                            break;
                        }
                        constant_port.u32[0] = mesh->vertex_count();
                        engine_resolved_elements = std::max(
                            engine_resolved_elements,
                            mesh->vertex_count());
                    }
                    else if (port->resource.value
                        == WZ_GPU_RESOURCE_REF_MESH_TRIANGLE_COUNT)
                    {
                        const auto* mesh =
                            find_entity_mesh_data(publish_context, job);
                        if (!mesh) {
                            report.publish_failures.push_back({
                                .work = job.work,
                                .port_name = port->name,
                                .reason = "mesh triangle count unavailable: "
                                    "entity has no resolvable mesh field "
                                    "visualization target",
                            });
                            ok = false;
                            break;
                        }
                        // Topology counts deliberately do not feed the
                        // legacy AUTO group derivation; kernels iterating
                        // triangles declare WZ_GPU_DISPATCH_DOMAIN_FACE.
                        constant_port.u32[0] = mesh->index_count() / 3u;
                    }
                    else if (port->resource.value
                        == WZ_GPU_RESOURCE_REF_MESH_SPARSE_OPERATOR_INFO)
                    {
                        const auto* op = find_entity_sparse_operator(
                            publish_context,
                            job,
                            port->u32[1],
                            nullptr);
                        if (!op) {
                            report.publish_failures.push_back({
                                .work = job.work,
                                .port_name = port->name,
                                .reason = "mesh sparse operator unavailable: "
                                    "no compiled operator for the entity's "
                                    "mesh (resolve the operator asset "
                                    "first)",
                            });
                            ok = false;
                            break;
                        }
                        constant_port.u32[0] = op->row_count;
                        constant_port.u32[1] = op->nonzero_count;
                        constant_port.u32[2] =
                            static_cast<uint32_t>(op->kind);
                        constant_port.u32[3] =
                            static_cast<uint32_t>(op->value_convention);
                    }
                    ok = write_root_constant(
                        constant_port,
                        binding,
                        root_constants);
                    if (!ok) {
                        break;
                    }
                    continue;
                }

                if (port->kind != WZ_GPU_PORT_STRUCTURED_BUFFER
                    || port->stride_bytes == 0u)
                {
                    ok = false;
                    break;
                }

                uint32_t element_count = port->element_count;
                wz::gpu::GPUHandle buffer{};
                wz::gpu::ComputeBindingResourceKind resource_kind =
                    wz::gpu::ComputeBindingResourceKind::ComputeBuffer;
                bool transient_buffer = true;
                if (port->direction == WZ_GPU_PORT_INPUT) {
                    if (port->resource.value
                        == WZ_GPU_RESOURCE_REF_MESH_VERTEX_POSITIONS)
                    {
                        wz::asset::AssetKey mesh_key{};
                        const auto* mesh = find_entity_mesh_data(
                            publish_context,
                            job,
                            &mesh_key);
                        if (!mesh
                            || port->stride_bytes != 3u * sizeof(float))
                        {
                            report.publish_failures.push_back({
                                .work = job.work,
                                .port_name = port->name,
                                .reason = !mesh
                                    ? "mesh vertex positions unavailable: "
                                      "entity has no resolvable mesh field "
                                      "visualization target"
                                    : "mesh vertex position port stride "
                                      "must be 12 bytes (float3)",
                            });
                            ok = false;
                            break;
                        }

                        element_count = mesh->vertex_count();
                        engine_resolved_elements = std::max(
                            engine_resolved_elements,
                            element_count);

                        // Mesh data is immutable after import: upload once
                        // per mesh into the resident table and reuse it on
                        // every later dispatch.
                        auto* resident =
                            publish_context->assets
                                ->gpu_resident_mesh_data()
                                .find_or_add(mesh_key);
                        if (resident && resident->positions.valid()) {
                            buffer = resident->positions;
                            transient_buffer = false;
                        }
                        else {
                            std::vector<float> positions;
                            positions.reserve(mesh->vertices.size() * 3u);
                            for (const auto& vertex : mesh->vertices) {
                                positions.push_back(vertex.position[0]);
                                positions.push_back(vertex.position[1]);
                                positions.push_back(vertex.position[2]);
                            }
                            buffer = wz::gpu::create_structured_buffer(
                                device, {
                                .element_count = element_count,
                                .stride_bytes = port->stride_bytes,
                                .initial_data = positions.data(),
                                .initial_data_bytes =
                                    positions.size() * sizeof(float),
                            });
                            if (resident && buffer.valid()) {
                                resident->positions = buffer;
                                resident->vertex_count = element_count;
                                transient_buffer = false;
                            }
                        }
                    }
                    else if (port->resource.value
                        == WZ_GPU_RESOURCE_REF_MESH_INDICES)
                    {
                        wz::asset::AssetKey mesh_key{};
                        const auto* mesh = find_entity_mesh_data(
                            publish_context,
                            job,
                            &mesh_key);
                        if (!mesh
                            || port->stride_bytes != sizeof(uint32_t))
                        {
                            report.publish_failures.push_back({
                                .work = job.work,
                                .port_name = port->name,
                                .reason = !mesh
                                    ? "mesh indices unavailable: entity has "
                                      "no resolvable mesh field "
                                      "visualization target"
                                    : "mesh index port stride must be "
                                      "4 bytes (uint)",
                            });
                            ok = false;
                            break;
                        }

                        element_count = mesh->index_count();
                        if (element_count == 0u) {
                            report.publish_failures.push_back({
                                .work = job.work,
                                .port_name = port->name,
                                .reason = "mesh indices unavailable: "
                                    "entity mesh has no indices",
                            });
                            ok = false;
                            break;
                        }
                        // Topology reads deliberately do not feed the
                        // legacy AUTO group derivation; a vertex-domain
                        // kernel that merely reads indices must not
                        // over-dispatch by index count. Kernels iterating
                        // corners declare WZ_GPU_DISPATCH_DOMAIN_CORNER.
                        auto* resident =
                            publish_context->assets
                                ->gpu_resident_mesh_data()
                                .find_or_add(mesh_key);
                        if (resident && resident->indices.valid()) {
                            buffer = resident->indices;
                            transient_buffer = false;
                        }
                        else {
                            buffer = wz::gpu::create_structured_buffer(
                                device, {
                                .element_count = element_count,
                                .stride_bytes = port->stride_bytes,
                                .initial_data = mesh->indices.data(),
                                .initial_data_bytes =
                                    mesh->indices.size()
                                    * sizeof(uint32_t),
                            });
                            if (resident && buffer.valid()) {
                                resident->indices = buffer;
                                resident->index_count = element_count;
                                resident->triangle_count =
                                    element_count / 3u;
                                transient_buffer = false;
                            }
                        }
                    }
                    else if (port->resource.value
                        == WZ_GPU_RESOURCE_REF_MESH_SPARSE_OPERATOR)
                    {
                        wz::asset::AssetKey operator_key{};
                        const auto* op = find_entity_sparse_operator(
                            publish_context,
                            job,
                            port->u32[1],
                            &operator_key);
                        if (!op || port->stride_bytes != 4u) {
                            report.publish_failures.push_back({
                                .work = job.work,
                                .port_name = port->name,
                                .reason = !op
                                    ? "mesh sparse operator unavailable: "
                                      "no compiled operator for the "
                                      "entity's mesh (resolve the operator "
                                      "asset first)"
                                    : "sparse operator port stride must be "
                                      "4 bytes",
                            });
                            ok = false;
                            break;
                        }

                        const void* initial_data = nullptr;
                        switch (port->u32[0]) {
                        case WZ_GPU_SPARSE_OPERATOR_ROW_OFFSETS:
                            element_count = op->row_count + 1u;
                            initial_data = op->row_offsets.data();
                            break;
                        case WZ_GPU_SPARSE_OPERATOR_COL_INDICES:
                            element_count = op->nonzero_count;
                            initial_data = op->col_indices.data();
                            break;
                        case WZ_GPU_SPARSE_OPERATOR_WEIGHTS:
                            element_count = op->nonzero_count;
                            initial_data = op->weights.data();
                            break;
                        case WZ_GPU_SPARSE_OPERATOR_VERTEX_MASS:
                            element_count = static_cast<uint32_t>(
                                op->vertex_mass.size());
                            initial_data = op->vertex_mass.data();
                            break;
                        default:
                            break;
                        }
                        if (!initial_data || element_count == 0u) {
                            report.publish_failures.push_back({
                                .work = job.work,
                                .port_name = port->name,
                                .reason = "sparse operator component "
                                    "unavailable: unknown component or "
                                    "operator has no data for it",
                            });
                            ok = false;
                            break;
                        }

                        // CSR sizes deliberately do not feed the legacy
                        // AUTO derivation: nnz dwarfs the iteration domain.
                        // Consumers declare WZ_GPU_DISPATCH_DOMAIN_VERTEX.
                        auto* resident =
                            publish_context->assets
                                ->gpu_resident_sparse_operators()
                                .find_or_add(operator_key);
                        wz::asset::ResourceHandle* slot = nullptr;
                        if (resident) {
                            resident->row_count = op->row_count;
                            resident->nonzero_count = op->nonzero_count;
                            switch (port->u32[0]) {
                            case WZ_GPU_SPARSE_OPERATOR_ROW_OFFSETS:
                                slot = &resident->row_offsets;
                                break;
                            case WZ_GPU_SPARSE_OPERATOR_COL_INDICES:
                                slot = &resident->col_indices;
                                break;
                            case WZ_GPU_SPARSE_OPERATOR_WEIGHTS:
                                slot = &resident->weights;
                                break;
                            case WZ_GPU_SPARSE_OPERATOR_VERTEX_MASS:
                                slot = &resident->vertex_mass;
                                break;
                            }
                        }

                        if (slot && slot->valid()) {
                            buffer = *slot;
                            transient_buffer = false;
                        }
                        else {
                            buffer = wz::gpu::create_structured_buffer(
                                device, {
                                .element_count = element_count,
                                .stride_bytes = port->stride_bytes,
                                .initial_data = initial_data,
                                .initial_data_bytes =
                                    static_cast<uint64_t>(element_count)
                                    * port->stride_bytes,
                            });
                            if (slot && buffer.valid()) {
                                *slot = buffer;
                                transient_buffer = false;
                            }
                        }
                    }
                    else if (port->resource.value
                        == WZ_GPU_RESOURCE_REF_MESH_DERIVED_FIELD_CHANNEL)
                    {
                        std::string signal_failure;
                        const MeshDerivedFieldInputSignal signal =
                            find_mesh_derived_field_input_signal(
                                publish_context,
                                job,
                                *port,
                                signal_failure);
                        if (!signal.valid()) {
                            report.publish_failures.push_back({
                                .work = job.work,
                                .port_name = port->name,
                                .reason = std::move(signal_failure),
                            });
                            ok = false;
                            break;
                        }

                        element_count = signal.field->element_count;
                        engine_resolved_elements = std::max(
                            engine_resolved_elements,
                            element_count);

                        if (signal.resident_resource.valid()) {
                            buffer = signal.resident_resource;
                            resource_kind = wz::gpu::ComputeBindingResourceKind
                                ::MeshFieldVisualization;
                            transient_buffer = false;
                        }
                        else {
                            const std::byte* begin =
                                signal.field->values.data()
                                + signal.channel->byte_offset;
                            buffer = wz::gpu::create_structured_buffer(
                                device, {
                                .element_count = element_count,
                                .stride_bytes = port->stride_bytes,
                                .initial_data = begin,
                                .initial_data_bytes =
                                    signal.channel->byte_count,
                            });
                        }
                    }
                    else {
                        if (element_count == 0u
                            || port->initial_data.empty())
                        {
                            ok = false;
                            break;
                        }
                        buffer = wz::gpu::create_structured_buffer(device, {
                            .element_count = element_count,
                            .stride_bytes = port->stride_bytes,
                            .initial_data = port->initial_data.data(),
                            .initial_data_bytes = port->initial_data.size(),
                        });
                    }
                }
                else if (port->direction == WZ_GPU_PORT_OUTPUT) {
                    if (element_count == 0u
                        && port->resource.value
                            == WZ_GPU_RESOURCE_REF_MESH_FIELD_VISUALIZATION)
                    {
                        std::string target_failure;
                        const MeshFieldPublishTarget target =
                            find_mesh_field_publish_target(
                                publish_context,
                                job,
                                port->u32[0],
                                target_failure);
                        if (!target.valid()) {
                            report.publish_failures.push_back({
                                .work = job.work,
                                .port_name = port->name,
                                .reason = "output element count "
                                    "unavailable: " + target_failure,
                            });
                            ok = false;
                            break;
                        }
                        element_count = target.element_count;
                        engine_resolved_elements = std::max(
                            engine_resolved_elements,
                            element_count);
                    }
                    if (element_count == 0u) {
                        ok = false;
                        break;
                    }

                    std::vector<std::byte> zeroes(
                        static_cast<size_t>(element_count)
                            * port->stride_bytes,
                        std::byte{ 0 });
                    buffer = wz::gpu::create_rw_structured_buffer(device, {
                        .element_count = element_count,
                        .stride_bytes = port->stride_bytes,
                        .initial_data = zeroes.data(),
                        .initial_data_bytes = zeroes.size(),
                    });
                    if (buffer.valid()) {
                        output_buffers.push_back({
                            .name = port->name,
                            .kind = port->kind,
                            .element_count = element_count,
                            .stride_bytes = port->stride_bytes,
                            .resource = port->resource,
                            .u32 = {
                                port->u32[0],
                                port->u32[1],
                                port->u32[2],
                                port->u32[3],
                            },
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

                // Resident mesh buffers are owned by the table for the
                // library's lifetime; only per-dispatch transients join the
                // release list.
                if (transient_buffer) {
                    buffers.push_back(buffer);
                }
                dispatch_bindings.push_back({
                    .kind = binding.binding_kind,
                    .resource_kind = resource_kind,
                    .shader_register = binding.shader_register,
                    .register_space = binding.register_space,
                    .buffer = buffer,
                });
            }

            uint32_t group_count_x = job.group_count_x;
            uint32_t group_count_y = job.group_count_y;
            uint32_t group_count_z = job.group_count_z;
            if (ok
                && group_count_x == 0u
                && kernel->thread_group_size_x > 0u)
            {
                // Explicit group counts always win (group_count_x != 0).
                // Otherwise the declared dispatch domain selects the element
                // count; AUTO preserves the legacy max-of-resolved-counts
                // derivation, which over-dispatches kernels whose read set
                // spans more elements than their iteration domain.
                uint32_t domain_elements = 0u;
                std::string domain_failure;
                switch (job.dispatch_domain) {
                case WZ_GPU_DISPATCH_DOMAIN_AUTO:
                    domain_elements = engine_resolved_elements;
                    break;
                case WZ_GPU_DISPATCH_DOMAIN_VERTEX:
                case WZ_GPU_DISPATCH_DOMAIN_FACE:
                case WZ_GPU_DISPATCH_DOMAIN_CORNER: {
                    const auto* mesh =
                        find_entity_mesh_data(publish_context, job);
                    if (!mesh) {
                        domain_failure =
                            "dispatch domain unresolvable: entity has no "
                            "resolvable mesh field visualization target";
                        break;
                    }
                    domain_elements =
                        job.dispatch_domain == WZ_GPU_DISPATCH_DOMAIN_VERTEX
                            ? mesh->vertex_count()
                            : job.dispatch_domain
                                    == WZ_GPU_DISPATCH_DOMAIN_FACE
                                ? mesh->index_count() / 3u
                                : mesh->index_count();
                    if (domain_elements == 0u) {
                        domain_failure =
                            "dispatch domain element count is zero";
                    }
                    break;
                }
                case WZ_GPU_DISPATCH_DOMAIN_EDGE:
                    domain_failure =
                        "edge dispatch domain is reserved: unique edge "
                        "count requires resident mesh topology";
                    break;
                case WZ_GPU_DISPATCH_DOMAIN_OUTPUT:
                    if (output_buffers.empty()) {
                        domain_failure =
                            "output dispatch domain unresolvable: job has "
                            "no output port";
                        break;
                    }
                    domain_elements = output_buffers.front().element_count;
                    break;
                default:
                    domain_failure = "unknown dispatch domain "
                        + std::to_string(job.dispatch_domain);
                    break;
                }

                if (!domain_failure.empty()) {
                    report.publish_failures.push_back({
                        .work = job.work,
                        .port_name = "dispatch_domain",
                        .reason = std::move(domain_failure),
                    });
                    ok = false;
                }
                else if (domain_elements > 0u) {
                    group_count_x =
                        (domain_elements
                            + kernel->thread_group_size_x - 1u)
                        / kernel->thread_group_size_x;
                    group_count_y =
                        group_count_y != 0u ? group_count_y : 1u;
                    group_count_z =
                        group_count_z != 0u ? group_count_z : 1u;
                    report.derived_dispatches.push_back({
                        .work = job.work,
                        .dispatch_domain = job.dispatch_domain,
                        .element_count = domain_elements,
                        .group_count_x = group_count_x,
                    });
                }
            }
            if (ok && group_count_x == 0u) {
                // AUTO resolved nothing (e.g. the job only reads topology,
                // which deliberately does not feed AUTO). Surface why the
                // job cannot dispatch instead of failing silently.
                report.publish_failures.push_back({
                    .work = job.work,
                    .port_name = "dispatch_domain",
                    .reason = "dispatch group count unresolved: set "
                        "explicit group counts or declare a dispatch "
                        "domain",
                });
                ok = false;
            }
            if (group_count_x == 0u
                || group_count_y == 0u
                || group_count_z == 0u)
            {
                ok = false;
            }

            if (ok) {
                ok = wz::gpu::dispatch_compute(device, {
                    .pipeline = kernel->pipeline,
                    .bindings = dispatch_bindings,
                    .root_constants = root_constants,
                    .group_count_x = group_count_x,
                    .group_count_y = group_count_y,
                    .group_count_z = group_count_z,
                });
            }

            if (ok) {
                for (const OutputBuffer& output : output_buffers) {
                    const bool publish_requested =
                        output.resource.value
                            == WZ_GPU_RESOURCE_REF_MESH_FIELD_VISUALIZATION;
                    bool published = false;
                    if (publish_requested) {
                        std::string failure;
                        published = publish_mesh_field_output(
                            device,
                            publish_context,
                            job,
                            output.u32[0],
                            output.buffer,
                            output.element_count,
                            output.stride_bytes,
                            failure);
                        if (published) {
                            ++report.published_mesh_fields;
                        }
                        else {
                            report.publish_failures.push_back({
                                .work = job.work,
                                .port_name = output.name,
                                .reason = std::move(failure),
                            });
                        }
                    }

                    // Published outputs are GPU-resident for rendering;
                    // skip the CPU readback that exists to surface outputs
                    // through completion events. Failed publishes keep the
                    // readback so the plugin can still inspect the data.
                    if (!published) {
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
    }

    BehaviorGpuDispatchReport dispatch_behavior_gpu_compute_jobs(
        wz::gpu::Device& device,
        std::span<const BehaviorGpuComputeJob> jobs,
        std::span<const BehaviorGpuKernelBinding> kernels)
    {
        return dispatch_behavior_gpu_compute_jobs_impl(
            device,
            jobs,
            kernels,
            nullptr);
    }

    BehaviorGpuDispatchReport dispatch_behavior_gpu_compute_jobs(
        wz::gpu::Device& device,
        std::span<const BehaviorGpuComputeJob> jobs,
        const BehaviorGpuKernelLibrary& library)
    {
        return dispatch_behavior_gpu_compute_jobs(
            device,
            jobs,
            library.kernels);
    }

    BehaviorGpuDispatchReport dispatch_behavior_gpu_compute_jobs(
        wz::gpu::Device& device,
        wz::engine::assets::EngineAssetLibrary& assets,
        const wz::engine::assets::SceneInstance& scene,
        std::span<const BehaviorGpuComputeJob> jobs,
        const BehaviorGpuKernelLibrary& library)
    {
        const BehaviorGpuPublishContext context{
            .assets = &assets,
            .scene = &scene,
        };
        return dispatch_behavior_gpu_compute_jobs_impl(
            device,
            jobs,
            library.kernels,
            &context);
    }

    bool build_kernel_library_from_scene(
        wz::gpu::Device& device,
        const wz::engine::assets::SceneAssetData& scene,
        const wz::engine::assets::EngineAssetLibrary& assets,
        std::span<const BehaviorGpuKernelContract> contracts,
        BehaviorGpuKernelLibrary& out_library,
        std::string* error)
    {
        using namespace wz::engine::assets;

        release_behavior_gpu_kernel_library(device, out_library);

        std::unordered_set<std::string> kernel_names;
        BehaviorGpuKernelLibrary library{};

        for (const SceneNodeAsset& node : scene.nodes) {
            if (!node.compute_kernel) {
                continue;
            }

            const SceneComputeKernelAsset& kernel = *node.compute_kernel;
            if (kernel.kernel_id.empty()) {
                set_error(error, "compute kernel has empty kernel_id");
                release_behavior_gpu_kernel_library(device, library);
                return false;
            }
            if (!kernel_names.insert(kernel.kernel_id).second) {
                set_error(
                    error,
                    "duplicate compute kernel id '" + kernel.kernel_id + "'");
                release_behavior_gpu_kernel_library(device, library);
                return false;
            }
            if (kernel.compute_shader_asset == wz::asset::AssetKey{}
                || kernel.compute_pipeline_asset == wz::asset::AssetKey{})
            {
                set_error(
                    error,
                    "compute kernel '" + kernel.kernel_id
                    + "' has not been materialized");
                release_behavior_gpu_kernel_library(device, library);
                return false;
            }

            const ComputeShaderHandle shader =
                assets.shaders().get_compute_shader(
                    ComputeShaderAsset{
                        .shader = kernel.compute_shader_asset,
                    });
            if (!shader.valid()) {
                set_error(
                    error,
                    "compute shader for kernel '" + kernel.kernel_id
                    + "' is unresolved");
                release_behavior_gpu_kernel_library(device, library);
                return false;
            }

            const auto pipeline_handle =
                assets.compute_pipelines().get_compute_pipeline(
                    ComputePipelineAsset{
                        .key = kernel.compute_pipeline_asset,
                    });
            const ComputePipelineData* pipeline_data =
                assets.compute_pipelines().get_compute_pipeline_data(
                    pipeline_handle);
            if (!pipeline_handle.valid() || !pipeline_data) {
                set_error(
                    error,
                    "compute pipeline for kernel '" + kernel.kernel_id
                    + "' is unresolved");
                release_behavior_gpu_kernel_library(device, library);
                return false;
            }

            const wz::gpu::GPUHandle pipeline =
                wz::gpu::create_compute_pipeline(
                    device,
                    *pipeline_data,
                    shader.shader);
            if (!pipeline.valid()) {
                set_error(
                    error,
                    "GPU pipeline creation failed for kernel '"
                    + kernel.kernel_id + "'");
                release_behavior_gpu_kernel_library(device, library);
                return false;
            }

            BehaviorGpuKernelBinding binding{};
            binding.name = kernel.kernel_id;
            binding.pipeline = pipeline;
            binding.root_constant_dwords =
                pipeline_data->root_constant_dwords;
            binding.thread_group_size_x = kernel.thread_group_size_x;
            binding.thread_group_size_y = kernel.thread_group_size_y;
            binding.thread_group_size_z = kernel.thread_group_size_z;
            binding.ports = derive_port_bindings(kernel, assets, error);
            if (binding.ports.empty() && !kernel.ports.empty()) {
                set_error(
                    error,
                    "compute kernel '" + kernel.kernel_id
                    + "' has no runtime port bindings");
                release_behavior_gpu_kernel_library(device, library);
                return false;
            }
            if (binding.ports.empty()) {
                release_behavior_gpu_kernel_library(device, library);
                return false;
            }
            if (!validate_contract(
                    binding,
                    find_contract(contracts, kernel.kernel_id),
                    error))
            {
                release_behavior_gpu_kernel_library(device, library);
                return false;
            }
            library.kernels.push_back(std::move(binding));
        }

        out_library = std::move(library);
        return true;
    }

    bool build_kernel_library_from_scene(
        wz::gpu::Device& device,
        const wz::engine::assets::SceneAssetData& scene,
        const wz::engine::assets::EngineAssetLibrary& assets,
        BehaviorGpuKernelLibrary& out_library,
        std::string* error)
    {
        return build_kernel_library_from_scene(
            device,
            scene,
            assets,
            std::span<const BehaviorGpuKernelContract>{},
            out_library,
            error);
    }

    uint32_t release_behavior_gpu_kernel_library(
        wz::gpu::Device& device,
        BehaviorGpuKernelLibrary& library)
    {
        uint32_t released = 0u;
        for (BehaviorGpuKernelBinding& kernel : library.kernels) {
            if (kernel.pipeline.valid()
                && wz::gpu::release_compute_pipeline(
                    device,
                    kernel.pipeline))
            {
                ++released;
            }
            kernel.pipeline = {};
        }
        library.kernels.clear();
        return released;
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
