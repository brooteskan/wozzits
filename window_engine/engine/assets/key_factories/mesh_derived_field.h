#pragma once

// engine/assets/key_factories/mesh_derived_field.h

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/mesh_derived_field_asset_module.h>
#include <engine/assets/schema_ids.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wz::engine::assets
{
    namespace detail_mesh_derived_field_key
    {
        [[nodiscard]] inline uint64_t float_bits(float value) noexcept
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return static_cast<uint64_t>(bits);
        }

        [[nodiscard]] inline uint64_t mix_bytes(
            uint64_t state,
            const std::vector<std::byte>& bytes) noexcept
        {
            state = detail::mix64(state, static_cast<uint64_t>(bytes.size()));
            for (const std::byte value : bytes) {
                state = detail::mix64(
                    state,
                    static_cast<uint64_t>(
                        static_cast<unsigned char>(value)));
            }
            return state;
        }
    }

    [[nodiscard]] inline wz::asset::AssetKey
    make_explicit_mesh_derived_field_key(
        const wz::asset::AssetKey& source_mesh_key,
        const ExplicitMeshDerivedFieldDesc& desc) noexcept
    {
        uint64_t h = kMeshDerivedFieldExplicitSchema.value;
        h = detail::mix64(h, static_cast<uint64_t>(desc.domain));
        h = detail::mix64(h, static_cast<uint64_t>(desc.element_count));
        h = detail::mix64(h, static_cast<uint64_t>(desc.channels.size()));
        for (const MeshDerivedFieldChannelDesc& channel : desc.channels) {
            h = detail::mix64(h, static_cast<uint64_t>(channel.channel_id));
            h = detail::mix64(h, static_cast<uint64_t>(channel.value_type));
            h = detail_mesh_derived_field_key::mix_bytes(h, channel.values);
        }

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(
                kMeshDerivedFieldExplicitSchema.value),
            .compiler_hash = detail::hash_u64(
                kMeshDerivedFieldCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_mesh_key),
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey
    make_behavior_field_placeholder_key(
        const wz::asset::AssetKey& source_mesh_key,
        const BehaviorFieldPlaceholderDesc& desc) noexcept
    {
        uint64_t h = kBehaviorFieldPlaceholderSchema.value;
        h = detail::mix64(h, static_cast<uint64_t>(desc.domain));
        h = detail::mix64(h, static_cast<uint64_t>(desc.channel_id));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(
                kBehaviorFieldPlaceholderSchema.value),
            .compiler_hash = detail::hash_u64(
                kBehaviorFieldPlaceholderCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_mesh_key),
        };
    }

    // Identity: params + output channel layout + declared mesh inputs in
    // content_hash; source mesh + compute pipeline in deps_hash, so editing
    // the kernel HLSL or swapping the mesh rebuilds the field and nothing
    // else does. Channel payload bytes are kernel output, not identity.
    [[nodiscard]] inline wz::asset::AssetKey
    make_mesh_compute_derived_field_key(
        const wz::asset::AssetKey& source_mesh_key,
        const MeshComputeDerivedFieldDesc& desc) noexcept
    {
        uint64_t h = kMeshComputeDerivedFieldSchema.value;
        h = detail::mix64(h, static_cast<uint64_t>(desc.domain));
        h = detail::mix64(h, static_cast<uint64_t>(desc.channels.size()));
        for (const MeshDerivedFieldChannelDesc& channel : desc.channels) {
            h = detail::mix64(h, static_cast<uint64_t>(channel.channel_id));
            h = detail::mix64(h, static_cast<uint64_t>(channel.value_type));
        }
        h = detail::mix64(h, static_cast<uint64_t>(desc.inputs.size()));
        for (const MeshComputeInput input : desc.inputs) {
            h = detail::mix64(h, static_cast<uint64_t>(input));
        }
        h = detail::mix64(
            h,
            static_cast<uint64_t>(desc.root_constants.size()));
        for (const uint32_t constant : desc.root_constants) {
            h = detail::mix64(h, static_cast<uint64_t>(constant));
        }

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(
                kMeshComputeDerivedFieldSchema.value),
            .compiler_hash = detail::hash_u64(
                kMeshComputeDerivedFieldCompilerVersion),
            .deps_hash = detail::combine_dep_hashes(
                detail::key_to_dep_hash(source_mesh_key),
                detail::key_to_dep_hash(desc.compute_pipeline.key)),
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey
    make_mesh_wavelet_analysis_field_key(
        const wz::asset::AssetKey& source_mesh_key,
        const MeshWaveletAnalysisDesc& desc) noexcept
    {
        uint64_t h = kMeshWaveletAnalysisSchema.value;
        h = detail::mix64(h, static_cast<uint64_t>(desc.scale_count));
        h = detail::mix64(
            h,
            detail_mesh_derived_field_key::float_bits(
                desc.lambda_max_estimate));
        h = detail::mix64(
            h,
            detail_mesh_derived_field_key::float_bits(desc.gamma));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kMeshWaveletAnalysisSchema.value),
            .compiler_hash = detail::hash_u64(
                kMeshWaveletAnalysisCompilerVersion),
            .deps_hash = desc.compute_pipeline.valid()
                ? detail::combine_dep_hashes(
                    detail::key_to_dep_hash(source_mesh_key),
                    detail::key_to_dep_hash(desc.compute_pipeline.key))
                : detail::key_to_dep_hash(source_mesh_key),
        };
    }
}
