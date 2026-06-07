#include <engine/assets/terrain/terrain_visual_proxy_compilers.h>

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/disk_cache_keys.h>
#include <engine/assets/disk_cache_paths.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/engine_asset_library_internal.h>

#include <algorithm>
#include <any>
#include <chrono>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr uint32_t kTerrainVisualProxyDiskCacheMagic = 0x56505a57u;
        constexpr uint32_t kTerrainVisualProxyDiskCacheVersion = 1u;

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

        void append_float_array(
            std::vector<uint8_t>& out,
            const float* values,
            uint32_t count)
        {
            for (uint32_t i = 0; i < count; ++i) {
                append_scalar(out, values[i]);
            }
        }

        bool read_float_array(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            float* values,
            uint32_t count)
        {
            for (uint32_t i = 0; i < count; ++i) {
                if (!read_scalar(bytes, offset, values[i])) {
                    return false;
                }
            }
            return true;
        }

        wz::fs::Path terrain_visual_proxy_cache_directory(
            const EngineAssetCacheSettings& cache)
        {
            return disk_cache_asset_directory(
                cache,
                kTerrainVisualProxyDiskCacheKey.subdirectory);
        }

        wz::fs::Path terrain_visual_proxy_cache_path(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key)
        {
            return disk_cache_asset_path(
                cache,
                kTerrainVisualProxyDiskCacheKey.subdirectory,
                key,
                kTerrainVisualProxyDiskCacheKey.seed_lo,
                kTerrainVisualProxyDiskCacheKey.seed_hi);
        }

        TerrainVisualRepresentationKind representation_for_terrain(
            const TerrainAssetData& terrain)
        {
            (void)terrain;
            return TerrainVisualRepresentationKind::MeshChunks;
        }

        TerrainVisualProxyBounds bounds_from_chunk(
            const TerrainVisualChunk& chunk)
        {
            TerrainVisualProxyBounds bounds{};
            for (uint32_t i = 0; i < 3u; ++i) {
                bounds.min[i] = chunk.bounds_min[i];
                bounds.max[i] = chunk.bounds_max[i];
            }
            return bounds;
        }

        TerrainVisualProxyAggregate aggregate_from_chunk(
            const TerrainVisualChunk& chunk)
        {
            TerrainVisualProxyAggregate out{};
            out.mean_height = chunk.aggregate.mean_height;
            out.height_variance = chunk.aggregate.height_variance;
            for (uint32_t i = 0; i < 3u; ++i) {
                out.normal_mean[i] = chunk.aggregate.normal_mean[i];
                out.albedo_mean[i] = chunk.aggregate.albedo_mean[i];
            }
            for (uint32_t i = 0; i < 2u; ++i) {
                out.normal_variance[i] = chunk.aggregate.normal_variance[i];
            }
            return out;
        }

        void compute_vertex_span(
            const TerrainAssetData& terrain,
            const TerrainVisualChunk& chunk,
            uint32_t& first_vertex,
            uint32_t& vertex_count)
        {
            uint32_t min_vertex = std::numeric_limits<uint32_t>::max();
            uint32_t max_vertex = 0u;
            const uint32_t end = chunk.first_index + chunk.index_count;
            for (uint32_t i = chunk.first_index; i < end; ++i) {
                if (i >= terrain.mesh_visual_indices.size()) {
                    continue;
                }
                const uint32_t vertex = terrain.mesh_visual_indices[i];
                min_vertex = std::min(min_vertex, vertex);
                max_vertex = std::max(max_vertex, vertex);
            }
            if (min_vertex == std::numeric_limits<uint32_t>::max()) {
                first_vertex = 0u;
                vertex_count = 0u;
                return;
            }
            first_vertex = min_vertex;
            vertex_count = max_vertex - min_vertex + 1u;
        }

        uint32_t boundary_flags_for_chunk(
            const TerrainVisualProxyBounds& chunk,
            const TerrainVisualProxyBounds& terrain)
        {
            uint32_t flags = TerrainVisualChunkBoundary_None;
            if (chunk.min[0] <= terrain.min[0]) {
                flags |= TerrainVisualChunkBoundary_NegativeX;
            }
            if (chunk.max[0] >= terrain.max[0]) {
                flags |= TerrainVisualChunkBoundary_PositiveX;
            }
            if (chunk.min[2] <= terrain.min[2]) {
                flags |= TerrainVisualChunkBoundary_NegativeZ;
            }
            if (chunk.max[2] >= terrain.max[2]) {
                flags |= TerrainVisualChunkBoundary_PositiveZ;
            }
            return flags;
        }

        TerrainVisualProxyData compile_single_lod_proxy(
            const wz::asset::AssetKey& proxy_key,
            const wz::asset::AssetKey& terrain_key,
            const TerrainAssetData& terrain)
        {
            TerrainVisualProxyData proxy{};
            proxy.schema_version = kTerrainVisualProxySchemaVersion;
            proxy.compiler_version =
                static_cast<uint32_t>(kTerrainVisualProxyCompilerVersion);
            proxy.source_asset_key = terrain_key;
            proxy.simplification_settings_hash = {};
            proxy.terrain_proxy_id = TerrainProxyId{ proxy_key };
            proxy.primary_representation_kind = representation_for_terrain(terrain);
            for (uint32_t i = 0; i < 3u; ++i) {
                proxy.bounds.min[i] = terrain.bounds_min[i];
                proxy.bounds.max[i] = terrain.bounds_max[i];
            }

            proxy.chunks.reserve(terrain.mesh_visual_chunks.size());
            for (uint32_t chunk_index = 0;
                 chunk_index < terrain.mesh_visual_chunks.size();
                 ++chunk_index)
            {
                const TerrainVisualChunk& source =
                    terrain.mesh_visual_chunks[chunk_index];

                TerrainVisualProxyChunkRecord chunk{};
                chunk.chunk_id = TerrainChunkId{ chunk_index };
                chunk.representation_id = TerrainRepresentationId{ 0u };
                chunk.representation_kind = proxy.primary_representation_kind;
                chunk.bounds = bounds_from_chunk(source);
                chunk.first_triangle = source.first_index / 3u;
                chunk.triangle_count = source.triangle_count();
                compute_vertex_span(
                    terrain,
                    source,
                    chunk.first_vertex,
                    chunk.vertex_count);
                chunk.aggregate = aggregate_from_chunk(source);
                chunk.boundary.boundary_flags =
                    boundary_flags_for_chunk(chunk.bounds, proxy.bounds);

                TerrainVisualProxyLodRecord lod{};
                lod.lod_id = TerrainLodId{ 0u };
                lod.representation_id = chunk.representation_id;
                lod.representation_kind = chunk.representation_kind;
                lod.first_index = source.first_index;
                lod.index_count = source.index_count;
                lod.first_vertex = chunk.first_vertex;
                lod.vertex_count = chunk.vertex_count;
                lod.triangle_count = chunk.triangle_count;
                lod.conservative_geometric_error = 0.0f;
                lod.mesh_asset = terrain.mesh;
                lod.source_region_aggregate = chunk.aggregate;
                lod.lod_surface_aggregate = chunk.aggregate;
                chunk.lods.push_back(std::move(lod));

                proxy.chunks.push_back(std::move(chunk));
            }

            return proxy;
        }

        void append_bounds(
            std::vector<uint8_t>& out,
            const TerrainVisualProxyBounds& bounds)
        {
            append_float_array(out, bounds.min, 3u);
            append_float_array(out, bounds.max, 3u);
        }

        bool read_bounds(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            TerrainVisualProxyBounds& bounds)
        {
            return read_float_array(bytes, offset, bounds.min, 3u)
                && read_float_array(bytes, offset, bounds.max, 3u);
        }

        void append_aggregate(
            std::vector<uint8_t>& out,
            const TerrainVisualProxyAggregate& aggregate)
        {
            append_scalar(out, aggregate.mean_height);
            append_scalar(out, aggregate.height_variance);
            append_float_array(out, aggregate.normal_mean, 3u);
            append_float_array(out, aggregate.normal_variance, 2u);
            append_float_array(out, aggregate.albedo_mean, 3u);
            append_scalar(
                out,
                static_cast<uint64_t>(aggregate.material_coverage.size()));
            for (const auto& coverage : aggregate.material_coverage) {
                append_scalar(out, coverage.material_id);
                append_scalar(out, coverage.coverage);
            }
        }

        bool read_aggregate(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            TerrainVisualProxyAggregate& aggregate)
        {
            uint64_t material_count = 0u;
            if (!read_scalar(bytes, offset, aggregate.mean_height)
                || !read_scalar(bytes, offset, aggregate.height_variance)
                || !read_float_array(bytes, offset, aggregate.normal_mean, 3u)
                || !read_float_array(bytes, offset, aggregate.normal_variance, 2u)
                || !read_float_array(bytes, offset, aggregate.albedo_mean, 3u)
                || !read_scalar(bytes, offset, material_count))
            {
                return false;
            }
            if (material_count > 1024u) {
                return false;
            }
            aggregate.material_coverage.resize(static_cast<size_t>(material_count));
            for (auto& coverage : aggregate.material_coverage) {
                if (!read_scalar(bytes, offset, coverage.material_id)
                    || !read_scalar(bytes, offset, coverage.coverage))
                {
                    return false;
                }
            }
            return true;
        }

        void append_lod(
            std::vector<uint8_t>& out,
            const TerrainVisualProxyLodRecord& lod)
        {
            append_scalar(out, lod.lod_id.value);
            append_scalar(out, lod.representation_id.value);
            append_scalar(out, static_cast<uint8_t>(lod.representation_kind));
            append_scalar(out, lod.first_index);
            append_scalar(out, lod.index_count);
            append_scalar(out, lod.first_vertex);
            append_scalar(out, lod.vertex_count);
            append_scalar(out, lod.triangle_count);
            append_scalar(out, lod.conservative_geometric_error);
            append_asset_key(out, lod.mesh_asset);
            append_aggregate(out, lod.source_region_aggregate);
            append_aggregate(out, lod.lod_surface_aggregate);
        }

        bool read_lod(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            TerrainVisualProxyLodRecord& lod)
        {
            uint8_t kind = 0u;
            if (!read_scalar(bytes, offset, lod.lod_id.value)
                || !read_scalar(bytes, offset, lod.representation_id.value)
                || !read_scalar(bytes, offset, kind)
                || !read_scalar(bytes, offset, lod.first_index)
                || !read_scalar(bytes, offset, lod.index_count)
                || !read_scalar(bytes, offset, lod.first_vertex)
                || !read_scalar(bytes, offset, lod.vertex_count)
                || !read_scalar(bytes, offset, lod.triangle_count)
                || !read_scalar(bytes, offset, lod.conservative_geometric_error)
                || !read_asset_key(bytes, offset, lod.mesh_asset)
                || !read_aggregate(bytes, offset, lod.source_region_aggregate)
                || !read_aggregate(bytes, offset, lod.lod_surface_aggregate))
            {
                return false;
            }
            lod.representation_kind =
                static_cast<TerrainVisualRepresentationKind>(kind);
            return true;
        }

        std::vector<uint8_t> serialize_terrain_visual_proxy(
            const wz::asset::AssetKey& key,
            const TerrainVisualProxyData& data)
        {
            std::vector<uint8_t> out;
            append_scalar(out, kTerrainVisualProxyDiskCacheMagic);
            append_scalar(out, kTerrainVisualProxyDiskCacheVersion);
            append_scalar(out, kTerrainVisualProxyCompilerVersion);
            append_asset_key(out, key);
            append_scalar(out, data.schema_version);
            append_scalar(out, data.compiler_version);
            append_asset_key(out, data.source_asset_key);
            append_hash(out, data.simplification_settings_hash);
            append_asset_key(out, data.terrain_proxy_id.key);
            append_scalar(out, static_cast<uint8_t>(data.primary_representation_kind));
            append_bounds(out, data.bounds);
            append_scalar(out, static_cast<uint64_t>(data.chunks.size()));
            for (const auto& chunk : data.chunks) {
                append_scalar(out, chunk.chunk_id.value);
                append_scalar(out, chunk.representation_id.value);
                append_scalar(out, static_cast<uint8_t>(chunk.representation_kind));
                append_bounds(out, chunk.bounds);
                append_scalar(out, chunk.first_triangle);
                append_scalar(out, chunk.triangle_count);
                append_scalar(out, chunk.first_vertex);
                append_scalar(out, chunk.vertex_count);
                append_aggregate(out, chunk.aggregate);
                append_scalar(out, chunk.boundary.boundary_flags);
                append_scalar(out, chunk.boundary.negative_x_neighbor.value);
                append_scalar(out, chunk.boundary.positive_x_neighbor.value);
                append_scalar(out, chunk.boundary.negative_z_neighbor.value);
                append_scalar(out, chunk.boundary.positive_z_neighbor.value);
                append_scalar(out, static_cast<uint64_t>(chunk.lods.size()));
                for (const auto& lod : chunk.lods) {
                    append_lod(out, lod);
                }
            }
            return out;
        }

        bool deserialize_terrain_visual_proxy(
            const std::vector<uint8_t>& bytes,
            const wz::asset::AssetKey& expected_key,
            TerrainVisualProxyData& data)
        {
            size_t offset = 0u;
            uint32_t magic = 0u;
            uint32_t version = 0u;
            uint64_t compiler_version = 0u;
            wz::asset::AssetKey stored_key{};
            uint8_t primary_kind = 0u;
            uint64_t chunk_count = 0u;
            if (!read_scalar(bytes, offset, magic)
                || !read_scalar(bytes, offset, version)
                || !read_scalar(bytes, offset, compiler_version)
                || !read_asset_key(bytes, offset, stored_key)
                || magic != kTerrainVisualProxyDiskCacheMagic
                || version != kTerrainVisualProxyDiskCacheVersion
                || compiler_version != kTerrainVisualProxyCompilerVersion
                || !(stored_key == expected_key)
                || !read_scalar(bytes, offset, data.schema_version)
                || !read_scalar(bytes, offset, data.compiler_version)
                || !read_asset_key(bytes, offset, data.source_asset_key)
                || !read_hash(bytes, offset, data.simplification_settings_hash)
                || !read_asset_key(bytes, offset, data.terrain_proxy_id.key)
                || !read_scalar(bytes, offset, primary_kind)
                || !read_bounds(bytes, offset, data.bounds)
                || !read_scalar(bytes, offset, chunk_count))
            {
                return false;
            }
            if (chunk_count == 0u || chunk_count > 100000u) {
                return false;
            }
            data.primary_representation_kind =
                static_cast<TerrainVisualRepresentationKind>(primary_kind);
            data.chunks.resize(static_cast<size_t>(chunk_count));
            for (auto& chunk : data.chunks) {
                uint8_t chunk_kind = 0u;
                uint64_t lod_count = 0u;
                if (!read_scalar(bytes, offset, chunk.chunk_id.value)
                    || !read_scalar(bytes, offset, chunk.representation_id.value)
                    || !read_scalar(bytes, offset, chunk_kind)
                    || !read_bounds(bytes, offset, chunk.bounds)
                    || !read_scalar(bytes, offset, chunk.first_triangle)
                    || !read_scalar(bytes, offset, chunk.triangle_count)
                    || !read_scalar(bytes, offset, chunk.first_vertex)
                    || !read_scalar(bytes, offset, chunk.vertex_count)
                    || !read_aggregate(bytes, offset, chunk.aggregate)
                    || !read_scalar(bytes, offset, chunk.boundary.boundary_flags)
                    || !read_scalar(bytes, offset, chunk.boundary.negative_x_neighbor.value)
                    || !read_scalar(bytes, offset, chunk.boundary.positive_x_neighbor.value)
                    || !read_scalar(bytes, offset, chunk.boundary.negative_z_neighbor.value)
                    || !read_scalar(bytes, offset, chunk.boundary.positive_z_neighbor.value)
                    || !read_scalar(bytes, offset, lod_count))
                {
                    return false;
                }
                if (lod_count == 0u || lod_count > 1024u) {
                    return false;
                }
                chunk.representation_kind =
                    static_cast<TerrainVisualRepresentationKind>(chunk_kind);
                chunk.lods.resize(static_cast<size_t>(lod_count));
                for (auto& lod : chunk.lods) {
                    if (!read_lod(bytes, offset, lod)) {
                        return false;
                    }
                }
            }
            return offset == bytes.size();
        }

        bool load_cached_terrain_visual_proxy_impl(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key,
            wz::Logger& logger,
            TerrainVisualProxyData& data)
        {
            if (!cache.enabled || cache.root.empty()) {
                return false;
            }
            const wz::fs::Path path = terrain_visual_proxy_cache_path(cache, key);
            const auto started = std::chrono::steady_clock::now();
            const auto bytes = wz::fs::read_file(path);
            if (!bytes) {
                logger.info("asset disk cache miss: terrain visual proxy " + path);
                return false;
            }
            TerrainVisualProxyData loaded{};
            if (!deserialize_terrain_visual_proxy(bytes.value, key, loaded)
                || !loaded.valid())
            {
                logger.warn(
                    "asset disk cache ignored invalid terrain visual proxy: "
                    + path);
                return false;
            }
            data = std::move(loaded);
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
            logger.info(
                "asset disk cache hit: terrain visual proxy "
                + path
                + " ms="
                + std::to_string(elapsed));
            return true;
        }

        void store_cached_terrain_visual_proxy(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key,
            const TerrainVisualProxyData& data,
            wz::Logger& logger)
        {
            if (!cache.enabled || cache.root.empty() || !data.valid()) {
                return;
            }
            const wz::fs::Path directory =
                terrain_visual_proxy_cache_directory(cache);
            if (wz::fs::create_directories(directory)
                != wz::fs::FileError::None)
            {
                logger.warn(
                    "asset disk cache directory unavailable: " + directory);
                return;
            }
            const wz::fs::Path path = terrain_visual_proxy_cache_path(cache, key);
            const std::vector<uint8_t> bytes =
                serialize_terrain_visual_proxy(key, data);
            const wz::fs::FileError err =
                wz::fs::write_file(path, bytes, true);
            if (err != wz::fs::FileError::None) {
                logger.warn(
                    "asset disk cache write failed: terrain visual proxy "
                    + path
                    + " error="
                    + std::to_string(static_cast<int>(err)));
                return;
            }
            logger.info(
                "asset disk cache stored: terrain visual proxy "
                + path
                + " bytes="
                + std::to_string(bytes.size()));
        }

        wz::asset::AssetNode compiled_proxy_node(
            const wz::asset::AssetNode& input,
            wz::asset::ResourceHandle handle)
        {
            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }
    }

    void register_terrain_visual_proxy_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        TerrainAssetTable& terrain_table,
        TerrainVisualProxyTable& terrain_visual_proxy_table,
        const EngineAssetCacheSettings& cache_settings)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kTerrainVisualProxySchema,
            .output_type = kAssetTypeTerrainVisualProxy,
            .compile = [&logger, &terrain_table, &terrain_visual_proxy_table, cache_settings](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                TerrainVisualProxyData cached{};
                if (load_cached_terrain_visual_proxy_impl(
                        cache_settings,
                        input.key,
                        logger,
                        cached))
                {
                    wz::asset::ResourceHandle handle =
                        terrain_visual_proxy_table.add(std::move(cached));
                    if (!handle.valid()) {
                        logger.error("failed to store cached terrain visual proxy");
                        return compile_failed_node(input);
                    }
                    return compiled_proxy_node(input, handle);
                }

                if (dep_handles.size() != 1u) {
                    logger.error("terrain visual proxy requires one terrain dependency");
                    return compile_failed_node(input);
                }

                const TerrainAssetData* terrain = terrain_table.get(dep_handles[0]);
                if (!terrain || !terrain->valid()
                    || !terrain->supports_render_mesh
                    || terrain->representation != TerrainRepresentationKind::MeshSurface
                    || terrain->mesh_visual_chunks.empty())
                {
                    logger.error("terrain visual proxy source terrain is invalid");
                    return compile_failed_node(input);
                }

                const wz::asset::AssetKey* terrain_key =
                    std::any_cast<wz::asset::AssetKey>(&input.meta);
                if (!terrain_key || *terrain_key == wz::asset::AssetKey{}) {
                    logger.error("terrain visual proxy missing source terrain key");
                    return compile_failed_node(input);
                }

                TerrainVisualProxyData proxy =
                    compile_single_lod_proxy(input.key, *terrain_key, *terrain);
                if (!proxy.valid()) {
                    logger.error("compiled terrain visual proxy is invalid");
                    return compile_failed_node(input);
                }

                store_cached_terrain_visual_proxy(
                    cache_settings,
                    input.key,
                    proxy,
                    logger);

                wz::asset::ResourceHandle handle =
                    terrain_visual_proxy_table.add(std::move(proxy));
                if (!handle.valid()) {
                    logger.error("failed to store terrain visual proxy");
                    return compile_failed_node(input);
                }
                return compiled_proxy_node(input, handle);
            },
        });
    }

    bool load_cached_terrain_visual_proxy(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        TerrainVisualProxyData& data)
    {
        return load_cached_terrain_visual_proxy_impl(cache, key, logger, data);
    }
}
