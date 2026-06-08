#pragma once

// engine/assets/key_factories/compute_pipeline.h

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/compute_pipeline/compute_pipeline.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>

namespace wz::engine::assets
{
    [[nodiscard]] inline wz::asset::AssetKey make_compute_pipeline_key(
        const ComputePipelineDesc& desc) noexcept
    {
        uint64_t h = kComputePipelineSchema.value;
        h = detail::mix64(h, detail::hash_str(desc.name).lo);
        h = detail::mix64(h, static_cast<uint64_t>(desc.root_constant_dwords));
        h = detail::mix64(h, static_cast<uint64_t>(desc.thread_group_size_x));
        h = detail::mix64(h, static_cast<uint64_t>(desc.thread_group_size_y));
        h = detail::mix64(h, static_cast<uint64_t>(desc.thread_group_size_z));
        h = detail::mix64(h, static_cast<uint64_t>(desc.bindings.size()));

        for (const ComputeBindingDesc& binding : desc.bindings) {
            h = detail::mix64(h, static_cast<uint64_t>(binding.kind));
            h = detail::mix64(h, static_cast<uint64_t>(binding.semantic));
            h = detail::mix64(h, binding.shader_register);
            h = detail::mix64(h, binding.register_space);
            h = detail::mix64(h, binding.descriptor_count);
            h = detail::mix64(h, binding.stride_bytes);
        }

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kComputePipelineSchema.value),
            .compiler_hash = detail::hash_u64(kComputePipelineCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(desc.compute_shader),
        };
    }
}
