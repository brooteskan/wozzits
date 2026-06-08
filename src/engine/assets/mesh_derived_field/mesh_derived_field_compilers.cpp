// src/engine/assets/mesh_derived_field/mesh_derived_field_compilers.cpp

#include <engine/assets/mesh_derived_field/mesh_derived_field_compilers.h>

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/disk_cache_keys.h>
#include <engine/assets/disk_cache_paths.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/mesh_derived_field_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <file/filesystem.h>

#include <algorithm>
#include <any>
#include <chrono>
#include <cstring>
#include <sstream>
#include <string>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr uint32_t kMeshDerivedFieldDiskCacheMagic = 0x4d445a57u;
        constexpr uint32_t kMeshDerivedFieldDiskCacheVersion = 1u;

        template<typename T>
        void append_scalar(std::vector<uint8_t>& out, const T& value)
        {
            const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
            out.insert(out.end(), bytes, bytes + sizeof(T));
        }

        template<typename T>
        bool read_scalar(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            T& out)
        {
            if (offset + sizeof(T) > bytes.size()) {
                return false;
            }
            std::memcpy(&out, bytes.data() + offset, sizeof(T));
            offset += sizeof(T);
            return true;
        }

        void append_raw_bytes(
            std::vector<uint8_t>& out,
            const void* data,
            size_t byte_count)
        {
            if (byte_count == 0u) {
                return;
            }
            const auto* first = static_cast<const uint8_t*>(data);
            out.insert(out.end(), first, first + byte_count);
        }

        bool read_raw_bytes(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            void* out,
            size_t byte_count)
        {
            if (byte_count == 0u) {
                return true;
            }
            if (offset + byte_count > bytes.size()) {
                return false;
            }
            std::memcpy(out, bytes.data() + offset, byte_count);
            offset += byte_count;
            return true;
        }

        void append_asset_key(
            std::vector<uint8_t>& out,
            const wz::asset::AssetKey& key)
        {
            append_scalar(out, key.content_hash.lo);
            append_scalar(out, key.content_hash.hi);
            append_scalar(out, key.schema_hash.lo);
            append_scalar(out, key.schema_hash.hi);
            append_scalar(out, key.compiler_hash.lo);
            append_scalar(out, key.compiler_hash.hi);
            append_scalar(out, key.deps_hash.lo);
            append_scalar(out, key.deps_hash.hi);
        }

        bool read_asset_key(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            wz::asset::AssetKey& key)
        {
            return read_scalar(bytes, offset, key.content_hash.lo)
                && read_scalar(bytes, offset, key.content_hash.hi)
                && read_scalar(bytes, offset, key.schema_hash.lo)
                && read_scalar(bytes, offset, key.schema_hash.hi)
                && read_scalar(bytes, offset, key.compiler_hash.lo)
                && read_scalar(bytes, offset, key.compiler_hash.hi)
                && read_scalar(bytes, offset, key.deps_hash.lo)
                && read_scalar(bytes, offset, key.deps_hash.hi);
        }

        void append_hash(std::vector<uint8_t>& out, const wz::asset::Hash& hash)
        {
            append_scalar(out, hash.lo);
            append_scalar(out, hash.hi);
        }

        bool read_hash(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            wz::asset::Hash& hash)
        {
            return read_scalar(bytes, offset, hash.lo)
                && read_scalar(bytes, offset, hash.hi);
        }

        wz::fs::Path mesh_derived_field_cache_path(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key)
        {
            return disk_cache_asset_path(
                cache,
                kMeshDerivedFieldDiskCacheKey.subdirectory,
                key,
                kMeshDerivedFieldDiskCacheKey.seed_lo,
                kMeshDerivedFieldDiskCacheKey.seed_hi);
        }

        std::vector<uint8_t> serialize_mesh_derived_field_asset(
            const wz::asset::AssetKey& key,
            const MeshDerivedFieldData& field)
        {
            std::vector<uint8_t> out;
            out.reserve(160u + field.values.size()
                + field.channels.size() * sizeof(MeshDerivedFieldChannel));
            append_scalar(out, kMeshDerivedFieldDiskCacheMagic);
            append_scalar(out, kMeshDerivedFieldDiskCacheVersion);
            append_scalar(out, kMeshDerivedFieldCompilerVersion);
            append_asset_key(out, key);
            append_asset_key(out, field.source_mesh_key);
            append_hash(out, field.source_topology_hash);
            append_scalar(out, static_cast<uint8_t>(field.domain));
            append_scalar(out, field.element_count);
            append_scalar(out, static_cast<uint64_t>(field.channels.size()));
            for (const MeshDerivedFieldChannel& channel : field.channels) {
                append_scalar(out, channel.channel_id);
                append_scalar(out, static_cast<uint8_t>(channel.value_type));
                append_scalar(out, channel.byte_offset);
                append_scalar(out, channel.byte_count);
            }
            append_scalar(out, static_cast<uint64_t>(field.values.size()));
            append_raw_bytes(out, field.values.data(), field.values.size());
            return out;
        }

        bool deserialize_mesh_derived_field_asset(
            const std::vector<uint8_t>& bytes,
            const wz::asset::AssetKey& expected_key,
            MeshDerivedFieldData& field)
        {
            size_t offset = 0;
            uint32_t magic = 0;
            uint32_t version = 0;
            uint64_t compiler_version = 0;
            if (!read_scalar(bytes, offset, magic)
                || !read_scalar(bytes, offset, version)
                || !read_scalar(bytes, offset, compiler_version)
                || magic != kMeshDerivedFieldDiskCacheMagic
                || version != kMeshDerivedFieldDiskCacheVersion
                || compiler_version != kMeshDerivedFieldCompilerVersion)
            {
                return false;
            }

            wz::asset::AssetKey stored_key{};
            uint8_t domain = 0;
            if (!read_asset_key(bytes, offset, stored_key)
                || stored_key != expected_key
                || !read_asset_key(bytes, offset, field.source_mesh_key)
                || !read_hash(bytes, offset, field.source_topology_hash)
                || !read_scalar(bytes, offset, domain)
                || !read_scalar(bytes, offset, field.element_count))
            {
                return false;
            }
            field.domain = static_cast<MeshDerivedFieldDomain>(domain);

            uint64_t channel_count = 0;
            if (!read_scalar(bytes, offset, channel_count)) {
                return false;
            }
            const uint64_t remaining_for_channels =
                static_cast<uint64_t>(bytes.size() - offset);
            if (channel_count > remaining_for_channels / 13u) {
                return false;
            }

            field.channels.resize(static_cast<size_t>(channel_count));
            for (MeshDerivedFieldChannel& channel : field.channels) {
                uint8_t value_type = 0;
                if (!read_scalar(bytes, offset, channel.channel_id)
                    || !read_scalar(bytes, offset, value_type)
                    || !read_scalar(bytes, offset, channel.byte_offset)
                    || !read_scalar(bytes, offset, channel.byte_count))
                {
                    return false;
                }
                channel.value_type =
                    static_cast<MeshDerivedFieldValueType>(value_type);
            }

            uint64_t value_count = 0;
            if (!read_scalar(bytes, offset, value_count)
                || value_count > bytes.size() - offset)
            {
                return false;
            }
            field.values.resize(static_cast<size_t>(value_count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    field.values.data(),
                    field.values.size()))
            {
                return false;
            }
            return offset == bytes.size() && field.valid();
        }

        bool store_cached_mesh_derived_field(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key,
            const MeshDerivedFieldData& field,
            wz::Logger& logger)
        {
            if (!cache.enabled || cache.root.empty()) {
                return false;
            }

            const wz::fs::Path dir = disk_cache_asset_directory(
                cache,
                kMeshDerivedFieldDiskCacheKey.subdirectory);
            const wz::fs::FileError dir_err = wz::fs::create_directories(dir);
            if (dir_err != wz::fs::FileError::None) {
                return false;
            }

            const wz::fs::Path path =
                mesh_derived_field_cache_path(cache, key);
            const std::vector<uint8_t> bytes =
                serialize_mesh_derived_field_asset(key, field);
            const wz::fs::FileError write_err =
                wz::fs::write_file(path, bytes);
            if (write_err != wz::fs::FileError::None) {
                logger.warn(
                    "asset disk cache store failed: mesh derived field "
                    + path);
                return false;
            }
            logger.info(
                "asset disk cache stored: mesh derived field " + path);
            return true;
        }

        wz::asset::AssetNode compiled_field_node(
            const wz::asset::AssetNode& input,
            wz::asset::ResourceHandle handle)
        {
            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }

        bool has_duplicate_channel_ids(
            const std::vector<MeshDerivedFieldChannelDesc>& channels)
        {
            std::vector<uint32_t> ids;
            ids.reserve(channels.size());
            for (const MeshDerivedFieldChannelDesc& channel : channels) {
                ids.push_back(channel.channel_id);
            }
            std::sort(ids.begin(), ids.end());
            return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
        }

        bool pack_explicit_field(
            const wz::asset::AssetKey& input_key,
            const ExplicitMeshDerivedFieldDesc& desc,
            const MeshData& source_mesh,
            wz::Logger& logger,
            MeshDerivedFieldData& out)
        {
            if (!source_mesh.valid()) {
                logger.error("mesh derived field source mesh is invalid");
                return false;
            }
            if (desc.source_mesh.output == wz::asset::AssetKey{}
                || desc.element_count == 0u
                || desc.channels.empty()
                || has_duplicate_channel_ids(desc.channels))
            {
                logger.error("mesh derived field explicit desc is invalid");
                return false;
            }

            const uint32_t expected_count =
                mesh_domain_element_count(source_mesh, desc.domain);
            if (desc.element_count != expected_count) {
                std::ostringstream msg;
                msg
                    << "mesh derived field element_count mismatch"
                    << " expected=" << expected_count
                    << " actual=" << desc.element_count;
                logger.error(msg.str());
                return false;
            }

            out = MeshDerivedFieldData{
                .source_mesh_key = desc.source_mesh.output,
                .source_topology_hash =
                    compute_mesh_topology_hash(source_mesh),
                .domain = desc.domain,
                .element_count = desc.element_count,
            };

            uint64_t total_bytes = 0;
            for (const MeshDerivedFieldChannelDesc& channel_desc :
                desc.channels)
            {
                const uint32_t stride =
                    mesh_derived_field_value_stride(channel_desc.value_type);
                const uint64_t expected_bytes =
                    static_cast<uint64_t>(desc.element_count) * stride;
                if (channel_desc.channel_id == 0u
                    || stride == 0u
                    || channel_desc.values.size() != expected_bytes
                    || expected_bytes > UINT32_MAX
                    || total_bytes > UINT32_MAX)
                {
                    logger.error(
                        "mesh derived field channel payload is invalid");
                    return false;
                }

                out.channels.push_back(MeshDerivedFieldChannel{
                    .channel_id = channel_desc.channel_id,
                    .value_type = channel_desc.value_type,
                    .byte_offset = static_cast<uint32_t>(total_bytes),
                    .byte_count = static_cast<uint32_t>(expected_bytes),
                });
                out.values.insert(
                    out.values.end(),
                    channel_desc.values.begin(),
                    channel_desc.values.end());
                total_bytes += expected_bytes;
            }

            if (!out.valid()) {
                logger.error("mesh derived field packed data is invalid");
                return false;
            }

            (void)input_key;
            return true;
        }

        bool cached_field_matches_source(
            const MeshDerivedFieldData& field,
            const ExplicitMeshDerivedFieldDesc& desc,
            const MeshData& source_mesh) noexcept
        {
            return field.valid()
                && field.source_mesh_key == desc.source_mesh.output
                && field.source_topology_hash
                    == compute_mesh_topology_hash(source_mesh)
                && field.domain == desc.domain
                && field.element_count == desc.element_count;
        }

        wz::asset::AssetNode compile_explicit_mesh_derived_field_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshDerivedFieldTable& field_table,
            const EngineAssetCacheSettings& cache_settings)
        {
            const auto* desc =
                std::any_cast<ExplicitMeshDerivedFieldDesc>(&input.meta);
            if (!desc || dep_handles.size() != 1u) {
                logger.error("mesh derived field node missing explicit desc");
                return compile_failed_node(input);
            }

            const MeshData* source_mesh = mesh_table.get(dep_handles[0]);
            if (!source_mesh) {
                logger.error("mesh derived field source mesh handle invalid");
                return compile_failed_node(input);
            }

            MeshDerivedFieldData cached{};
            if (load_cached_mesh_derived_field(
                    cache_settings,
                    input.key,
                    logger,
                    cached))
            {
                if (cached_field_matches_source(cached, *desc, *source_mesh)) {
                    const wz::asset::ResourceHandle handle =
                        field_table.add(std::move(cached));
                    return handle.valid()
                        ? compiled_field_node(input, handle)
                        : compile_failed_node(input);
                }
                logger.warn(
                    "asset disk cache ignored stale mesh derived field");
            }

            MeshDerivedFieldData field{};
            if (!pack_explicit_field(
                    input.key,
                    *desc,
                    *source_mesh,
                    logger,
                    field))
            {
                return compile_failed_node(input);
            }

            store_cached_mesh_derived_field(
                cache_settings,
                input.key,
                field,
                logger);

            const wz::asset::ResourceHandle handle =
                field_table.add(std::move(field));
            return handle.valid()
                ? compiled_field_node(input, handle)
                : compile_failed_node(input);
        }
    }

    void register_mesh_derived_field_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshTable& mesh_table,
        MeshDerivedFieldTable& mesh_derived_field_table,
        const EngineAssetCacheSettings& cache_settings)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshDerivedFieldExplicitSchema,
            .output_type = kAssetTypeMeshDerivedField,
            .compile = [
                &logger,
                &mesh_table,
                &mesh_derived_field_table,
                cache_settings](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode>,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_explicit_mesh_derived_field_node(
                    input,
                    dep_handles,
                    logger,
                    mesh_table,
                    mesh_derived_field_table,
                    cache_settings);
            }
        });
    }

    bool load_cached_mesh_derived_field(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        MeshDerivedFieldData& field)
    {
        if (!cache.enabled || cache.root.empty()) {
            return false;
        }

        const wz::fs::Path path =
            mesh_derived_field_cache_path(cache, key);
        const auto started = std::chrono::steady_clock::now();
        const auto bytes = wz::fs::read_file(path);
        if (!bytes) {
            logger.info(
                "asset disk cache miss: mesh derived field " + path);
            return false;
        }

        MeshDerivedFieldData loaded{};
        if (!deserialize_mesh_derived_field_asset(bytes.value, key, loaded)) {
            logger.warn(
                "asset disk cache ignored invalid mesh derived field: "
                + path);
            return false;
        }

        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
        field = std::move(loaded);
        logger.info(
            "asset disk cache hit: mesh derived field "
            + path
            + " ms="
            + std::to_string(elapsed));
        return true;
    }
}
