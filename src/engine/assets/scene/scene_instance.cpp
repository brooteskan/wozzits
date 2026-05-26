// src/engine/assets/scene/scene_instance.cpp

#include <engine/assets/scene/scene_instance.h>

#include <scene/scene_graph.h>
#include <scene/compile/scene_node_class.h>
#include <scene/compile/legacy_classification.h>

#include <math/mat4.h>
#include <math/math_types.h>
#include <math/projection.h>

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

        // Build a view matrix from a camera node's world transform.
        // The view matrix is the inverse of the world matrix.  For a rigid-body
        // transform (rotation + translation, no non-uniform scale) this is:
        //   V = transpose(R) with translation = -transpose(R) * t
        wz::math::Mat4 view_matrix_from_world(const wz::math::Mat4& w)
        {
            // Extract the 3x3 rotation columns from the column-major world matrix.
            const float rx = w.m[0], ry = w.m[1], rz = w.m[2];   // column 0 = right
            const float ux = w.m[4], uy = w.m[5], uz = w.m[6];   // column 1 = up
            const float fx = w.m[8], fy = w.m[9], fz = w.m[10];  // column 2 = forward
            const float px = w.m[12], py = w.m[13], pz = w.m[14]; // translation

            wz::math::Mat4 V{};
            // Transposed rotation: each ROW of the result is one column of the
            // original rotation so that V * world_pos projects into camera space.
            V.m[0]  = rx;    V.m[1]  = ux;    V.m[2]  = fx;    V.m[3]  = 0.0f;
            V.m[4]  = ry;    V.m[5]  = uy;    V.m[6]  = fy;    V.m[7]  = 0.0f;
            V.m[8]  = rz;    V.m[9]  = uz;    V.m[10] = fz;    V.m[11] = 0.0f;
            V.m[12] = -(rx * px + ry * py + rz * pz);
            V.m[13] = -(ux * px + uy * py + uz * pz);
            V.m[14] = -(fx * px + fy * py + fz * pz);
            V.m[15] = 1.0f;
            return V;
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

        // Build non-render component tables
        for (const auto& node : scene.nodes) {
            NodeHandle h = id_to_handle[node.id];

            if (node.input_receiver) {
                inst.input_receivers.push_back({
                    .node = h,
                    .component = InputReceiverComponent{
                        .input_map = node.input_receiver->input_map,
                    },
                });
            }

            if (node.flying_camera_controller) {
                const auto& fc = *node.flying_camera_controller;
                inst.flying_camera_controllers.push_back({
                    .node = h,
                    .component = FlyingCameraControllerComponent{
                        .move_speed       = fc.move_speed,
                        .look_speed       = fc.look_speed,
                        .boost_multiplier = fc.boost_multiplier,
                        .roll_speed       = fc.roll_speed,
                    },
                });
            }

            if (node.audio_listener) {
                inst.audio_listeners.push_back({
                    .node = h,
                    .component = AudioListenerComponent{
                        .active = node.audio_listener->active,
                    },
                });
            }

            if (node.event_listener) {
                inst.event_listeners.push_back({
                    .node = h,
                    .component = EventListenerComponent{
                        .channels = node.event_listener->channels,
                    },
                });
            }

            if (node.debug_visual) {
                inst.debug_visuals.push_back({
                    .node = h,
                    .component = DebugVisualComponent{
                        .kind    = node.debug_visual->kind,
                        .scale   = node.debug_visual->scale,
                        .visible = node.debug_visual->visible,
                    },
                });
            }
        }

        // Build light records
        for (const auto& light : scene.lights) {
            inst.lights.push_back(light.light);
        }

        // Populate default_view from the active camera node, if specified.
        if (scene.defaults.active_camera_node) {
            const auto& cam_id = *scene.defaults.active_camera_node;
            auto cam_it = id_to_handle.find(cam_id);
            if (cam_it != id_to_handle.end()) {
                // Find the camera intrinsics on this node.
                const SceneCameraAsset* cam_asset = nullptr;
                for (const auto& node : scene.nodes) {
                    if (node.id == cam_id && node.camera) {
                        cam_asset = &*node.camera;
                        break;
                    }
                }

                if (cam_asset) {
                    const auto& cam_world = wz::core::graph::node_data(
                        inst.storage.polytree, cam_it->second).world;

                    inst.default_view.camera_position = {
                        cam_world.m[12], cam_world.m[13], cam_world.m[14]
                    };
                    inst.default_view.view = view_matrix_from_world(cam_world);
                    inst.default_view.projection =
                        wz::math::projection_perspective_dx(
                            cam_asset->fov_y,
                            cam_asset->aspect,
                            cam_asset->near_plane,
                            cam_asset->far_plane);
                    inst.default_view.view_projection =
                        wz::math::mul(
                            inst.default_view.projection,
                            inst.default_view.view);
                }
            }
        }

        result.error = SceneInstantiateError::None;
        return result;
    }

} // namespace wz::engine::assets
