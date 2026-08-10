#pragma once

// engine/app/scene_document.h
//
// SceneDocument — the authored, serialized scene document (#258 avenue 2, the
// #183 "editing model into the engine" extraction). It owns the node array and
// the edit-time bookkeeping that the #221 ownership contract names the single
// source of truth for the scene's NON-transform authoring data (the single
// writer is the edit verbs). Transform + hierarchy live truth is NOT here — it
// belongs to the simulation polytree; reads go through the host's
// scene_world_transforms / node_world_transform, and the one transform edit seam
// is the host's polytree write.
//
// This is the first extraction stage: the document owns the STORAGE, exposed to
// the host (WozzitsApp_v1) that still holds the edit verbs + the reactions.
// Later stages move the pure mutators (each returning a SceneChange) and the pure
// readers onto it, leaving the host as the composition root that maps each
// SceneChange to its reaction (apply_scene_change) — reactions touch the behavior
// runtime / renderer / bound graph and stay with the host.

#include <engine/app/scene_change.h>                 // SceneChange, SceneEdit
#include <engine/assets/scene/scene_asset_data.h>    // SceneNodeAsset
#include <asset/draft.h>                             // AssetGraphDraftNodeId
#include <scene/scene_ecs.h>                          // AuthoredEntityId

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wz::app
{
    class SceneDocument
    {
    public:
        // The authored node array — the serialized document. Mutable access for
        // the edit verbs and the reactions that re-bridge keys / graft children.
        std::vector<wz::engine::assets::SceneNodeAsset>& nodes() { return nodes_; }
        const std::vector<wz::engine::assets::SceneNodeAsset>& nodes() const
        {
            return nodes_;
        }

        // Ids of nodes currently grafted from a scene_source reference (#213
        // instance mode): runtime-only children appended to nodes(), tracked so a
        // re-graft drops the previous graft and save excludes them.
        std::vector<wz::scene::AuthoredEntityId>& grafted_ids() { return grafted_ids_; }
        const std::vector<wz::scene::AuthoredEntityId>& grafted_ids() const
        {
            return grafted_ids_;
        }

        // Unsaved-edits flag: every mutating verb sets it, save clears it. Exposed
        // by reference so the existing `dirty() = dirty() || ok` assignments keep
        // working; a later stage narrows this to mark/clear.
        bool& dirty() { return dirty_; }
        bool  dirty() const { return dirty_; }

        // --- pure-document queries (read nodes()/grafted_ids() only) ------------
        // The read side of the editing model: any host or tool holding a document
        // can query it directly. WozzitsApp_v1's node_* accessors delegate here.
        // Transform-derived reads (node_local_translation / world transforms) are
        // NOT here — they cross into the simulation polytree and stay with the host.
        bool node_has_component(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& kind) const;
        std::size_t child_node_count(
            const wz::scene::AuthoredEntityId& parent_id) const;
        std::optional<wz::asset::AssetGraphDraftNodeId> node_renderable_asset_node_id(
            const wz::scene::AuthoredEntityId& node_id) const;
        std::optional<wz::asset::AssetGraphDraftNodeId> node_scene_source_node_id(
            const wz::scene::AuthoredEntityId& node_id) const;
        bool node_has_glb_scene_source(
            const wz::scene::AuthoredEntityId& node_id) const;
        const wz::engine::assets::SceneGLBSceneSource* node_glb_scene_source(
            const wz::scene::AuthoredEntityId& node_id) const;
        const wz::engine::assets::SceneCollisionAsset* node_collision(
            const wz::scene::AuthoredEntityId& node_id) const;
        const wz::engine::assets::SceneMotionAsset* node_motion(
            const wz::scene::AuthoredEntityId& node_id) const;
        // A snapshot copy of the grafted (scene-source instance) children.
        std::vector<wz::engine::assets::SceneNodeAsset> grafted_nodes() const;

        // --- structural mutators (mutate nodes()/dirty(), return a SceneEdit) ---
        // Each performs the pure document mutation and returns the mutation result
        // plus the SceneChange the host dispatches. No runtime/renderer contact —
        // the reaction (rebuild etc.) is the host's, via apply_scene_change.
        SceneEdit<wz::engine::assets::SceneAddChildResult> add_child(
            const wz::scene::AuthoredEntityId& parent_id);
        SceneEdit<bool> set_properties(
            const wz::scene::AuthoredEntityId& id,
            std::string name,
            bool visible);
        SceneEdit<bool> reparent(
            const wz::scene::AuthoredEntityId& id,
            const wz::scene::AuthoredEntityId& new_parent_id);
        SceneEdit<bool> remove(const wz::scene::AuthoredEntityId& id);
        SceneEdit<bool> reorder(
            const wz::scene::AuthoredEntityId& id,
            const wz::scene::AuthoredEntityId& before_id);
        SceneEdit<bool> set_render_order(
            const wz::scene::AuthoredEntityId& id,
            int render_order);

        // --- behavior-binding mutators (RuntimeRebuild on success) -------------
        SceneEdit<wz::engine::assets::SceneAddBehaviorResult> add_behavior(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& module);
        SceneEdit<bool> remove_behavior(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& binding_id);
        SceneEdit<bool> set_behavior_enabled(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& binding_id,
            bool enabled);
        SceneEdit<bool> set_behavior_fields(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& binding_id,
            const std::string& label,
            const std::string& module);
        SceneEdit<bool> set_behavior_events(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& binding_id,
            const std::vector<std::string>& events);
        SceneEdit<bool> set_behavior_config(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& binding_id,
            const wz::engine::assets::SceneBehaviorConfigValue& value);
        SceneEdit<bool> clear_behavior_config(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& binding_id,
            const std::string& key);

        // --- optional-component mutators (None: no runtime reaction) -----------
        // Return { ok, None }; the host verb keeps its own no-op warning log (the
        // document has no logger) and dispatches the None change (a no-op).
        SceneEdit<bool> add_component(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& kind);
        SceneEdit<bool> remove_component(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& kind);
        SceneEdit<bool> set_renderable_asset(
            const wz::scene::AuthoredEntityId& node_id,
            wz::asset::AssetGraphDraftNodeId asset_graph_node_id);
        SceneEdit<bool> set_audio_renderable(
            const wz::scene::AuthoredEntityId& node_id,
            wz::asset::AssetGraphDraftNodeId asset_graph_node_id);
        SceneEdit<bool> set_audio_source_play(
            const wz::scene::AuthoredEntityId& node_id,
            bool auto_play,
            bool enabled);

        // --- renderable / collision / scene-source mutators --------------------
        // Reaction-bearing edits (RenderBinding / Collision / SceneSource) whose
        // MUTATION is pure document. The reaction stays with the host. Each returns
        // { ok, change }; ok=false (node missing) keeps the host's warning log.
        // (For the RenderBinding/SceneSource kinds the #252 remat-caller log now
        // names the SceneDocument mutator rather than the host verb — immaterial.)
        SceneEdit<bool> set_geometry_asset(
            const wz::scene::AuthoredEntityId& node_id,
            wz::asset::AssetGraphDraftNodeId asset_graph_node_id);
        SceneEdit<bool> set_collision_asset(
            const wz::scene::AuthoredEntityId& node_id,
            wz::asset::AssetGraphDraftNodeId asset_graph_node_id,
            bool constrain_movement);
        SceneEdit<bool> set_scene_source(
            const wz::scene::AuthoredEntityId& node_id,
            wz::asset::AssetGraphDraftNodeId asset_graph_node_id);

        // --- renderable-recipe mutators (RenderBinding reaction) ---------------
        // Author the render-program / semantic-binding / per-instance-constant /
        // render-to-texture halves of a node's renderable recipe. Pure document
        // writes; the host re-assembles the render bindings. set_render_program's
        // grafted-child override mirror stays on the host (it consults grafted
        // state); the two multi-guard verbs (binding/constant) keep their
        // empty-arg warning on the host and assume a non-empty semantic/name here.
        SceneEdit<bool> set_render_program(
            const wz::scene::AuthoredEntityId& node_id,
            wz::asset::AssetGraphDraftNodeId asset_graph_node_id);
        SceneEdit<bool> set_renderable_binding(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& semantic,
            wz::asset::AssetGraphDraftNodeId asset_graph_node_id);
        // Returns RenderBindingNode only on the custom-form flip (first/last
        // override on a geometry node with no bindings); a plain override is None.
        SceneEdit<bool> set_renderable_constant(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& name,
            const float* value);
        SceneEdit<bool> set_render_to_texture(
            const wz::scene::AuthoredEntityId& node_id,
            const wz::engine::assets::SceneRenderToTextureAsset& render_to_texture);

        // --- render/sim component mutators -------------------------------------
        // camera / atmosphere / environment re-materialize the runtime
        // UNCONDITIONALLY (RuntimeRebuild). The two motion mutators tweak fields on
        // an EXISTING component in place (MotionFieldTweak / MotionFilterTweak, so
        // the host patches the live record) but ADDING one is RuntimeRebuild.
        SceneEdit<bool> set_camera(
            const wz::scene::AuthoredEntityId& node_id,
            const wz::engine::assets::SceneCameraAsset& camera);
        SceneEdit<bool> set_atmosphere(
            const wz::scene::AuthoredEntityId& node_id,
            const wz::engine::assets::SceneAtmosphereAsset& atmosphere);
        SceneEdit<bool> set_environment(
            const wz::scene::AuthoredEntityId& node_id,
            const wz::engine::assets::SceneEnvironmentAsset& environment);
        SceneEdit<bool> set_motion_terrain_fields(
            const wz::scene::AuthoredEntityId& node_id,
            bool terrain_constrained,
            float ride_height,
            float footprint_radius,
            bool align_to_surface,
            float alignment_strength);
        SceneEdit<bool> set_motion_filter(
            const wz::scene::AuthoredEntityId& node_id,
            const wz::engine::assets::SceneMotionFilterAsset& filter);

        // --- GLB scene-source / per-mesh style mutators (GlbSource reaction) ----
        // A cleared clear_glb_mesh_style (no such override) is a no-op-success:
        // result=true, change=None. A missing node / no glb_scene_source is
        // result=false (the host logs its warning).
        SceneEdit<bool> set_glb_scene_source(
            const wz::scene::AuthoredEntityId& node_id,
            const wz::engine::assets::SceneGLBSceneSource& descriptor);
        SceneEdit<bool> set_glb_base_style(
            const wz::scene::AuthoredEntityId& node_id,
            const wz::engine::assets::MeshRenderStyleData& style);
        SceneEdit<bool> set_glb_mesh_style(
            const wz::scene::AuthoredEntityId& node_id,
            uint32_t mesh_index,
            const wz::engine::assets::MeshRenderStyleData& style);
        SceneEdit<bool> clear_glb_mesh_style(
            const wz::scene::AuthoredEntityId& node_id,
            uint32_t mesh_index);

    private:
        std::vector<wz::engine::assets::SceneNodeAsset> nodes_{};
        std::vector<wz::scene::AuthoredEntityId>        grafted_ids_{};
        bool                                            dirty_ = false;
    };
}
