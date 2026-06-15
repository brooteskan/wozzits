// src/engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy_compilers.cpp

#include <engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy_compilers.h>

#include <engine/assets/compute_pipeline/compute_pipeline.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>
#include <engine/assets/mesh_cluster_hierarchy_asset_module.h>
#include <engine/assets/mesh_sparse_operator/mesh_sparse_operator.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <array>
#include <any>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr std::array<std::string_view, 3> kHierarchyMethodOptions = {
            "Identity",
            "Graph coarsen",
            "Laplacian graph cells",
        };

        template<class Enum, std::size_t Count>
        Enum enum_param(
            const wz::asset::ParamBlock& params,
            std::string_view name,
            Enum fallback,
            const std::array<std::string_view, Count>&)
        {
            const int64_t value =
                params.get<int64_t>(name, static_cast<int64_t>(fallback));
            if (value >= 0 && value < static_cast<int64_t>(Count)) {
                return static_cast<Enum>(value);
            }
            return fallback;
        }

        MeshAsset mesh_asset_from_dep(
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::size_t index)
        {
            MeshAsset asset{};
            if (dep_nodes.size() > index) {
                asset.output = dep_nodes[index].key;
            }
            return asset;
        }

        MeshClusterHierarchyAsset hierarchy_asset_from_dep(
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::size_t index)
        {
            MeshClusterHierarchyAsset asset{};
            if (dep_nodes.size() > index) {
                asset.output = dep_nodes[index].key;
            }
            return asset;
        }

        MeshClusterHierarchyDesc hierarchy_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            MeshClusterHierarchyDesc desc{};
            desc.name = params.get<std::string>("name", {});
            desc.source_mesh = mesh_asset_from_dep(dep_nodes, 0u);
            desc.method =
                enum_param(
                    params,
                    "method",
                    desc.method,
                    kHierarchyMethodOptions);
            desc.max_level_index =
                params.get<uint32_t>(
                    "max_level_index",
                    desc.max_level_index);
            return desc;
        }

        MeshClusterHierarchyPreviewMeshDesc preview_mesh_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            MeshClusterHierarchyPreviewMeshDesc desc{};
            desc.name = params.get<std::string>("name", {});
            desc.hierarchy = hierarchy_asset_from_dep(dep_nodes, 0u);
            desc.level_index =
                params.get<uint32_t>("level_index", desc.level_index);
            return desc;
        }

        wz::asset::AssetNode compiled_node(
            const wz::asset::AssetNode& input,
            wz::asset::ResourceHandle handle)
        {
            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }

        MeshClusterHierarchyData build_identity_hierarchy(
            const MeshClusterHierarchyDesc& desc,
            const MeshData& source_mesh)
        {
            MeshClusterHierarchyData data{};
            data.source_mesh_key = desc.source_mesh.output;
            data.source_topology_hash =
                compute_mesh_topology_hash(source_mesh);
            data.method = desc.method;

            MeshClusterHierarchyLevel level{};
            level.level_index = 0;
            level.cluster_count = source_mesh.index_count() > 0u ? 1u : 0u;
            level.vertex_count = source_mesh.vertex_count();
            level.triangle_count = source_mesh.index_count() / 3u;
            level.conservative_error = 0.0f;
            level.preview_mesh = source_mesh;

            data.levels.push_back(std::move(level));
            return data;
        }

        float distance_squared(const float a[3], const float b[3]) noexcept
        {
            const float dx = a[0] - b[0];
            const float dy = a[1] - b[1];
            const float dz = a[2] - b[2];
            return dx * dx + dy * dy + dz * dz;
        }

        void normalize3(float v[3]) noexcept
        {
            const float len_sq =
                v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
            if (len_sq <= 0.0f) {
                return;
            }
            const float inv_len = 1.0f / std::sqrt(len_sq);
            v[0] *= inv_len;
            v[1] *= inv_len;
            v[2] *= inv_len;
        }

        struct TriangleRecord
        {
            std::array<uint32_t, 3> key{};
            std::array<uint32_t, 3> indices{};
        };

        bool triangle_record_less(
            const TriangleRecord& a,
            const TriangleRecord& b) noexcept
        {
            return a.key < b.key;
        }

        struct EdgeCandidate
        {
            std::array<uint32_t, 2> key{};
            std::array<uint32_t, 2> opposite_vertices{};
            uint32_t a = 0;
            uint32_t b = 0;
            uint32_t adjacent_triangle_count = 0;
            uint32_t opposite_vertex_count = 0;
            float score = 0.0f;
        };

        bool edge_candidate_key_less(
            const EdgeCandidate& a,
            const EdgeCandidate& b) noexcept
        {
            return a.key < b.key;
        }

        bool edge_candidate_score_less(
            const EdgeCandidate& a,
            const EdgeCandidate& b) noexcept
        {
            if (a.score != b.score) {
                return a.score < b.score;
            }
            return a.key < b.key;
        }

        const char* hierarchy_method_name(
            MeshClusterHierarchyBuildMethod method) noexcept
        {
            switch (method) {
            case MeshClusterHierarchyBuildMethod::Identity:
                return "Identity";
            case MeshClusterHierarchyBuildMethod::GraphCoarsen:
                return "GraphCoarsen";
            case MeshClusterHierarchyBuildMethod::LaplacianGraphCells:
                return "LaplacianGraphCells";
            }
            return "Unknown";
        }

        uint32_t hierarchy_max_level_count(
            const MeshClusterHierarchyDesc& desc) noexcept
        {
            constexpr uint32_t kHardMaxLevels = 8u;
            return (std::min)(desc.max_level_index + 1u, kHardMaxLevels);
        }

        struct GraphCoarsenWeights
        {
            bool active = false;
            std::vector<float> vertex_weights;
        };

        const MeshDerivedFieldChannel* find_float1_channel(
            const MeshDerivedFieldData& field,
            uint32_t channel_id) noexcept
        {
            for (const MeshDerivedFieldChannel& channel : field.channels) {
                if (channel.channel_id == channel_id
                    && channel.value_type
                        == MeshDerivedFieldValueType::Float1)
                {
                    return &channel;
                }
            }
            return nullptr;
        }

        bool read_float1(
            const MeshDerivedFieldData& field,
            const MeshDerivedFieldChannel& channel,
            uint32_t element,
            float& out) noexcept
        {
            constexpr size_t kFloatBytes = sizeof(float);
            const size_t offset =
                static_cast<size_t>(channel.byte_offset)
                + static_cast<size_t>(element) * kFloatBytes;
            if (element >= field.element_count
                || offset + kFloatBytes > field.values.size())
            {
                return false;
            }
            std::memcpy(&out, field.values.data() + offset, kFloatBytes);
            return true;
        }

        bool mask_rule_matches(
            const MeshDerivedFieldData& field,
            const MeshMaskRule& rule,
            uint32_t element) noexcept
        {
            if (!rule.enabled) {
                return false;
            }
            const MeshDerivedFieldChannel* channel =
                find_float1_channel(field, rule.input_channel_id);
            if (!channel) {
                return false;
            }

            float value = 0.0f;
            if (!read_float1(field, *channel, element, value)
                || !std::isfinite(value))
            {
                return false;
            }

            const float lo = (std::min)(rule.lo, rule.hi);
            const float hi = (std::max)(rule.lo, rule.hi);
            return value >= lo && value <= hi;
        }

        bool mask_element_matches(
            const MeshDerivedFieldData& field,
            const MeshMaskRenderStyleData& mask,
            uint32_t element) noexcept
        {
            for (const MeshMaskRule& rule : mask.rules) {
                if (mask_rule_matches(field, rule, element)) {
                    return true;
                }
            }
            return false;
        }

        GraphCoarsenWeights graph_coarsen_weights_for_region_mask(
            const MeshClusterHierarchyDesc& desc,
            const MeshData& source_mesh,
            const MeshDerivedFieldData* field,
            wz::Logger& logger)
        {
            GraphCoarsenWeights weights{};
            if (!desc.region_mask
                || !desc.region_mask->mask.enabled
                || desc.region_mask->mask.rules.empty()
                || !field
                || !field->valid())
            {
                return weights;
            }

            const wz::asset::Hash source_topology =
                compute_mesh_topology_hash(source_mesh);
            if (field->source_topology_hash != source_topology) {
                logger.error(
                    "mesh cluster hierarchy region mask field topology "
                    "does not match source mesh");
                return weights;
            }

            const MeshMaskRenderStyleData& mask = desc.region_mask->mask;
            weights.vertex_weights.assign(source_mesh.vertex_count(), 0.0f);

            switch (field->domain) {
            case MeshDerivedFieldDomain::Vertex:
            {
                const uint32_t count =
                    (std::min)(field->element_count,
                        source_mesh.vertex_count());
                for (uint32_t v = 0; v < count; ++v) {
                    if (mask_element_matches(*field, mask, v)) {
                        weights.vertex_weights[v] = 1.0f;
                        weights.active = true;
                    }
                }
                break;
            }

            case MeshDerivedFieldDomain::Face:
            {
                const uint32_t triangle_count =
                    source_mesh.index_count() / 3u;
                const uint32_t count =
                    (std::min)(field->element_count, triangle_count);
                for (uint32_t tri = 0; tri < count; ++tri) {
                    if (!mask_element_matches(*field, mask, tri)) {
                        continue;
                    }
                    weights.active = true;
                    for (uint32_t corner = 0; corner < 3u; ++corner) {
                        const uint32_t vertex =
                            source_mesh.indices[tri * 3u + corner];
                        if (vertex < weights.vertex_weights.size()) {
                            weights.vertex_weights[vertex] = 1.0f;
                        }
                    }
                }
                break;
            }

            case MeshDerivedFieldDomain::Edge:
            case MeshDerivedFieldDomain::Corner:
                logger.error(
                    "mesh cluster hierarchy region mask only supports "
                    "vertex or face fields");
                weights.vertex_weights.clear();
                weights.active = false;
                break;
            }

            return weights;
        }

        bool graph_coarsen_edge_allowed(
            const GraphCoarsenWeights* weights,
            uint32_t a,
            uint32_t b) noexcept
        {
            if (!weights || !weights->active) {
                return true;
            }
            return a < weights->vertex_weights.size()
                && b < weights->vertex_weights.size()
                && weights->vertex_weights[a] > 0.0f
                && weights->vertex_weights[b] > 0.0f;
        }

        float triangle_shape_penalty(
            const float a[3],
            const float b[3],
            const float c[3]) noexcept
        {
            const float ab[3]{
                b[0] - a[0],
                b[1] - a[1],
                b[2] - a[2],
            };
            const float ac[3]{
                c[0] - a[0],
                c[1] - a[1],
                c[2] - a[2],
            };
            const float cross[3]{
                ab[1] * ac[2] - ab[2] * ac[1],
                ab[2] * ac[0] - ab[0] * ac[2],
                ab[0] * ac[1] - ab[1] * ac[0],
            };
            const float area2_sq =
                cross[0] * cross[0]
                + cross[1] * cross[1]
                + cross[2] * cross[2];
            const float max_edge_sq = (std::max)(
                distance_squared(a, b),
                (std::max)(
                    distance_squared(b, c),
                    distance_squared(c, a)));
            if (max_edge_sq <= 0.0f) {
                return 1000.0f;
            }

            const float normalized_area =
                std::sqrt(area2_sq) / max_edge_sq;
            return (std::min)(
                1000.0f,
                1.0f / ((std::max)(normalized_area, 0.001f)));
        }

        void add_edge_candidate_opposite(
            EdgeCandidate& candidate,
            uint32_t opposite) noexcept
        {
            for (uint32_t i = 0; i < candidate.opposite_vertex_count; ++i) {
                if (candidate.opposite_vertices[i] == opposite) {
                    return;
                }
            }
            if (candidate.opposite_vertex_count
                < candidate.opposite_vertices.size())
            {
                candidate.opposite_vertices[
                    candidate.opposite_vertex_count++] = opposite;
            }
        }

        float edge_local_shape_penalty(
            const MeshData& mesh,
            const EdgeCandidate& candidate) noexcept
        {
            float penalty = 0.0f;
            for (uint32_t i = 0; i < candidate.opposite_vertex_count; ++i) {
                const uint32_t opposite = candidate.opposite_vertices[i];
                if (opposite >= mesh.vertex_count()) {
                    continue;
                }
                penalty = (std::max)(
                    penalty,
                    triangle_shape_penalty(
                        mesh.vertices[candidate.a].position,
                        mesh.vertices[candidate.b].position,
                        mesh.vertices[opposite].position));
            }

            return penalty;
        }

        std::vector<EdgeCandidate> graph_coarsen_edge_candidates(
            const MeshData& mesh,
            const GraphCoarsenWeights* weights)
        {
            const uint32_t vertex_count = mesh.vertex_count();
            const uint32_t triangle_count = mesh.index_count() / 3u;
            std::vector<EdgeCandidate> candidates;
            candidates.reserve(triangle_count * 3u);

            const auto add_edge =
                [&](uint32_t a, uint32_t b, uint32_t opposite) {
                if (a >= vertex_count || b >= vertex_count || a == b) {
                    return;
                }
                if (!graph_coarsen_edge_allowed(weights, a, b)) {
                    return;
                }
                EdgeCandidate candidate{};
                candidate.a = a;
                candidate.b = b;
                candidate.key = {
                    (std::min)(a, b),
                    (std::max)(a, b),
                };
                candidate.adjacent_triangle_count = 1u;
                add_edge_candidate_opposite(candidate, opposite);
                candidates.push_back(candidate);
            };

            for (uint32_t tri = 0; tri < triangle_count; ++tri) {
                const uint32_t i0 = mesh.indices[tri * 3u + 0u];
                const uint32_t i1 = mesh.indices[tri * 3u + 1u];
                const uint32_t i2 = mesh.indices[tri * 3u + 2u];
                add_edge(i0, i1, i2);
                add_edge(i1, i2, i0);
                add_edge(i2, i0, i1);
            }

            std::sort(
                candidates.begin(),
                candidates.end(),
                edge_candidate_key_less);

            size_t unique_count = 0u;
            for (EdgeCandidate& candidate : candidates) {
                if (unique_count > 0u
                    && candidates[unique_count - 1u].key == candidate.key)
                {
                    EdgeCandidate& merged =
                        candidates[unique_count - 1u];
                    ++merged.adjacent_triangle_count;
                    for (uint32_t i = 0;
                         i < candidate.opposite_vertex_count;
                         ++i)
                    {
                        add_edge_candidate_opposite(
                            merged,
                            candidate.opposite_vertices[i]);
                    }
                    continue;
                }
                candidates[unique_count++] = candidate;
            }
            candidates.resize(unique_count);

            for (EdgeCandidate& candidate : candidates) {
                const float length_sq = distance_squared(
                    mesh.vertices[candidate.a].position,
                    mesh.vertices[candidate.b].position);
                const float shape_penalty = edge_local_shape_penalty(
                    mesh,
                    candidate);
                const float boundary_penalty =
                    candidate.adjacent_triangle_count <= 1u ? 4.0f : 1.0f;
                float mask_pressure = 0.0f;
                if (weights && weights->active) {
                    mask_pressure =
                        (weights->vertex_weights[candidate.a]
                            + weights->vertex_weights[candidate.b])
                        * 0.5f;
                }
                const float mask_bias = 1.0f / (1.0f + mask_pressure);
                candidate.score =
                    length_sq
                    * (1.0f + shape_penalty)
                    * boundary_penalty
                    * mask_bias;
            }

            std::sort(
                candidates.begin(),
                candidates.end(),
                edge_candidate_score_less);
            return candidates;
        }

        MeshData graph_coarsen_once(
            const MeshData& source_mesh,
            const GraphCoarsenWeights* weights,
            GraphCoarsenWeights* weights_out,
            float& conservative_error_out)
        {
            conservative_error_out = 0.0f;
            if (weights_out) {
                *weights_out = {};
            }

            const uint32_t vertex_count = source_mesh.vertex_count();
            const uint32_t triangle_count = source_mesh.index_count() / 3u;
            if (vertex_count < 4u || triangle_count < 2u) {
                return source_mesh;
            }

            constexpr uint32_t kUnassigned =
                (std::numeric_limits<uint32_t>::max)();
            std::vector<uint32_t> partner(vertex_count, kUnassigned);

            const auto try_pair = [&](const EdgeCandidate& candidate) {
                const uint32_t a = candidate.a;
                const uint32_t b = candidate.b;
                if (partner[a] != kUnassigned
                    || partner[b] != kUnassigned)
                {
                    return;
                }
                partner[a] = b;
                partner[b] = a;
            };

            const std::vector<EdgeCandidate> candidates =
                graph_coarsen_edge_candidates(source_mesh, weights);
            for (const EdgeCandidate& candidate : candidates) {
                try_pair(candidate);
            }

            std::vector<uint32_t> cluster_for_vertex(vertex_count, kUnassigned);
            std::vector<std::array<uint32_t, 2>> cluster_members;
            cluster_members.reserve(vertex_count);

            for (uint32_t v = 0; v < vertex_count; ++v) {
                if (cluster_for_vertex[v] != kUnassigned) {
                    continue;
                }
                const uint32_t p = partner[v];
                const uint32_t cluster =
                    static_cast<uint32_t>(cluster_members.size());
                if (p != kUnassigned && p != v) {
                    cluster_for_vertex[v] = cluster;
                    cluster_for_vertex[p] = cluster;
                    cluster_members.push_back({ v, p });
                }
                else {
                    cluster_for_vertex[v] = cluster;
                    cluster_members.push_back({ v, kUnassigned });
                }
            }

            MeshData coarse{};
            coarse.topology = source_mesh.topology;
            coarse.index_format = MeshIndexFormat::UInt32;
            coarse.has_normals = source_mesh.has_normals;
            coarse.has_uv0 = source_mesh.has_uv0;
            coarse.vertices.resize(cluster_members.size());
            std::vector<float> cluster_weights;
            if (weights && weights->active) {
                cluster_weights.assign(cluster_members.size(), 0.0f);
            }

            for (uint32_t cluster = 0u;
                 cluster < static_cast<uint32_t>(cluster_members.size());
                 ++cluster)
            {
                const auto members = cluster_members[cluster];
                const MeshVertex& a = source_mesh.vertices[members[0]];
                MeshVertex& out = coarse.vertices[cluster];
                out = a;
                if (members[1] != kUnassigned) {
                    const MeshVertex& b = source_mesh.vertices[members[1]];
                    for (uint32_t i = 0; i < 3u; ++i) {
                        out.position[i] =
                            (a.position[i] + b.position[i]) * 0.5f;
                        out.normal[i] =
                            (a.normal[i] + b.normal[i]) * 0.5f;
                    }
                    for (uint32_t i = 0; i < 2u; ++i) {
                        out.uv[i] = (a.uv[i] + b.uv[i]) * 0.5f;
                    }
                    normalize3(out.normal);
                }
                if (!cluster_weights.empty()) {
                    float weight = 0.0f;
                    if (members[0] < weights->vertex_weights.size()) {
                        weight = (std::max)(
                            weight,
                            weights->vertex_weights[members[0]]);
                    }
                    if (members[1] != kUnassigned
                        && members[1] < weights->vertex_weights.size())
                    {
                        weight = (std::max)(
                            weight,
                            weights->vertex_weights[members[1]]);
                    }
                    cluster_weights[cluster] = weight;
                }
            }

            std::vector<TriangleRecord> triangles;
            triangles.reserve(triangle_count);
            for (uint32_t tri = 0; tri < triangle_count; ++tri) {
                const uint32_t c0 =
                    cluster_for_vertex[source_mesh.indices[tri * 3u + 0u]];
                const uint32_t c1 =
                    cluster_for_vertex[source_mesh.indices[tri * 3u + 1u]];
                const uint32_t c2 =
                    cluster_for_vertex[source_mesh.indices[tri * 3u + 2u]];
                if (c0 == c1 || c1 == c2 || c2 == c0) {
                    continue;
                }

                TriangleRecord record{};
                record.indices = { c0, c1, c2 };
                record.key = record.indices;
                std::sort(record.key.begin(), record.key.end());
                triangles.push_back(record);
            }

            std::sort(
                triangles.begin(),
                triangles.end(),
                triangle_record_less);
            triangles.erase(
                std::unique(
                    triangles.begin(),
                    triangles.end(),
                    [](const TriangleRecord& a, const TriangleRecord& b) {
                        return a.key == b.key;
                    }),
                triangles.end());

            if (triangles.empty()
                || triangles.size() >= static_cast<size_t>(triangle_count))
            {
                return source_mesh;
            }

            std::vector<uint32_t> compact(
                coarse.vertices.size(),
                kUnassigned);
            std::vector<MeshVertex> compact_vertices;
            compact_vertices.reserve(coarse.vertices.size());
            std::vector<float> compact_weights;
            if (!cluster_weights.empty()) {
                compact_weights.reserve(coarse.vertices.size());
            }

            for (const TriangleRecord& tri : triangles) {
                for (const uint32_t index : tri.indices) {
                    if (compact[index] == kUnassigned) {
                        compact[index] =
                            static_cast<uint32_t>(compact_vertices.size());
                        compact_vertices.push_back(coarse.vertices[index]);
                        if (!cluster_weights.empty()) {
                            compact_weights.push_back(
                                cluster_weights[index]);
                        }
                    }
                }
            }

            MeshData compact_mesh{};
            compact_mesh.topology = source_mesh.topology;
            compact_mesh.index_format = MeshIndexFormat::UInt32;
            compact_mesh.has_normals = source_mesh.has_normals;
            compact_mesh.has_uv0 = source_mesh.has_uv0;
            compact_mesh.vertices = std::move(compact_vertices);
            compact_mesh.indices.reserve(triangles.size() * 3u);
            for (const TriangleRecord& tri : triangles) {
                compact_mesh.indices.push_back(compact[tri.indices[0]]);
                compact_mesh.indices.push_back(compact[tri.indices[1]]);
                compact_mesh.indices.push_back(compact[tri.indices[2]]);
            }

            float max_error_sq = 0.0f;
            for (uint32_t v = 0; v < vertex_count; ++v) {
                const uint32_t cluster = cluster_for_vertex[v];
                if (cluster == kUnassigned
                    || compact[cluster] == kUnassigned)
                {
                    continue;
                }
                max_error_sq = (std::max)(
                    max_error_sq,
                    distance_squared(
                        source_mesh.vertices[v].position,
                        compact_mesh.vertices[compact[cluster]].position));
            }
            conservative_error_out = std::sqrt(max_error_sq);
            if (weights_out && !compact_weights.empty()) {
                weights_out->active = true;
                weights_out->vertex_weights = std::move(compact_weights);
            }
            return compact_mesh.valid() ? compact_mesh : source_mesh;
        }

        MeshClusterHierarchyData build_graph_coarsen_hierarchy(
            const MeshClusterHierarchyDesc& desc,
            const MeshData& source_mesh,
            const MeshDerivedFieldData* region_field,
            wz::Logger& logger)
        {
            MeshClusterHierarchyData data = build_identity_hierarchy(
                desc,
                source_mesh);
            data.method = MeshClusterHierarchyBuildMethod::GraphCoarsen;

            MeshData current = source_mesh;
            GraphCoarsenWeights weights =
                graph_coarsen_weights_for_region_mask(
                    desc,
                    source_mesh,
                    region_field,
                    logger);
            float accumulated_error = 0.0f;
            const uint32_t max_levels = hierarchy_max_level_count(desc);
            constexpr uint32_t kMinTriangles = 1u;

            for (uint32_t level_index = 1u;
                 level_index < max_levels
                 && current.index_count() / 3u > kMinTriangles;
                 ++level_index)
            {
                const auto level_started = std::chrono::steady_clock::now();
                float level_error = 0.0f;
                GraphCoarsenWeights next_weights{};
                MeshData next =
                    graph_coarsen_once(
                        current,
                        weights.active ? &weights : nullptr,
                        &next_weights,
                        level_error);
                if (!next.valid()
                    || next.index_count() >= current.index_count())
                {
                    break;
                }

                accumulated_error += level_error;

                MeshClusterHierarchyLevel level{};
                level.level_index = level_index;
                level.cluster_count = next.vertex_count();
                level.vertex_count = next.vertex_count();
                level.triangle_count = next.index_count() / 3u;
                level.conservative_error = accumulated_error;
                level.preview_mesh = next;
                data.levels.push_back(std::move(level));

                const auto level_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - level_started)
                        .count();
                logger.info(
                    std::string(
                        "asset compile: mesh cluster hierarchy level method=")
                    + hierarchy_method_name(data.method)
                    + " level="
                    + std::to_string(level_index)
                    + " vertices="
                    + std::to_string(data.levels.back().vertex_count)
                    + " triangles="
                    + std::to_string(data.levels.back().triangle_count)
                    + " ms="
                    + std::to_string(level_ms));

                current = std::move(next);
                weights = std::move(next_weights);
            }

            return data;
        }

        class DisjointSet
        {
        public:
            explicit DisjointSet(uint32_t count)
                : parent_(count)
                , size_(count, 1u)
            {
                for (uint32_t i = 0; i < count; ++i) {
                    parent_[i] = i;
                }
            }

            uint32_t find(uint32_t value)
            {
                uint32_t root = value;
                while (parent_[root] != root) {
                    root = parent_[root];
                }
                while (parent_[value] != value) {
                    const uint32_t next = parent_[value];
                    parent_[value] = root;
                    value = next;
                }
                return root;
            }

            uint32_t size(uint32_t value)
            {
                return size_[find(value)];
            }

            bool merge(uint32_t a, uint32_t b, uint32_t max_size)
            {
                uint32_t root_a = find(a);
                uint32_t root_b = find(b);
                if (root_a == root_b) {
                    return false;
                }
                if (size_[root_a] + size_[root_b] > max_size) {
                    return false;
                }
                if (size_[root_a] < size_[root_b]) {
                    std::swap(root_a, root_b);
                }
                parent_[root_b] = root_a;
                size_[root_a] += size_[root_b];
                return true;
            }

        private:
            std::vector<uint32_t> parent_;
            std::vector<uint32_t> size_;
        };

        MeshData build_laplacian_graph_cells_from_cells(
            const MeshData& source_mesh,
            const GraphCoarsenWeights* weights,
            GraphCoarsenWeights* weights_out,
            DisjointSet& cells,
            float& conservative_error_out)
        {
            conservative_error_out = 0.0f;
            if (weights_out) {
                *weights_out = {};
            }

            const uint32_t vertex_count = source_mesh.vertex_count();
            const uint32_t triangle_count = source_mesh.index_count() / 3u;
            if (vertex_count < 4u || triangle_count < 2u)
            {
                return source_mesh;
            }

            constexpr uint32_t kUnassigned =
                (std::numeric_limits<uint32_t>::max)();
            std::vector<uint32_t> cluster_for_root(vertex_count, kUnassigned);
            std::vector<uint32_t> cluster_for_vertex(vertex_count, kUnassigned);
            std::vector<uint32_t> cluster_counts;

            MeshData coarse{};
            coarse.topology = source_mesh.topology;
            coarse.index_format = MeshIndexFormat::UInt32;
            coarse.has_normals = source_mesh.has_normals;
            coarse.has_uv0 = source_mesh.has_uv0;

            std::vector<float> cluster_weights;
            for (uint32_t v = 0; v < vertex_count; ++v) {
                const uint32_t root = cells.find(v);
                uint32_t cluster = cluster_for_root[root];
                if (cluster == kUnassigned) {
                    cluster = static_cast<uint32_t>(coarse.vertices.size());
                    cluster_for_root[root] = cluster;
                    coarse.vertices.emplace_back();
                    cluster_counts.push_back(0u);
                    if (weights && weights->active) {
                        cluster_weights.push_back(0.0f);
                    }
                }

                cluster_for_vertex[v] = cluster;
                MeshVertex& out = coarse.vertices[cluster];
                const MeshVertex& in = source_mesh.vertices[v];
                for (uint32_t i = 0; i < 3u; ++i) {
                    out.position[i] += in.position[i];
                    out.normal[i] += in.normal[i];
                }
                for (uint32_t i = 0; i < 2u; ++i) {
                    out.uv[i] += in.uv[i];
                }
                ++cluster_counts[cluster];
                if (!cluster_weights.empty()
                    && v < weights->vertex_weights.size())
                {
                    cluster_weights[cluster] = (std::max)(
                        cluster_weights[cluster],
                        weights->vertex_weights[v]);
                }
            }

            if (coarse.vertices.size() >= source_mesh.vertices.size()) {
                return source_mesh;
            }

            for (uint32_t cluster = 0u;
                 cluster < static_cast<uint32_t>(coarse.vertices.size());
                 ++cluster)
            {
                const float inv_count =
                    cluster_counts[cluster] > 0u
                        ? 1.0f / static_cast<float>(cluster_counts[cluster])
                        : 1.0f;
                MeshVertex& out = coarse.vertices[cluster];
                for (uint32_t i = 0; i < 3u; ++i) {
                    out.position[i] *= inv_count;
                    out.normal[i] *= inv_count;
                }
                for (uint32_t i = 0; i < 2u; ++i) {
                    out.uv[i] *= inv_count;
                }
                normalize3(out.normal);
            }

            std::vector<TriangleRecord> triangles;
            triangles.reserve(triangle_count);
            for (uint32_t tri = 0; tri < triangle_count; ++tri) {
                const uint32_t c0 =
                    cluster_for_vertex[source_mesh.indices[tri * 3u + 0u]];
                const uint32_t c1 =
                    cluster_for_vertex[source_mesh.indices[tri * 3u + 1u]];
                const uint32_t c2 =
                    cluster_for_vertex[source_mesh.indices[tri * 3u + 2u]];
                if (c0 == c1 || c1 == c2 || c2 == c0) {
                    continue;
                }

                TriangleRecord record{};
                record.indices = { c0, c1, c2 };
                record.key = record.indices;
                std::sort(record.key.begin(), record.key.end());
                triangles.push_back(record);
            }

            std::sort(
                triangles.begin(),
                triangles.end(),
                triangle_record_less);
            triangles.erase(
                std::unique(
                    triangles.begin(),
                    triangles.end(),
                    [](const TriangleRecord& a, const TriangleRecord& b) {
                        return a.key == b.key;
                    }),
                triangles.end());

            if (triangles.empty()) {
                return source_mesh;
            }

            std::vector<uint32_t> compact(
                coarse.vertices.size(),
                kUnassigned);
            std::vector<MeshVertex> compact_vertices;
            compact_vertices.reserve(coarse.vertices.size());
            std::vector<float> compact_weights;
            if (!cluster_weights.empty()) {
                compact_weights.reserve(coarse.vertices.size());
            }

            for (const TriangleRecord& tri : triangles) {
                for (const uint32_t index : tri.indices) {
                    if (compact[index] == kUnassigned) {
                        compact[index] =
                            static_cast<uint32_t>(compact_vertices.size());
                        compact_vertices.push_back(coarse.vertices[index]);
                        if (!cluster_weights.empty()) {
                            compact_weights.push_back(
                                cluster_weights[index]);
                        }
                    }
                }
            }

            MeshData compact_mesh{};
            compact_mesh.topology = source_mesh.topology;
            compact_mesh.index_format = MeshIndexFormat::UInt32;
            compact_mesh.has_normals = source_mesh.has_normals;
            compact_mesh.has_uv0 = source_mesh.has_uv0;
            compact_mesh.vertices = std::move(compact_vertices);
            compact_mesh.indices.reserve(triangles.size() * 3u);
            for (const TriangleRecord& tri : triangles) {
                compact_mesh.indices.push_back(compact[tri.indices[0]]);
                compact_mesh.indices.push_back(compact[tri.indices[1]]);
                compact_mesh.indices.push_back(compact[tri.indices[2]]);
            }

            float max_error_sq = 0.0f;
            for (uint32_t v = 0; v < vertex_count; ++v) {
                const uint32_t cluster = cluster_for_vertex[v];
                if (cluster == kUnassigned
                    || compact[cluster] == kUnassigned)
                {
                    continue;
                }
                max_error_sq = (std::max)(
                    max_error_sq,
                    distance_squared(
                        source_mesh.vertices[v].position,
                        compact_mesh.vertices[compact[cluster]].position));
            }
            conservative_error_out = std::sqrt(max_error_sq);
            if (weights_out && !compact_weights.empty()) {
                weights_out->active = true;
                weights_out->vertex_weights = std::move(compact_weights);
            }
            return compact_mesh.valid() ? compact_mesh : source_mesh;
        }

        MeshData laplacian_graph_cells_once(
            const MeshData& source_mesh,
            const GraphCoarsenWeights* weights,
            GraphCoarsenWeights* weights_out,
            uint32_t max_cell_size,
            float& conservative_error_out)
        {
            conservative_error_out = 0.0f;
            if (weights_out) {
                *weights_out = {};
            }

            const uint32_t vertex_count = source_mesh.vertex_count();
            const uint32_t triangle_count = source_mesh.index_count() / 3u;
            if (vertex_count < 4u
                || triangle_count < 2u
                || max_cell_size < 2u)
            {
                return source_mesh;
            }

            DisjointSet cells(vertex_count);

            const auto try_merge = [&](uint32_t a, uint32_t b) {
                if (a >= vertex_count || b >= vertex_count || a == b) {
                    return;
                }
                if (!graph_coarsen_edge_allowed(weights, a, b)) {
                    return;
                }
                (void)cells.merge(a, b, max_cell_size);
            };

            for (uint32_t tri = 0; tri < triangle_count; ++tri) {
                const uint32_t i0 = source_mesh.indices[tri * 3u + 0u];
                const uint32_t i1 = source_mesh.indices[tri * 3u + 1u];
                const uint32_t i2 = source_mesh.indices[tri * 3u + 2u];
                try_merge(i0, i1);
                try_merge(i1, i2);
                try_merge(i2, i0);
            }

            return build_laplacian_graph_cells_from_cells(
                source_mesh,
                weights,
                weights_out,
                cells,
                conservative_error_out);
        }

        struct GpuGraphCellsPreferenceResult
        {
            bool used_gpu = false;
            uint64_t dispatch_ms = 0;
            wz::asset::ResourceHandle preferred_neighbors_buffer{};
            std::vector<uint32_t> preferred_neighbors;
        };

        struct GpuPosition3
        {
            float position[3]{};
        };

        bool upload_gpu_cluster_hierarchy_level(
            const wz::asset::AssetKey& hierarchy_key,
            const MeshClusterHierarchyData& hierarchy,
            const MeshClusterHierarchyLevel& level,
            MeshFieldComputeBackend& compute,
            GpuResidentMeshClusterHierarchyTable& resident_hierarchy_table,
            wz::Logger& logger)
        {
            if (!compute.available()
                || hierarchy_key == wz::asset::AssetKey{}
                || !level.valid())
            {
                return false;
            }
            if (const GpuResidentMeshClusterHierarchyEntry* existing =
                    resident_hierarchy_table.find(hierarchy_key))
            {
                for (const GpuResidentMeshClusterHierarchyLevel& resident :
                    existing->levels)
                {
                    if (resident.level_index == level.level_index
                        && resident.valid())
                    {
                        return true;
                    }
                }
            }

            const MeshData& mesh = level.preview_mesh;
            std::vector<GpuPosition3> positions;
            positions.reserve(mesh.vertices.size());
            for (const MeshVertex& vertex : mesh.vertices) {
                GpuPosition3 out{};
                out.position[0] = vertex.position[0];
                out.position[1] = vertex.position[1];
                out.position[2] = vertex.position[2];
                positions.push_back(out);
            }

            std::vector<uint32_t> representative_vertices(mesh.vertex_count());
            for (uint32_t i = 0; i < mesh.vertex_count(); ++i) {
                representative_vertices[i] = i;
            }

            const auto upload_started = std::chrono::steady_clock::now();
            const wz::asset::ResourceHandle positions_buffer =
                compute.create_structured_buffer({
                    .element_count = mesh.vertex_count(),
                    .stride_bytes = sizeof(GpuPosition3),
                    .initial_data = positions.data(),
                    .initial_data_bytes =
                        static_cast<uint64_t>(
                            positions.size() * sizeof(GpuPosition3)),
                });
            const wz::asset::ResourceHandle indices_buffer =
                compute.create_structured_buffer({
                    .element_count = mesh.index_count(),
                    .stride_bytes = sizeof(uint32_t),
                    .initial_data = mesh.indices.data(),
                    .initial_data_bytes =
                        static_cast<uint64_t>(
                            mesh.indices.size() * sizeof(uint32_t)),
                });
            const wz::asset::ResourceHandle representatives_buffer =
                compute.create_structured_buffer({
                    .element_count = mesh.vertex_count(),
                    .stride_bytes = sizeof(uint32_t),
                    .initial_data = representative_vertices.data(),
                    .initial_data_bytes =
                        static_cast<uint64_t>(
                            representative_vertices.size()
                            * sizeof(uint32_t)),
                });

            if (!positions_buffer.valid()
                || !indices_buffer.valid()
                || !representatives_buffer.valid())
            {
                if (positions_buffer.valid()) {
                    compute.release_buffer(positions_buffer);
                }
                if (indices_buffer.valid()) {
                    compute.release_buffer(indices_buffer);
                }
                if (representatives_buffer.valid()) {
                    compute.release_buffer(representatives_buffer);
                }
                logger.warn(
                    "mesh cluster hierarchy GPU resident level upload failed");
                return false;
            }

            GpuResidentMeshClusterHierarchyEntry* entry =
                resident_hierarchy_table.find_or_add(hierarchy_key);
            GpuResidentMeshClusterHierarchyLevel* resident_level =
                resident_hierarchy_table.find_or_add_level(
                    hierarchy_key,
                    level.level_index);
            if (!entry || !resident_level) {
                compute.release_buffer(positions_buffer);
                compute.release_buffer(indices_buffer);
                compute.release_buffer(representatives_buffer);
                return false;
            }

            for (wz::asset::ResourceHandle* handle : {
                &resident_level->positions,
                &resident_level->indices,
                &resident_level->source_vertices })
            {
                if (handle->valid()) {
                    compute.release_buffer(*handle);
                    *handle = {};
                }
            }

            entry->source_mesh_key = hierarchy.source_mesh_key;
            entry->method = hierarchy.method;
            resident_level->positions = positions_buffer;
            resident_level->indices = indices_buffer;
            resident_level->source_vertices = representatives_buffer;
            resident_level->vertex_count = mesh.vertex_count();
            resident_level->index_count = mesh.index_count();
            resident_level->triangle_count = mesh.index_count() / 3u;

            const auto upload_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - upload_started)
                    .count();
            logger.info(
                "asset compile: mesh cluster hierarchy GPU resident level="
                + std::to_string(level.level_index)
                + " vertices="
                + std::to_string(resident_level->vertex_count)
                + " triangles="
                + std::to_string(resident_level->triangle_count)
                + " upload_ms="
                + std::to_string(upload_ms));
            return resident_level->valid();
        }

        void publish_gpu_cluster_hierarchy(
            const wz::asset::AssetKey& hierarchy_key,
            const MeshClusterHierarchyData& hierarchy,
            MeshFieldComputeBackend& compute,
            GpuResidentMeshClusterHierarchyTable& resident_hierarchy_table,
            wz::Logger& logger)
        {
            if (!compute.available() || !hierarchy.valid()) {
                return;
            }

            for (const MeshClusterHierarchyLevel& level : hierarchy.levels) {
                (void)upload_gpu_cluster_hierarchy_level(
                    hierarchy_key,
                    hierarchy,
                    level,
                    compute,
                    resident_hierarchy_table,
                    logger);
            }
        }

        wz::asset::ResourceHandle ensure_gpu_positions(
            const wz::asset::AssetKey& mesh_key,
            const MeshData& mesh,
            MeshFieldComputeBackend& compute,
            GpuResidentMeshDataTable& resident_mesh_data)
        {
            if (!compute.available() || mesh.vertex_count() == 0u) {
                return {};
            }

            if (const GpuResidentMeshDataEntry* existing =
                    resident_mesh_data.find(mesh_key))
            {
                if (existing->positions.valid()) {
                    return existing->positions;
                }
            }

            std::vector<GpuPosition3> positions;
            positions.reserve(mesh.vertices.size());
            for (const MeshVertex& vertex : mesh.vertices) {
                GpuPosition3 out{};
                out.position[0] = vertex.position[0];
                out.position[1] = vertex.position[1];
                out.position[2] = vertex.position[2];
                positions.push_back(out);
            }

            const wz::asset::ResourceHandle buffer =
                compute.create_structured_buffer({
                    .element_count = mesh.vertex_count(),
                    .stride_bytes = sizeof(GpuPosition3),
                    .initial_data = positions.data(),
                    .initial_data_bytes =
                        static_cast<uint64_t>(
                            positions.size() * sizeof(GpuPosition3)),
                });
            if (!buffer.valid()) {
                return {};
            }

            if (GpuResidentMeshDataEntry* entry =
                    resident_mesh_data.find_or_add(mesh_key))
            {
                if (!entry->positions.valid()) {
                    entry->positions = buffer;
                    entry->vertex_count = mesh.vertex_count();
                    return buffer;
                }
            }

            compute.release_buffer(buffer);
            return {};
        }

        wz::asset::ResourceHandle ensure_gpu_indices(
            const wz::asset::AssetKey& mesh_key,
            const MeshData& mesh,
            MeshFieldComputeBackend& compute,
            GpuResidentMeshDataTable& resident_mesh_data)
        {
            if (!compute.available() || mesh.index_count() == 0u) {
                return {};
            }

            if (const GpuResidentMeshDataEntry* existing =
                    resident_mesh_data.find(mesh_key))
            {
                if (existing->indices.valid()) {
                    return existing->indices;
                }
            }

            const wz::asset::ResourceHandle buffer =
                compute.create_structured_buffer({
                    .element_count = mesh.index_count(),
                    .stride_bytes = sizeof(uint32_t),
                    .initial_data = mesh.indices.data(),
                    .initial_data_bytes =
                        static_cast<uint64_t>(
                            mesh.indices.size() * sizeof(uint32_t)),
                });
            if (!buffer.valid()) {
                return {};
            }

            if (GpuResidentMeshDataEntry* entry =
                    resident_mesh_data.find_or_add(mesh_key))
            {
                if (!entry->indices.valid()) {
                    entry->indices = buffer;
                    entry->index_count = mesh.index_count();
                    entry->triangle_count = mesh.index_count() / 3u;
                    return buffer;
                }
            }

            compute.release_buffer(buffer);
            return {};
        }

        const GpuResidentSparseOperatorEntry* ensure_gpu_sparse_operator(
            const wz::asset::AssetKey& operator_key,
            const MeshSparseOperatorData& sparse_operator,
            MeshFieldComputeBackend& compute,
            GpuResidentSparseOperatorTable& resident_sparse_operators)
        {
            if (!compute.available()
                || operator_key == wz::asset::AssetKey{}
                || !sparse_operator.valid())
            {
                return nullptr;
            }

            GpuResidentSparseOperatorEntry* entry =
                resident_sparse_operators.find_or_add(operator_key);
            if (!entry) {
                return nullptr;
            }

            entry->row_count = sparse_operator.row_count;
            entry->nonzero_count = sparse_operator.nonzero_count;

            if (!entry->row_offsets.valid()) {
                entry->row_offsets = compute.create_structured_buffer({
                    .element_count =
                        static_cast<uint32_t>(
                            sparse_operator.row_offsets.size()),
                    .stride_bytes = sizeof(uint32_t),
                    .initial_data = sparse_operator.row_offsets.data(),
                    .initial_data_bytes =
                        static_cast<uint64_t>(
                            sparse_operator.row_offsets.size()
                            * sizeof(uint32_t)),
                });
            }
            if (!entry->col_indices.valid()) {
                entry->col_indices = compute.create_structured_buffer({
                    .element_count =
                        static_cast<uint32_t>(
                            sparse_operator.col_indices.size()),
                    .stride_bytes = sizeof(uint32_t),
                    .initial_data = sparse_operator.col_indices.data(),
                    .initial_data_bytes =
                        static_cast<uint64_t>(
                            sparse_operator.col_indices.size()
                            * sizeof(uint32_t)),
                });
            }

            return entry->row_offsets.valid() && entry->col_indices.valid()
                ? entry
                : nullptr;
        }

        GpuGraphCellsPreferenceResult compute_gpu_graph_cell_preferences(
            const MeshClusterHierarchyDesc& desc,
            const MeshData& mesh,
            const ComputePipelineData* pipeline_data,
            const MeshSparseOperatorData* sparse_operator,
            MeshFieldComputeBackend& compute,
            GpuResidentMeshDataTable& resident_mesh_data,
            GpuResidentSparseOperatorTable& resident_sparse_operators,
            wz::Logger& logger)
        {
            GpuGraphCellsPreferenceResult result{};
            if (!compute.available()
                || !pipeline_data
                || !pipeline_data->valid()
                || !sparse_operator
                || !sparse_operator->valid()
                || sparse_operator->row_count != mesh.vertex_count())
            {
                return result;
            }

            const wz::asset::ResourceHandle positions =
                ensure_gpu_positions(
                    desc.source_mesh.output,
                    mesh,
                    compute,
                    resident_mesh_data);
            const GpuResidentSparseOperatorEntry* resident_operator =
                ensure_gpu_sparse_operator(
                    desc.graph_operator.output,
                    *sparse_operator,
                    compute,
                    resident_sparse_operators);
            if (!positions.valid() || !resident_operator) {
                logger.warn(
                    "mesh cluster hierarchy GPU graph-cell buffer upload failed; using CPU path");
                return result;
            }

            const wz::asset::ResourceHandle pipeline =
                compute.create_compute_pipeline(
                    *pipeline_data,
                    pipeline_data->compute_shader);
            if (!pipeline.valid()) {
                logger.warn(
                    "mesh cluster hierarchy GPU graph-cell pipeline creation failed; using CPU path");
                return result;
            }

            std::vector<uint32_t> zeroes(mesh.vertex_count(), 0u);
            const wz::asset::ResourceHandle output =
                compute.create_rw_structured_buffer({
                    .element_count = mesh.vertex_count(),
                    .stride_bytes = sizeof(uint32_t),
                    .initial_data = zeroes.data(),
                    .initial_data_bytes =
                        static_cast<uint64_t>(
                            zeroes.size() * sizeof(uint32_t)),
                });
            if (!output.valid()) {
                compute.release_pipeline(pipeline);
                logger.warn(
                    "mesh cluster hierarchy GPU graph-cell output creation failed; using CPU path");
                return result;
            }

            const std::array<uint32_t, 4> root_constants{
                mesh.vertex_count(),
                0u,
                0u,
                0u,
            };
            const std::array<MeshFieldComputeBackend::DispatchBinding, 4>
                bindings{{
                {
                    .kind = MeshFieldComputeBackend::BindingKind
                        ::StructuredBufferSRV,
                    .shader_register = 0,
                    .register_space = 0,
                    .buffer = positions,
                },
                {
                    .kind = MeshFieldComputeBackend::BindingKind
                        ::StructuredBufferSRV,
                    .shader_register = 1,
                    .register_space = 0,
                    .buffer = resident_operator->row_offsets,
                },
                {
                    .kind = MeshFieldComputeBackend::BindingKind
                        ::StructuredBufferSRV,
                    .shader_register = 2,
                    .register_space = 0,
                    .buffer = resident_operator->col_indices,
                },
                {
                    .kind = MeshFieldComputeBackend::BindingKind
                        ::StructuredBufferUAV,
                    .shader_register = 0,
                    .register_space = 0,
                    .buffer = output,
                },
            }};

            const auto started = std::chrono::steady_clock::now();
            const bool dispatched = compute.dispatch({
                .pipeline = pipeline,
                .bindings = bindings,
                .root_constants = root_constants,
                .group_count_x =
                    (mesh.vertex_count()
                        + pipeline_data->thread_group_size_x - 1u)
                    / pipeline_data->thread_group_size_x,
                .group_count_y = 1,
                .group_count_z = 1,
            });
            std::vector<std::byte> bytes;
            if (dispatched) {
                bytes = compute.readback_buffer(output);
            }
            result.dispatch_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count();

            compute.release_pipeline(pipeline);

            if (!dispatched
                || bytes.size()
                    != static_cast<size_t>(mesh.vertex_count())
                        * sizeof(uint32_t))
            {
                compute.release_buffer(output);
                logger.warn(
                    "mesh cluster hierarchy GPU graph-cell dispatch/readback failed; using CPU path");
                return result;
            }

            result.preferred_neighbors.resize(mesh.vertex_count());
            std::memcpy(
                result.preferred_neighbors.data(),
                bytes.data(),
                bytes.size());
            result.preferred_neighbors_buffer = output;
            result.used_gpu = true;
            return result;
        }

        bool compute_gpu_resident_graph_cells_level(
            const wz::asset::AssetKey& hierarchy_key,
            uint32_t level_index,
            const MeshClusterHierarchyDesc& desc,
            const MeshData& mesh,
            const GraphCoarsenWeights& weights,
            const ComputePipelineData* pipeline_data,
            wz::asset::ResourceHandle preferred_neighbors,
            MeshFieldComputeBackend& compute,
            GpuResidentMeshDataTable& resident_mesh_data,
            GpuResidentMeshClusterHierarchyTable& resident_hierarchy_table,
            MeshData* preview_mesh_out,
            GraphCoarsenWeights* preview_weights_out,
            float* preview_error_out,
            wz::Logger& logger)
        {
            if (!compute.available()
                || hierarchy_key == wz::asset::AssetKey{}
                || !pipeline_data
                || !pipeline_data->valid()
                || !preferred_neighbors.valid()
                || mesh.vertex_count() == 0u
                || mesh.index_count() == 0u)
            {
                return false;
            }

            const wz::asset::ResourceHandle positions =
                ensure_gpu_positions(
                    desc.source_mesh.output,
                    mesh,
                    compute,
                    resident_mesh_data);
            const wz::asset::ResourceHandle indices =
                ensure_gpu_indices(
                    desc.source_mesh.output,
                    mesh,
                    compute,
                    resident_mesh_data);
            if (!positions.valid() || !indices.valid()) {
                logger.warn(
                    "mesh cluster hierarchy GPU compact input upload failed");
                return false;
            }

            std::vector<float> fallback_weight{ 1.0f };
            const std::vector<float>* weight_source = &fallback_weight;
            uint32_t use_weights = 0u;
            if (weights.active
                && weights.vertex_weights.size() == mesh.vertex_count())
            {
                weight_source = &weights.vertex_weights;
                use_weights = 1u;
            }

            const wz::asset::ResourceHandle weights_buffer =
                compute.create_structured_buffer({
                    .element_count =
                        static_cast<uint32_t>(weight_source->size()),
                    .stride_bytes = sizeof(float),
                    .initial_data = weight_source->data(),
                    .initial_data_bytes =
                        static_cast<uint64_t>(
                            weight_source->size() * sizeof(float)),
                });
            if (!weights_buffer.valid()) {
                logger.warn(
                    "mesh cluster hierarchy GPU compact weights upload failed");
                return false;
            }

            const wz::asset::ResourceHandle pipeline =
                compute.create_compute_pipeline(
                    *pipeline_data,
                    pipeline_data->compute_shader);
            if (!pipeline.valid()) {
                compute.release_buffer(weights_buffer);
                logger.warn(
                    "mesh cluster hierarchy GPU compact pipeline creation failed");
                return false;
            }

            const wz::asset::ResourceHandle out_positions =
                compute.create_rw_structured_buffer({
                    .element_count = mesh.vertex_count(),
                    .stride_bytes = sizeof(GpuPosition3),
                    .initial_data = nullptr,
                    .initial_data_bytes = 0u,
                });
            const wz::asset::ResourceHandle out_indices =
                compute.create_rw_structured_buffer({
                    .element_count = mesh.index_count(),
                    .stride_bytes = sizeof(uint32_t),
                    .initial_data = nullptr,
                    .initial_data_bytes = 0u,
                });
            uint32_t zero = 0u;
            const wz::asset::ResourceHandle out_counter =
                compute.create_rw_structured_buffer({
                    .element_count = 1u,
                    .stride_bytes = sizeof(uint32_t),
                    .initial_data = &zero,
                    .initial_data_bytes = sizeof(uint32_t),
                });
            std::vector<uint32_t> representative_vertices(mesh.vertex_count());
            for (uint32_t i = 0; i < mesh.vertex_count(); ++i) {
                representative_vertices[i] = i;
            }
            const wz::asset::ResourceHandle representatives =
                compute.create_structured_buffer({
                    .element_count = mesh.vertex_count(),
                    .stride_bytes = sizeof(uint32_t),
                    .initial_data = representative_vertices.data(),
                    .initial_data_bytes =
                        static_cast<uint64_t>(
                            representative_vertices.size()
                            * sizeof(uint32_t)),
                });

            if (!out_positions.valid()
                || !out_indices.valid()
                || !out_counter.valid()
                || !representatives.valid())
            {
                if (out_positions.valid()) {
                    compute.release_buffer(out_positions);
                }
                if (out_indices.valid()) {
                    compute.release_buffer(out_indices);
                }
                if (out_counter.valid()) {
                    compute.release_buffer(out_counter);
                }
                if (representatives.valid()) {
                    compute.release_buffer(representatives);
                }
                compute.release_pipeline(pipeline);
                compute.release_buffer(weights_buffer);
                logger.warn(
                    "mesh cluster hierarchy GPU compact output creation failed");
                return false;
            }

            const uint32_t triangle_count = mesh.index_count() / 3u;
            const std::array<uint32_t, 4> root_constants{
                mesh.vertex_count(),
                triangle_count,
                mesh.index_count(),
                use_weights,
            };
            const std::array<MeshFieldComputeBackend::DispatchBinding, 7>
                bindings{{
                {
                    .kind = MeshFieldComputeBackend::BindingKind
                        ::StructuredBufferSRV,
                    .shader_register = 0,
                    .register_space = 0,
                    .buffer = positions,
                },
                {
                    .kind = MeshFieldComputeBackend::BindingKind
                        ::StructuredBufferSRV,
                    .shader_register = 1,
                    .register_space = 0,
                    .buffer = indices,
                },
                {
                    .kind = MeshFieldComputeBackend::BindingKind
                        ::StructuredBufferSRV,
                    .shader_register = 2,
                    .register_space = 0,
                    .buffer = preferred_neighbors,
                },
                {
                    .kind = MeshFieldComputeBackend::BindingKind
                        ::StructuredBufferSRV,
                    .shader_register = 3,
                    .register_space = 0,
                    .buffer = weights_buffer,
                },
                {
                    .kind = MeshFieldComputeBackend::BindingKind
                        ::StructuredBufferUAV,
                    .shader_register = 0,
                    .register_space = 0,
                    .buffer = out_positions,
                },
                {
                    .kind = MeshFieldComputeBackend::BindingKind
                        ::StructuredBufferUAV,
                    .shader_register = 1,
                    .register_space = 0,
                    .buffer = out_indices,
                },
                {
                    .kind = MeshFieldComputeBackend::BindingKind
                        ::StructuredBufferUAV,
                    .shader_register = 2,
                    .register_space = 0,
                    .buffer = out_counter,
                },
            }};

            const auto started = std::chrono::steady_clock::now();
            const uint32_t work_count =
                (std::max)(mesh.vertex_count(), triangle_count);
            const bool dispatched = compute.dispatch({
                .pipeline = pipeline,
                .bindings = bindings,
                .root_constants = root_constants,
                .group_count_x =
                    (work_count + pipeline_data->thread_group_size_x - 1u)
                    / pipeline_data->thread_group_size_x,
                .group_count_y = 1u,
                .group_count_z = 1u,
            });
            std::vector<std::byte> counter_bytes;
            if (dispatched) {
                counter_bytes = compute.readback_buffer(out_counter);
            }
            const auto dispatch_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count();

            compute.release_buffer(out_counter);
            compute.release_pipeline(pipeline);
            compute.release_buffer(weights_buffer);

            if (!dispatched || counter_bytes.size() < sizeof(uint32_t)) {
                compute.release_buffer(out_positions);
                compute.release_buffer(out_indices);
                compute.release_buffer(representatives);
                logger.warn(
                    "mesh cluster hierarchy GPU compact dispatch/readback failed");
                return false;
            }

            uint32_t index_count = 0u;
            std::memcpy(&index_count, counter_bytes.data(), sizeof(uint32_t));
            index_count = (std::min)(index_count, mesh.index_count());
            index_count -= index_count % 3u;
            if (index_count == 0u) {
                compute.release_buffer(out_positions);
                compute.release_buffer(out_indices);
                compute.release_buffer(representatives);
                logger.warn(
                    "mesh cluster hierarchy GPU compact produced no triangles");
                return false;
            }

            if (preview_mesh_out) {
                const auto readback_started = std::chrono::steady_clock::now();
                std::vector<std::byte> position_bytes =
                    compute.readback_buffer(out_positions);
                std::vector<std::byte> index_bytes =
                    compute.readback_buffer(out_indices);
                const size_t expected_position_bytes =
                    static_cast<size_t>(mesh.vertex_count())
                    * sizeof(GpuPosition3);
                const size_t expected_index_bytes =
                    static_cast<size_t>(mesh.index_count())
                    * sizeof(uint32_t);
                if (position_bytes.size() == expected_position_bytes
                    && index_bytes.size() == expected_index_bytes)
                {
                    MeshData preview{};
                    preview.topology = mesh.topology;
                    preview.index_format = MeshIndexFormat::UInt32;
                    preview.has_normals = mesh.has_normals;
                    preview.has_uv0 = mesh.has_uv0;
                    preview.vertices = mesh.vertices;
                    preview.indices.resize(index_count);

                    float max_error_sq = 0.0f;
                    for (uint32_t i = 0; i < mesh.vertex_count(); ++i) {
                        GpuPosition3 gpu_position{};
                        std::memcpy(
                            &gpu_position,
                            position_bytes.data()
                                + static_cast<size_t>(i)
                                    * sizeof(GpuPosition3),
                            sizeof(GpuPosition3));
                        MeshVertex& out = preview.vertices[i];
                        const float old_position[3]{
                            out.position[0],
                            out.position[1],
                            out.position[2],
                        };
                        out.position[0] = gpu_position.position[0];
                        out.position[1] = gpu_position.position[1];
                        out.position[2] = gpu_position.position[2];
                        max_error_sq = (std::max)(
                            max_error_sq,
                            distance_squared(old_position, out.position));
                    }

                    std::memcpy(
                        preview.indices.data(),
                        index_bytes.data(),
                        static_cast<size_t>(index_count)
                            * sizeof(uint32_t));

                    if (preview.valid()) {
                        *preview_mesh_out = std::move(preview);
                        if (preview_weights_out) {
                            if (weights.active) {
                                preview_weights_out->active = true;
                                preview_weights_out->vertex_weights =
                                    weights.vertex_weights;
                            }
                            else {
                                *preview_weights_out = {};
                            }
                        }
                        if (preview_error_out) {
                            *preview_error_out = std::sqrt(max_error_sq);
                        }

                        const auto readback_ms =
                            std::chrono::duration_cast<
                                std::chrono::milliseconds>(
                                std::chrono::steady_clock::now()
                                    - readback_started)
                                .count();
                        logger.info(
                            "asset compile: mesh cluster hierarchy GPU preview readback level="
                            + std::to_string(level_index)
                            + " vertices="
                            + std::to_string(preview_mesh_out->vertex_count())
                            + " triangles="
                            + std::to_string(
                                preview_mesh_out->index_count() / 3u)
                            + " readback_ms="
                            + std::to_string(readback_ms));
                    }
                }
                else {
                    logger.warn(
                        "mesh cluster hierarchy GPU preview readback failed");
                }
            }

            GpuResidentMeshClusterHierarchyEntry* entry =
                resident_hierarchy_table.find_or_add(hierarchy_key);
            GpuResidentMeshClusterHierarchyLevel* resident_level =
                resident_hierarchy_table.find_or_add_level(
                    hierarchy_key,
                    level_index);
            if (!entry || !resident_level) {
                compute.release_buffer(out_positions);
                compute.release_buffer(out_indices);
                compute.release_buffer(representatives);
                return false;
            }

            for (wz::asset::ResourceHandle* handle : {
                &resident_level->positions,
                &resident_level->indices,
                &resident_level->source_vertices })
            {
                if (handle->valid()) {
                    compute.release_buffer(*handle);
                    *handle = {};
                }
            }

            entry->source_mesh_key = desc.source_mesh.output;
            entry->method = MeshClusterHierarchyBuildMethod
                ::LaplacianGraphCells;
            resident_level->level_index = level_index;
            resident_level->positions = out_positions;
            resident_level->indices = out_indices;
            resident_level->source_vertices = representatives;
            resident_level->vertex_count = mesh.vertex_count();
            resident_level->index_count = index_count;
            resident_level->triangle_count = index_count / 3u;

            logger.info(
                "asset compile: mesh cluster hierarchy GPU compact level="
                + std::to_string(level_index)
                + " source_triangles="
                + std::to_string(mesh.index_count() / 3u)
                + " vertices="
                + std::to_string(resident_level->vertex_count)
                + " pulled_triangles="
                + std::to_string(resident_level->triangle_count)
                + " submitted_indices="
                + std::to_string(resident_level->index_count)
                + " dispatch_ms="
                + std::to_string(dispatch_ms));
            return resident_level->valid();
        }

        MeshData laplacian_graph_cells_from_preferred_neighbors(
            const MeshData& source_mesh,
            const GraphCoarsenWeights* weights,
            GraphCoarsenWeights* weights_out,
            std::span<const uint32_t> preferred_neighbors,
            uint32_t max_cell_size,
            float& conservative_error_out)
        {
            conservative_error_out = 0.0f;
            const uint32_t vertex_count = source_mesh.vertex_count();
            if (vertex_count < 4u
                || preferred_neighbors.size() != vertex_count
                || max_cell_size < 2u)
            {
                return source_mesh;
            }

            DisjointSet cells(vertex_count);
            for (uint32_t v = 0; v < vertex_count; ++v) {
                const uint32_t preferred = preferred_neighbors[v];
                if (preferred >= vertex_count || preferred == v) {
                    continue;
                }
                if (!graph_coarsen_edge_allowed(weights, v, preferred)) {
                    continue;
                }
                (void)cells.merge(v, preferred, max_cell_size);
            }

            return build_laplacian_graph_cells_from_cells(
                source_mesh,
                weights,
                weights_out,
                cells,
                conservative_error_out);
        }

        MeshClusterHierarchyData build_laplacian_graph_cells_hierarchy(
            const wz::asset::AssetKey& hierarchy_key,
            const MeshClusterHierarchyDesc& desc,
            const MeshData& source_mesh,
            const ComputePipelineData* graph_cells_pipeline,
            const ComputePipelineData* graph_cells_compact_pipeline,
            const MeshSparseOperatorData* graph_operator,
            const MeshDerivedFieldData* region_field,
            MeshFieldComputeBackend& mesh_field_compute,
            GpuResidentMeshDataTable& gpu_resident_mesh_data_table,
            GpuResidentSparseOperatorTable& gpu_resident_sparse_operator_table,
            GpuResidentMeshClusterHierarchyTable&
                gpu_resident_mesh_cluster_hierarchy_table,
            wz::Logger& logger)
        {
            MeshClusterHierarchyData data = build_identity_hierarchy(
                desc,
                source_mesh);
            data.method = MeshClusterHierarchyBuildMethod::LaplacianGraphCells;

            MeshData current = source_mesh;
            GraphCoarsenWeights weights =
                graph_coarsen_weights_for_region_mask(
                    desc,
                    source_mesh,
                    region_field,
                    logger);
            float accumulated_error = 0.0f;
            const uint32_t max_levels = hierarchy_max_level_count(desc);
            constexpr uint32_t kMinTriangles = 1u;
            constexpr uint32_t kMaxCellSize = 2u;
            GpuGraphCellsPreferenceResult gpu_preferences{};

            for (uint32_t level_index = 1u;
                 level_index < max_levels
                 && current.index_count() / 3u > kMinTriangles;
                 ++level_index)
            {
                const auto level_started = std::chrono::steady_clock::now();
                float level_error = 0.0f;
                GraphCoarsenWeights next_weights{};
                MeshData next{};
                if (level_index == 1u) {
                    gpu_preferences =
                        compute_gpu_graph_cell_preferences(
                            desc,
                            current,
                            graph_cells_pipeline,
                            graph_operator,
                            mesh_field_compute,
                            gpu_resident_mesh_data_table,
                            gpu_resident_sparse_operator_table,
                            logger);
                }
                if (gpu_preferences.used_gpu
                    && level_index == 1u)
                {
                    MeshData gpu_preview{};
                    GraphCoarsenWeights gpu_preview_weights{};
                    float gpu_preview_error = 0.0f;
                    (void)compute_gpu_resident_graph_cells_level(
                        hierarchy_key,
                        level_index,
                        desc,
                        current,
                        weights,
                        graph_cells_compact_pipeline,
                        gpu_preferences.preferred_neighbors_buffer,
                        mesh_field_compute,
                        gpu_resident_mesh_data_table,
                        gpu_resident_mesh_cluster_hierarchy_table,
                        &gpu_preview,
                        &gpu_preview_weights,
                        &gpu_preview_error,
                        logger);
                    if (gpu_preview.valid()) {
                        next = std::move(gpu_preview);
                        next_weights = std::move(gpu_preview_weights);
                        level_error = gpu_preview_error;
                    }
                    else {
                        next = laplacian_graph_cells_from_preferred_neighbors(
                            current,
                            weights.active ? &weights : nullptr,
                            &next_weights,
                            gpu_preferences.preferred_neighbors,
                            kMaxCellSize,
                            level_error);
                    }
                }
                else {
                    next = laplacian_graph_cells_once(
                        current,
                        weights.active ? &weights : nullptr,
                        &next_weights,
                        kMaxCellSize,
                        level_error);
                }
                if (!next.valid()
                    || (next.index_count() >= current.index_count()
                        && next.vertex_count() >= current.vertex_count()))
                {
                    break;
                }

                accumulated_error += level_error;

                MeshClusterHierarchyLevel level{};
                level.level_index = level_index;
                level.cluster_count = next.vertex_count();
                level.vertex_count = next.vertex_count();
                level.triangle_count = next.index_count() / 3u;
                level.conservative_error = accumulated_error;
                level.preview_mesh = next;
                data.levels.push_back(std::move(level));

                const auto level_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - level_started)
                        .count();
                logger.info(
                    std::string(
                        "asset compile: mesh cluster hierarchy level method=")
                    + hierarchy_method_name(data.method)
                    + " level="
                    + std::to_string(level_index)
                    + " vertices="
                    + std::to_string(data.levels.back().vertex_count)
                    + " triangles="
                    + std::to_string(data.levels.back().triangle_count)
                    + " ms="
                    + std::to_string(level_ms));

                current = std::move(next);
                weights = std::move(next_weights);
            }

            if (gpu_preferences.used_gpu) {
                logger.info(
                    "asset compile: mesh cluster hierarchy GPU graph preferences vertices="
                    + std::to_string(source_mesh.vertex_count())
                    + " ms="
                    + std::to_string(gpu_preferences.dispatch_ms));
            }
            if (gpu_preferences.preferred_neighbors_buffer.valid()) {
                mesh_field_compute.release_buffer(
                    gpu_preferences.preferred_neighbors_buffer);
            }

            return data;
        }

        wz::asset::AssetNode compile_mesh_cluster_hierarchy_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshFieldComputeBackend& mesh_field_compute,
            MeshTable& mesh_table,
            ComputePipelineTable& compute_pipeline_table,
            MeshDerivedFieldTable& mesh_derived_field_table,
            MeshSparseOperatorTable& mesh_sparse_operator_table,
            GpuResidentMeshDataTable& gpu_resident_mesh_data_table,
            GpuResidentSparseOperatorTable& gpu_resident_sparse_operator_table,
            GpuResidentMeshClusterHierarchyTable&
                gpu_resident_mesh_cluster_hierarchy_table,
            MeshClusterHierarchyTable& hierarchy_table)
        {
            MeshClusterHierarchyDesc param_desc{};
            const auto* desc =
                std::any_cast<MeshClusterHierarchyDesc>(&input.meta);
            if (!desc) {
                if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    param_desc = hierarchy_desc_from_params(
                        *params,
                        dep_nodes);
                    desc = &param_desc;
                }
            }
            if (!desc || dep_handles.empty())
            {
                logger.error("mesh cluster hierarchy node missing desc");
                return compile_failed_node(input);
            }

            size_t dep_index = 0u;
            const MeshData* source_mesh = mesh_table.get(dep_handles[0]);
            if (!source_mesh || !source_mesh->valid()) {
                logger.error("mesh cluster hierarchy source mesh is invalid");
                return compile_failed_node(input);
            }
            ++dep_index;

            const MeshDerivedFieldData* region_field = nullptr;
            if (desc->region_mask && desc->region_mask->field.valid()) {
                if (dep_index >= dep_handles.size()) {
                    logger.error(
                        "mesh cluster hierarchy region mask field missing");
                    return compile_failed_node(input);
                }
                region_field =
                    mesh_derived_field_table.get(dep_handles[dep_index++]);
                if (!region_field || !region_field->valid()) {
                    logger.error(
                        "mesh cluster hierarchy region mask field is invalid");
                    return compile_failed_node(input);
                }
            }

            const MeshSparseOperatorData* graph_operator = nullptr;
            if (desc->graph_operator.valid()) {
                if (dep_index >= dep_handles.size()) {
                    logger.error(
                        "mesh cluster hierarchy graph operator missing");
                    return compile_failed_node(input);
                }
                graph_operator =
                    mesh_sparse_operator_table.get(dep_handles[dep_index++]);
                if (!graph_operator || !graph_operator->valid()) {
                    logger.error(
                        "mesh cluster hierarchy graph operator is invalid");
                    return compile_failed_node(input);
                }
            }

            const ComputePipelineData* graph_cells_pipeline = nullptr;
            if (desc->graph_cells_pipeline.valid()) {
                if (dep_index >= dep_handles.size()) {
                    logger.error(
                        "mesh cluster hierarchy graph cells pipeline missing");
                    return compile_failed_node(input);
                }
                graph_cells_pipeline =
                    compute_pipeline_table.get(dep_handles[dep_index++]);
                if (!graph_cells_pipeline || !graph_cells_pipeline->valid()) {
                    logger.error(
                        "mesh cluster hierarchy graph cells pipeline is invalid");
                    return compile_failed_node(input);
                }
            }

            const ComputePipelineData* graph_cells_compact_pipeline = nullptr;
            if (desc->graph_cells_compact_pipeline.valid()) {
                if (dep_index >= dep_handles.size()) {
                    logger.error(
                        "mesh cluster hierarchy graph cells compact pipeline missing");
                    return compile_failed_node(input);
                }
                graph_cells_compact_pipeline =
                    compute_pipeline_table.get(dep_handles[dep_index++]);
                if (!graph_cells_compact_pipeline
                    || !graph_cells_compact_pipeline->valid())
                {
                    logger.error(
                        "mesh cluster hierarchy graph cells compact pipeline is invalid");
                    return compile_failed_node(input);
                }
            }

            if (dep_index != dep_handles.size()) {
                logger.error("mesh cluster hierarchy has unexpected deps");
                return compile_failed_node(input);
            }

            const auto build_started = std::chrono::steady_clock::now();
            MeshClusterHierarchyData data{};
            switch (desc->method) {
            case MeshClusterHierarchyBuildMethod::Identity:
                data = build_identity_hierarchy(*desc, *source_mesh);
                break;

            case MeshClusterHierarchyBuildMethod::GraphCoarsen:
                data = build_graph_coarsen_hierarchy(
                    *desc,
                    *source_mesh,
                    region_field,
                    logger);
                break;

            case MeshClusterHierarchyBuildMethod::LaplacianGraphCells:
                data = build_laplacian_graph_cells_hierarchy(
                    input.key,
                    *desc,
                    *source_mesh,
                    graph_cells_pipeline,
                    graph_cells_compact_pipeline,
                    graph_operator,
                    region_field,
                    mesh_field_compute,
                    gpu_resident_mesh_data_table,
                    gpu_resident_sparse_operator_table,
                    gpu_resident_mesh_cluster_hierarchy_table,
                    logger);
                break;
            }

            const auto build_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - build_started)
                    .count();

            if (!data.valid()) {
                logger.error(
                    "mesh cluster hierarchy compiler produced invalid data");
                return compile_failed_node(input);
            }

            const MeshClusterHierarchyLevel& first = data.levels.front();
            const MeshClusterHierarchyLevel& last = data.levels.back();
            logger.info(
                std::string("asset compile: mesh cluster hierarchy method=")
                + hierarchy_method_name(data.method)
                + " levels="
                + std::to_string(data.level_count())
                + " source_vertices="
                + std::to_string(first.vertex_count)
                + " source_triangles="
                + std::to_string(first.triangle_count)
                + " final_vertices="
                + std::to_string(last.vertex_count)
                + " final_triangles="
                + std::to_string(last.triangle_count)
                + " final_error="
                + std::to_string(last.conservative_error)
                + " ms="
                + std::to_string(build_ms));

            publish_gpu_cluster_hierarchy(
                input.key,
                data,
                mesh_field_compute,
                gpu_resident_mesh_cluster_hierarchy_table,
                logger);

            const wz::asset::ResourceHandle handle =
                hierarchy_table.add(std::move(data));
            return handle.valid()
                ? compiled_node(input, handle)
                : compile_failed_node(input);
        }

        wz::asset::AssetNode compile_mesh_cluster_hierarchy_preview_mesh_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshClusterHierarchyTable& hierarchy_table)
        {
            MeshClusterHierarchyPreviewMeshDesc param_desc{};
            const auto* desc =
                std::any_cast<MeshClusterHierarchyPreviewMeshDesc>(
                    &input.meta);
            if (!desc) {
                if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    param_desc = preview_mesh_desc_from_params(
                        *params,
                        dep_nodes);
                    desc = &param_desc;
                }
            }
            if (!desc || dep_handles.size() != 1u) {
                logger.error(
                    "mesh cluster hierarchy preview mesh node missing desc");
                return compile_failed_node(input);
            }

            const MeshClusterHierarchyData* hierarchy =
                hierarchy_table.get(dep_handles[0]);
            if (!hierarchy || !hierarchy->valid()) {
                logger.error(
                    "mesh cluster hierarchy preview source is invalid");
                return compile_failed_node(input);
            }

            if (desc->level_index >= hierarchy->level_count()) {
                logger.error(
                    "mesh cluster hierarchy preview level is out of range");
                return compile_failed_node(input);
            }

            const MeshData& preview =
                hierarchy->levels[desc->level_index].preview_mesh;
            if (!preview.valid()) {
                logger.error(
                    "mesh cluster hierarchy preview level is invalid");
                return compile_failed_node(input);
            }

            const auto copy_started = std::chrono::steady_clock::now();
            const wz::asset::ResourceHandle handle =
                mesh_table.add(preview);
            const auto copy_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - copy_started)
                    .count();
            logger.info(
                std::string(
                    "asset compile: mesh cluster hierarchy preview method=")
                + hierarchy_method_name(hierarchy->method)
                + " level="
                + std::to_string(desc->level_index)
                + " vertices="
                + std::to_string(preview.vertex_count())
                + " triangles="
                + std::to_string(preview.index_count() / 3u)
                + " copy_ms="
                + std::to_string(copy_ms));
            return handle.valid()
                ? compiled_node(input, handle)
                : compile_failed_node(input);
        }
    }

    void register_mesh_cluster_hierarchy_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshFieldComputeBackend& mesh_field_compute,
        MeshTable& mesh_table,
        ComputePipelineTable& compute_pipeline_table,
        MeshDerivedFieldTable& mesh_derived_field_table,
        MeshSparseOperatorTable& mesh_sparse_operator_table,
        GpuResidentMeshDataTable& gpu_resident_mesh_data_table,
        GpuResidentSparseOperatorTable& gpu_resident_sparse_operator_table,
        GpuResidentMeshClusterHierarchyTable&
            gpu_resident_mesh_cluster_hierarchy_table,
        MeshClusterHierarchyTable& hierarchy_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshClusterHierarchySchema,
            .output_type = kAssetTypeMeshClusterHierarchy,
            .input_ports = {
                { "source_mesh", kAssetTypeMesh },
            },
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "method",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Method",
                    .default_num =
                        static_cast<double>(
                            MeshClusterHierarchyBuildMethod::Identity),
                    .options = kHierarchyMethodOptions,
                },
                {
                    .name = "max_level_index",
                    .type = wz::asset::ParamType::Int,
                    .label = "Max level index",
                    .default_num = 3.0,
                    .min = 0.0,
                    .max = 7.0,
                },
            },
            .compile = [
                &logger,
                &mesh_field_compute,
                &mesh_table,
                &compute_pipeline_table,
                &mesh_derived_field_table,
                &mesh_sparse_operator_table,
                &gpu_resident_mesh_data_table,
                &gpu_resident_sparse_operator_table,
                &gpu_resident_mesh_cluster_hierarchy_table,
                &hierarchy_table](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_mesh_cluster_hierarchy_node(
                    input,
                    dep_nodes,
                    dep_handles,
                    logger,
                    mesh_field_compute,
                    mesh_table,
                    compute_pipeline_table,
                    mesh_derived_field_table,
                    mesh_sparse_operator_table,
                    gpu_resident_mesh_data_table,
                    gpu_resident_sparse_operator_table,
                    gpu_resident_mesh_cluster_hierarchy_table,
                    hierarchy_table);
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshClusterHierarchyPreviewMeshSchema,
            .output_type = kAssetTypeMesh,
            .input_ports = {
                { "hierarchy", kAssetTypeMeshClusterHierarchy },
            },
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "level_index",
                    .type = wz::asset::ParamType::Int,
                    .label = "Level index",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 7.0,
                },
            },
            .compile = [
                &logger,
                &mesh_table,
                &hierarchy_table](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_mesh_cluster_hierarchy_preview_mesh_node(
                    input,
                    dep_nodes,
                    dep_handles,
                    logger,
                    mesh_table,
                    hierarchy_table);
            }
        });
    }
}
