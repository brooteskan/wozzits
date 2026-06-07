#include <engine/assets/terrain/terrain_visual_proxy_compilers.h>

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/disk_cache_keys.h>
#include <engine/assets/disk_cache_paths.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/engine_asset_library_internal.h>

#include <algorithm>
#include <array>
#include <any>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr uint32_t kTerrainVisualProxyDiskCacheMagic = 0x56505a57u;
        constexpr uint32_t kTerrainVisualProxyDiskCacheVersion = 3u;
        constexpr uint32_t kTerrainVisualProxyTargetLodCount = 4u;
        constexpr float kTerrainVisualProxyEdgeEpsilon = 1e-5f;

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

        TerrainVisualProxyBounds empty_bounds()
        {
            TerrainVisualProxyBounds bounds{};
            for (uint32_t axis = 0; axis < 3u; ++axis) {
                bounds.min[axis] = (std::numeric_limits<float>::max)();
                bounds.max[axis] = (std::numeric_limits<float>::lowest)();
            }
            return bounds;
        }

        void expand_bounds(
            TerrainVisualProxyBounds& bounds,
            const float position[3])
        {
            for (uint32_t axis = 0; axis < 3u; ++axis) {
                bounds.min[axis] = std::min(bounds.min[axis], position[axis]);
                bounds.max[axis] = std::max(bounds.max[axis], position[axis]);
            }
        }

        float distance3(const float a[3], const float b[3])
        {
            const float dx = a[0] - b[0];
            const float dy = a[1] - b[1];
            const float dz = a[2] - b[2];
            return std::sqrt(dx * dx + dy * dy + dz * dz);
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

        bool nearly_equal(float a, float b) noexcept
        {
            return std::abs(a - b) <= kTerrainVisualProxyEdgeEpsilon;
        }

        void append_boundary_point(
            std::vector<TerrainVisualProxyBoundaryPoint>& points,
            const float position[3])
        {
            TerrainVisualProxyBoundaryPoint point{};
            for (uint32_t axis = 0; axis < 3u; ++axis) {
                point.position[axis] = position[axis];
            }
            points.push_back(point);
        }

        void sort_boundary_ring(TerrainVisualProxyLodBoundaryRing& ring)
        {
            const auto by_z = [](const auto& lhs, const auto& rhs) {
                if (lhs.position[2] != rhs.position[2]) {
                    return lhs.position[2] < rhs.position[2];
                }
                return lhs.position[1] < rhs.position[1];
            };
            const auto by_x = [](const auto& lhs, const auto& rhs) {
                if (lhs.position[0] != rhs.position[0]) {
                    return lhs.position[0] < rhs.position[0];
                }
                return lhs.position[1] < rhs.position[1];
            };
            std::sort(ring.negative_x.begin(), ring.negative_x.end(), by_z);
            std::sort(ring.positive_x.begin(), ring.positive_x.end(), by_z);
            std::sort(ring.negative_z.begin(), ring.negative_z.end(), by_x);
            std::sort(ring.positive_z.begin(), ring.positive_z.end(), by_x);
        }

        float boundary_parameter(
            TerrainVisualProxyBoundaryEdge edge,
            const TerrainVisualProxyBoundaryPoint& point) noexcept
        {
            switch (edge) {
            case TerrainVisualProxyBoundaryEdge::NegativeX:
            case TerrainVisualProxyBoundaryEdge::PositiveX:
                return point.position[2];
            case TerrainVisualProxyBoundaryEdge::NegativeZ:
            case TerrainVisualProxyBoundaryEdge::PositiveZ:
                return point.position[0];
            }
            return 0.0f;
        }

        const std::vector<TerrainVisualProxyBoundaryPoint>& boundary_points(
            const TerrainVisualProxyLodBoundaryRing& ring,
            TerrainVisualProxyBoundaryEdge edge) noexcept
        {
            switch (edge) {
            case TerrainVisualProxyBoundaryEdge::NegativeX:
                return ring.negative_x;
            case TerrainVisualProxyBoundaryEdge::PositiveX:
                return ring.positive_x;
            case TerrainVisualProxyBoundaryEdge::NegativeZ:
                return ring.negative_z;
            case TerrainVisualProxyBoundaryEdge::PositiveZ:
                return ring.positive_z;
            }
            return ring.negative_x;
        }

        TerrainVisualProxyBoundaryEdge opposite_boundary_edge(
            TerrainVisualProxyBoundaryEdge edge) noexcept
        {
            switch (edge) {
            case TerrainVisualProxyBoundaryEdge::NegativeX:
                return TerrainVisualProxyBoundaryEdge::PositiveX;
            case TerrainVisualProxyBoundaryEdge::PositiveX:
                return TerrainVisualProxyBoundaryEdge::NegativeX;
            case TerrainVisualProxyBoundaryEdge::NegativeZ:
                return TerrainVisualProxyBoundaryEdge::PositiveZ;
            case TerrainVisualProxyBoundaryEdge::PositiveZ:
                return TerrainVisualProxyBoundaryEdge::NegativeZ;
            }
            return TerrainVisualProxyBoundaryEdge::NegativeX;
        }

        std::vector<TerrainVisualProxyBoundaryPoint> normalized_boundary(
            const std::vector<TerrainVisualProxyBoundaryPoint>& points,
            TerrainVisualProxyBoundaryEdge edge)
        {
            std::vector<TerrainVisualProxyBoundaryPoint> out = points;
            std::sort(
                out.begin(),
                out.end(),
                [edge](const auto& lhs, const auto& rhs) {
                    return boundary_parameter(edge, lhs)
                        < boundary_parameter(edge, rhs);
                });
            out.erase(
                std::unique(
                    out.begin(),
                    out.end(),
                    [edge](const auto& lhs, const auto& rhs) {
                        return nearly_equal(
                            boundary_parameter(edge, lhs),
                            boundary_parameter(edge, rhs));
                    }),
                out.end());
            return out;
        }

        TerrainVisualProxyTransitionVertex sample_boundary(
            const std::vector<TerrainVisualProxyBoundaryPoint>& points,
            TerrainVisualProxyBoundaryEdge edge,
            float parameter)
        {
            TerrainVisualProxyTransitionVertex out{};
            if (points.empty()) {
                return out;
            }
            if (points.size() == 1u
                || parameter <= boundary_parameter(edge, points.front()))
            {
                std::copy(
                    std::begin(points.front().position),
                    std::end(points.front().position),
                    std::begin(out.position));
                return out;
            }
            if (parameter >= boundary_parameter(edge, points.back())) {
                std::copy(
                    std::begin(points.back().position),
                    std::end(points.back().position),
                    std::begin(out.position));
                return out;
            }

            for (size_t i = 0u; i + 1u < points.size(); ++i) {
                const float a = boundary_parameter(edge, points[i]);
                const float b = boundary_parameter(edge, points[i + 1u]);
                if (parameter < a || parameter > b || nearly_equal(a, b)) {
                    continue;
                }
                const float t = (parameter - a) / (b - a);
                for (uint32_t axis = 0u; axis < 3u; ++axis) {
                    out.position[axis] =
                        points[i].position[axis]
                        + (points[i + 1u].position[axis]
                           - points[i].position[axis])
                            * t;
                }
                return out;
            }

            std::copy(
                std::begin(points.back().position),
                std::end(points.back().position),
                std::begin(out.position));
            return out;
        }

        void append_transition_triangle(
            TerrainVisualProxyTransitionStrip& strip,
            uint32_t a,
            uint32_t b,
            uint32_t c)
        {
            strip.indices.push_back(a);
            strip.indices.push_back(b);
            strip.indices.push_back(c);
        }

        TerrainVisualProxyTransitionStrip build_transition_strip(
            TerrainChunkId chunk_id,
            TerrainChunkId neighbor_chunk_id,
            const TerrainVisualProxyLodRecord& lod,
            const TerrainVisualProxyLodRecord& neighbor_lod,
            TerrainVisualProxyBoundaryEdge edge)
        {
            TerrainVisualProxyTransitionStrip strip{};
            strip.chunk_id = chunk_id;
            strip.neighbor_chunk_id = neighbor_chunk_id;
            strip.lod_id = lod.lod_id;
            strip.neighbor_lod_id = neighbor_lod.lod_id;
            strip.edge = edge;

            if (lod.lod_id == neighbor_lod.lod_id) {
                return strip;
            }

            const TerrainVisualProxyBoundaryEdge neighbor_edge =
                opposite_boundary_edge(edge);
            const std::vector<TerrainVisualProxyBoundaryPoint> points =
                normalized_boundary(
                    boundary_points(lod.boundary_ring, edge),
                    edge);
            const std::vector<TerrainVisualProxyBoundaryPoint> neighbor_points =
                normalized_boundary(
                    boundary_points(neighbor_lod.boundary_ring, neighbor_edge),
                    neighbor_edge);
            if (points.empty() || neighbor_points.empty()) {
                return strip;
            }

            const float min_parameter = std::max(
                boundary_parameter(edge, points.front()),
                boundary_parameter(neighbor_edge, neighbor_points.front()));
            const float max_parameter = std::min(
                boundary_parameter(edge, points.back()),
                boundary_parameter(neighbor_edge, neighbor_points.back()));
            if (max_parameter - min_parameter
                <= kTerrainVisualProxyEdgeEpsilon)
            {
                return strip;
            }

            std::vector<float> parameters;
            parameters.reserve(points.size() + neighbor_points.size());
            parameters.push_back(min_parameter);
            parameters.push_back(max_parameter);
            for (const auto& point : points) {
                const float parameter = boundary_parameter(edge, point);
                if (parameter >= min_parameter
                    && parameter <= max_parameter)
                {
                    parameters.push_back(parameter);
                }
            }
            for (const auto& point : neighbor_points) {
                const float parameter =
                    boundary_parameter(neighbor_edge, point);
                if (parameter >= min_parameter
                    && parameter <= max_parameter)
                {
                    parameters.push_back(parameter);
                }
            }
            std::sort(parameters.begin(), parameters.end());
            parameters.erase(
                std::unique(
                    parameters.begin(),
                    parameters.end(),
                    [](float a, float b) { return nearly_equal(a, b); }),
                parameters.end());
            if (parameters.size() < 2u) {
                return strip;
            }

            strip.vertices.reserve(parameters.size() * 2u);
            for (float parameter : parameters) {
                TerrainVisualProxyTransitionVertex owner_vertex =
                    sample_boundary(points, edge, parameter);
                owner_vertex.side = 0u;
                strip.vertices.push_back(owner_vertex);

                TerrainVisualProxyTransitionVertex neighbor_vertex =
                    sample_boundary(
                        neighbor_points,
                        neighbor_edge,
                        parameter);
                neighbor_vertex.side = 1u;
                strip.vertices.push_back(neighbor_vertex);
            }

            strip.indices.reserve((parameters.size() - 1u) * 6u);
            for (uint32_t i = 0u; i + 1u < parameters.size(); ++i) {
                const uint32_t a0 = i * 2u;
                const uint32_t b0 = a0 + 1u;
                const uint32_t a1 = (i + 1u) * 2u;
                const uint32_t b1 = a1 + 1u;
                append_transition_triangle(strip, a0, b0, a1);
                append_transition_triangle(strip, a1, b0, b1);
            }
            return strip;
        }

        struct SimplifiedVertex
        {
            float position[3]{};
            uint32_t count = 0;
        };

        struct SimplifiedChunkLod
        {
            uint32_t triangle_count = 0;
            uint32_t vertex_count = 0;
            float conservative_error = 0.0f;
            TerrainVisualProxyBounds bounds{};
            TerrainVisualProxyLodBoundaryRing boundary_ring{};
        };

        struct ChunkVertexMap
        {
            std::vector<uint32_t> unique_vertices;
            std::vector<uint32_t> index_local_vertices;
        };

        ChunkVertexMap make_chunk_vertex_map(
            const TerrainAssetData& terrain,
            const TerrainVisualChunk& chunk)
        {
            ChunkVertexMap out{};
            out.unique_vertices.reserve(chunk.index_count);
            out.index_local_vertices.reserve(chunk.index_count);

            const uint32_t end = std::min(
                static_cast<uint32_t>(terrain.mesh_visual_indices.size()),
                chunk.first_index + chunk.index_count);
            for (uint32_t i = chunk.first_index; i < end; ++i) {
                const uint32_t vertex = terrain.mesh_visual_indices[i];
                if (vertex * 3u + 2u < terrain.mesh_surface_points.size()) {
                    out.unique_vertices.push_back(vertex);
                }
            }
            std::sort(out.unique_vertices.begin(), out.unique_vertices.end());
            out.unique_vertices.erase(
                std::unique(
                    out.unique_vertices.begin(),
                    out.unique_vertices.end()),
                out.unique_vertices.end());

            for (uint32_t i = chunk.first_index; i < end; ++i) {
                const uint32_t vertex = terrain.mesh_visual_indices[i];
                const auto it = std::lower_bound(
                    out.unique_vertices.begin(),
                    out.unique_vertices.end(),
                    vertex);
                out.index_local_vertices.push_back(
                    it != out.unique_vertices.end() && *it == vertex
                        ? static_cast<uint32_t>(
                            it - out.unique_vertices.begin())
                        : UINT32_MAX);
            }
            return out;
        }

        SimplifiedChunkLod simplify_chunk_for_grid(
            const TerrainAssetData& terrain,
            const TerrainVisualChunk& chunk,
            const ChunkVertexMap& chunk_vertices,
            uint32_t grid_cells)
        {
            // Deterministic vertex clustering is intentionally used here as a
            // robust first compiler pass: it handles pathological fixture
            // geometry cheaply, at the cost of looser error bounds than QEM.
            SimplifiedChunkLod out{};
            out.bounds = empty_bounds();
            const float extent_x = chunk.bounds_max[0] - chunk.bounds_min[0];
            const float extent_z = chunk.bounds_max[2] - chunk.bounds_min[2];
            if (extent_x <= kTerrainVisualProxyEdgeEpsilon
                || extent_z <= kTerrainVisualProxyEdgeEpsilon)
            {
                return out;
            }

            const uint32_t grid_vertices = grid_cells + 1u;
            std::vector<SimplifiedVertex> vertices(
                static_cast<size_t>(grid_vertices) * grid_vertices);

            const auto cluster_for_position =
                [&](const float position[3]) -> uint32_t {
                    const float ux = std::clamp(
                        (position[0] - chunk.bounds_min[0]) / extent_x,
                        0.0f,
                        1.0f);
                    const float uz = std::clamp(
                        (position[2] - chunk.bounds_min[2]) / extent_z,
                        0.0f,
                        1.0f);
                    const uint32_t gx = std::min(
                        grid_cells,
                        static_cast<uint32_t>(
                            ux * static_cast<float>(grid_cells) + 0.5f));
                    const uint32_t gz = std::min(
                        grid_cells,
                        static_cast<uint32_t>(
                            uz * static_cast<float>(grid_cells) + 0.5f));
                    return gz * grid_vertices + gx;
                };

            std::vector<uint32_t> vertex_clusters(
                chunk_vertices.unique_vertices.size(),
                UINT32_MAX);
            float min_height = (std::numeric_limits<float>::max)();
            float max_height = (std::numeric_limits<float>::lowest)();

            for (uint32_t vertex_index = 0u;
                 vertex_index < chunk_vertices.unique_vertices.size();
                 ++vertex_index)
            {
                const uint32_t source_vertex =
                    chunk_vertices.unique_vertices[vertex_index];
                const float* position =
                    terrain.mesh_surface_points.data() + source_vertex * 3u;
                min_height = std::min(min_height, position[1]);
                max_height = std::max(max_height, position[1]);
                const uint32_t cluster = cluster_for_position(position);
                vertex_clusters[vertex_index] = cluster;
                SimplifiedVertex& vertex = vertices[cluster];
                for (uint32_t axis = 0; axis < 3u; ++axis) {
                    vertex.position[axis] += position[axis];
                }
                ++vertex.count;
            }

            std::vector<uint32_t> compact_index(vertices.size(), UINT32_MAX);
            for (uint32_t i = 0; i < vertices.size(); ++i) {
                SimplifiedVertex& vertex = vertices[i];
                if (vertex.count == 0u) {
                    continue;
                }
                const float inv = 1.0f / static_cast<float>(vertex.count);
                for (uint32_t axis = 0; axis < 3u; ++axis) {
                    vertex.position[axis] *= inv;
                }
                compact_index[i] = out.vertex_count++;
                expand_bounds(out.bounds, vertex.position);
            }

            std::set<std::array<uint32_t, 3>> emitted;
            for (uint32_t i = 0u; i + 2u < chunk.index_count; i += 3u) {
                uint32_t mapped[3]{};
                bool valid = true;
                for (uint32_t corner = 0; corner < 3u; ++corner) {
                    const uint32_t local =
                        i + corner < chunk_vertices.index_local_vertices.size()
                            ? chunk_vertices.index_local_vertices[i + corner]
                            : UINT32_MAX;
                    if (local == UINT32_MAX
                        || local >= vertex_clusters.size())
                    {
                        valid = false;
                        break;
                    }
                    const uint32_t cluster =
                        vertex_clusters[local];
                    if (cluster == UINT32_MAX
                        || cluster >= compact_index.size()
                        || compact_index[cluster] == UINT32_MAX)
                    {
                        valid = false;
                        break;
                    }
                    mapped[corner] = compact_index[cluster];
                }
                if (!valid
                    || mapped[0] == mapped[1]
                    || mapped[1] == mapped[2]
                    || mapped[2] == mapped[0])
                {
                    continue;
                }
                std::array<uint32_t, 3> key{
                    mapped[0],
                    mapped[1],
                    mapped[2],
                };
                std::sort(key.begin(), key.end());
                if (emitted.insert(key).second) {
                    ++out.triangle_count;
                }
            }

            if (out.triangle_count == 0u || out.vertex_count == 0u) {
                out = {};
                return out;
            }

            auto mapped_position =
                [&](uint32_t local_vertex) -> const float* {
                    const uint32_t cluster = vertex_clusters[local_vertex];
                    return vertices[cluster].position;
                };

            for (uint32_t local_vertex = 0u;
                 local_vertex < chunk_vertices.unique_vertices.size();
                 ++local_vertex)
            {
                const uint32_t source_vertex =
                    chunk_vertices.unique_vertices[local_vertex];
                const float* source_position =
                    terrain.mesh_surface_points.data() + source_vertex * 3u;
                out.conservative_error = std::max(
                    out.conservative_error,
                    distance3(source_position, mapped_position(local_vertex)));

                const float* simplified = mapped_position(local_vertex);
                // Corner vertices intentionally belong to both adjacent edge
                // rings; #133 can deduplicate per transition strip if needed.
                if (nearly_equal(source_position[0], chunk.bounds_min[0])) {
                    append_boundary_point(out.boundary_ring.negative_x, simplified);
                }
                if (nearly_equal(source_position[0], chunk.bounds_max[0])) {
                    append_boundary_point(out.boundary_ring.positive_x, simplified);
                }
                if (nearly_equal(source_position[2], chunk.bounds_min[2])) {
                    append_boundary_point(out.boundary_ring.negative_z, simplified);
                }
                if (nearly_equal(source_position[2], chunk.bounds_max[2])) {
                    append_boundary_point(out.boundary_ring.positive_z, simplified);
                }
            }

            for (uint32_t i = 0u; i + 2u < chunk.index_count; i += 3u) {
                uint32_t local[3]{};
                bool valid = true;
                for (uint32_t corner = 0; corner < 3u; ++corner) {
                    local[corner] =
                        i + corner < chunk_vertices.index_local_vertices.size()
                            ? chunk_vertices.index_local_vertices[i + corner]
                            : UINT32_MAX;
                    if (local[corner] == UINT32_MAX
                        || local[corner] >= vertex_clusters.size()
                        || vertex_clusters[local[corner]] == UINT32_MAX)
                    {
                        valid = false;
                        break;
                    }
                }
                if (!valid) {
                    continue;
                }

                const uint32_t base = chunk.first_index + i;
                if (base + 2u >= terrain.mesh_visual_indices.size()) {
                    continue;
                }
                const uint32_t source_indices[3]{
                    terrain.mesh_visual_indices[base + 0u],
                    terrain.mesh_visual_indices[base + 1u],
                    terrain.mesh_visual_indices[base + 2u],
                };
                if (source_indices[0] * 3u + 2u
                        >= terrain.mesh_surface_points.size()
                    || source_indices[1] * 3u + 2u
                        >= terrain.mesh_surface_points.size()
                    || source_indices[2] * 3u + 2u
                        >= terrain.mesh_surface_points.size())
                {
                    continue;
                }

                float source_mid[3][3]{};
                float simplified_mid[3][3]{};
                float source_centroid[3]{};
                float simplified_centroid[3]{};
                const float* s[3]{
                    terrain.mesh_surface_points.data()
                        + source_indices[0] * 3u,
                    terrain.mesh_surface_points.data()
                        + source_indices[1] * 3u,
                    terrain.mesh_surface_points.data()
                        + source_indices[2] * 3u,
                };
                const float* l[3]{
                    mapped_position(local[0]),
                    mapped_position(local[1]),
                    mapped_position(local[2]),
                };
                for (uint32_t axis = 0; axis < 3u; ++axis) {
                    for (uint32_t edge = 0u; edge < 3u; ++edge) {
                        const uint32_t next = (edge + 1u) % 3u;
                        source_mid[edge][axis] =
                            (s[edge][axis] + s[next][axis]) * 0.5f;
                        simplified_mid[edge][axis] =
                            (l[edge][axis] + l[next][axis]) * 0.5f;
                    }
                    source_centroid[axis] =
                        (s[0][axis] + s[1][axis] + s[2][axis]) / 3.0f;
                    simplified_centroid[axis] =
                        (l[0][axis] + l[1][axis] + l[2][axis]) / 3.0f;
                }
                for (uint32_t edge = 0u; edge < 3u; ++edge) {
                    out.conservative_error = std::max(
                        out.conservative_error,
                        distance3(source_mid[edge], simplified_mid[edge]));
                }
                out.conservative_error = std::max(
                    out.conservative_error,
                    distance3(source_centroid, simplified_centroid));
            }

            sort_boundary_ring(out.boundary_ring);
            if (max_height - min_height <= kTerrainVisualProxyEdgeEpsilon) {
                // For planar terrain, tessellation changes are visually exact in
                // this terrain-height error model even when XZ vertices move.
                out.conservative_error = 0.0f;
            }
            out.conservative_error =
                std::max(0.0f, out.conservative_error);
            return out;
        }

        void assign_chunk_neighbors(std::vector<TerrainVisualProxyChunkRecord>& chunks)
        {
            for (uint32_t i = 0; i < chunks.size(); ++i) {
                auto& a = chunks[i];
                for (uint32_t j = 0; j < chunks.size(); ++j) {
                    if (i == j) {
                        continue;
                    }
                    const auto& b = chunks[j];
                    const bool z_overlap =
                        a.bounds.min[2] <= b.bounds.max[2]
                        && b.bounds.min[2] <= a.bounds.max[2];
                    const bool x_overlap =
                        a.bounds.min[0] <= b.bounds.max[0]
                        && b.bounds.min[0] <= a.bounds.max[0];
                    if (z_overlap && nearly_equal(a.bounds.min[0], b.bounds.max[0])) {
                        a.boundary.negative_x_neighbor = b.chunk_id;
                    }
                    if (z_overlap && nearly_equal(a.bounds.max[0], b.bounds.min[0])) {
                        a.boundary.positive_x_neighbor = b.chunk_id;
                    }
                    if (x_overlap && nearly_equal(a.bounds.min[2], b.bounds.max[2])) {
                        a.boundary.negative_z_neighbor = b.chunk_id;
                    }
                    if (x_overlap && nearly_equal(a.bounds.max[2], b.bounds.min[2])) {
                        a.boundary.positive_z_neighbor = b.chunk_id;
                    }
                }
            }
        }

        TerrainVisualProxyChunkRecord* find_chunk(
            std::vector<TerrainVisualProxyChunkRecord>& chunks,
            TerrainChunkId chunk_id) noexcept
        {
            for (TerrainVisualProxyChunkRecord& chunk : chunks) {
                if (chunk.chunk_id == chunk_id) {
                    return &chunk;
                }
            }
            return nullptr;
        }

        void append_transition_strips_for_neighbor(
            TerrainVisualProxyChunkRecord& chunk,
            const TerrainVisualProxyChunkRecord& neighbor,
            TerrainVisualProxyBoundaryEdge edge)
        {
            if (chunk.chunk_id.value >= neighbor.chunk_id.value) {
                return;
            }

            // Precompute every mixed LOD pair so runtime selection is a simple
            // lookup keyed by (chunk, neighbor, edge, lod, neighbor_lod). This
            // is O(L^2) per neighbor edge; keep transition counts visible in
            // tests before increasing kTerrainVisualProxyTargetLodCount.
            for (const TerrainVisualProxyLodRecord& lod : chunk.lods) {
                for (const TerrainVisualProxyLodRecord& neighbor_lod :
                     neighbor.lods)
                {
                    TerrainVisualProxyTransitionStrip strip =
                        build_transition_strip(
                            chunk.chunk_id,
                            neighbor.chunk_id,
                            lod,
                            neighbor_lod,
                            edge);
                    if (strip.valid()) {
                        chunk.transition_strips.push_back(std::move(strip));
                    }
                }
            }
        }

        void generate_transition_strips(
            std::vector<TerrainVisualProxyChunkRecord>& chunks)
        {
            for (TerrainVisualProxyChunkRecord& chunk : chunks) {
                if (chunk.boundary.negative_x_neighbor
                    != kInvalidTerrainChunkId)
                {
                    if (const TerrainVisualProxyChunkRecord* neighbor =
                            find_chunk(
                                chunks,
                                chunk.boundary.negative_x_neighbor))
                    {
                        append_transition_strips_for_neighbor(
                            chunk,
                            *neighbor,
                            TerrainVisualProxyBoundaryEdge::NegativeX);
                    }
                }
                if (chunk.boundary.positive_x_neighbor
                    != kInvalidTerrainChunkId)
                {
                    if (const TerrainVisualProxyChunkRecord* neighbor =
                            find_chunk(
                                chunks,
                                chunk.boundary.positive_x_neighbor))
                    {
                        append_transition_strips_for_neighbor(
                            chunk,
                            *neighbor,
                            TerrainVisualProxyBoundaryEdge::PositiveX);
                    }
                }
                if (chunk.boundary.negative_z_neighbor
                    != kInvalidTerrainChunkId)
                {
                    if (const TerrainVisualProxyChunkRecord* neighbor =
                            find_chunk(
                                chunks,
                                chunk.boundary.negative_z_neighbor))
                    {
                        append_transition_strips_for_neighbor(
                            chunk,
                            *neighbor,
                            TerrainVisualProxyBoundaryEdge::NegativeZ);
                    }
                }
                if (chunk.boundary.positive_z_neighbor
                    != kInvalidTerrainChunkId)
                {
                    if (const TerrainVisualProxyChunkRecord* neighbor =
                            find_chunk(
                                chunks,
                                chunk.boundary.positive_z_neighbor))
                    {
                        append_transition_strips_for_neighbor(
                            chunk,
                            *neighbor,
                            TerrainVisualProxyBoundaryEdge::PositiveZ);
                    }
                }
            }
        }

        TerrainVisualProxyData compile_multi_lod_proxy(
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
                lod.bounds = chunk.bounds;
                lod.first_index = source.first_index;
                lod.index_count = source.index_count;
                lod.first_vertex = chunk.first_vertex;
                lod.vertex_count = chunk.vertex_count;
                lod.triangle_count = chunk.triangle_count;
                lod.conservative_geometric_error = 0.0f;
                lod.mesh_asset = terrain.mesh;
                lod.source_region_aggregate = chunk.aggregate;
                lod.lod_surface_aggregate = chunk.aggregate;
                const ChunkVertexMap chunk_vertices =
                    make_chunk_vertex_map(terrain, source);
                {
                    for (uint32_t vertex : chunk_vertices.unique_vertices) {
                        const float* position =
                            terrain.mesh_surface_points.data() + vertex * 3u;
                        if (nearly_equal(position[0], source.bounds_min[0])) {
                            append_boundary_point(
                                lod.boundary_ring.negative_x,
                                position);
                        }
                        if (nearly_equal(position[0], source.bounds_max[0])) {
                            append_boundary_point(
                                lod.boundary_ring.positive_x,
                                position);
                        }
                        if (nearly_equal(position[2], source.bounds_min[2])) {
                            append_boundary_point(
                                lod.boundary_ring.negative_z,
                                position);
                        }
                        if (nearly_equal(position[2], source.bounds_max[2])) {
                            append_boundary_point(
                                lod.boundary_ring.positive_z,
                                position);
                        }
                    }
                    sort_boundary_ring(lod.boundary_ring);
                }
                chunk.lods.push_back(std::move(lod));

                uint32_t previous_triangles = chunk.triangle_count;
                float previous_error = 0.0f;
                for (uint32_t lod_index = 1u;
                     lod_index < kTerrainVisualProxyTargetLodCount;
                     ++lod_index)
                {
                    if (previous_triangles <= 2u) {
                        break;
                    }
                    const uint32_t target_triangles = std::max(
                        2u,
                        previous_triangles / 2u);
                    const uint32_t grid_cells = std::max(
                        1u,
                        static_cast<uint32_t>(
                            std::sqrt(
                                static_cast<float>(target_triangles)
                                * 0.5f)));
                    SimplifiedChunkLod simplified =
                        simplify_chunk_for_grid(
                            terrain,
                            source,
                            chunk_vertices,
                            grid_cells);
                    if (simplified.triangle_count == 0u
                        || simplified.triangle_count > previous_triangles)
                    {
                        break;
                    }

                    TerrainVisualProxyLodRecord coarse{};
                    coarse.lod_id = TerrainLodId{ lod_index };
                    coarse.representation_id = chunk.representation_id;
                    coarse.representation_kind = chunk.representation_kind;
                    // Coarse bounds are derived from clustered vertices, so
                    // they may shrink relative to the source chunk bounds.
                    coarse.bounds = simplified.bounds;
                    // #126 currently persists the LOD selection metadata. The
                    // renderer still needs a follow-up drawable-geometry store
                    // before these spans can identify distinct LOD buffers.
                    coarse.first_index = source.first_index;
                    coarse.index_count = simplified.triangle_count * 3u;
                    coarse.first_vertex = chunk.first_vertex;
                    coarse.vertex_count = simplified.vertex_count;
                    coarse.triangle_count = simplified.triangle_count;
                    coarse.conservative_geometric_error = std::max(
                        previous_error,
                        simplified.conservative_error);
                    coarse.mesh_asset = terrain.mesh;
                    coarse.source_region_aggregate = chunk.aggregate;
                    coarse.lod_surface_aggregate = chunk.aggregate;
                    coarse.lod_surface_aggregate.height_variance =
                        chunk.aggregate.height_variance
                        + coarse.conservative_geometric_error
                            * coarse.conservative_geometric_error;
                    coarse.boundary_ring = std::move(simplified.boundary_ring);

                    previous_triangles = coarse.triangle_count;
                    previous_error = coarse.conservative_geometric_error;
                    chunk.lods.push_back(std::move(coarse));
                }

                proxy.chunks.push_back(std::move(chunk));
            }

            assign_chunk_neighbors(proxy.chunks);
            generate_transition_strips(proxy.chunks);

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

        void append_boundary_points(
            std::vector<uint8_t>& out,
            const std::vector<TerrainVisualProxyBoundaryPoint>& points)
        {
            append_scalar(out, static_cast<uint64_t>(points.size()));
            for (const auto& point : points) {
                append_float_array(out, point.position, 3u);
            }
        }

        bool read_boundary_points(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            std::vector<TerrainVisualProxyBoundaryPoint>& points)
        {
            uint64_t count = 0u;
            if (!read_scalar(bytes, offset, count) || count > 100000u) {
                return false;
            }
            points.resize(static_cast<size_t>(count));
            for (auto& point : points) {
                if (!read_float_array(bytes, offset, point.position, 3u)) {
                    return false;
                }
            }
            return true;
        }

        void append_boundary_ring(
            std::vector<uint8_t>& out,
            const TerrainVisualProxyLodBoundaryRing& ring)
        {
            append_boundary_points(out, ring.negative_x);
            append_boundary_points(out, ring.positive_x);
            append_boundary_points(out, ring.negative_z);
            append_boundary_points(out, ring.positive_z);
        }

        bool read_boundary_ring(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            TerrainVisualProxyLodBoundaryRing& ring)
        {
            return read_boundary_points(bytes, offset, ring.negative_x)
                && read_boundary_points(bytes, offset, ring.positive_x)
                && read_boundary_points(bytes, offset, ring.negative_z)
                && read_boundary_points(bytes, offset, ring.positive_z);
        }

        void append_transition_strip(
            std::vector<uint8_t>& out,
            const TerrainVisualProxyTransitionStrip& strip)
        {
            append_scalar(out, strip.chunk_id.value);
            append_scalar(out, strip.neighbor_chunk_id.value);
            append_scalar(out, strip.lod_id.value);
            append_scalar(out, strip.neighbor_lod_id.value);
            append_scalar(out, static_cast<uint8_t>(strip.edge));
            append_scalar(out, static_cast<uint64_t>(strip.vertices.size()));
            for (const auto& vertex : strip.vertices) {
                append_float_array(out, vertex.position, 3u);
                append_scalar(out, vertex.side);
            }
            append_scalar(out, static_cast<uint64_t>(strip.indices.size()));
            for (uint32_t index : strip.indices) {
                append_scalar(out, index);
            }
        }

        bool read_transition_strip(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            TerrainVisualProxyTransitionStrip& strip)
        {
            uint8_t edge = 0u;
            uint64_t vertex_count = 0u;
            uint64_t index_count = 0u;
            if (!read_scalar(bytes, offset, strip.chunk_id.value)
                || !read_scalar(bytes, offset, strip.neighbor_chunk_id.value)
                || !read_scalar(bytes, offset, strip.lod_id.value)
                || !read_scalar(bytes, offset, strip.neighbor_lod_id.value)
                || !read_scalar(bytes, offset, edge)
                || !read_scalar(bytes, offset, vertex_count))
            {
                return false;
            }
            if (vertex_count > 100000u) {
                return false;
            }
            strip.edge = static_cast<TerrainVisualProxyBoundaryEdge>(edge);
            strip.vertices.resize(static_cast<size_t>(vertex_count));
            for (auto& vertex : strip.vertices) {
                if (!read_float_array(bytes, offset, vertex.position, 3u)
                    || !read_scalar(bytes, offset, vertex.side)
                    || vertex.side > 1u)
                {
                    return false;
                }
            }

            if (!read_scalar(bytes, offset, index_count)
                || index_count > 300000u)
            {
                return false;
            }
            strip.indices.resize(static_cast<size_t>(index_count));
            for (uint32_t& index : strip.indices) {
                if (!read_scalar(bytes, offset, index)
                    || index >= strip.vertices.size())
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
            append_bounds(out, lod.bounds);
            append_scalar(out, lod.first_index);
            append_scalar(out, lod.index_count);
            append_scalar(out, lod.first_vertex);
            append_scalar(out, lod.vertex_count);
            append_scalar(out, lod.triangle_count);
            append_scalar(out, lod.conservative_geometric_error);
            append_asset_key(out, lod.mesh_asset);
            append_aggregate(out, lod.source_region_aggregate);
            append_aggregate(out, lod.lod_surface_aggregate);
            append_boundary_ring(out, lod.boundary_ring);
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
                || !read_bounds(bytes, offset, lod.bounds)
                || !read_scalar(bytes, offset, lod.first_index)
                || !read_scalar(bytes, offset, lod.index_count)
                || !read_scalar(bytes, offset, lod.first_vertex)
                || !read_scalar(bytes, offset, lod.vertex_count)
                || !read_scalar(bytes, offset, lod.triangle_count)
                || !read_scalar(bytes, offset, lod.conservative_geometric_error)
                || !read_asset_key(bytes, offset, lod.mesh_asset)
                || !read_aggregate(bytes, offset, lod.source_region_aggregate)
                || !read_aggregate(bytes, offset, lod.lod_surface_aggregate)
                || !read_boundary_ring(bytes, offset, lod.boundary_ring))
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
                append_scalar(
                    out,
                    static_cast<uint64_t>(chunk.transition_strips.size()));
                for (const auto& strip : chunk.transition_strips) {
                    append_transition_strip(out, strip);
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
                uint64_t transition_count = 0u;
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
                if (!read_scalar(bytes, offset, transition_count)
                    || transition_count > 100000u)
                {
                    return false;
                }
                chunk.transition_strips.resize(
                    static_cast<size_t>(transition_count));
                for (auto& strip : chunk.transition_strips) {
                    if (!read_transition_strip(bytes, offset, strip)) {
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
                    compile_multi_lod_proxy(input.key, *terrain_key, *terrain);
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

    TerrainVisualProxyData compile_terrain_visual_proxy_for_tests(
        const wz::asset::AssetKey& proxy_key,
        const wz::asset::AssetKey& terrain_key,
        const TerrainAssetData& terrain)
    {
        return compile_multi_lod_proxy(proxy_key, terrain_key, terrain);
    }
}
