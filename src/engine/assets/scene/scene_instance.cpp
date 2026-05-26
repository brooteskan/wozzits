// src/engine/assets/scene/scene_instance.cpp

#include <engine/assets/scene/scene_instance.h>

#include <scene/scene_graph.h>
#include <scene/compile/scene_node_class.h>
#include <scene/compile/legacy_classification.h>

#include <math/mat4.h>
#include <math/math_types.h>

#include <unordered_map>
#include <unordered_set>

namespace wz::engine::assets
{
    namespace
    {
        wz::math::Mat4 authored_to_mat4(const AuthoredTransform& t)
        {
            wz::math::Transform xform{};
            xform.position = { t.translation[0], t.translation[1], t.translation[2] };
            xform.rotation = {
                t.rotation_quat[0], t.rotation_quat[1],
                t.rotation_quat[2], t.rotation_quat[3]
            };
            xform.scale = { t.scale[0], t.scale[1], t.scale[2] };
            return wz::math::transform(xform);
        }
    }

    SceneInstantiateResult instantiate_scene(const SceneAssetData& scene)
    {
        using namespace wz::scene;
        using namespace wz::core::graph;

        SceneInstantiateResult result{};

        // Validate unique node ids
        std::unordered_set<std::string> seen_ids;
        for (const auto& node : scene.nodes) {
            if (!seen_ids.insert(node.id).second) {
                result.error = SceneInstantiateError::DuplicateNodeId;
                result.error_detail = node.id;
                return result;
            }
        }

        // Validate parent ids exist
        for (const auto& node : scene.nodes) {
            if (node.parent_id && !seen_ids.contains(*node.parent_id)) {
                result.error = SceneInstantiateError::ParentNotFound;
                result.error_detail = *node.parent_id;
                return result;
            }
        }

        // Build scene graph
        SceneBuilder builder;
        auto& inst = result.instance;

        // Create all nodes first
        std::unordered_map<std::string, NodeHandle> id_to_handle;
        for (const auto& node : scene.nodes) {
            TransformNode tn{};
            tn.local = authored_to_mat4(node.local);
            tn.motion_type = node.motion_type;

            if (node.renderable) {
                tn.flags = TransformNodeFlag::RenderDomain;
            }

            NodeHandle h = add_node(builder, tn);
            id_to_handle[node.id] = h;
        }

        // Add parent-child edges
        for (const auto& node : scene.nodes) {
            if (!node.parent_id) continue;

            NodeHandle child = id_to_handle[node.id];
            NodeHandle parent_h = id_to_handle[*node.parent_id];

            if (!add_edge(builder, parent_h, child)) {
                result.error = SceneInstantiateError::ParentCycle;
                result.error_detail = node.id;
                return result;
            }
        }

        // Build the polytree
        auto storage_result = build(std::move(builder));
        if (!storage_result.has_value()) {
            result.error = SceneInstantiateError::PolytreeBuildFailed;
            return result;
        }

        inst.storage = std::move(*storage_result);

        // Propagate world transforms
        propagate_all(inst.storage.polytree);

        // Build authored id <-> runtime handle maps
        uint32_t nc = node_count(inst.storage.polytree);
        inst.runtime_to_authored.resize(nc);
        for (const auto& node : scene.nodes) {
            NodeHandle h = id_to_handle[node.id];
            inst.runtime_to_authored[h] = node.id;
            inst.authored_to_runtime[node.id] = h;
        }

        // Allocate renderables sized to node count
        inst.renderables.resize(nc);

        // Fill renderable descriptors
        for (const auto& node : scene.nodes) {
            NodeHandle h = id_to_handle[node.id];

            if (node.renderable) {
                const auto& rb = *node.renderable;
                inst.renderables[h] = RenderableDescriptor{
                    .node_class = rb.node_class,
                    .mesh = rb.mesh,
                    .material = rb.material,
                    .local_bounds = rb.local_bounds,
                    .visible = rb.visible && node.visible,
                };
            }
            else {
                inst.renderables[h] = RenderableDescriptor{};
            }
        }

        // Build light records
        for (const auto& light : scene.lights) {
            inst.lights.push_back(light.light);
        }

        result.error = SceneInstantiateError::None;
        return result;
    }

} // namespace wz::engine::assets
