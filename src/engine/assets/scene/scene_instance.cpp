// src/engine/assets/scene/scene_instance.cpp

#include <engine/assets/scene/scene_instance.h>
#include <engine/assets/scene/scene_fingerprint.h>

#include <scene/scene_graph.h>
#include <scene/compile/scene_node_class.h>
#include <scene/compile/legacy_classification.h>

#include <math/mat4.h>
#include <math/math_types.h>
#include <math/projection.h>

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace wz::engine::assets
{
    namespace
    {
        // Build a view matrix from a camera node's world transform.
        // The view matrix is the inverse of the world matrix.  For a rigid-body
        // transform (rotation + translation, no non-uniform scale) this is:
        //   V = transpose(R) with translation = -transpose(R) * t
        wz::scene::RenderableDescriptor descriptor_from_renderable_asset(
            const RenderableAssetData& data, bool node_visible)
        {
            namespace sc = wz::scene;

            sc::SceneNodeClass nc{};
            nc.role    = sc::SceneRole::Renderable;
            nc.compile = sc::CompileBehavior::Static;

            switch (data.kind) {
            case RenderableKind::Mesh:
                if (data.program == BuiltinRenderProgram::TerrainMeshSurface) {
                    nc.producer = sc::ProducerKind::TerrainPatch;
                    nc.spatial  = sc::SpatialKind::Box;
                }
                else {
                    nc.producer = sc::ProducerKind::Mesh;
                    nc.spatial  = sc::SpatialKind::MeshBounds;
                }
                break;
            case RenderableKind::GaussianSplatCloud:
                nc.producer = sc::ProducerKind::SplatCloud;
                nc.spatial  = sc::SpatialKind::Box;
                break;
            case RenderableKind::ScalarField:
            case RenderableKind::VectorField:
                // Provisional: scalar fields enter scene-render as mesh-like
                // descriptors for debug visualization. Vector fields reuse this
                // placeholder until field renderables get a dedicated producer.
                nc.producer = sc::ProducerKind::Mesh;
                nc.spatial  = sc::SpatialKind::Box;
                break;
            }

            switch (data.domain) {
            case RenderDomain::Debug:
                nc.default_surface = sc::SurfaceClass::Opaque;
                nc.domains = sc::RenderDomain::Surface | sc::RenderDomain::Debug;
                break;
            case RenderDomain::Sky:
                nc.role = sc::SceneRole::None;
                nc.default_surface = sc::SurfaceClass::None;
                nc.domains = 0;
                break;
            case RenderDomain::Opaque:
                nc.default_surface = sc::SurfaceClass::Opaque;
                nc.domains = sc::RenderDomain::Surface | sc::RenderDomain::Shadow;
                break;
            case RenderDomain::Transparent:
                nc.default_surface = sc::SurfaceClass::Transparent;
                nc.domains = sc::RenderDomain::Surface | sc::RenderDomain::Transparent;
                break;
            case RenderDomain::Splat:
                nc.default_surface = sc::SurfaceClass::Transparent;
                nc.domains = sc::RenderDomain::Splat | sc::RenderDomain::Transparent;
                break;
            }

            // mesh/material: RenderableAssetData carries source_asset (AssetKey)
            // but not scene-render MeshHandle/MaterialHandle.  Those are GPU-side
            // indices resolved during the realize/upload path.  Use 0 as a valid
            // placeholder (not INVALID_MESH) so the compile path routes the node
            // into the correct draw bucket.  A later issue should extend the
            // resolver or context to supply real GPU handles.
            return sc::RenderableDescriptor{
                .node_class   = nc,
                .mesh         = 0,
                .material     = 0,
                .local_bounds = sc::AABB{
                    .min = { data.bounds_min[0], data.bounds_min[1], data.bounds_min[2] },
                    .max = { data.bounds_max[0], data.bounds_max[1], data.bounds_max[2] },
                },
                .visible      = node_visible,
            };
        }

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

        const char* log_owner(const SceneInstantiateContext& context)
        {
            return context.log_owner ? context.log_owner : "Scene";
        }

        std::string node_log_name(const SceneNodeAsset& node)
        {
            return "node id='" + node.id + "' name='" + node.name + "'";
        }

        void log_instantiate_start(
            const SceneAssetData& scene,
            const SceneInstantiateContext& context)
        {
            if (!context.logger) {
                return;
            }

            const auto summary = summarize_authored_scene_components(scene);
            std::ostringstream msg;
            msg
                << "[" << log_owner(context) << "] instantiate_scene begin"
                << " scene=" << scene.name
                << " scene_hash=" << scene_asset_fingerprint_string(scene)
                << " authored_nodes=" << summary.nodes
                << " authored_renderables=" << summary.renderables
                << " cameras=" << summary.cameras
                << " lights=" << summary.lights
                << " input_receivers=" << summary.input_receivers
                << " flying_camera_controllers="
                << summary.flying_camera_controllers
                << " actor_movement_controllers="
                << summary.actor_movement_controllers
                << " ground_boundaries=" << summary.ground_boundaries
                << " audio_listeners=" << summary.audio_listeners
                << " event_listeners=" << summary.event_listeners
                << " auxiliary_visuals=" << summary.auxiliary_visuals
                << " editor_handles=" << summary.editor_handles;
            context.logger->info(msg.str());
        }

        void log_channel_warnings(
            const SceneNodeAsset& node,
            const SceneInstantiateContext& context,
            const char* component_name,
            const wz::engine::behavior::EventChannelCompileResult& compiled)
        {
            if (!context.logger) {
                return;
            }

            if (compiled.unknown_count > 0u) {
                std::ostringstream msg;
                msg << "[" << log_owner(context) << "] "
                    << node_log_name(node)
                    << " " << component_name << " has "
                    << compiled.unknown_count
                    << " unknown channel token(s)";
                context.logger->warn(msg.str());
            }

            if (compiled.redundant_count > 0u) {
                std::ostringstream msg;
                msg << "[" << log_owner(context) << "] "
                    << node_log_name(node)
                    << " " << component_name << " has "
                    << compiled.redundant_count
                    << " redundant channel token(s)";
                context.logger->warn(msg.str());
            }
        }

        void log_event_listener_channel_warnings(
            const SceneNodeAsset& node,
            const SceneInstantiateContext& context,
            const wz::engine::behavior::EventChannelCompileResult& compiled)
        {
            log_channel_warnings(
                node,
                context,
                "event_listener",
                compiled);
        }

        std::string effective_behavior_binding_id(
            const SceneNodeAsset& node,
            const SceneBehaviorAsset& behavior,
            uint32_t binding_ordinal)
        {
            if (!behavior.id.empty()) {
                return behavior.id;
            }
            return node.id + "/behavior/" + std::to_string(binding_ordinal);
        }

        BehaviorComponent instantiate_behavior_component(
            const SceneNodeAsset& node,
            const SceneInstantiateContext& context,
            const SceneBehaviorAsset& behavior,
            std::string binding_id)
        {
            const auto compiled =
                wz::engine::behavior::compile_channel_mask(behavior.events);
            log_channel_warnings(node, context, "behavior", compiled);
            return BehaviorComponent{
                .binding_id = std::move(binding_id),
                .module = behavior.module,
                .name = behavior.name,
                .enabled = behavior.enabled,
                .events = behavior.events,
                .channel_mask = compiled.mask,
                .config = behavior.config,
            };
        }

        void log_instantiate_failure(
            const SceneInstantiateResult& result,
            const SceneInstantiateContext& context)
        {
            if (!context.logger) {
                return;
            }

            std::ostringstream msg;
            msg
                << "[" << log_owner(context) << "] instantiate_scene failed"
                << " owner=" << static_cast<const void*>(&context)
                << " error=" << static_cast<int>(result.error)
                << " detail=" << result.error_detail;
            context.logger->error(msg.str());
        }

        void log_instantiate_success(
            const SceneAssetData& scene,
            const SceneInstance& inst,
            const SceneInstantiateContext& context)
        {
            if (!context.logger) {
                return;
            }

            const auto summary = summarize_scene_instance_components(inst);
            std::ostringstream msg;
            msg
                << "[" << log_owner(context) << "] instantiate_scene complete"
                << " scene=" << scene.name
                << " scene_hash=" << scene_asset_fingerprint_string(scene)
                << " instance=" << static_cast<const void*>(&inst)
                << " storage=" << static_cast<const void*>(&inst.storage)
                << " runtime_entities=" << summary.runtime_entities
                << " descriptor_slots=" << summary.renderable_descriptor_slots
                << " cameras=" << summary.cameras
                << " lights=" << summary.lights
                << " ambient_lighting=" << summary.ambient_lighting
                << " hdri_environments=" << summary.hdri_environments
                << " sky_draws=" << summary.sky_draws
                << " input_receivers=" << summary.input_receivers
                << " flying_camera_controllers="
                << summary.flying_camera_controllers
                << " actor_movement_controllers="
                << summary.actor_movement_controllers
                << " ground_boundaries=" << summary.ground_boundaries
                << " audio_listeners=" << summary.audio_listeners
                << " event_listeners=" << summary.event_listeners
                << " auxiliary_visuals=" << summary.auxiliary_visuals;
            context.logger->info(msg.str());
        }

        uint32_t count_ambient_lighting_records(
            const std::vector<wz::scene::LightRecord>& lights) noexcept
        {
            return static_cast<uint32_t>(std::count_if(
                lights.begin(), lights.end(),
                [](const wz::scene::LightRecord& light) {
                    return light.type == wz::scene::LightType::Ambient;
                }));
        }
    }

    wz::math::Mat4 compose_scene_transform(const AuthoredTransform& t)
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

    bool update_scene_asset_node_transform(
        SceneAssetData& asset,
        const SceneInstance& instance,
        wz::core::graph::NodeHandle node,
        const AuthoredTransform& local)
    {
        if (node >= instance.runtime_to_authored.size())
            return false;

        return update_scene_asset_node_transform(
            asset,
            instance,
            instance.runtime_to_authored[node],
            local);
    }

    bool update_scene_asset_node_transform(
        SceneAssetData& asset,
        const SceneInstance& instance,
        const std::string& authored_node_id,
        const AuthoredTransform& local)
    {
        if (!instance.authored_to_runtime.contains(authored_node_id))
            return false;

        const auto it = std::find_if(
            asset.nodes.begin(), asset.nodes.end(),
            [&](const auto& node) { return node.id == authored_node_id; });
        if (it == asset.nodes.end())
            return false;

        it->local = local;
        return true;
    }

    wz::scene::SceneRuntimeComponentSummary summarize_scene_instance_components(
        const SceneInstance& instance)
    {
        return wz::scene::SceneRuntimeComponentSummary{
            .runtime_entities = static_cast<uint32_t>(
                wz::core::graph::node_count(instance.storage.polytree)),
            .renderable_descriptor_slots = static_cast<uint32_t>(
                instance.renderables.size()),
            .cameras = instance.cameras,
            .lights = static_cast<uint32_t>(instance.lights.size()),
            .ambient_lighting = count_ambient_lighting_records(
                instance.lights),
            .hdri_environments = instance.hdri_environments,
            .sky_draws = static_cast<uint32_t>(instance.sky_draws.size()),
            .input_receivers = static_cast<uint32_t>(
                instance.input_receivers.size()),
            .flying_camera_controllers = static_cast<uint32_t>(
                instance.flying_camera_controllers.size()),
            .actor_movement_controllers = static_cast<uint32_t>(
                instance.actor_movement_controllers.size()),
            .ground_boundaries = static_cast<uint32_t>(
                instance.ground_boundaries.size()),
            .collisions = static_cast<uint32_t>(
                instance.collisions.size()),
            .terrains = static_cast<uint32_t>(
                instance.terrains.size()),
            .audio_listeners = static_cast<uint32_t>(
                instance.audio_listeners.size()),
            .event_listeners = static_cast<uint32_t>(
                instance.event_listeners.size()),
            .proximities = static_cast<uint32_t>(
                instance.proximities.size()),
            .motions = static_cast<uint32_t>(
                instance.motions.size()),
            .behaviors = static_cast<uint32_t>(
                instance.behaviors.size()),
            .compute_kernels = instance.compute_kernels,
            .render_shaders = instance.render_shaders,
            .auxiliary_visuals = static_cast<uint32_t>(
                instance.auxiliary_visuals.size()),
        };
    }

    SceneInstantiateResult instantiate_scene(
        const SceneAssetData& scene,
        const SceneInstantiateContext& context)
    {
        using namespace wz::scene;
        using namespace wz::core::graph;

        SceneInstantiateResult result{};
        log_instantiate_start(scene, context);

        // Validate unique node ids
        std::unordered_set<std::string> seen_ids;
        for (const auto& node : scene.nodes) {
            if (!seen_ids.insert(node.id).second) {
                result.error = SceneInstantiateError::DuplicateNodeId;
                result.error_detail = "duplicate " + node_log_name(node);
                log_instantiate_failure(result, context);
                return result;
            }
        }

        // Validate parent ids exist
        for (const auto& node : scene.nodes) {
            if (node.parent_id && !seen_ids.contains(*node.parent_id)) {
                result.error = SceneInstantiateError::ParentNotFound;
                result.error_detail =
                    node_log_name(node) + " has missing parent id='"
                    + *node.parent_id + "'";
                log_instantiate_failure(result, context);
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
            tn.local = compose_scene_transform(node.local);
            tn.motion_type = node.motion_type;

            if (node.renderable || node.renderable_asset) {
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
                result.error_detail = "parent cycle at " + node_log_name(node);
                log_instantiate_failure(result, context);
                return result;
            }
        }

        // Build the polytree
        auto storage_result = build(std::move(builder));
        if (!storage_result.has_value()) {
            result.error = SceneInstantiateError::PolytreeBuildFailed;
            log_instantiate_failure(result, context);
            return result;
        }

        inst.storage = std::move(*storage_result);

        // Propagate world transforms
        propagate_all(inst.storage.polytree);

        // Build authored id <-> runtime handle maps
        uint32_t nc = node_count(inst.storage.polytree);
        inst.runtime_to_authored.resize(nc);
        inst.runtime_names.resize(nc);
        for (const auto& node : scene.nodes) {
            NodeHandle h = id_to_handle[node.id];
            inst.runtime_to_authored[h] = node.id;
            inst.runtime_names[h] = node.name;
            inst.authored_to_runtime[node.id] = h;
        }

        // Allocate renderables sized to node count
        inst.renderables.resize(nc);

        // Fill renderable descriptors
        for (const auto& node : scene.nodes) {
            NodeHandle h = id_to_handle[node.id];

            if (node.renderable_asset) {
                if (!context.renderable_resolver) {
                    result.error = SceneInstantiateError::RenderableResolveFailed;
                    result.error_detail = node_log_name(node)
                        + " has renderable_asset but no resolver provided";
                    log_instantiate_failure(result, context);
                    return result;
                }

                const auto* rdata = context.renderable_resolver->get(
                    *node.renderable_asset);

                if (!rdata) {
                    result.error = SceneInstantiateError::RenderableResolveFailed;
                    result.error_detail = node_log_name(node)
                        + " renderable asset could not be resolved";
                    log_instantiate_failure(result, context);
                    return result;
                }

                auto desc = descriptor_from_renderable_asset(
                    *rdata, node.visible);

                if (context.resource_resolver) {
                    if (!context.resource_resolver->realize_renderable_descriptor(
                            *rdata, desc))
                    {
                        result.error = SceneInstantiateError::RenderableRealizeFailed;
                        result.error_detail = node_log_name(node)
                            + " renderable descriptor could not be realized";
                        log_instantiate_failure(result, context);
                        return result;
                    }
                }
                else if (rdata->kind != RenderableKind::Mesh) {
                    result.error = SceneInstantiateError::RenderableRealizeFailed;
                    result.error_detail = node_log_name(node)
                        + " has non-mesh renderable kind but no resource resolver";
                    log_instantiate_failure(result, context);
                    return result;
                }

                inst.renderables[h] = desc;
            }
            else if (node.renderable) {
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
        std::unordered_set<std::string> behavior_binding_ids;
        for (const auto& node : scene.nodes) {
            NodeHandle h = id_to_handle[node.id];

            if (node.camera) {
                ++inst.cameras;
            }

            if (node.hdri_environment) {
                ++inst.hdri_environments;
            }

            if (node.input_receiver) {
                inst.input_receivers.push_back({
                    .node = h,
                    .component = InputReceiverComponent{
                        .input_map = node.input_receiver->input_map,
                        .log_input = node.input_receiver->log_input,
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

            if (node.actor_movement_controller) {
                const auto& ac = *node.actor_movement_controller;
                inst.actor_movement_controllers.push_back({
                    .node = h,
                    .component = ActorMovementControllerComponent{
                        .move_speed = ac.move_speed,
                        .boost_multiplier = ac.boost_multiplier,
                        .movement_space = ac.movement_space,
                    },
                });
            }

            if (node.ground_boundary) {
                const auto& gb = *node.ground_boundary;
                GroundBoundaryComponent component{};
                component.min[0] = gb.min[0];
                component.min[1] = gb.min[1];
                component.min[2] = gb.min[2];
                component.max[0] = gb.max[0];
                component.max[1] = gb.max[1];
                component.max[2] = gb.max[2];
                component.constrain_vertical = gb.constrain_vertical;
                component.enabled = gb.enabled;

                inst.ground_boundaries.push_back({
                    .node = h,
                    .component = component,
                });
            }

            if (node.collision) {
                const auto& collision = *node.collision;
                inst.collisions.push_back({
                    .node = h,
                    .component = CollisionComponent{
                        .collision_asset = collision.collision_asset,
                        .layer_mask = collision.layer_mask,
                        .collides_with_mask = collision.collides_with_mask,
                        .is_trigger = collision.is_trigger,
                        .enabled = collision.enabled,
                    },
                });
            }

            if (node.terrain) {
                const auto& terrain = *node.terrain;
                inst.terrains.push_back({
                    .node = h,
                    .component = TerrainComponent{
                        .terrain_asset = terrain.terrain_asset,
                        .visual_proxy_asset = terrain.visual_proxy_asset,
                        .constraint_surface_asset =
                            terrain.constraint_surface_asset,
                        .visible = terrain.visible,
                        .queryable = terrain.queryable,
                        .constrain_movement = terrain.constrain_movement,
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
                const auto compiled =
                    wz::engine::behavior::compile_channel_mask(
                        node.event_listener->channels);
                log_event_listener_channel_warnings(
                    node,
                    context,
                    compiled);
                inst.event_listeners.push_back({
                    .node = h,
                    .component = EventListenerComponent{
                        .channels = node.event_listener->channels,
                        .channel_mask = compiled.mask,
                    },
                });
            }

            if (node.proximity) {
                const auto& proximity = *node.proximity;
                inst.proximities.push_back({
                    .node = h,
                    .component = ProximityComponent{
                        .radius = proximity.radius,
                        .layer_mask = proximity.layer_mask,
                        .detects_with_mask = proximity.detects_with_mask,
                        .enabled = proximity.enabled,
                    },
                });
            }

            if (node.motion) {
                const auto& motion = *node.motion;
                MotionComponent component{};
                component.linear_velocity[0] = motion.linear_velocity[0];
                component.linear_velocity[1] = motion.linear_velocity[1];
                component.linear_velocity[2] = motion.linear_velocity[2];
                component.angular_velocity[0] = motion.angular_velocity[0];
                component.angular_velocity[1] = motion.angular_velocity[1];
                component.angular_velocity[2] = motion.angular_velocity[2];
                component.space = motion.space;
                component.terrain_constrained = motion.terrain_constrained;
                component.terrain_ride_height = motion.terrain_ride_height;
                component.terrain_footprint_radius =
                    motion.terrain_footprint_radius;
                component.terrain_align_to_surface =
                    motion.terrain_align_to_surface;
                component.terrain_alignment_strength =
                    motion.terrain_alignment_strength;
                component.enabled = motion.enabled;
                inst.motions.push_back({
                    .node = h,
                    .component = component,
                });
            }

            if (node.behavior) {
                const std::string binding_id =
                    effective_behavior_binding_id(
                        node,
                        *node.behavior,
                        0u);
                if (!behavior_binding_ids.insert(binding_id).second) {
                    result.error =
                        SceneInstantiateError::DuplicateBehaviorBindingId;
                    result.error_detail =
                        node_log_name(node)
                        + " has duplicate behavior binding id='"
                        + binding_id + "'";
                    log_instantiate_failure(result, context);
                    return result;
                }
                inst.behaviors.push_back({
                    .node = h,
                    .component = instantiate_behavior_component(
                        node,
                        context,
                        *node.behavior,
                        binding_id),
                });
            }
            uint32_t behavior_ordinal = node.behavior ? 1u : 0u;
            for (const auto& behavior : node.behaviors) {
                const std::string binding_id =
                    effective_behavior_binding_id(
                        node,
                        behavior,
                        behavior_ordinal);
                ++behavior_ordinal;
                if (!behavior_binding_ids.insert(binding_id).second) {
                    result.error =
                        SceneInstantiateError::DuplicateBehaviorBindingId;
                    result.error_detail =
                        node_log_name(node)
                        + " has duplicate behavior binding id='"
                        + binding_id + "'";
                    log_instantiate_failure(result, context);
                    return result;
                }
                inst.behaviors.push_back({
                    .node = h,
                    .component = instantiate_behavior_component(
                        node,
                        context,
                        behavior,
                        binding_id),
                });
            }

            if (node.compute_kernel) {
                ++inst.compute_kernels;
            }

            if (node.render_shader) {
                ++inst.render_shaders;
            }

            if (node.mesh_render_style
                && node.mesh_render_style->field_visualization_enabled
                && !(node.mesh_render_style->field_visualization_asset
                    == wz::asset::AssetKey{}))
            {
                inst.mesh_field_visualization_targets.push_back({
                    .node = h,
                    .component = MeshFieldVisualizationTargetComponent{
                        .field_asset =
                            node.mesh_render_style->field_visualization_asset,
                        .channel_id =
                            node.mesh_render_style
                                ->field_visualization_channel_id,
                    },
                });
            }

            if (node.debug_visual) {
                inst.auxiliary_visuals.push_back({
                    .node = h,
                    .component = AuxiliaryVisualComponent{
                        .kind    = node.debug_visual->kind,
                        .scale   = node.debug_visual->scale,
                        .visible = node.debug_visual->visible,
                    },
                });
            }

            if (node.editor_handle) {
                inst.editor_handles.push_back({
                    .node = h,
                    .component = EditorHandleComponent{
                        .kind    = node.editor_handle->kind,
                        .enabled = node.editor_handle->enabled,
                        .visible = node.editor_handle->visible,
                        .size    = node.editor_handle->size,
                    },
                });
            }
        }

        // Build light records
        for (const auto& light : scene.lights) {
            inst.lights.push_back(light.light);
        }

        inst.sky_draws = scene.sky_draws;

        // Populate default_view from the active camera node, if specified.
        if (scene.defaults.active_camera_node) {
            const auto& cam_id = *scene.defaults.active_camera_node;
            auto cam_it = id_to_handle.find(cam_id);
            if (cam_it != id_to_handle.end()) {
                // Find the camera intrinsics on this node.
                const auto cam_node_it = std::find_if(
                    scene.nodes.begin(), scene.nodes.end(),
                    [&](const auto& node) {
                        return node.id == cam_id && node.camera;
                    });
                const SceneCameraAsset* cam_asset =
                    cam_node_it != scene.nodes.end()
                        ? &*cam_node_it->camera
                        : nullptr;

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
        log_instantiate_success(scene, inst, context);
        return result;
    }

} // namespace wz::engine::assets
