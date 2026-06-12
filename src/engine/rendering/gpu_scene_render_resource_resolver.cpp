// src/engine/rendering/gpu_scene_render_resource_resolver.cpp

#include <engine/rendering/gpu_scene_render_resource_resolver.h>

#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/mesh_derived_field_asset_module.h>
#include <engine/assets/scalar_field_asset_module.h>
#include <engine/assets/terrain_asset_module.h>
#include <engine/assets/compiler_version_tokens.h>
#include <gpu/gaussian_splat.h>
#include <gpu/mesh.h>
#include <gpu/mesh_field_visualization.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <span>
#include <string>
#include <vector>

namespace wz::engine::rendering
{
    namespace
    {
        constexpr uint32_t kTerrainRenderMeshDiskCacheMagic = 0x52545a57u;
        constexpr uint32_t kTerrainRenderMeshDiskCacheVersion = 2u;

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
                key.content_hash.lo, key.content_hash.hi,
                key.schema_hash.lo, key.schema_hash.hi,
                key.compiler_hash.lo, key.compiler_hash.hi,
                key.deps_hash.lo, key.deps_hash.hi,
            };
            uint64_t lo = 0x3f84d5b5b5470917ull;
            uint64_t hi = 0x9216d5d98979fb1bull;
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

        uint32_t mesh_mask_channel_set_id(
            const wz::engine::assets::MeshMaskRenderStyleData& mask)
        {
            std::vector<uint32_t> channels;
            channels.reserve(mask.rules.size());
            for (const wz::engine::assets::MeshMaskRule& rule : mask.rules) {
                if (rule.enabled && rule.input_channel_id != 0u) {
                    channels.push_back(rule.input_channel_id);
                }
            }
            std::sort(channels.begin(), channels.end());
            channels.erase(
                std::unique(channels.begin(), channels.end()),
                channels.end());
            if (channels.empty()) {
                return 0u;
            }

            uint32_t hash = 2166136261u;
            for (uint32_t channel : channels) {
                for (uint32_t i = 0u; i < 4u; ++i) {
                    hash ^=
                        static_cast<uint8_t>((channel >> (i * 8u)) & 0xffu);
                    hash *= 16777619u;
                }
            }
            return hash == 0u ? 1u : hash;
        }

        std::vector<uint32_t> mesh_mask_channel_ids(
            const wz::engine::assets::MeshMaskRenderStyleData& mask)
        {
            std::vector<uint32_t> channels;
            channels.reserve(mask.rules.size());
            for (const wz::engine::assets::MeshMaskRule& rule : mask.rules) {
                if (rule.enabled && rule.input_channel_id != 0u) {
                    channels.push_back(rule.input_channel_id);
                }
            }
            std::sort(channels.begin(), channels.end());
            channels.erase(
                std::unique(channels.begin(), channels.end()),
                channels.end());
            return channels;
        }

        wz::fs::Path terrain_render_mesh_cache_directory(
            const wz::engine::assets::EngineAssetCacheSettings& cache)
        {
            return wz::fs::join(
                wz::fs::join(cache.root, "assets"),
                "terrain_render_mesh");
        }

        wz::fs::Path terrain_render_mesh_cache_path(
            const wz::engine::assets::EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key)
        {
            return wz::fs::join(
                terrain_render_mesh_cache_directory(cache),
                short_asset_key_hex(key) + ".bin");
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
            const uint64_t remaining =
                static_cast<uint64_t>(bytes.size() - offset);
            return min_bytes_per_entry == 0u
                || count <= remaining / min_bytes_per_entry;
        }

        std::vector<uint8_t> serialize_terrain_render_mesh(
            const wz::asset::AssetKey& key,
            const wz::engine::assets::MeshData& mesh,
            const std::vector<wz::engine::assets::TerrainVisualChunk>& chunks,
            const std::vector<TerrainTransitionDrawRange>& transition_ranges)
        {
            std::vector<uint8_t> out;
            out.reserve(
                160u
                + mesh.vertices.size() * sizeof(wz::engine::assets::MeshVertex)
                + mesh.indices.size() * sizeof(uint32_t)
                + chunks.size() * 96u
                + transition_ranges.size()
                    * sizeof(TerrainTransitionDrawRange));
            append_scalar(out, kTerrainRenderMeshDiskCacheMagic);
            append_scalar(out, kTerrainRenderMeshDiskCacheVersion);
            append_scalar(out, wz::engine::assets::kTerrainCompilerVersion);
            append_asset_key(out, key);
            append_scalar(out, static_cast<uint8_t>(mesh.topology));
            append_scalar(out, static_cast<uint8_t>(mesh.index_format));
            append_scalar(out, static_cast<uint8_t>(mesh.has_normals));
            append_scalar(out, static_cast<uint8_t>(mesh.has_uv0));
            append_scalar(out, static_cast<uint64_t>(mesh.vertices.size()));
            append_raw_bytes(
                out,
                mesh.vertices.data(),
                mesh.vertices.size()
                    * sizeof(wz::engine::assets::MeshVertex));
            append_scalar(out, static_cast<uint64_t>(mesh.indices.size()));
            append_raw_bytes(
                out,
                mesh.indices.data(),
                mesh.indices.size() * sizeof(uint32_t));
            append_scalar(out, static_cast<uint64_t>(chunks.size()));
            append_raw_bytes(
                out,
                chunks.data(),
                chunks.size()
                    * sizeof(wz::engine::assets::TerrainVisualChunk));
            append_scalar(
                out,
                static_cast<uint64_t>(transition_ranges.size()));
            append_raw_bytes(
                out,
                transition_ranges.data(),
                transition_ranges.size()
                    * sizeof(TerrainTransitionDrawRange));
            return out;
        }

