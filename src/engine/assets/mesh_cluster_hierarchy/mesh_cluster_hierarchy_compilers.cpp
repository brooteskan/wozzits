// src/engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy_compilers.cpp

#include <engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy_compilers.h>

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>
#include <engine/assets/mesh_cluster_hierarchy_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <array>
#include <any>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
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
            uint32_t a = 0;
            uint32_t b = 0;
            uint32_t adjacent_triangle_count = 0;
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

        float collapse_shape_penalty(
            const MeshData& mesh,
            uint32_t a,
            uint32_t b) noexcept
        {
            float midpoint[3]{
                (mesh.vertices[a].position[0]
                    + mesh.vertices[b].position[0]) * 0.5f,
                (mesh.vertices[a].position[1]
                    + mesh.vertices[b].position[1]) * 0.5f,
                (mesh.vertices[a].position[2]
                    + mesh.vertices[b].position[2]) * 0.5f,
            };

            float penalty = 0.0f;
            const uint32_t triangle_count = mesh.index_count() / 3u;
            for (uint32_t tri = 0; tri < triangle_count; ++tri) {
                const uint32_t i0 = mesh.indices[tri * 3u + 0u];
                const uint32_t i1 = mesh.indices[tri * 3u + 1u];
                const uint32_t i2 = mesh.indices[tri * 3u + 2u];
                const bool has_a = i0 == a || i1 == a || i2 == a;
                const bool has_b = i0 == b || i1 == b || i2 == b;
                if (!has_a && !has_b) {
                    continue;
                }
                if (has_a && has_b) {
                    continue;
                }

                const float* p0 = i0 == a || i0 == b
                    ? midpoint
                    : mesh.vertices[i0].position;
                const float* p1 = i1 == a || i1 == b
                    ? midpoint
                    : mesh.vertices[i1].position;
                const float* p2 = i2 == a || i2 == b
                    ? midpoint
                    : mesh.vertices[i2].position;
                penalty = (std::max)(
                    penalty,
                    triangle_shape_penalty(p0, p1, p2));
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

            const auto add_edge = [&](uint32_t a, uint32_t b) {
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
                candidates.push_back(candidate);
            };

            for (uint32_t tri = 0; tri < triangle_count; ++tri) {
                const uint32_t i0 = mesh.indices[tri * 3u + 0u];
                const uint32_t i1 = mesh.indices[tri * 3u + 1u];
                const uint32_t i2 = mesh.indices[tri * 3u + 2u];
                add_edge(i0, i1);
                add_edge(i1, i2);
                add_edge(i2, i0);
            }

            std::sort(
                candidates.begin(),
                candidates.end(),
                edge_candidate_key_less);

            std::vector<EdgeCandidate> unique;
            unique.reserve(candidates.size());
            for (const EdgeCandidate& candidate : candidates) {
                if (!unique.empty()
                    && unique.back().key == candidate.key)
                {
                    ++unique.back().adjacent_triangle_count;
                    continue;
                }
                unique.push_back(candidate);
            }

            for (EdgeCandidate& candidate : unique) {
                const float length_sq = distance_squared(
                    mesh.vertices[candidate.a].position,
                    mesh.vertices[candidate.b].position);
                const float shape_penalty = collapse_shape_penalty(
                    mesh,
                    candidate.a,
                    candidate.b);
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
                unique.begin(),
                unique.end(),
                edge_candidate_score_less);
            return unique;
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
            constexpr uint32_t kMaxLevels = 4u;
            constexpr uint32_t kMinTriangles = 1u;

            for (uint32_t level_index = 1u;
                 level_index < kMaxLevels
                 && current.index_count() / 3u > kMinTriangles;
                 ++level_index)
            {
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

                current = std::move(next);
                weights = std::move(next_weights);
            }

            return data;
        }

        wz::asset::AssetNode compile_mesh_cluster_hierarchy_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshDerivedFieldTable& mesh_derived_field_table,
            MeshClusterHierarchyTable& hierarchy_table)
        {
            const auto* desc =
                std::any_cast<MeshClusterHierarchyDesc>(&input.meta);
            if (!desc
                || (dep_handles.size() != 1u && dep_handles.size() != 2u))
            {
                logger.error("mesh cluster hierarchy node missing desc");
                return compile_failed_node(input);
            }

            const MeshData* source_mesh = mesh_table.get(dep_handles[0]);
            if (!source_mesh || !source_mesh->valid()) {
                logger.error("mesh cluster hierarchy source mesh is invalid");
                return compile_failed_node(input);
            }

            const MeshDerivedFieldData* region_field = nullptr;
            if (desc->region_mask && desc->region_mask->field.valid()) {
                if (dep_handles.size() != 2u) {
                    logger.error(
                        "mesh cluster hierarchy region mask field missing");
                    return compile_failed_node(input);
                }
                region_field =
                    mesh_derived_field_table.get(dep_handles[1]);
                if (!region_field || !region_field->valid()) {
                    logger.error(
                        "mesh cluster hierarchy region mask field is invalid");
                    return compile_failed_node(input);
                }
            }

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
            }

            if (!data.valid()) {
                logger.error(
                    "mesh cluster hierarchy compiler produced invalid data");
                return compile_failed_node(input);
            }

            const wz::asset::ResourceHandle handle =
                hierarchy_table.add(std::move(data));
            return handle.valid()
                ? compiled_node(input, handle)
                : compile_failed_node(input);
        }

        wz::asset::AssetNode compile_mesh_cluster_hierarchy_preview_mesh_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshClusterHierarchyTable& hierarchy_table)
        {
            const auto* desc =
                std::any_cast<MeshClusterHierarchyPreviewMeshDesc>(
                    &input.meta);
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

            const wz::asset::ResourceHandle handle =
                mesh_table.add(preview);
            return handle.valid()
                ? compiled_node(input, handle)
                : compile_failed_node(input);
        }
    }

    void register_mesh_cluster_hierarchy_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshTable& mesh_table,
        MeshDerivedFieldTable& mesh_derived_field_table,
        MeshClusterHierarchyTable& hierarchy_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshClusterHierarchySchema,
            .output_type = kAssetTypeMeshClusterHierarchy,
            .compile = [
                &logger,
                &mesh_table,
                &mesh_derived_field_table,
                &hierarchy_table](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode>,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_mesh_cluster_hierarchy_node(
                    input,
                    dep_handles,
                    logger,
                    mesh_table,
                    mesh_derived_field_table,
                    hierarchy_table);
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshClusterHierarchyPreviewMeshSchema,
            .output_type = kAssetTypeMesh,
            .compile = [
                &logger,
                &mesh_table,
                &hierarchy_table](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode>,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_mesh_cluster_hierarchy_preview_mesh_node(
                    input,
                    dep_handles,
                    logger,
                    mesh_table,
                    hierarchy_table);
            }
        });
    }
}
