// src/engine/assets/terrain/terrain_compilers.cpp

#include <engine/assets/terrain/terrain_compilers.h>

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <any>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <span>
#include <string>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr uint32_t kMeshTerrainDiskCacheMagic = 0x54435a57u;
        constexpr uint32_t kMeshTerrainDiskCacheVersion = 1u;

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

        uint64_t mix_cache_key_word(uint64_t state, uint64_t word)
        {
            state ^= word + 0x9e3779b97f4a7c15ull + (state << 6) + (state >> 2);
            state ^= state >> 30;
            state *= 0xbf58476d1ce4e5b9ull;
            state ^= state >> 27;
            state *= 0x94d049bb133111ebull;
            return state ^ (state >> 31);
        }

        std::string short_asset_key_hex(const wz::asset::AssetKey& key)
        {
            const uint64_t words[]{
                key.content_hash.lo,
                key.content_hash.hi,
                key.schema_hash.lo,
                key.schema_hash.hi,
                key.compiler_hash.lo,
                key.compiler_hash.hi,
                key.deps_hash.lo,
                key.deps_hash.hi,
            };

            uint64_t lo = 0xa4093822299f31d0ull;
            uint64_t hi = 0x082efa98ec4e6c89ull;
            for (uint64_t word : words) {
                lo = mix_cache_key_word(lo, word);
                hi = mix_cache_key_word(hi, word ^ lo);
            }

            std::ostringstream out;
            out << std::hex << std::setfill('0')
                << std::setw(16) << lo
                << std::setw(16) << hi;
            return out.str();
        }

        wz::fs::Path mesh_terrain_cache_directory(
            const EngineAssetCacheSettings& cache)
        {
            return wz::fs::join(
                wz::fs::join(cache.root, "assets"),
                "mesh_terrain");
        }

        wz::fs::Path mesh_terrain_cache_path(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key)
        {
            return wz::fs::join(
                mesh_terrain_cache_directory(cache),
                short_asset_key_hex(key) + ".bin");
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

        template<typename T>
        void append_vector_count(std::vector<uint8_t>& out, const std::vector<T>& v)
        {
            append_scalar(out, static_cast<uint64_t>(v.size()));
        }

        bool read_vector_count(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            uint64_t& count,
            uint64_t min_bytes_per_entry)
        {
            if (!read_scalar(bytes, offset, count)) {
                return false;
            }
            if (min_bytes_per_entry == 0u) {
                return true;
            }
            const uint64_t remaining =
                static_cast<uint64_t>(bytes.size() - offset);
            return count <= remaining / min_bytes_per_entry;
        }

        std::vector<uint8_t> serialize_mesh_terrain_asset(
            const wz::asset::AssetKey& key,
            const TerrainAssetData& data)
        {
            std::vector<uint8_t> out;
            out.reserve(
                512u
                + data.height_samples.size() * sizeof(float)
                + data.mesh_surface_points.size() * sizeof(float)
                + data.mesh_surface_indices.size() * sizeof(uint32_t)
                + data.mesh_visual_indices.size() * sizeof(uint32_t)
                + data.mesh_visual_chunks.size() * 96u);

            append_scalar(out, kMeshTerrainDiskCacheMagic);
            append_scalar(out, kMeshTerrainDiskCacheVersion);
            append_scalar(out, kTerrainCompilerVersion);
            append_asset_key(out, key);
            append_scalar(out, static_cast<uint8_t>(data.representation));
            append_asset_key(out, data.source_asset);
            append_asset_key(out, data.height_field);
            append_asset_key(out, data.mesh);
            append_asset_key(out, data.normal_field);
            append_asset_key(out, data.material_mask_set);
            append_scalar(out, static_cast<uint8_t>(data.mesh_height_policy));
            append_scalar(out, data.min_surface_normal_y);
            append_scalar(out, static_cast<uint8_t>(data.include_backfaces));
            append_scalar(out, static_cast<uint8_t>(data.normal_source));
            append_scalar(out, static_cast<uint8_t>(data.uv_source));
            append_scalar(out, static_cast<uint8_t>(data.mesh_has_source_normals));
            append_scalar(out, static_cast<uint8_t>(data.mesh_has_source_uv0));
            append_scalar(out, data.mesh_triangle_count);
            append_scalar(out, data.mesh_accepted_surface_triangle_count);
            append_scalar(out, data.mesh_visual_chunk_count);
            append_float_array(out, data.origin, 2);
            append_float_array(out, data.size, 2);
            append_scalar(out, data.resolution_x);
            append_scalar(out, data.resolution_y);
            append_scalar(out, data.vertical_scale);
            append_scalar(out, data.base_height);
            append_scalar(out, data.min_height);
            append_scalar(out, data.max_height);
            append_float_array(out, data.bounds_min, 3);
            append_float_array(out, data.bounds_max, 3);
            append_scalar(out, static_cast<uint8_t>(data.render_mode));
            append_scalar(out, static_cast<uint8_t>(data.collision_mode));
            append_scalar(out, static_cast<uint8_t>(data.supports_height_query));
            append_scalar(out, static_cast<uint8_t>(data.supports_ray_query));
            append_scalar(out, static_cast<uint8_t>(data.supports_render_mesh));

            append_vector_count(out, data.height_samples);
            append_raw_bytes(
                out,
                data.height_samples.data(),
                data.height_samples.size() * sizeof(float));
            append_vector_count(out, data.mesh_surface_points);
            append_raw_bytes(
                out,
                data.mesh_surface_points.data(),
                data.mesh_surface_points.size() * sizeof(float));
            append_vector_count(out, data.mesh_surface_indices);
            append_raw_bytes(
                out,
                data.mesh_surface_indices.data(),
                data.mesh_surface_indices.size() * sizeof(uint32_t));
            append_vector_count(out, data.mesh_visual_indices);
            append_raw_bytes(
                out,
                data.mesh_visual_indices.data(),
                data.mesh_visual_indices.size() * sizeof(uint32_t));
            append_vector_count(out, data.mesh_visual_chunks);
            append_raw_bytes(
                out,
                data.mesh_visual_chunks.data(),
                data.mesh_visual_chunks.size() * sizeof(TerrainVisualChunk));

            return out;
        }

        bool deserialize_mesh_terrain_asset(
            const std::vector<uint8_t>& bytes,
            const wz::asset::AssetKey& expected_key,
            TerrainAssetData& data)
        {
            size_t offset = 0;
            uint32_t magic = 0;
            uint32_t version = 0;
            uint64_t compiler_version = 0;
            if (!read_scalar(bytes, offset, magic)
                || !read_scalar(bytes, offset, version)
                || !read_scalar(bytes, offset, compiler_version)
                || magic != kMeshTerrainDiskCacheMagic
                || version != kMeshTerrainDiskCacheVersion
                || compiler_version != kTerrainCompilerVersion)
            {
                return false;
            }

            wz::asset::AssetKey stored_key{};
            uint8_t representation = 0;
            uint8_t mesh_height_policy = 0;
            uint8_t include_backfaces = 0;
            uint8_t normal_source = 0;
            uint8_t uv_source = 0;
            uint8_t mesh_has_source_normals = 0;
            uint8_t mesh_has_source_uv0 = 0;
            uint8_t render_mode = 0;
            uint8_t collision_mode = 0;
            uint8_t supports_height_query = 0;
            uint8_t supports_ray_query = 0;
            uint8_t supports_render_mesh = 0;

            if (!read_asset_key(bytes, offset, stored_key)
                || stored_key != expected_key
                || !read_scalar(bytes, offset, representation)
                || !read_asset_key(bytes, offset, data.source_asset)
                || !read_asset_key(bytes, offset, data.height_field)
                || !read_asset_key(bytes, offset, data.mesh)
                || !read_asset_key(bytes, offset, data.normal_field)
                || !read_asset_key(bytes, offset, data.material_mask_set)
                || !read_scalar(bytes, offset, mesh_height_policy)
                || !read_scalar(bytes, offset, data.min_surface_normal_y)
                || !read_scalar(bytes, offset, include_backfaces)
                || !read_scalar(bytes, offset, normal_source)
                || !read_scalar(bytes, offset, uv_source)
                || !read_scalar(bytes, offset, mesh_has_source_normals)
                || !read_scalar(bytes, offset, mesh_has_source_uv0)
                || !read_scalar(bytes, offset, data.mesh_triangle_count)
                || !read_scalar(bytes, offset, data.mesh_accepted_surface_triangle_count)
                || !read_scalar(bytes, offset, data.mesh_visual_chunk_count)
                || !read_float_array(bytes, offset, data.origin, 2)
                || !read_float_array(bytes, offset, data.size, 2)
                || !read_scalar(bytes, offset, data.resolution_x)
                || !read_scalar(bytes, offset, data.resolution_y)
                || !read_scalar(bytes, offset, data.vertical_scale)
                || !read_scalar(bytes, offset, data.base_height)
                || !read_scalar(bytes, offset, data.min_height)
                || !read_scalar(bytes, offset, data.max_height)
                || !read_float_array(bytes, offset, data.bounds_min, 3)
                || !read_float_array(bytes, offset, data.bounds_max, 3)
                || !read_scalar(bytes, offset, render_mode)
                || !read_scalar(bytes, offset, collision_mode)
                || !read_scalar(bytes, offset, supports_height_query)
                || !read_scalar(bytes, offset, supports_ray_query)
                || !read_scalar(bytes, offset, supports_render_mesh))
            {
                return false;
            }

            data.representation =
                static_cast<TerrainRepresentationKind>(representation);
            data.mesh_height_policy =
                static_cast<TerrainMeshSurfaceHeightPolicy>(mesh_height_policy);
            data.include_backfaces = include_backfaces != 0u;
            data.normal_source = static_cast<TerrainNormalSource>(normal_source);
            data.uv_source = static_cast<TerrainUVSource>(uv_source);
            data.mesh_has_source_normals = mesh_has_source_normals != 0u;
            data.mesh_has_source_uv0 = mesh_has_source_uv0 != 0u;
            data.render_mode = static_cast<TerrainRenderMode>(render_mode);
            data.collision_mode =
                static_cast<TerrainCollisionMode>(collision_mode);
            data.supports_height_query = supports_height_query != 0u;
            data.supports_ray_query = supports_ray_query != 0u;
            data.supports_render_mesh = supports_render_mesh != 0u;

            uint64_t count = 0;
            if (!read_vector_count(bytes, offset, count, sizeof(float))) {
                return false;
            }
            data.height_samples.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    data.height_samples.data(),
                    data.height_samples.size() * sizeof(float)))
            {
                return false;
            }

            if (!read_vector_count(bytes, offset, count, sizeof(float))) {
                return false;
            }
            data.mesh_surface_points.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    data.mesh_surface_points.data(),
                    data.mesh_surface_points.size() * sizeof(float)))
            {
                return false;
            }

            if (!read_vector_count(bytes, offset, count, sizeof(uint32_t))) {
                return false;
            }
            data.mesh_surface_indices.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    data.mesh_surface_indices.data(),
                    data.mesh_surface_indices.size() * sizeof(uint32_t)))
            {
                return false;
            }

            if (!read_vector_count(bytes, offset, count, sizeof(uint32_t))) {
                return false;
            }
            data.mesh_visual_indices.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    data.mesh_visual_indices.data(),
                    data.mesh_visual_indices.size() * sizeof(uint32_t)))
            {
                return false;
            }

            if (!read_vector_count(
                    bytes,
                    offset,
                    count,
                    sizeof(TerrainVisualChunk)))
            {
                return false;
            }
            data.mesh_visual_chunks.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    data.mesh_visual_chunks.data(),
                    data.mesh_visual_chunks.size()
                        * sizeof(TerrainVisualChunk)))
            {
                return false;
            }

            return offset == bytes.size();
        }

        bool load_cached_mesh_terrain(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key,
            wz::Logger& logger,
            TerrainAssetData& data)
        {
            if (!cache.enabled || cache.root.empty()) {
                return false;
            }

            const wz::fs::Path path = mesh_terrain_cache_path(cache, key);
            const auto started = std::chrono::steady_clock::now();
            const auto bytes = wz::fs::read_file(path);
            if (!bytes) {
                logger.info("asset disk cache miss: mesh terrain " + path);
                return false;
            }

            TerrainAssetData loaded{};
            if (!deserialize_mesh_terrain_asset(bytes.value, key, loaded)
                || !loaded.valid()
                || loaded.representation != TerrainRepresentationKind::MeshSurface)
            {
                logger.warn(
                    "asset disk cache ignored invalid mesh terrain: "
                    + path);
                return false;
            }

            data = std::move(loaded);
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
            logger.info(
                "asset disk cache hit: mesh terrain "
                + path
                + " ms="
                + std::to_string(elapsed));
            return true;
        }

        void store_cached_mesh_terrain(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key,
            const TerrainAssetData& data,
            wz::Logger& logger)
        {
            if (!cache.enabled
                || cache.root.empty()
                || !data.valid()
                || data.representation != TerrainRepresentationKind::MeshSurface)
            {
                return;
            }

            const wz::fs::Path directory = mesh_terrain_cache_directory(cache);
            if (wz::fs::create_directories(directory)
                != wz::fs::FileError::None)
            {
                logger.warn(
                    "asset disk cache directory unavailable: " + directory);
                return;
            }

            const wz::fs::Path path = mesh_terrain_cache_path(cache, key);
            const std::vector<uint8_t> bytes =
                serialize_mesh_terrain_asset(key, data);
            const wz::fs::FileError err =
                wz::fs::write_file(path, bytes, true);
            if (err != wz::fs::FileError::None) {
                logger.warn(
                    "asset disk cache write failed: mesh terrain "
                    + path
                    + " error="
                    + std::to_string(static_cast<int>(err)));
                return;
            }

            logger.info(
                "asset disk cache stored: mesh terrain "
                + path
                + " bytes="
                + std::to_string(bytes.size()));
        }

        wz::asset::AssetNode compiled_terrain_node(
            const wz::asset::AssetNode& input,
            wz::asset::ResourceHandle handle)
        {
            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }

        void copy_mesh_bounds(
            float dst_min[3],
            float dst_max[3],
            const MeshData& mesh)
        {
            for (int axis = 0; axis < 3; ++axis) {
                dst_min[axis] = mesh.vertices[0].position[axis];
                dst_max[axis] = mesh.vertices[0].position[axis];
            }

            for (const auto& vertex : mesh.vertices) {
                for (int axis = 0; axis < 3; ++axis) {
                    dst_min[axis] =
                        std::min(dst_min[axis], vertex.position[axis]);
                    dst_max[axis] =
                        std::max(dst_max[axis], vertex.position[axis]);
                }
            }
        }

        TerrainNormalSource choose_terrain_normal_source(
            TerrainNormalSource preferred,
            const MeshData& mesh) noexcept
        {
            if (preferred == TerrainNormalSource::MeshVertexNormal
                && mesh.has_normals)
            {
                return TerrainNormalSource::MeshVertexNormal;
            }
            if (preferred == TerrainNormalSource::ImportedField) {
                return TerrainNormalSource::ImportedField;
            }
            return TerrainNormalSource::DerivedGeometry;
        }

        TerrainUVSource choose_terrain_uv_source(
            TerrainUVSource preferred,
            const MeshData& mesh) noexcept
        {
            if (preferred == TerrainUVSource::MeshUV0 && mesh.has_uv0) {
                return TerrainUVSource::MeshUV0;
            }
            if (preferred == TerrainUVSource::ImportedField) {
                return TerrainUVSource::ImportedField;
            }
            if (preferred == TerrainUVSource::PlanarXZ) {
                return TerrainUVSource::PlanarXZ;
            }
            return TerrainUVSource::None;
        }

        float mesh_triangle_normal_y(
            const MeshData& mesh,
            uint32_t ia,
            uint32_t ib,
            uint32_t ic) noexcept
        {
            const auto& a = mesh.vertices[ia];
            const auto& b = mesh.vertices[ib];
            const auto& c = mesh.vertices[ic];

            const float abx = b.position[0] - a.position[0];
            const float aby = b.position[1] - a.position[1];
            const float abz = b.position[2] - a.position[2];
            const float acx = c.position[0] - a.position[0];
            const float acy = c.position[1] - a.position[1];
            const float acz = c.position[2] - a.position[2];

            const float nx = aby * acz - abz * acy;
            const float ny = abz * acx - abx * acz;
            const float nz = abx * acy - aby * acx;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len <= 0.0f) {
                return 0.0f;
            }
            return ny / len;
        }

        uint32_t count_accepted_mesh_surface_triangles(
            const MeshData& mesh,
            float min_surface_normal_y,
            bool include_backfaces) noexcept
        {
            uint32_t accepted = 0;
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const uint32_t ia = mesh.indices[i + 0];
                const uint32_t ib = mesh.indices[i + 1];
                const uint32_t ic = mesh.indices[i + 2];
                if (ia >= mesh.vertices.size()
                    || ib >= mesh.vertices.size()
                    || ic >= mesh.vertices.size())
                {
                    continue;
                }

                const float normal_y = mesh_triangle_normal_y(mesh, ia, ib, ic);
                const float comparable_y =
                    include_backfaces ? std::abs(normal_y) : normal_y;
                if (comparable_y >= min_surface_normal_y) {
                    ++accepted;
                }
            }
            return accepted;
        }

        struct ChunkBuildState
        {
            std::vector<uint32_t> indices;
            TerrainVisualChunk chunk{};
            double area_sum = 0.0;
            double height_sum = 0.0;
            double height_sq_sum = 0.0;
            double normal_sum[3]{};
            double normal_sq_sum[3]{};
            bool initialized_bounds = false;
        };

        float triangle_area_and_normal(
            const MeshData& mesh,
            uint32_t ia,
            uint32_t ib,
            uint32_t ic,
            float normal[3]) noexcept
        {
            const auto& a = mesh.vertices[ia];
            const auto& b = mesh.vertices[ib];
            const auto& c = mesh.vertices[ic];

            const float abx = b.position[0] - a.position[0];
            const float aby = b.position[1] - a.position[1];
            const float abz = b.position[2] - a.position[2];
            const float acx = c.position[0] - a.position[0];
            const float acy = c.position[1] - a.position[1];
            const float acz = c.position[2] - a.position[2];

            float nx = aby * acz - abz * acy;
            float ny = abz * acx - abx * acz;
            float nz = abx * acy - aby * acx;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len <= 0.0f) {
                normal[0] = 0.0f;
                normal[1] = 1.0f;
                normal[2] = 0.0f;
                return 0.0f;
            }

            const float inv_len = 1.0f / len;
            normal[0] = nx * inv_len;
            normal[1] = ny * inv_len;
            normal[2] = nz * inv_len;
            return 0.5f * len;
        }

        void expand_chunk_bounds(
            ChunkBuildState& chunk,
            const MeshData& mesh,
            uint32_t ia,
            uint32_t ib,
            uint32_t ic)
        {
            const uint32_t ids[3]{ ia, ib, ic };
            for (uint32_t id : ids) {
                const auto& vertex = mesh.vertices[id];
                if (!chunk.initialized_bounds) {
                    for (int axis = 0; axis < 3; ++axis) {
                        chunk.chunk.bounds_min[axis] = vertex.position[axis];
                        chunk.chunk.bounds_max[axis] = vertex.position[axis];
                    }
                    chunk.initialized_bounds = true;
                    continue;
                }

                for (int axis = 0; axis < 3; ++axis) {
                    chunk.chunk.bounds_min[axis] = std::min(
                        chunk.chunk.bounds_min[axis],
                        vertex.position[axis]);
                    chunk.chunk.bounds_max[axis] = std::max(
                        chunk.chunk.bounds_max[axis],
                        vertex.position[axis]);
                }
            }
        }

        void accumulate_chunk_aggregate(
            ChunkBuildState& chunk,
            const MeshData& mesh,
            uint32_t ia,
            uint32_t ib,
            uint32_t ic)
        {
            float normal[3]{};
            const float area = triangle_area_and_normal(
                mesh,
                ia,
                ib,
                ic,
                normal);
            const double weight = area > 0.0f ? static_cast<double>(area) : 1.0;
            const auto& a = mesh.vertices[ia];
            const auto& b = mesh.vertices[ib];
            const auto& c = mesh.vertices[ic];
            const double centroid_y =
                (static_cast<double>(a.position[1])
                    + static_cast<double>(b.position[1])
                    + static_cast<double>(c.position[1])) / 3.0;

            chunk.area_sum += weight;
            chunk.height_sum += centroid_y * weight;
            chunk.height_sq_sum += centroid_y * centroid_y * weight;
            for (int axis = 0; axis < 3; ++axis) {
                const double n = normal[axis];
                chunk.normal_sum[axis] += n * weight;
                chunk.normal_sq_sum[axis] += n * n * weight;
            }
            ++chunk.chunk.aggregate.triangle_count;
        }

        void finalize_chunk_aggregate(ChunkBuildState& state)
        {
            TerrainVisualChunkAggregate& out = state.chunk.aggregate;
            if (state.area_sum <= 0.0) {
                return;
            }

            out.mean_height =
                static_cast<float>(state.height_sum / state.area_sum);
            const double height_mean = out.mean_height;
            out.height_variance = static_cast<float>(
                std::max(
                    0.0,
                    state.height_sq_sum / state.area_sum
                        - height_mean * height_mean));

            double normal_len_sq = 0.0;
            double normal_mean[3]{};
            for (int axis = 0; axis < 3; ++axis) {
                normal_mean[axis] = state.normal_sum[axis] / state.area_sum;
                normal_len_sq += normal_mean[axis] * normal_mean[axis];
            }
            const double normal_len = std::sqrt(normal_len_sq);
            if (normal_len > 1e-9) {
                for (int axis = 0; axis < 3; ++axis) {
                    out.normal_mean[axis] =
                        static_cast<float>(normal_mean[axis] / normal_len);
                }
            }

            const double var_x = std::max(
                0.0,
                state.normal_sq_sum[0] / state.area_sum
                    - normal_mean[0] * normal_mean[0]);
            const double var_z = std::max(
                0.0,
                state.normal_sq_sum[2] / state.area_sum
                    - normal_mean[2] * normal_mean[2]);
            out.normal_variance[0] = static_cast<float>(var_x);
            out.normal_variance[1] = static_cast<float>(var_z);
        }

        struct TriangleCentroid
        {
            uint32_t tri_index;
            float cx;
            float cz;
        };

        constexpr uint32_t kDefaultVisualChunkCount = 4096u;

        void kd_split_visual_chunks(
            const MeshData& mesh,
            std::vector<TriangleCentroid>& centroids,
            size_t begin,
            size_t end,
            uint32_t target_triangles,
            std::vector<ChunkBuildState>& out_chunks)
        {
            const size_t count = end - begin;
            if (count == 0) {
                return;
            }

            if (count <= target_triangles) {
                ChunkBuildState chunk{};
                chunk.indices.reserve(count * 3u);
                for (size_t i = begin; i < end; ++i) {
                    const size_t base =
                        static_cast<size_t>(centroids[i].tri_index) * 3u;
                    const uint32_t ia = mesh.indices[base + 0];
                    const uint32_t ib = mesh.indices[base + 1];
                    const uint32_t ic = mesh.indices[base + 2];
                    chunk.indices.push_back(ia);
                    chunk.indices.push_back(ib);
                    chunk.indices.push_back(ic);
                    expand_chunk_bounds(chunk, mesh, ia, ib, ic);
                    accumulate_chunk_aggregate(chunk, mesh, ia, ib, ic);
                }
                out_chunks.push_back(std::move(chunk));
                return;
            }

            float min_x = centroids[begin].cx;
            float max_x = min_x;
            float min_z = centroids[begin].cz;
            float max_z = min_z;
            for (size_t i = begin + 1; i < end; ++i) {
                min_x = std::min(min_x, centroids[i].cx);
                max_x = std::max(max_x, centroids[i].cx);
                min_z = std::min(min_z, centroids[i].cz);
                max_z = std::max(max_z, centroids[i].cz);
            }

            const size_t mid = begin + count / 2u;
            if ((max_x - min_x) >= (max_z - min_z)) {
                std::nth_element(
                    centroids.begin() + static_cast<ptrdiff_t>(begin),
                    centroids.begin() + static_cast<ptrdiff_t>(mid),
                    centroids.begin() + static_cast<ptrdiff_t>(end),
                    [](const TriangleCentroid& a,
                       const TriangleCentroid& b) {
                        return a.cx < b.cx;
                    });
            }
            else {
                std::nth_element(
                    centroids.begin() + static_cast<ptrdiff_t>(begin),
                    centroids.begin() + static_cast<ptrdiff_t>(mid),
                    centroids.begin() + static_cast<ptrdiff_t>(end),
                    [](const TriangleCentroid& a,
                       const TriangleCentroid& b) {
                        return a.cz < b.cz;
                    });
            }

            kd_split_visual_chunks(
                mesh, centroids, begin, mid,
                target_triangles, out_chunks);
            kd_split_visual_chunks(
                mesh, centroids, mid, end,
                target_triangles, out_chunks);
        }

        bool mesh_triangle_is_accepted_surface(
            const MeshData& mesh,
            uint32_t ia,
            uint32_t ib,
            uint32_t ic,
            float min_surface_normal_y,
            bool include_backfaces) noexcept
        {
            const float normal_y = mesh_triangle_normal_y(mesh, ia, ib, ic);
            const float comparable_y =
                include_backfaces ? std::abs(normal_y) : normal_y;
            return comparable_y >= min_surface_normal_y;
        }

        void copy_accepted_mesh_surface(
            TerrainAssetData& data,
            const MeshData& mesh,
            float min_surface_normal_y,
            bool include_backfaces)
        {
            data.mesh_surface_points.reserve(mesh.vertices.size() * 3u);
            for (const auto& vertex : mesh.vertices) {
                data.mesh_surface_points.push_back(vertex.position[0]);
                data.mesh_surface_points.push_back(vertex.position[1]);
                data.mesh_surface_points.push_back(vertex.position[2]);
            }

            data.mesh_surface_indices.clear();
            data.mesh_surface_indices.reserve(mesh.indices.size());

            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const uint32_t ia = mesh.indices[i + 0];
                const uint32_t ib = mesh.indices[i + 1];
                const uint32_t ic = mesh.indices[i + 2];
                if (ia >= mesh.vertices.size()
                    || ib >= mesh.vertices.size()
                    || ic >= mesh.vertices.size())
                {
                    continue;
                }

                if (!mesh_triangle_is_accepted_surface(
                        mesh,
                        ia,
                        ib,
                        ic,
                        min_surface_normal_y,
                        include_backfaces))
                {
                    continue;
                }

                data.mesh_surface_indices.push_back(ia);
                data.mesh_surface_indices.push_back(ib);
                data.mesh_surface_indices.push_back(ic);
            }
        }

        void copy_chunked_visual_mesh_surface(
            TerrainAssetData& data,
            const MeshData& mesh,
            uint32_t visual_chunk_count)
        {
            const uint32_t triangle_count =
                static_cast<uint32_t>(mesh.indices.size() / 3u);

            data.mesh_visual_indices.clear();
            data.mesh_visual_chunks.clear();

            if (triangle_count == 0) {
                return;
            }

            const uint32_t chunk_count =
                visual_chunk_count == 0u
                    ? kDefaultVisualChunkCount
                    : visual_chunk_count;
            const uint32_t target_triangles_per_chunk = std::max(
                1u,
                (triangle_count + chunk_count - 1u) / chunk_count);

            std::vector<TriangleCentroid> centroids;
            centroids.reserve(triangle_count);
            for (uint32_t t = 0; t < triangle_count; ++t) {
                const size_t base = static_cast<size_t>(t) * 3u;
                const uint32_t ia = mesh.indices[base + 0];
                const uint32_t ib = mesh.indices[base + 1];
                const uint32_t ic = mesh.indices[base + 2];
                if (ia >= mesh.vertices.size()
                    || ib >= mesh.vertices.size()
                    || ic >= mesh.vertices.size())
                {
                    continue;
                }
                const auto& a = mesh.vertices[ia];
                const auto& b = mesh.vertices[ib];
                const auto& c = mesh.vertices[ic];
                centroids.push_back({
                    t,
                    (a.position[0] + b.position[0] + c.position[0]) / 3.0f,
                    (a.position[2] + b.position[2] + c.position[2]) / 3.0f,
                });
            }

            std::vector<ChunkBuildState> chunks;
            kd_split_visual_chunks(
                mesh,
                centroids,
                0,
                centroids.size(),
                target_triangles_per_chunk,
                chunks);

            data.mesh_visual_indices.reserve(mesh.indices.size());
            data.mesh_visual_chunks.reserve(chunks.size());

            for (ChunkBuildState& chunk : chunks) {
                if (chunk.indices.empty()) {
                    continue;
                }

                finalize_chunk_aggregate(chunk);
                chunk.chunk.first_index =
                    static_cast<uint32_t>(data.mesh_visual_indices.size());
                chunk.chunk.index_count =
                    static_cast<uint32_t>(chunk.indices.size());
                data.mesh_visual_indices.insert(
                    data.mesh_visual_indices.end(),
                    chunk.indices.begin(),
                    chunk.indices.end());
                data.mesh_visual_chunks.push_back(chunk.chunk);
            }
        }
    }

    void register_terrain_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        ScalarFieldTable& scalar_fields_table,
        MeshTable& mesh_table,
        TerrainAssetTable& terrain_table,
        const EngineAssetCacheSettings& cache_settings)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kTerrainFromHeightFieldSchema,
            .output_type = kAssetTypeTerrain,
            .compile = [&logger, &scalar_fields_table, &terrain_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<TerrainFromHeightFieldCompileDesc>(
                        &input.meta);
                if (!desc) {
                    logger.error("terrain heightfield missing compile desc");
                    return compile_failed_node(input);
                }
                if (dep_handles.size() != 1) {
                    logger.error(
                        "terrain heightfield requires one scalar field dependency");
                    return compile_failed_node(input);
                }

                const ScalarFieldData* field =
                    scalar_fields_table.get(dep_handles[0]);
                if (!field || !field->valid()) {
                    logger.error("terrain heightfield source is invalid");
                    return compile_failed_node(input);
                }
                if (field->depth != 1) {
                    logger.error("terrain heightfield source must be 2D");
                    return compile_failed_node(input);
                }
                if (desc->size[0] <= 0.0f || desc->size[1] <= 0.0f) {
                    logger.error("terrain heightfield size must be positive");
                    return compile_failed_node(input);
                }

                const float min_h =
                    desc->base_height + field->min_value * desc->vertical_scale;
                const float max_h =
                    desc->base_height + field->max_value * desc->vertical_scale;

                TerrainAssetData data{};
                data.representation = TerrainRepresentationKind::HeightField;
                data.source_asset = desc->height_field;
                data.height_field = desc->height_field;
                data.normal_field = desc->normal_field;
                data.material_mask_set = desc->material_mask_set;
                data.origin[0] = desc->origin[0];
                data.origin[1] = desc->origin[1];
                data.size[0] = desc->size[0];
                data.size[1] = desc->size[1];
                data.resolution_x = field->width;
                data.resolution_y = field->height;
                data.vertical_scale = desc->vertical_scale;
                data.base_height = desc->base_height;
                data.min_height = std::min(min_h, max_h);
                data.max_height = std::max(min_h, max_h);
                data.height_samples = field->values;
                data.bounds_min[0] = desc->origin[0];
                data.bounds_min[1] = data.min_height;
                data.bounds_min[2] = desc->origin[1];
                data.bounds_max[0] = desc->origin[0] + desc->size[0];
                data.bounds_max[1] = data.max_height;
                data.bounds_max[2] = desc->origin[1] + desc->size[1];
                data.render_mode = desc->render_mode;
                data.collision_mode = desc->collision_mode;
                data.supports_height_query = true;
                data.supports_ray_query = false;
                data.supports_render_mesh =
                    desc->render_mode == TerrainRenderMode::DebugMesh;

                wz::asset::ResourceHandle handle =
                    terrain_table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("failed to store heightfield terrain");
                    return compile_failed_node(input);
                }

                return compiled_terrain_node(input, handle);
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kTerrainFromMeshSchema,
            .output_type = kAssetTypeTerrain,
            .compile = [&logger, &mesh_table, &terrain_table, cache_settings](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<TerrainFromMeshCompileDesc>(&input.meta);
                if (!desc) {
                    logger.error("terrain mesh missing compile desc");
                    return compile_failed_node(input);
                }

                TerrainAssetData cached_data{};
                if (load_cached_mesh_terrain(
                        cache_settings,
                        input.key,
                        logger,
                        cached_data))
                {
                    wz::asset::ResourceHandle handle =
                        terrain_table.add(std::move(cached_data));
                    if (!handle.valid()) {
                        logger.error("failed to store cached mesh terrain");
                        return compile_failed_node(input);
                    }
                    return compiled_terrain_node(input, handle);
                }

                if (dep_handles.size() != 1) {
                    logger.error("terrain mesh requires one mesh dependency");
                    return compile_failed_node(input);
                }

                const MeshData* mesh = mesh_table.get(dep_handles[0]);
                if (!mesh || !mesh->valid()) {
                    logger.error("terrain mesh source is invalid");
                    return compile_failed_node(input);
                }

                TerrainAssetData data{};
                data.representation = TerrainRepresentationKind::MeshSurface;
                data.source_asset = desc->mesh;
                data.mesh = desc->mesh;
                data.mesh_height_policy = desc->height_policy;
                data.min_surface_normal_y = desc->min_surface_normal_y;
                data.include_backfaces = desc->include_backfaces;
                data.mesh_has_source_normals = mesh->has_normals;
                data.mesh_has_source_uv0 = mesh->has_uv0;
                data.mesh_visual_chunk_count =
                    desc->visual_chunk_count == 0u
                        ? kDefaultVisualChunkCount
                        : desc->visual_chunk_count;
                data.mesh_triangle_count =
                    static_cast<uint32_t>(mesh->indices.size() / 3u);
                data.mesh_accepted_surface_triangle_count =
                    count_accepted_mesh_surface_triangles(
                        *mesh,
                        desc->min_surface_normal_y,
                        desc->include_backfaces);
                data.normal_source = choose_terrain_normal_source(
                    desc->preferred_normal_source,
                    *mesh);
                data.uv_source = choose_terrain_uv_source(
                    desc->preferred_uv_source,
                    *mesh);
                copy_mesh_bounds(data.bounds_min, data.bounds_max, *mesh);
                copy_accepted_mesh_surface(
                    data,
                    *mesh,
                    desc->min_surface_normal_y,
                    desc->include_backfaces);
                copy_chunked_visual_mesh_surface(
                    data,
                    *mesh,
                    data.mesh_visual_chunk_count);
                data.origin[0] = data.bounds_min[0];
                data.origin[1] = data.bounds_min[2];
                data.size[0] = data.bounds_max[0] - data.bounds_min[0];
                data.size[1] = data.bounds_max[2] - data.bounds_min[2];
                data.min_height = data.bounds_min[1];
                data.max_height = data.bounds_max[1];
                data.render_mode = desc->render_mode;
                data.collision_mode = desc->collision_mode;
                data.supports_height_query = false;
                data.supports_ray_query = false;
                data.supports_render_mesh =
                    desc->render_mode == TerrainRenderMode::DebugMesh;

                store_cached_mesh_terrain(
                    cache_settings,
                    input.key,
                    data,
                    logger);

                wz::asset::ResourceHandle handle =
                    terrain_table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("failed to store mesh terrain");
                    return compile_failed_node(input);
                }

                return compiled_terrain_node(input, handle);
            }
        });
    }
}