        bool deserialize_terrain_render_mesh(
            const std::vector<uint8_t>& bytes,
            const wz::asset::AssetKey& expected_key,
            wz::engine::assets::MeshData& mesh,
            std::vector<wz::engine::assets::TerrainVisualChunk>& chunks,
            std::vector<TerrainTransitionDrawRange>& transition_ranges)
        {
            size_t offset = 0;
            uint32_t magic = 0;
            uint32_t version = 0;
            uint64_t compiler_version = 0;
            if (!read_scalar(bytes, offset, magic)
                || !read_scalar(bytes, offset, version)
                || !read_scalar(bytes, offset, compiler_version)
                || magic != kTerrainRenderMeshDiskCacheMagic
                || version != kTerrainRenderMeshDiskCacheVersion
                || compiler_version != wz::engine::assets::kTerrainCompilerVersion)
            {
                return false;
            }

            wz::asset::AssetKey stored_key{};
            uint8_t topology = 0;
            uint8_t index_format = 0;
            uint8_t has_normals = 0;
            uint8_t has_uv0 = 0;
            if (!read_asset_key(bytes, offset, stored_key)
                || stored_key != expected_key
                || !read_scalar(bytes, offset, topology)
                || !read_scalar(bytes, offset, index_format)
                || !read_scalar(bytes, offset, has_normals)
                || !read_scalar(bytes, offset, has_uv0))
            {
                return false;
            }
            mesh.topology =
                static_cast<wz::engine::assets::MeshPrimitiveTopology>(topology);
            mesh.index_format =
                static_cast<wz::engine::assets::MeshIndexFormat>(index_format);
            mesh.has_normals = has_normals != 0u;
            mesh.has_uv0 = has_uv0 != 0u;

            uint64_t count = 0;
            if (!read_vector_count(bytes, offset, count, 8u * sizeof(float))) {
                return false;
            }
            mesh.vertices.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    mesh.vertices.data(),
                    mesh.vertices.size()
                        * sizeof(wz::engine::assets::MeshVertex)))
            {
                return false;
            }

            if (!read_vector_count(bytes, offset, count, sizeof(uint32_t))) {
                return false;
            }
            mesh.indices.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    mesh.indices.data(),
                    mesh.indices.size() * sizeof(uint32_t)))
            {
                return false;
            }

            if (!read_vector_count(
                    bytes,
                    offset,
                    count,
                    sizeof(wz::engine::assets::TerrainVisualChunk)))
            {
                return false;
            }
            chunks.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    chunks.data(),
                    chunks.size()
                        * sizeof(wz::engine::assets::TerrainVisualChunk)))
            {
                return false;
            }

            if (!read_vector_count(
                    bytes,
                    offset,
                    count,
                    sizeof(TerrainTransitionDrawRange)))
            {
                return false;
            }
            transition_ranges.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    transition_ranges.data(),
                    transition_ranges.size()
                        * sizeof(TerrainTransitionDrawRange)))
            {
                return false;
            }
            return offset == bytes.size();
        }

        bool load_cached_terrain_render_mesh(
            const wz::engine::assets::EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key,
            wz::Logger& logger,
            wz::engine::assets::MeshData& mesh,
            std::vector<wz::engine::assets::TerrainVisualChunk>& chunks,
            std::vector<TerrainTransitionDrawRange>& transition_ranges)
        {
            if (!cache.enabled || cache.root.empty()) {
                return false;
            }
            const wz::fs::Path path = terrain_render_mesh_cache_path(cache, key);
            const auto started = std::chrono::steady_clock::now();
            const auto bytes = wz::fs::read_file(path);
            if (!bytes) {
                logger.info("asset disk cache miss: terrain render mesh " + path);
                return false;
            }
            wz::engine::assets::MeshData loaded_mesh{};
            std::vector<wz::engine::assets::TerrainVisualChunk> loaded_chunks;
            std::vector<TerrainTransitionDrawRange> loaded_transition_ranges;
            if (!deserialize_terrain_render_mesh(
                    bytes.value,
                    key,
                    loaded_mesh,
                    loaded_chunks,
                    loaded_transition_ranges)
                || !loaded_mesh.valid()
                || loaded_chunks.empty())
            {
                logger.warn(
                    "asset disk cache ignored invalid terrain render mesh: "
                    + path);
                return false;
            }
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
            mesh = std::move(loaded_mesh);
            chunks = std::move(loaded_chunks);
            transition_ranges = std::move(loaded_transition_ranges);
            logger.info(
                "asset disk cache hit: terrain render mesh "
                + path
                + " ms="
                + std::to_string(elapsed));
            return true;
        }

        void store_cached_terrain_render_mesh(
            const wz::engine::assets::EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key,
            const wz::engine::assets::MeshData& mesh,
            const std::vector<wz::engine::assets::TerrainVisualChunk>& chunks,
            const std::vector<TerrainTransitionDrawRange>& transition_ranges,
            wz::Logger& logger)
        {
            if (!cache.enabled
                || cache.root.empty()
                || !mesh.valid()
                || chunks.empty())
            {
                return;
            }
            const wz::fs::Path directory =
                terrain_render_mesh_cache_directory(cache);
            if (wz::fs::create_directories(directory)
                != wz::fs::FileError::None)
            {
                logger.warn("asset disk cache directory unavailable: " + directory);
                return;
            }
            const wz::fs::Path path = terrain_render_mesh_cache_path(cache, key);
            const std::vector<uint8_t> bytes =
                serialize_terrain_render_mesh(
                    key,
                    mesh,
                    chunks,
                    transition_ranges);
            const wz::fs::FileError err = wz::fs::write_file(path, bytes, true);
            if (err != wz::fs::FileError::None) {
                logger.warn(
                    "asset disk cache write failed: terrain render mesh "
                    + path
                    + " error="
                    + std::to_string(static_cast<int>(err)));
                return;
            }
            logger.info(
                "asset disk cache stored: terrain render mesh "
                + path
                + " bytes="
                + std::to_string(bytes.size()));
        }

        wz::engine::assets::MeshData make_heightfield_preview_mesh(
            const wz::engine::assets::TerrainAssetData& terrain,
            const wz::engine::assets::ScalarFieldData& field)
        {
            wz::engine::assets::MeshData mesh{};

            if (field.width < 2 || field.height < 2 || field.values.empty()) {
                return mesh;
            }

            constexpr uint32_t kMaxPreviewResolution = 129;
            const uint32_t step_x = std::max(
                1u,
                (field.width + kMaxPreviewResolution - 1u)
                    / kMaxPreviewResolution);
            const uint32_t step_y = std::max(
                1u,
                (field.height + kMaxPreviewResolution - 1u)
                    / kMaxPreviewResolution);
            const uint32_t preview_w =
                1u + (field.width - 1u + step_x - 1u) / step_x;
            const uint32_t preview_h =
                1u + (field.height - 1u + step_y - 1u) / step_y;

            mesh.vertices.reserve(
                static_cast<size_t>(preview_w) * preview_h);
            mesh.indices.reserve(
                static_cast<size_t>(preview_w - 1u)
                * (preview_h - 1u)
                * 6u);

            for (uint32_t y = 0; y < preview_h; ++y) {
                const uint32_t src_y =
                    std::min(y * step_y, field.height - 1u);
                const float v = field.height > 1
                    ? static_cast<float>(src_y)
                        / static_cast<float>(field.height - 1u)
                    : 0.0f;

                for (uint32_t x = 0; x < preview_w; ++x) {
                    const uint32_t src_x =
                        std::min(x * step_x, field.width - 1u);
                    const float u = field.width > 1
                        ? static_cast<float>(src_x)
                            / static_cast<float>(field.width - 1u)
                        : 0.0f;
                    const size_t sample_index =
                        static_cast<size_t>(src_y) * field.width + src_x;
                    const float sample = sample_index < field.values.size()
                        ? field.values[sample_index]
                        : 0.0f;

                    wz::engine::assets::MeshVertex vertex{};
                    vertex.position[0] = terrain.origin[0] + u * terrain.size[0];
                    vertex.position[1] =
                        terrain.base_height + sample * terrain.vertical_scale;
                    vertex.position[2] = terrain.origin[1] + v * terrain.size[1];
                    vertex.normal[1] = 1.0f;
                    vertex.uv[0] = u;
                    vertex.uv[1] = v;
                    mesh.vertices.push_back(vertex);
                }
            }

            for (uint32_t y = 0; y + 1u < preview_h; ++y) {
                for (uint32_t x = 0; x + 1u < preview_w; ++x) {
                    const uint32_t i0 = y * preview_w + x;
                    const uint32_t i1 = i0 + 1u;
                    const uint32_t i2 = i0 + preview_w;
                    const uint32_t i3 = i2 + 1u;
                    mesh.indices.push_back(i0);
                    mesh.indices.push_back(i2);
                    mesh.indices.push_back(i1);
                    mesh.indices.push_back(i1);
                    mesh.indices.push_back(i2);
                    mesh.indices.push_back(i3);
                }
            }

            return mesh;
        }

        wz::engine::assets::MeshData make_terrain_surface_mesh(
            const wz::engine::assets::TerrainAssetData& terrain,
            const wz::engine::assets::MeshData& source,
            const wz::engine::assets::TerrainVisualProxyData* visual_proxy,
            std::vector<wz::engine::assets::TerrainVisualChunk>& chunks,
            std::vector<TerrainTransitionDrawRange>& transition_ranges)
        {
            wz::engine::assets::MeshData mesh = source;
            if (!terrain.mesh_visual_indices.empty()) {
                mesh.indices = terrain.mesh_visual_indices;
            }
            chunks = terrain.mesh_visual_chunks;
            const uint32_t source_vertex_count =
                static_cast<uint32_t>(mesh.vertices.size());
            const uint32_t detail_index_count =
                static_cast<uint32_t>(mesh.indices.size());
            const uint32_t detail_triangle_count = detail_index_count / 3u;

            constexpr uint32_t kMinReplacementSourceTriangles = 96u;
            constexpr uint32_t kMaxReplacementTriangles = 256u;

            struct ClusterAccum
            {
                double position[3]{};
                double normal[3]{};
                double uv[2]{};
                uint32_t count = 0;
                uint32_t vertex_index = UINT32_MAX;
            };

            struct EdgeRef
            {
                uint32_t a = 0;
                uint32_t b = 0;
                uint32_t triangle = 0;
                uint32_t chunk = UINT32_MAX;
            };

            std::vector<uint32_t> triangle_chunks(
                detail_triangle_count,
                UINT32_MAX);
            for (uint32_t chunk_index = 0;
                chunk_index < chunks.size();
                ++chunk_index)
            {
                const auto& chunk = chunks[chunk_index];
                const uint32_t begin = chunk.first_index;
                const uint32_t end = std::min(
                    detail_index_count,
                    chunk.first_index + chunk.index_count);
                for (uint32_t i = begin; i + 2u < end; i += 3u) {
                    triangle_chunks[i / 3u] = chunk_index;
                }
            }

            const auto push_edge =
                [](std::vector<EdgeRef>& edges,
                   uint32_t a,
                   uint32_t b,
                   uint32_t triangle,
                   uint32_t chunk) {
                    if (a > b) {
                        std::swap(a, b);
                    }
                    edges.push_back(EdgeRef{ a, b, triangle, chunk });
                };

            std::vector<EdgeRef> edges;
            edges.reserve(static_cast<size_t>(detail_triangle_count) * 3u);
            for (uint32_t tri = 0; tri < detail_triangle_count; ++tri) {
                const uint32_t chunk = triangle_chunks[tri];
                if (chunk == UINT32_MAX) {
                    continue;
                }
                const uint32_t base = tri * 3u;
                const uint32_t ia = mesh.indices[base + 0u];
                const uint32_t ib = mesh.indices[base + 1u];
                const uint32_t ic = mesh.indices[base + 2u];
                if (ia >= source_vertex_count
                    || ib >= source_vertex_count
                    || ic >= source_vertex_count)
                {
                    continue;
                }
                push_edge(edges, ia, ib, tri, chunk);
                push_edge(edges, ib, ic, tri, chunk);
                push_edge(edges, ic, ia, tri, chunk);
            }
            std::sort(
                edges.begin(),
                edges.end(),
                [](const EdgeRef& lhs, const EdgeRef& rhs) {
                    if (lhs.a != rhs.a) {
                        return lhs.a < rhs.a;
                    }
                    if (lhs.b != rhs.b) {
                        return lhs.b < rhs.b;
                    }
                    return lhs.chunk < rhs.chunk;
                });

            std::vector<uint8_t> fixed_vertices(source_vertex_count, 0u);
            for (size_t i = 0; i < edges.size();) {
                size_t end = i + 1u;
                bool crosses_chunk = false;
                while (end < edges.size()
                    && edges[end].a == edges[i].a
                    && edges[end].b == edges[i].b)
                {
                    crosses_chunk =
                        crosses_chunk || edges[end].chunk != edges[i].chunk;
                    ++end;
                }

                if (crosses_chunk || end - i == 1u) {
                    fixed_vertices[edges[i].a] = 1u;
                    fixed_vertices[edges[i].b] = 1u;
                }
                i = end;
            }

            const auto cluster_for_vertex =
                [](const wz::engine::assets::TerrainVisualChunk& chunk,
                   const wz::engine::assets::MeshVertex& vertex,
                   uint32_t grid_vertices) -> uint32_t {
                    const float extent_x =
                        chunk.bounds_max[0] - chunk.bounds_min[0];
                    const float extent_z =
                        chunk.bounds_max[2] - chunk.bounds_min[2];
                    if (extent_x <= 1e-6f || extent_z <= 1e-6f) {
                        return UINT32_MAX;
                    }

                    const float ux = std::clamp(
                        (vertex.position[0] - chunk.bounds_min[0]) / extent_x,
                        0.0f,
                        1.0f);
                    const float uz = std::clamp(
                        (vertex.position[2] - chunk.bounds_min[2]) / extent_z,
                        0.0f,
                        1.0f);
                    const uint32_t max_grid = grid_vertices - 1u;
                    const uint32_t gx = std::min(
                        max_grid,
                        static_cast<uint32_t>(
                            ux * static_cast<float>(max_grid) + 0.5f));
                    const uint32_t gz = std::min(
                        max_grid,
                        static_cast<uint32_t>(
                            uz * static_cast<float>(max_grid) + 0.5f));
                    return gz * grid_vertices + gx;
                };

            const auto triangle_area_and_normal =
                [](const wz::engine::assets::MeshData& mesh_data,
                   uint32_t ia,
                   uint32_t ib,
                   uint32_t ic,
                   float normal[3]) -> float {
                    if (ia >= mesh_data.vertices.size()
                        || ib >= mesh_data.vertices.size()
                        || ic >= mesh_data.vertices.size())
                    {
                        normal[0] = 0.0f;
                        normal[1] = 1.0f;
                        normal[2] = 0.0f;
                        return 0.0f;
                    }

                    const auto& a = mesh_data.vertices[ia];
                    const auto& b = mesh_data.vertices[ib];
                    const auto& c = mesh_data.vertices[ic];
                    const float abx = b.position[0] - a.position[0];
                    const float aby = b.position[1] - a.position[1];
                    const float abz = b.position[2] - a.position[2];
                    const float acx = c.position[0] - a.position[0];
                    const float acy = c.position[1] - a.position[1];
                    const float acz = c.position[2] - a.position[2];
                    normal[0] = aby * acz - abz * acy;
                    normal[1] = abz * acx - abx * acz;
                    normal[2] = abx * acy - aby * acx;
                    const float len = std::sqrt(
                        normal[0] * normal[0]
                        + normal[1] * normal[1]
                        + normal[2] * normal[2]);
                    if (len <= 1e-8f) {
                        normal[0] = 0.0f;
                        normal[1] = 1.0f;
                        normal[2] = 0.0f;
                        return 0.0f;
                    }
                    const float inv_len = 1.0f / len;
                    normal[0] *= inv_len;
                    normal[1] *= inv_len;
                    normal[2] *= inv_len;
                    return 0.5f * len;
                };

            for (auto& chunk : chunks) {
                chunk.replacement_first_index = 0;
                chunk.replacement_index_count = 0;

                const uint32_t source_triangles = chunk.triangle_count();
                if (source_triangles < kMinReplacementSourceTriangles) {
                    continue;
                }
                const float extent_x = chunk.bounds_max[0] - chunk.bounds_min[0];
                const float extent_z = chunk.bounds_max[2] - chunk.bounds_min[2];
                if (extent_x <= 1e-6f || extent_z <= 1e-6f) {
                    continue;
                }

                const uint32_t desired_triangles = std::clamp(
                    source_triangles / 32u,
                    16u,
                    kMaxReplacementTriangles);
                const uint32_t grid_cells = std::clamp(
                    static_cast<uint32_t>(
                        std::sqrt(
                            static_cast<float>(desired_triangles) * 0.5f))
                        + 1u,
                    3u,
                    24u);
                const uint32_t grid_vertices = grid_cells + 1u;
                std::vector<ClusterAccum> clusters(
                    static_cast<size_t>(grid_vertices) * grid_vertices);

                for (uint32_t i = 0; i + 2u < chunk.index_count; i += 3u) {
                    const uint32_t base = chunk.first_index + i;
                    if (base + 2u >= mesh.indices.size()) {
                        continue;
                    }

                    const uint32_t ids[3]{
                        mesh.indices[base + 0u],
                        mesh.indices[base + 1u],
                        mesh.indices[base + 2u],
                    };
                    for (uint32_t id : ids) {
                        if (id >= source_vertex_count
                            || fixed_vertices[id] != 0u)
                        {
                            continue;
                        }
                        const auto& vertex = mesh.vertices[id];
                        const uint32_t cluster_id = cluster_for_vertex(
                            chunk,
                            vertex,
                            grid_vertices);
                        if (cluster_id >= clusters.size()) {
                            continue;
                        }

                        ClusterAccum& accum = clusters[cluster_id];
                        for (int axis = 0; axis < 3; ++axis) {
                            accum.position[axis] += vertex.position[axis];
                            accum.normal[axis] += vertex.normal[axis];
                        }
                        accum.uv[0] += vertex.uv[0];
                        accum.uv[1] += vertex.uv[1];
                        ++accum.count;
                    }
                }

                for (ClusterAccum& accum : clusters) {
                    if (accum.count == 0) {
                        continue;
                    }

                    const double inv_count =
                        1.0 / static_cast<double>(accum.count);
                    wz::engine::assets::MeshVertex vertex{};
                    for (int axis = 0; axis < 3; ++axis) {
                        vertex.position[axis] =
                            static_cast<float>(
                                accum.position[axis] * inv_count);
                        vertex.normal[axis] =
                            static_cast<float>(
                                accum.normal[axis] * inv_count);
                    }
                    const float normal_len = std::sqrt(
                        vertex.normal[0] * vertex.normal[0]
                        + vertex.normal[1] * vertex.normal[1]
                        + vertex.normal[2] * vertex.normal[2]);
                    if (normal_len > 1e-6f) {
                        vertex.normal[0] /= normal_len;
                        vertex.normal[1] /= normal_len;
                        vertex.normal[2] /= normal_len;
                    }
                    else {
                        vertex.normal[0] = 0.0f;
                        vertex.normal[1] = 1.0f;
                        vertex.normal[2] = 0.0f;
                    }
                    vertex.uv[0] = static_cast<float>(accum.uv[0] * inv_count);
                    vertex.uv[1] = static_cast<float>(accum.uv[1] * inv_count);
                    accum.vertex_index =
                        static_cast<uint32_t>(mesh.vertices.size());
                    mesh.vertices.push_back(vertex);
                }

                const uint32_t replacement_first =
                    static_cast<uint32_t>(mesh.indices.size());
                std::vector<std::array<uint32_t, 3>> emitted_keys;
                emitted_keys.reserve(desired_triangles);

                for (uint32_t i = 0; i + 2u < chunk.index_count; i += 3u) {
                    const uint32_t base = chunk.first_index + i;
                    if (base + 2u >= mesh.indices.size()) {
                        continue;
                    }

                    uint32_t replacement[3]{};
                    bool valid = true;
                    for (uint32_t corner = 0; corner < 3u; ++corner) {
                        const uint32_t source_id = mesh.indices[base + corner];
                        if (source_id >= source_vertex_count) {
                            valid = false;
                            break;
                        }
                        if (fixed_vertices[source_id] != 0u) {
                            replacement[corner] = source_id;
                            continue;
                        }
                        const uint32_t cluster_id = cluster_for_vertex(
                            chunk,
                            mesh.vertices[source_id],
                            grid_vertices);
                        if (cluster_id >= clusters.size()
                            || clusters[cluster_id].vertex_index == UINT32_MAX)
                        {
                            valid = false;
                            break;
                        }
                        replacement[corner] =
                            clusters[cluster_id].vertex_index;
                    }
                    if (!valid) {
                        mesh.indices.push_back(mesh.indices[base + 0u]);
                        mesh.indices.push_back(mesh.indices[base + 1u]);
                        mesh.indices.push_back(mesh.indices[base + 2u]);
                        continue;
                    }

                    if (replacement[0] == replacement[1]
                        || replacement[1] == replacement[2]
                        || replacement[2] == replacement[0])
                    {
                        mesh.indices.push_back(mesh.indices[base + 0u]);
                        mesh.indices.push_back(mesh.indices[base + 1u]);
                        mesh.indices.push_back(mesh.indices[base + 2u]);
                        continue;
                    }

                    float source_normal[3]{};
                    const float source_area = triangle_area_and_normal(
                        mesh,
                        mesh.indices[base + 0u],
                        mesh.indices[base + 1u],
                        mesh.indices[base + 2u],
                        source_normal);
                    float replacement_normal[3]{};
                    const float replacement_area = triangle_area_and_normal(
                        mesh,
                        replacement[0],
                        replacement[1],
                        replacement[2],
                        replacement_normal);
                    const float normal_dot =
                        source_normal[0] * replacement_normal[0]
                        + source_normal[1] * replacement_normal[1]
                        + source_normal[2] * replacement_normal[2];
                    if (replacement_area <= source_area * 0.01f
                        || normal_dot <= 0.0f)
                    {
                        mesh.indices.push_back(mesh.indices[base + 0u]);
                        mesh.indices.push_back(mesh.indices[base + 1u]);
                        mesh.indices.push_back(mesh.indices[base + 2u]);
                        continue;
                    }

                    std::array<uint32_t, 3> key{
                        replacement[0],
                        replacement[1],
                        replacement[2],
                    };
                    std::sort(key.begin(), key.end());
                    const bool already_emitted = std::find(
                        emitted_keys.begin(),
                        emitted_keys.end(),
                        key) != emitted_keys.end();
                    if (already_emitted) {
                        mesh.indices.push_back(mesh.indices[base + 0u]);
                        mesh.indices.push_back(mesh.indices[base + 1u]);
                        mesh.indices.push_back(mesh.indices[base + 2u]);
                        continue;
                    }
                    emitted_keys.push_back(key);

                    mesh.indices.push_back(replacement[0]);
                    mesh.indices.push_back(replacement[1]);
                    mesh.indices.push_back(replacement[2]);
                }

                const uint32_t replacement_count =
                    static_cast<uint32_t>(mesh.indices.size())
                    - replacement_first;
                if (replacement_count >= 3u
                    && replacement_count < chunk.index_count)
                {
                    chunk.replacement_first_index = replacement_first;
                    chunk.replacement_index_count = replacement_count;
                }
                else {
                    mesh.indices.resize(replacement_first);
                }
            }

            transition_ranges.clear();
            if (visual_proxy) {
                for (const auto& proxy_chunk : visual_proxy->chunks) {
                    for (const auto& strip : proxy_chunk.transition_strips) {
                        if (!strip.valid()) {
                            continue;
                        }

                        const uint32_t first_vertex =
                            static_cast<uint32_t>(mesh.vertices.size());
                        for (const auto& strip_vertex : strip.vertices) {
                            wz::engine::assets::MeshVertex vertex{};
                            vertex.position[0] = strip_vertex.position[0];
                            vertex.position[1] = strip_vertex.position[1];
                            vertex.position[2] = strip_vertex.position[2];
                            vertex.normal[1] = 1.0f;
                            vertex.uv[0] = strip_vertex.position[0];
                            vertex.uv[1] = strip_vertex.position[2];
                            mesh.vertices.push_back(vertex);
                        }

                        const uint32_t first_index =
                            static_cast<uint32_t>(mesh.indices.size());
                        for (uint32_t index : strip.indices) {
                            if (index >= strip.vertices.size()) {
                                continue;
                            }
                            mesh.indices.push_back(first_vertex + index);
                        }
                        const uint32_t index_count =
                            static_cast<uint32_t>(mesh.indices.size())
                            - first_index;
                        if (index_count < 3u) {
                            mesh.indices.resize(first_index);
                            mesh.vertices.resize(first_vertex);
                            continue;
                        }

                        transition_ranges.push_back(
                            TerrainTransitionDrawRange{
                                .chunk_id = strip.chunk_id,
                                .neighbor_chunk_id = strip.neighbor_chunk_id,
                                .lod_id = strip.lod_id,
                                .neighbor_lod_id = strip.neighbor_lod_id,
                                .edge = strip.edge,
                                .first_index = first_index,
                                .index_count = index_count,
                            });
                    }
                }
            }

            mesh.has_normals = true;
            mesh.has_uv0 = true;
            return mesh;
        }

        std::vector<TerrainFarSplatChunk> make_terrain_far_splat_chunks(
            const wz::engine::assets::TerrainVisualProxyData& proxy,
            std::span<const wz::gpu::GPUHandle> gpu_resources = {})
        {
            std::vector<TerrainFarSplatChunk> out;
            std::vector<wz::engine::assets::TerrainLodId> density_ids;
            for (const auto& chunk : proxy.chunks) {
                for (const auto& level : chunk.surfel_density_levels) {
                    if (!level.valid()) {
                        continue;
                    }
                    const auto existing = std::find_if(
                        density_ids.begin(),
                        density_ids.end(),
                        [&](wz::engine::assets::TerrainLodId id)
                        {
                            return id == level.density_id;
                        });
                    if (existing == density_ids.end()) {
                        density_ids.push_back(level.density_id);
                    }
                }
            }
            std::sort(
                density_ids.begin(),
                density_ids.end(),
                [](wz::engine::assets::TerrainLodId a,
                   wz::engine::assets::TerrainLodId b)
                {
                    return a.value < b.value;
                });

            for (size_t density_index = 0;
                 density_index < density_ids.size();
                 ++density_index)
            {
                const auto density_id = density_ids[density_index];
                const wz::gpu::GPUHandle gpu_resource =
                    density_index < gpu_resources.size()
                    ? gpu_resources[density_index]
                    : wz::gpu::GPUHandle{};
                uint32_t first_splat = 0u;

                for (const auto& chunk : proxy.chunks) {
                    for (const auto& level : chunk.surfel_density_levels) {
                        if (!level.valid()
                            || !(level.density_id == density_id))
                        {
                            continue;
                        }
                        out.push_back(TerrainFarSplatChunk{
                            .chunk_id = chunk.chunk_id,
                            .density_id = level.density_id,
                            .gpu_resource = gpu_resource,
                            .first_splat = first_splat,
                            .splat_count = level.surfel_count,
                        });
                        first_splat += level.surfel_count;
                        break;
                    }
                }
            }
            return out;
        }

        std::vector<wz::gpu::GPUHandle> upload_terrain_far_splat_clouds(
            wz::gpu::Device& device,
            std::span<const wz::engine::assets::GaussianSplatCloudData> clouds)
        {
            std::vector<wz::gpu::GPUHandle> out;
            out.reserve(clouds.size());
            for (const auto& cloud : clouds) {
                out.push_back(wz::gpu::upload_gaussian_splat_cloud(
                    device,
                    cloud));
            }
            return out;
        }
    }

    GpuSceneRenderResourceResolver::GpuSceneRenderResourceResolver(
        wz::gpu::Device& device,
        wz::engine::assets::EngineAssetLibrary& assets,
        RenderResourceResolver& render_resolver,
        RenderableGpuCache* cache)
        : device_(device)
        , assets_(assets)
        , render_resolver_(render_resolver)
        , cache_(cache)
    {
    }

    GpuSceneRenderResourceResolver::~GpuSceneRenderResourceResolver()
    {
        preview_gpu_resources_.clear();
        preview_release_queue_.flush(device_);
    }

    bool GpuSceneRenderResourceResolver::realize_renderable_descriptor(
        const wz::engine::assets::RenderableAssetData& renderable,
        wz::scene::RenderableDescriptor& descriptor) const
    {
        wz::engine::assets::MeshData preview_mesh{};
        const wz::engine::assets::MeshData* mesh_data = nullptr;
        wz::gpu::GPUHandle cached_mesh{};
        wz::gpu::GPUHandle mesh_field_visualization_resource{};

        const bool is_terrain_surface =
            renderable.kind == wz::engine::assets::RenderableKind::Mesh
            && renderable.program
                == wz::engine::assets::BuiltinRenderProgram::TerrainMeshSurface
            && !(renderable.companion_asset == wz::asset::AssetKey{});

        const wz::engine::assets::TerrainAssetData* terrain_data = nullptr;
        const wz::engine::assets::TerrainVisualProxyData* terrain_visual_proxy_data =
            nullptr;
        std::vector<wz::engine::assets::TerrainVisualChunk> terrain_chunks_storage;
        std::vector<TerrainFarSplatChunk> terrain_far_splat_chunks_storage;
        std::vector<TerrainTransitionDrawRange> terrain_transition_ranges_storage;

        if (renderable.kind == wz::engine::assets::RenderableKind::Mesh) {
            if (is_terrain_surface) {
                const wz::engine::assets::TerrainVisualProxyAsset
                    visual_proxy_asset{
                        .output = renderable.companion_asset,
                    };
                const wz::engine::assets::TerrainVisualProxyHandle proxy_handle =
                    assets_.terrain_visual_proxies().get_proxy(
                        visual_proxy_asset);
                if (!proxy_handle.valid()) {
                    return false;
                }

                const wz::engine::assets::TerrainVisualProxyData*
                    visual_proxy =
                        assets_.terrain_visual_proxies().get_proxy_data(
                            proxy_handle);
                if (!visual_proxy || !visual_proxy->valid()) {
                    return false;
                }
                terrain_visual_proxy_data = visual_proxy;

                const wz::engine::assets::TerrainAsset terrain_asset{
                    .output = visual_proxy->source_asset_key,
                };
                const wz::engine::assets::TerrainHandle terrain_handle =
                    assets_.terrains().get_terrain(terrain_asset);
                if (!terrain_handle.valid()) {
                    return false;
                }

                terrain_data =
                    assets_.terrains().get_terrain_data(terrain_handle);
                if (!terrain_data || !terrain_data->valid()) {
                    return false;
                }

                const wz::engine::assets::MeshAsset mesh_asset{
                    .output = renderable.source_asset,
                };
                const wz::engine::assets::MeshHandle mesh_handle =
                    assets_.meshes().get_mesh(mesh_asset);
                if (!mesh_handle.valid()) {
                    return false;
                }

                const wz::engine::assets::MeshData* source_mesh =
                    assets_.meshes().get_mesh_data(mesh_handle);
                if (!source_mesh || !source_mesh->valid()) {
                    return false;
                }

                if (cache_) {
                    const PreparedRenderable cached =
                        cache_->find_mesh_data(
                            renderable.companion_asset,
                            renderable.program,
                            renderable.render_program,
                            renderable.domain,
                            renderable.policy_flags);
                    if (cached.valid()) {
                        if (const auto* cached_chunks =
                                cache_->find_terrain_mesh_chunks(
                                    renderable.companion_asset))
                        {
                            terrain_chunks_storage = *cached_chunks;
                            cached_mesh = cached.gpu_resource;
                            if (const auto* cached_transition_ranges =
                                    cache_->find_terrain_transition_ranges(
                                        renderable.companion_asset))
                            {
                                terrain_transition_ranges_storage =
                                    *cached_transition_ranges;
                            }
                            if (const auto* cached_far_splats =
                                    cache_->find_terrain_far_splat_chunks(
                                        renderable.companion_asset))
                            {
                                terrain_far_splat_chunks_storage =
                                    *cached_far_splats;
                            }
                        }
                    }

                    if (!cached_mesh.valid()) {
                        const auto& cache_settings = assets_.cache_settings();
                        wz::Logger& logger = assets_.logger();
                        if (!load_cached_terrain_render_mesh(
                                cache_settings,
                                renderable.companion_asset,
                                logger,
                                preview_mesh,
                                terrain_chunks_storage,
                                terrain_transition_ranges_storage))
                        {
                            const auto build_started =
                                std::chrono::steady_clock::now();
                            preview_mesh = make_terrain_surface_mesh(
                                *terrain_data,
                                *source_mesh,
                                terrain_visual_proxy_data,
                                terrain_chunks_storage,
                                terrain_transition_ranges_storage);
                            const auto build_elapsed =
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now()
                                    - build_started).count();
                            logger.info(
                                "asset compile: terrain render mesh build ms="
                                + std::to_string(build_elapsed));
                            store_cached_terrain_render_mesh(
                                cache_settings,
                                renderable.companion_asset,
                                preview_mesh,
                                terrain_chunks_storage,
                                terrain_transition_ranges_storage,
                                logger);
                        }
                        if (!preview_mesh.valid()) {
                            return false;
                        }

                        const PreparedRenderable prepared =
                            cache_->realize_mesh_data(
                                device_,
                                renderable.companion_asset,
                                preview_mesh,
                                renderable.program,
                                renderable.render_program,
                                renderable.domain,
                                renderable.policy_flags);
                        if (!prepared.valid()) {
                            return false;
                        }
                        cached_mesh = prepared.gpu_resource;
                        cache_->add_terrain_mesh_chunks(
                            renderable.companion_asset,
                            terrain_chunks_storage);
                        cache_->add_terrain_transition_ranges(
                            renderable.companion_asset,
                            terrain_transition_ranges_storage);
                    }

                    if (terrain_far_splat_chunks_storage.empty()
                        && !renderable.terrain_far_splat_chunks.empty())
                    {
                        std::vector<wz::gpu::GPUHandle> far_splat_handles =
                            upload_terrain_far_splat_clouds(
                                device_,
                                renderable.terrain_far_splat_chunks);
                        terrain_far_splat_chunks_storage =
                            make_terrain_far_splat_chunks(
                                *visual_proxy,
                                far_splat_handles);
                        cache_->add_terrain_far_splat_chunks(
                            renderable.companion_asset,
                            terrain_far_splat_chunks_storage,
                            std::move(far_splat_handles));
                    }
                }
                else {
                    const auto& cache_settings = assets_.cache_settings();
                    wz::Logger& logger = assets_.logger();
                    if (!load_cached_terrain_render_mesh(
                            cache_settings,
                            renderable.companion_asset,
                            logger,
                            preview_mesh,
                            terrain_chunks_storage,
                            terrain_transition_ranges_storage))
                    {
                        const auto build_started =
                            std::chrono::steady_clock::now();
                        preview_mesh = make_terrain_surface_mesh(
                            *terrain_data,
                            *source_mesh,
                            terrain_visual_proxy_data,
                            terrain_chunks_storage,
                            terrain_transition_ranges_storage);
                        const auto build_elapsed =
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now()
                                - build_started).count();
                        logger.info(
                            "asset compile: terrain render mesh build ms="
                            + std::to_string(build_elapsed));
                        store_cached_terrain_render_mesh(
                            cache_settings,
                            renderable.companion_asset,
                            preview_mesh,
                            terrain_chunks_storage,
                            terrain_transition_ranges_storage,
                            logger);
                    }
                    if (!preview_mesh.valid()) {
                        return false;
                    }
                    mesh_data = &preview_mesh;
                    if (!renderable.terrain_far_splat_chunks.empty()) {
                        std::vector<wz::gpu::GPUHandle> far_splat_handles =
                            upload_terrain_far_splat_clouds(
                                device_,
                                renderable.terrain_far_splat_chunks);
                        terrain_far_splat_chunks_storage =
                            make_terrain_far_splat_chunks(
                                *visual_proxy,
                                far_splat_handles);
                        for (wz::gpu::GPUHandle handle : far_splat_handles) {
                            if (handle.valid()) {
                                preview_gpu_resources_.emplace_back(
                                    preview_release_queue_,
                                    handle);
                            }
                        }
                    }
                }
            }
            else if (cache_) {
                const PreparedRenderable prepared =
                    cache_->realize_data(device_, assets_, renderable);
                if (!prepared.valid()) {
                    return false;
                }
                cached_mesh = prepared.gpu_resource;
                mesh_field_visualization_resource =
                    prepared.mesh_field_visualization_resource;
            }
            else {
                const wz::engine::assets::MeshAsset mesh_asset{
                    .output = renderable.source_asset,
                };

                const wz::engine::assets::MeshHandle mesh_handle =
                    assets_.meshes().get_mesh(mesh_asset);

                if (!mesh_handle.valid())
                    return false;

                mesh_data = assets_.meshes().get_mesh_data(mesh_handle);

                if (!mesh_data || !mesh_data->valid())
                    return false;
            }
        }
        else if (renderable.kind
            == wz::engine::assets::RenderableKind::ScalarField)
        {
            if (renderable.companion_asset == wz::asset::AssetKey{})
                return false;

            const wz::engine::assets::TerrainAsset terrain_asset{
                .output = renderable.companion_asset,
            };
            const wz::engine::assets::TerrainHandle terrain_handle =
                assets_.terrains().get_terrain(terrain_asset);
            if (!terrain_handle.valid())
                return false;

            const wz::engine::assets::TerrainAssetData* terrain =
                assets_.terrains().get_terrain_data(terrain_handle);
            if (!terrain || !terrain->valid())
                return false;

            const wz::engine::assets::ScalarFieldAsset field_asset{
                .output = renderable.source_asset,
            };
            const wz::engine::assets::ScalarFieldHandle field_handle =
                assets_.scalar_fields().get_scalar_field(field_asset);
            if (!field_handle.valid())
                return false;

            const wz::engine::assets::ScalarFieldData* field =
                assets_.scalar_fields().get_scalar_field_data(field_handle);
            if (!field || !field->valid())
                return false;

            preview_mesh = make_heightfield_preview_mesh(*terrain, *field);
            if (!preview_mesh.valid())
                return false;

            mesh_data = &preview_mesh;
        }
        else {
            return false;
        }

        const wz::gpu::GPUHandle gpu_mesh = cached_mesh.valid()
            ? cached_mesh
            : wz::gpu::upload_mesh(device_, *mesh_data);

        if (!gpu_mesh.valid())
            return false;

        if (!mesh_field_visualization_resource.valid()
            && !(renderable.mesh_field_visualization_asset
                == wz::asset::AssetKey{}))
        {
            const bool wants_mask =
                renderable.program
                    == wz::engine::assets::BuiltinRenderProgram::MeshMaskStyle
                && renderable.mesh_style.mask.enabled;
            const uint32_t channel_id = wants_mask
                ? mesh_mask_channel_set_id(renderable.mesh_style.mask)
                : renderable.mesh_style.field_visualization.channel_id;
            const wz::engine::assets::GpuResidentFieldLayout layout =
                wants_mask
                    ? wz::engine::assets::GpuResidentFieldLayout::FaceRaw
                    : wz::engine::assets::GpuResidentFieldLayout::
                        VertexProjected;
            const wz::gpu::GPUHandle resident_field =
                channel_id == 0u
                    ? wz::gpu::GPUHandle{}
                    : assets_.gpu_resident_fields().find(
                        renderable.mesh_field_visualization_asset,
                        channel_id,
                        layout);
            if (resident_field.valid()) {
                mesh_field_visualization_resource = resident_field;
            }
        }

        if (!mesh_field_visualization_resource.valid()
            && !(renderable.mesh_field_visualization_asset
                == wz::asset::AssetKey{}))
        {
            if (!mesh_data || !mesh_data->valid()) {
                return false;
            }

            const wz::engine::assets::MeshDerivedFieldAsset field_asset{
                .output = renderable.mesh_field_visualization_asset,
            };
            const wz::engine::assets::MeshDerivedFieldHandle field_handle =
                assets_.mesh_derived_fields().get_mesh_derived_field(
                    field_asset);
            if (!field_handle.valid()) {
                return false;
            }

            const wz::engine::assets::MeshDerivedFieldData* field_data =
                assets_.mesh_derived_fields().get_mesh_derived_field_data(
                    field_handle);
            if (!field_data || !field_data->valid()) {
                return false;
            }

            const bool wants_mask =
                renderable.program
                    == wz::engine::assets::BuiltinRenderProgram::MeshMaskStyle
                && renderable.mesh_style.mask.enabled;
            if (wants_mask) {
                const std::vector<uint32_t> channels =
                    mesh_mask_channel_ids(renderable.mesh_style.mask);
                if (channels.empty()) {
                    return false;
                }
                mesh_field_visualization_resource =
                    wz::gpu::upload_mesh_field_raw_faces(
                        device_,
                        wz::gpu::MeshFieldRawFaceUploadDesc{
                            .mesh = mesh_data,
                            .field = field_data,
                            .channel_ids = channels.data(),
                            .channel_count =
                                static_cast<uint32_t>(channels.size()),
                        });
            }
            else {
                mesh_field_visualization_resource =
                    wz::gpu::upload_mesh_field_visualization(
                        device_,
                        wz::gpu::MeshFieldVisualizationUploadDesc{
                            .mesh = mesh_data,
                            .field = field_data,
                            .channel_id =
                                renderable.mesh_style
                                    .field_visualization.channel_id,
                        });
            }
            if (!mesh_field_visualization_resource.valid()) {
                return false;
            }

            preview_gpu_resources_.emplace_back(
                preview_release_queue_,
                mesh_field_visualization_resource);
        }

        std::span<const wz::engine::assets::TerrainVisualChunk> terrain_chunks{};
        std::span<const TerrainFarSplatChunk> terrain_far_splat_chunks{};
        std::span<const TerrainTransitionDrawRange> terrain_transition_ranges{};
        if (terrain_data) {
            terrain_chunks = terrain_chunks_storage;
            terrain_far_splat_chunks = terrain_far_splat_chunks_storage;
            terrain_transition_ranges = terrain_transition_ranges_storage;
        }

        const wz::scene::MeshHandle scene_mesh =
            render_resolver_.register_mesh(
                gpu_mesh,
                renderable.program,
                renderable.render_program,
                renderable.terrain_lighting,
                renderable.terrain_target_pixels_per_triangle,
                renderable.mesh_style,
                terrain_chunks,
                terrain_far_splat_chunks,
                terrain_transition_ranges,
                mesh_field_visualization_resource);

        descriptor.mesh = scene_mesh;
        if (!wz::engine::assets::is_mesh_render_style_drawable(
                renderable.mesh_style))
        {
            descriptor.node_class.default_surface =
                wz::scene::SurfaceClass::None;
            descriptor.node_class.domains = 0;
        }
        if (renderable.program
            == wz::engine::assets::BuiltinRenderProgram::TerrainMeshSurface)
        {
            const wz::engine::assets::TerrainProxyId terrain_proxy_id{
                renderable.companion_asset,
            };
            render_resolver_.register_terrain_proxy(
                terrain_proxy_id,
                gpu_mesh,
                renderable.program,
                renderable.render_program,
                renderable.terrain_lighting,
                renderable.terrain_target_pixels_per_triangle,
                renderable.mesh_style,
                terrain_chunks,
                terrain_far_splat_chunks,
                terrain_transition_ranges);
            descriptor.terrain_visual_proxy_asset =
                renderable.companion_asset;
            descriptor.terrain_proxy_id =
                wz::scene::TerrainProxyId{ terrain_proxy_id.key };
            descriptor.terrain_visual_proxy_data = terrain_visual_proxy_data;
            descriptor.terrain_visual_chunk_count =
                static_cast<uint32_t>(terrain_chunks.size());
        }
        descriptor.material = wz::scene::INVALID_MATERIAL;

        return true;
    }
}
