// src/engine/app/wozzits_app_v1_behavior.cpp
//
// WozzitsApp_v1 -- the behavior runtime: the per-frame dispatch loop and its
// named phases (#256 seam B), the single command applier (#256 seam A), the
// runtime SceneInstance materialization (rebuild_behavior_scene), behavior-module
// loading, and the scene-camera-via-behavior selection. Split out of
// wozzits_app_v1.cpp as a partial-class TU (#256 seam D): the class is declared
// once in engine/app/wozzits_app_v1.h; these are just its behavior-loop method
// definitions, co-located. The renderable-param + effective-active file-local
// helpers move with them (used only here). rebuild_behavior_scene still does the
// full O(scene) reset+renumber; making it incremental is the behavior track #257.

#include <engine/app/wozzits_app_v1.h>

#include <engine/assets/scene/asset_graph_json.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene/scene_authoring_materialize.h>
#include <engine/assets/scene/scene_compilers.h>
#include <engine/assets/scene/scene_instance.h>
#include <engine/assets/scene/scene_json_export.h>
#include <engine/assets/scene/prefab_instantiate.h>
#include <engine/assets/scene/scene_subtree_export.h>
#include <engine/assets/scene_asset_module.h>
#include <engine/assets/render_program/render_program.h>
#include <engine/assets/renderable/render_binding_sources.h>
#include <engine/assets/renderable_asset_module.h>
#include <engine/assets/type_extensions.h>
#include <engine/audio/scene_audio.h>

#include <engine/behavior/behavior_command_apply.h>
#include <engine/behavior/behavior_dispatch.h>
#include <engine/behavior/builtin_behaviors.h>
#include <engine/behavior/quantum_agent_behaviors.h>
#include <engine/collision/collision_frame.h>
#include <engine/input_events.h>

#include <asset/system.h>

#include <bench/flying_camera.h>
#include <external/json/json_parser.h>
#include <external/json/json_writer.h>
#include <file/filesystem.h>
#include <input/input.h>
#include <math/math_types.h>
#include <math/mat4.h>
#include <math/projection.h>
#include <scene/scene_graph.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wz::app
{
    namespace
    {
        // FNV-1a/32 over a renderable-constant name. Matches
        // wz_renderable_param_hash (behavior-side) bit-for-bit so a
        // SET_RENDERABLE_PARAM command's hash resolves to the declared field of
        // the same name (#232). Same algorithm as prefab_name_hash — kept a
        // distinct function for a semantically distinct call site.
        uint32_t renderable_param_name_hash(std::string_view name) noexcept
        {
            uint32_t h = 2166136261u;
            for (const unsigned char c : name) {
                h ^= static_cast<uint32_t>(c);
                h *= 16777619u;
            }
            return h;
        }

        // Effective (inherited) "active" per authored node id (#252 live axis): a
        // node is live only if it AND every ancestor is `active`, so parking a node
        // parks its whole subtree (a spawn host parks its grafted children). Mirrors
        // the renderer's effective-visibility parent-walk (rhi_scene_renderer.cpp),
        // keyed on `active` instead of `visible`. Cycle/dangling-parent safe: an
        // unresolved parent falls back to the node's own `active` (never spuriously
        // parked). Returns id -> 1(live)/0(parked) so the caller projects it onto
        // runtime entities via runtime_to_authored.
        std::unordered_map<std::string, std::uint8_t>
        compute_scene_node_effective_active(
            const std::vector<wz::engine::assets::SceneNodeAsset>& nodes)
        {
            const std::size_t n = nodes.size();

            std::unordered_map<std::string, std::size_t> index_by_id;
            index_by_id.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                index_by_id.emplace(nodes[i].id, i);
            }

            std::vector<std::size_t> parent(n, n);
            for (std::size_t i = 0; i < n; ++i) {
                if (!nodes[i].parent_id.has_value()) {
                    continue;
                }
                const auto it = index_by_id.find(*nodes[i].parent_id);
                if (it != index_by_id.end() && it->second != i) {
                    parent[i] = it->second;
                }
            }

            // effective = node.active AND parent.effective, resolved parents-first
            // (cycle-safe chain unwind, identical to the visibility helper).
            std::vector<std::uint8_t> effective(n, 1u);
            std::vector<std::uint8_t> state(n, 0u);
            std::vector<std::size_t> chain;
            chain.reserve(n);
            for (std::size_t start = 0; start < n; ++start) {
                if (state[start] != 0u) {
                    continue;
                }
                chain.clear();
                std::size_t cur = start;
                while (cur != n && state[cur] == 0u) {
                    state[cur] = 1u;
                    chain.push_back(cur);
                    cur = parent[cur];
                }
                for (std::size_t k = chain.size(); k-- > 0;) {
                    const std::size_t i = chain[k];
                    const std::size_t p = parent[i];
                    const bool parent_active = (p == n || state[p] != 2u)
                        ? true
                        : effective[p] != 0u;
                    effective[i] = (nodes[i].active && parent_active) ? 1u : 0u;
                    state[i] = 2u;
                }
            }

            std::unordered_map<std::string, std::uint8_t> by_id;
            by_id.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                by_id.emplace(nodes[i].id, effective[i]);
            }
            return by_id;
        }
    }

    void WozzitsApp_v1::load_behavior_modules(
        const wz::fs::Path& module_folder)
    {
        if (module_folder.empty()) {
            return;
        }

        // Resolve relative to the asset resource root, matching how the rest of
        // load_scene resolves authored paths through the file system.
        const wz::fs::Path resolved =
            ctx_.assets
                ? ctx_.assets->files().resolve_path(module_folder)
                : module_folder;

        const uint32_t loaded =
            plugins_.load_dynamic_modules_from_directory(
                registry_,
                std::filesystem::path{ resolved },
                &ctx_.logger);
        ctx_.logger.info(
            "load_scene: loaded " + std::to_string(loaded)
            + " behavior module DLL(s) from " + resolved);
    }

    void WozzitsApp_v1::rebuild_behavior_scene(bool append_only)
    {
        ++rebuild_scene_count_this_frame_;
        // DIAGNOSTIC (#219): a rebuild while the scene camera is the active source
        // renumbers polytree handles; refresh_active_camera_entity re-seats the
        // handle below, but logging the event tells us whether an unexpected
        // per-frame rebuild (not pure movement) is what correlates with the flip.
        if (view_.scene_source_active()) {
            ctx_.logger.warn(
                "rebuild_behavior_scene while scene camera active (anchor='"
                + view_.active_scene_camera_id() + "')");
        }

        // Capture the outgoing instance's behavior state BEFORE the reset so a
        // rebuild (structural edit / prefab spawn) preserves every pre-existing
        // binding's BehaviorStateBlock: instance state is keyed by binding id, and
        // a binding id is re-derived from the node id (effective_behavior_binding_id),
        // so a binding present in both the old and rebuilt scenes keeps the SAME
        // key. Moving the maps into the rebuilt instance before initialize_behaviors
        // means on_init's wz_get_instance_state finds the preserved block and does
        // NOT re-construct it (state survives), while a newly added binding has no
        // block yet and initializes fresh. Carried for both instance and shared
        // state. Default-empty when there was no prior instance.
        wz::engine::assets::BehaviorStateStorage preserved_state;
        // Behavior-set runtime Motion overrides do NOT live on the authored asset,
        // so reinstantiating below would wipe them. Capture the terrain-alignment
        // rate keyed by STABLE authored node id (runtime ids are renumbered on
        // rebuild) and restore it after, so a surviving actor keeps the rate a
        // self.start handler set -- without that, a spawn would reset every existing
        // actor to instant alignment. Keyed by authored id, mirroring how
        // behavior_state survives by binding id.
        std::unordered_map<std::string, float> preserved_alignment_rate;
        // #221: sim-accumulated LOCAL transforms don't live on the authored asset
        // either — document_.nodes() stays at its authored pose now that the per-frame
        // write-back is gone. Rebuilding materializes the polytree from
        // document_.nodes(), which would snap every actor back to its authored start
        // (losing the motion a surviving binding accrued). Capture each live
        // node's LOCAL matrix keyed by STABLE authored id and restore it after
        // materialization, so a binding present in both the old and rebuilt scenes
        // continues from where it was — the same "state survives rebuild" contract
        // behavior_state and alignment_rate keep. A newly spawned/added node has no
        // entry and materializes at its authored transform.
        std::unordered_map<std::string, wz::math::Mat4> preserved_local;
        if (behavior_scene_) {
            preserved_state = std::move(behavior_scene_->behavior_state);
            const std::size_t live_node_count =
                wz::core::graph::node_count(behavior_scene_->storage.polytree);
            for (std::size_t entity = 0;
                 entity < behavior_scene_->runtime_to_authored.size()
                     && entity < live_node_count;
                 ++entity)
            {
                preserved_local[behavior_scene_->runtime_to_authored[entity]] =
                    wz::core::graph::node_data(
                        behavior_scene_->storage.polytree, entity).local;
            }
            for (const auto& record : behavior_scene_->motions) {
                if (record.component.terrain_alignment_rate != 0.0f
                    && record.node
                        < behavior_scene_->runtime_to_authored.size())
                {
                    preserved_alignment_rate[
                        behavior_scene_->runtime_to_authored[record.node]] =
                        record.component.terrain_alignment_rate;
                }
            }
        }

        behavior_scene_.reset();

        // #221: materialize the runtime SceneInstance for EVERY loaded scene, not
        // just ones that carry a behavior/motion/terrain/constraint. The polytree
        // is now the single source of truth for the drawn + saved + edited world
        // transforms (see scene_world_transforms / apply_node_local_transform), so
        // even a fully static scene needs its polytree live — a transform edit
        // lands there and the renderer reads it. A static scene simply comes up
        // with empty behavior/motion/terrain tables; simulation_tick's sub-passes
        // iterate those tables, so they are cheap no-ops (nothing to integrate or
        // constrain). behavior_scene_-null tolerance survives in every consumer as
        // a DEFENSIVE fallback for the one path that still leaves it null: a
        // FAILED instantiate_scene (a malformed scene), below.

        // Materialize a runtime SceneInstance from the authored scene. The
        // renderable resolver is intentionally null: WozzitsApp_v1 renders from
        // document_.nodes(), so the instance is used only for its polytree + behavior
        // tables (renderables in the instance are unused). Because instantiate_
        // scene FAILS a node that carries a renderable_asset when no resolver is
        // provided, strip the renderable references from the instance's node
        // copies first — otherwise a renderable-carrying node (notably the
        // GLB-grafted scene-source children, #213) would fail materialization and
        // the whole behavior runtime (incl. those children's addressability)
        // would not come up. The strip is on the COPY only; document_.nodes() (the
        // renderer's source of truth) keeps its renderables.
        wz::engine::assets::SceneAssetData scene_data;
        scene_data.nodes = document_.nodes();
        for (wz::engine::assets::SceneNodeAsset& node : scene_data.nodes) {
            node.renderable.reset();
            node.renderable_asset.reset();
            node.renderable_asset_node_id.reset();
        }

        wz::engine::assets::SceneInstantiateContext instantiate_ctx{
            .logger = &ctx_.logger,
            .log_owner = "WozzitsApp_v1",
        };
        wz::engine::assets::SceneInstantiateResult instantiated =
            wz::engine::assets::instantiate_scene(scene_data, instantiate_ctx);
        if (!instantiated.ok()) {
            ctx_.logger.error(
                "behavior scene instantiate failed: "
                + instantiated.error_detail);
            refresh_active_camera_entity();
            return;
        }

        behavior_scene_ = std::move(instantiated.instance);

        // Carry the preserved state into the rebuilt instance BEFORE init, so a
        // binding that survives the rebuild keeps its block (and skips re-
        // construction); a new binding has no block and initializes fresh.
        behavior_scene_->behavior_state = std::move(preserved_state);

        // Restore behavior-set runtime alignment rates onto the rebuilt motions
        // (matched by authored node id). A motion not in the map keeps its
        // default 0; a newly spawned actor gets its rate from the self.start pass
        // below instead.
        if (!preserved_alignment_rate.empty()) {
            for (auto& record : behavior_scene_->motions) {
                if (record.node
                    >= behavior_scene_->runtime_to_authored.size())
                {
                    continue;
                }
                const auto it = preserved_alignment_rate.find(
                    behavior_scene_->runtime_to_authored[record.node]);
                if (it != preserved_alignment_rate.end()) {
                    record.component.terrain_alignment_rate = it->second;
                }
            }
        }

        // #221: restore the captured sim-accumulated LOCAL transforms onto the
        // rebuilt polytree (matched by authored id), then re-propagate so the
        // world matrices reflect them. A node not in the map (newly added/spawned)
        // keeps the authored local it just materialized with. This preserves the
        // pre-#221 semantic that a surviving actor continues from its accrued pose
        // across a rebuild — the write-back used to achieve it by keeping
        // document_.nodes() sim-current; now the transform is carried directly.
        if (!preserved_local.empty()) {
            const std::size_t live_node_count =
                wz::core::graph::node_count(behavior_scene_->storage.polytree);
            for (std::size_t entity = 0;
                 entity < behavior_scene_->runtime_to_authored.size()
                     && entity < live_node_count;
                 ++entity)
            {
                const auto it = preserved_local.find(
                    behavior_scene_->runtime_to_authored[entity]);
                if (it != preserved_local.end()) {
                    const_cast<wz::scene::TransformNode&>(
                        wz::core::graph::node_data(
                            behavior_scene_->storage.polytree, entity))
                        .local = it->second;
                }
            }
            wz::scene::propagate_all(behavior_scene_->storage.polytree);
        }

        // Initialize behaviors (init callbacks + per-binding/shared state) once
        // for the materialized scene, right after it is built. On an APPEND-ONLY
        // rebuild (a prefab spawn) skip survivors' on_init
        // (#257 B1): their runtime ids + cached handles are stable across a pure
        // append, and their state is preserved above, so only the newly materialized
        // bindings need to init -- re-running every survivor's scene-walking on_init
        // was the dominant per-spawn cost.
        wz::engine::behavior::initialize_behaviors(
            *behavior_scene_, registry_, &ctx_.logger,
            /*skip_initialized=*/append_only);

        // Prune carried-over instance-state blocks whose binding id is no longer
        // present among the rebuilt scene's bindings (a removed/renamed node, or a
        // node never re-added). Without this, those blocks would leak across every
        // rebuild. Shared state is keyed independently of nodes (it is created on
        // demand by key), so it is NOT pruned here — it is carried as-is.
        {
            std::unordered_set<std::string> live_binding_ids;
            live_binding_ids.reserve(behavior_scene_->behaviors.size());
            for (const auto& record : behavior_scene_->behaviors) {
                live_binding_ids.insert(record.component.binding_id);
            }
            auto& instance_state =
                behavior_scene_->behavior_state.instance_state;
            for (auto it = instance_state.begin();
                 it != instance_state.end();)
            {
                if (live_binding_ids.find(it->first)
                    == live_binding_ids.end())
                {
                    it = instance_state.erase(it);
                } else {
                    ++it;
                }
            }

            // Same hygiene for the cognition wake schedule: drop wakes for
            // bindings that no longer exist so they do not leak across rebuilds.
            auto& next_wakes = behavior_scene_->behavior_state.next_wakes;
            for (auto it = next_wakes.begin(); it != next_wakes.end();) {
                if (live_binding_ids.find(it->first)
                    == live_binding_ids.end())
                {
                    it = next_wakes.erase(it);
                } else {
                    ++it;
                }
            }

            // And the #257 B1 init-scoping set: drop the initialized flag for a
            // binding that is gone, so if its id is ever reused it re-inits fresh.
            auto& initialized =
                behavior_scene_->behavior_state.initialized_bindings;
            for (auto it = initialized.begin(); it != initialized.end();) {
                if (live_binding_ids.find(*it) == live_binding_ids.end()) {
                    it = initialized.erase(it);
                } else {
                    ++it;
                }
            }

            // And the self.start set (dispatch_self_start skips any binding already in
            // it). Same reuse hazard, worse consequence: a re-added node with a REUSED
            // binding id (a live edit / undo) would never re-fire self.start, so its
            // quantum_agent would never (re)create its agent -- handle stays 0, every
            // read "deliberating", every write a silent no-op: a permanently brain-dead
            // NPC -- and the set would leak one string per despawn. Prune it like the
            // initialized set so a reused id starts fresh.
            auto& started = behavior_scene_->behavior_state.started_bindings;
            for (auto it = started.begin(); it != started.end();) {
                if (live_binding_ids.find(*it) == live_binding_ids.end()) {
                    it = started.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Release cognition wave functions whose quantum_agent binding vanished
        // this rebuild (a despawned NPC, a removed node, a scene swap). The store
        // outlives the scene and keys agents by an opaque handle the binding's POD
        // state carries, so sweep it against the surviving quantum_agent handles.
        {
            std::vector<wz::engine::cognition::AgentHandle> live_agents;
            for (const auto& record : behavior_scene_->behaviors) {
                if (record.component.module
                    != wz::engine::behavior::kQuantumAgentModule)
                {
                    continue;
                }
                const auto* block =
                    behavior_scene_->behavior_state.find_instance_state(
                        record.component.binding_id);
                if (block && block->data) {
                    const auto* state = static_cast<
                        const wz::engine::behavior::QuantumAgentState*>(
                            block->data);
                    if (state->handle != 0u) {
                        live_agents.push_back(state->handle);
                    }
                }
            }
            wz::engine::behavior::quantum_agent_store().retain(live_agents);
        }

        ctx_.logger.info(
            "behavior scene initialized (bindings="
            + std::to_string(behavior_scene_->behaviors.size()) + ")");

        // Cache whether any behavior subscribes to self.activated, so the per-frame
        // rising-edge scan in dispatch_scene_behaviors is skipped entirely when
        // nothing listens (#252 pooling -- zero cost until a pool manager wires it).
        has_self_activated_subscriber_ =
            wz::engine::behavior::scene_has_event_subscriber(
                *behavior_scene_, registry_, WZ_EVENT_SELF_ACTIVATED);

        // One-shot self.start lifecycle pass: fire WZ_EVENT_SELF_START for every
        // binding that has not started yet (tracked in behavior_state, preserved
        // across rebuilds) and apply the commands it produces. On initial load
        // this fires for all subscribed authored bindings; on a prefab spawn or a
        // live edit it fires ONLY for the newly materialized bindings -- existing
        // actors keep their started flag (and their preserved runtime overrides),
        // so nothing re-runs per spawn. A single integration point covers load,
        // spawn, and edit because they all funnel through rebuild_behavior_scene.
        {
            wz::engine::behavior::BehaviorCommandBuffer self_start_commands;
            wz::engine::behavior::BehaviorFrameContext self_start_ctx{
                .scene = &*behavior_scene_,
                .behavior_state = &behavior_scene_->behavior_state,
                .commands = &self_start_commands,
                // A prewarm behavior submits its pool on self.start (#252 pooling);
                // wire the sink so those submits are captured (drained next frame).
                .spawn_requests = &spawn_identity_buffer_,
                .logger = &ctx_.logger,
            };
            wz::engine::behavior::dispatch_self_start(
                *behavior_scene_, registry_, self_start_ctx);
            if (!self_start_commands.commands.empty()) {
                std::vector<wz::scene::RuntimeEntityId> self_start_changed;
                (void)wz::engine::behavior::apply_behavior_commands(
                    *behavior_scene_,
                    self_start_commands.commands,
                    &self_start_changed);
            }
        }

        // Re-point the active camera handle at the rebuilt scene's entities, so a
        // selected scene camera survives a rebuild (e.g. a behavior spawning a
        // child) without falling back to free-fly.
        refresh_active_camera_entity();
    }

    void WozzitsApp_v1::select_scene_loaded_active_camera()
    {
        // One-shot WZ_EVENT_SCENE_LOADED pass: a scene-setup behavior (bound to
        // e.g. the scene root) may emit SET_ACTIVE_CAMERA naming a camera node.
        // Apply it here, before the first frame. The behavior only names the
        // node; the camera math lives here so the convention matches the
        // free-fly camera and the standalone driver hard-codes nothing.
        if (!behavior_scene_) {
            return;
        }

        frame_storage_.behavior_commands.clear();
        wz::engine::behavior::BehaviorFrameContext ctx{
            .frame_storage = &frame_storage_,
            .scene = &*behavior_scene_,
            .behavior_state = &behavior_scene_->behavior_state,
            .commands = &frame_storage_.behavior_commands,
            .logger = &ctx_.logger,
        };
        // Re-decide the active camera on every (re)load: drop the prior anchor
        // and fall back to the free-fly source until a scene-setup behavior
        // selects a camera (and, in play, prefer_scene_camera_ flips the source).
        view_.clear_scene_camera();
        wz::engine::behavior::dispatch_scene_loaded(
            *behavior_scene_, registry_, ctx);

        for (const wz::engine::behavior::BehaviorCommand& command :
             frame_storage_.behavior_commands.commands)
        {
            if (command.kind
                == wz::engine::behavior::BehaviorCommandKind::SetActiveCamera)
            {
                apply_scene_active_camera(command.entity);
            }
        }

        // Materialize the active view once now so the first render after a load
        // (before the first simulation_tick) draws through the selected camera.
        materialize_active_view();
    }

    void WozzitsApp_v1::apply_scene_active_camera(
        wz::scene::RuntimeEntityId runtime_entity)
    {
        if (!behavior_scene_
            || runtime_entity >= behavior_scene_->runtime_to_authored.size())
        {
            return;
        }
        const wz::scene::AuthoredEntityId& authored_id =
            behavior_scene_->runtime_to_authored[runtime_entity];

        // One-shot selection: capture the camera's projection params from its
        // authored node (a single scan, here, not per frame) and remember the
        // node by id + live polytree handle. update_active_view() then reads the
        // node's already-maintained world transform through the handle.
        const auto it = std::find_if(
            document_.nodes().begin(),
            document_.nodes().end(),
            [&](const wz::engine::assets::SceneNodeAsset& node) {
                return node.id == authored_id;
            });
        if (it == document_.nodes().end() || !it->camera) {
            ctx_.logger.warn(
                "scene_camera: node '" + authored_id
                + "' has no camera component; active camera unchanged");
            return;
        }

        // Record the anchor (id + live entity + captured projection params) in
        // view_, which flips the source to Scene when prefer_scene_camera is set
        // and logs the selection. The scene-document lookup above was the app's
        // half; the view policy is view_'s.
        view_.select_scene_camera(
            authored_id, runtime_entity, *it->camera, ctx_.logger);
    }

    void WozzitsApp_v1::refresh_active_camera_entity()
    {
        const wz::scene::AuthoredEntityId& anchor_id =
            view_.active_scene_camera_id();
        if (anchor_id.empty() || !behavior_scene_) {
            view_.set_active_camera_entity(wz::scene::INVALID_RUNTIME_ENTITY);
            return;
        }
        const auto it = behavior_scene_->authored_to_runtime.find(anchor_id);
        view_.set_active_camera_entity(
            it != behavior_scene_->authored_to_runtime.end()
                ? it->second
                : wz::scene::INVALID_RUNTIME_ENTITY);
    }

    void WozzitsApp_v1::reload_behavior_modules(
        const wz::fs::Path& module_folder)
    {
        // Drop the previously loaded modules (built-ins + project DLLs) and the
        // plugin host's dynamic libraries, then rebuild from scratch. Clearing
        // first is required: load_behavior_modules only reloads same-path DLLs,
        // so without a clear a renamed/removed module would linger, and the
        // built-ins (registered only in the ctor) must be re-registered after
        // the registry clear or the scene would lose them.
        ctx_.logger.info(
            "reload_behavior_modules: reloading behavior modules from "
            + (module_folder.empty() ? std::string("<builtins only>")
                                     : std::string(module_folder)));
        registry_.clear();
        plugins_.clear();
        wz::engine::behavior::register_builtin_behaviors(
            registry_, plugins_, ctx_.logger);
        load_behavior_modules(module_folder);
        rebuild_behavior_scene();
    }

    std::vector<std::string> WozzitsApp_v1::behavior_module_names() const
    {
        std::vector<std::string> names;
        const auto modules = registry_.modules();
        names.reserve(modules.size());
        for (const auto& registration : modules) {
            names.push_back(registration.module);
        }
        return names;
    }

    void WozzitsApp_v1::apply_all_behavior_commands(
        const std::vector<wz::engine::behavior::BehaviorCommand>& commands,
        std::vector<wz::scene::RuntimeEntityId>&                  changed_entities,
        std::vector<DeferredSpawnRequest>&                        out_spawn_requests)
    {
        // Precondition: a live behavior scene. The main-frame caller already holds
        // this (dispatch_scene_behaviors returns early otherwise); guard anyway so
        // the one applier is safe for any future dispatch site to call.
        if (!behavior_scene_) {
            return;
        }

        // Apply the produced command buffer: transform/velocity commands mutate
        // the instance polytree, then world Y etc. settle on the next propagate.
        (void)wz::engine::behavior::apply_behavior_commands(
            *behavior_scene_,
            commands,
            &changed_entities);

        // Audio behavior commands (item 9): play/stop/set-gain the addressed
        // entity's AudioSource through the realtime scheduler. Only while the
        // audio runtime is live (play mode + a device); otherwise dropped (no
        // device => no sound). apply_behavior_commands ignores these kinds —
        // they don't mutate the entity, they post to the audio thread.
        if (ctx_.assets && audio_runtime_.running()) {
            namespace ea_audio = wz::engine::audio;
            for (const wz::engine::behavior::BehaviorCommand& command :
                 commands)
            {
                // Grain-param control routes to a different scheduler path
                // (it targets a grain cloud, not a voice).
                if (command.kind == wz::engine::behavior::
                        BehaviorCommandKind::SetGrainParam) {
                    ea_audio::apply_grain_param_command(
                        *behavior_scene_, audio_runtime_.scheduler(),
                        command.entity,
                        static_cast<uint8_t>(command.values[0]),
                        command.values[1],
                        static_cast<uint32_t>(
                            command.values[2] > 0.0f ? command.values[2]
                                                     : 0.0f),
                        // values[3] = source index (GrainParam::SourceWeight).
                        static_cast<uint8_t>(
                            command.values[3] > 0.0f ? command.values[3]
                                                     : 0.0f));
                    continue;
                }

                ea_audio::AudioBehaviorVerb verb;
                switch (command.kind) {
                case wz::engine::behavior::BehaviorCommandKind::PlaySound:
                    verb = ea_audio::AudioBehaviorVerb::Play;
                    break;
                case wz::engine::behavior::BehaviorCommandKind::StopSound:
                    verb = ea_audio::AudioBehaviorVerb::Stop;
                    break;
                case wz::engine::behavior::BehaviorCommandKind::SetSoundGain:
                    verb = ea_audio::AudioBehaviorVerb::SetGain;
                    break;
                default:
                    continue;
                }
                ea_audio::apply_audio_behavior_command(
                    *ctx_.assets, *behavior_scene_,
                    audio_runtime_.scheduler(), verb, command.entity,
                    command.values[0], command.values[1]);
            }
        }

        // SET_RENDERABLE_PARAM (issue #232): write the addressed node's
        // per-instance renderable_constants override, which the next
        // frame's pack merges into the draw packet (the #229 seam). Like
        // the audio verbs this is host-handled (apply_behavior_commands
        // ignores it) and runs in the command pass — it mutates only the
        // authored SceneNodeAsset (no behavior-runtime rebuild, no
        // recompile, no re-key), so it does not renumber the runtime ids
        // the remaining commands address. `entity` (runtime) is resolved to
        // its stable authored id here while behavior_scene_ is current.
        for (const wz::engine::behavior::BehaviorCommand& command :
             commands)
        {
            if (command.kind
                != wz::engine::behavior::BehaviorCommandKind
                    ::SetRenderableParam)
            {
                continue;
            }
            if (command.entity
                >= behavior_scene_->runtime_to_authored.size())
            {
                continue;
            }
            const wz::scene::AuthoredEntityId authored_id =
                behavior_scene_->runtime_to_authored[command.entity];

            uint32_t name_hash = 0u;
            std::memcpy(
                &name_hash, &command.values[0], sizeof(uint32_t));

            // Resolve the name hash to a real field name: prefer the
            // synthesized recipe's declared constants (the full addressable
            // set), then the node's authored overrides (so a look that has
            // not compiled — e.g. no device — is still addressable by an
            // existing override).
            std::string name;
            if (ctx_.assets) {
                if (const std::optional<wz::asset::AssetKey> key =
                        node_renderable_asset(authored_id))
                {
                    if (const wz::engine::assets::RhiRenderableRecipe*
                            recipe =
                                ctx_.assets->renderables()
                                    .get_rhi_renderable_recipe(
                                        wz::engine::assets::RenderableAsset{
                                            .output = *key }))
                    {
                        for (const wz::engine::assets::RhiRenderableConstant&
                                 c : recipe->constants)
                        {
                            if (renderable_param_name_hash(c.name)
                                == name_hash)
                            {
                                name = c.name;
                                break;
                            }
                        }
                    }
                }
            }
            if (name.empty()) {
                if (const wz::engine::assets::SceneNodeAsset* node =
                        wz::engine::assets::find_scene_node(
                            document_.nodes(), authored_id))
                {
                    for (const wz::engine::assets::
                             SceneRenderableConstantOverride& c :
                         node->renderable_constants)
                    {
                        if (renderable_param_name_hash(c.name) == name_hash)
                        {
                            name = c.name;
                            break;
                        }
                    }
                }
            }
            if (name.empty()) {
                ctx_.logger.warn(
                    "behavior set_renderable_param: node '" + authored_id
                    + "' has no declared or overridden constant matching "
                      "the command's name hash (skipped)");
                continue;
            }

            // The command carries only x/y/z; preserve the field's fourth
            // component (w / alpha) from any existing override, else a
            // sensible opaque default so a first color pulse is visible.
            float w = 1.0f;
            if (const std::optional<std::array<float, 4>> existing =
                    node_renderable_constant(authored_id, name))
            {
                w = (*existing)[3];
            }
            const float value[4] = {
                command.values[1],
                command.values[2],
                command.values[3],
                w,
            };
            (void)set_node_renderable_constant(authored_id, name, value);
        }

        // SET_NODE_VISIBLE (issue #250): flip the addressed node's authored
        // `visible` flag. Render-only + host-handled (apply ignores it) — a
        // cheap field write, no behavior rebuild / render re-assemble. The
        // renderer honors visibility hierarchically, so hiding a node hides its
        // whole subtree. Same runtime->authored id resolution as above.
        for (const wz::engine::behavior::BehaviorCommand& command :
             commands)
        {
            if (command.kind
                != wz::engine::behavior::BehaviorCommandKind::SetNodeVisible)
            {
                continue;
            }
            if (command.entity
                >= behavior_scene_->runtime_to_authored.size())
            {
                continue;
            }
            const wz::scene::AuthoredEntityId authored_id =
                behavior_scene_->runtime_to_authored[command.entity];
            if (wz::engine::assets::SceneNodeAsset* node =
                    wz::engine::assets::find_scene_node(
                        document_.nodes(), authored_id))
            {
                node->visible = command.values[0] != 0.0f;
                document_.dirty() = true;
            }
        }

        // SET_NODE_ACTIVE (issue #252, the "live?" axis): flip the addressed
        // node's authored `active` flag. Host-handled (apply ignores it) — a
        // cheap field write, no behavior rebuild. The hierarchical effect gates
        // dispatch + collision via the entity_active mask rebuilt each frame in
        // dispatch_scene_behaviors (NOT render, unlike SET_NODE_VISIBLE). Same
        // runtime->authored id resolution as above.
        for (const wz::engine::behavior::BehaviorCommand& command :
             commands)
        {
            if (command.kind
                != wz::engine::behavior::BehaviorCommandKind::SetNodeActive)
            {
                continue;
            }
            if (command.entity
                >= behavior_scene_->runtime_to_authored.size())
            {
                continue;
            }
            const wz::scene::AuthoredEntityId authored_id =
                behavior_scene_->runtime_to_authored[command.entity];
            if (wz::engine::assets::SceneNodeAsset* node =
                    wz::engine::assets::find_scene_node(
                        document_.nodes(), authored_id))
            {
                node->active = command.values[0] != 0.0f;
                document_.dirty() = true;
            }
        }

        // Per-frame SET_ACTIVE_CAMERA: a behavior (e.g. spawn_player, after the
        // deferred spawn of the player prefab) can switch the runtime camera on ANY
        // frame, not just at scene load. The scene-load path already applies this
        // once; mirror it here. behavior_scene_ is still current (spawns are applied
        // at the frame boundary below), and apply_scene_active_camera caches the
        // authored id, so any same-frame spawn renumbering is re-seated by
        // refresh_active_camera_entity.
        for (const wz::engine::behavior::BehaviorCommand& command :
             commands)
        {
            if (command.kind
                == wz::engine::behavior::BehaviorCommandKind::SetActiveCamera)
            {
                apply_scene_active_camera(command.entity);
            }
        }

        // Collect SPAWN_PREFAB requests (runtime prefab spawning). Resolve the
        // spawner runtime entity -> its STABLE authored id NOW, while
        // behavior_scene_ is still the scene the command was issued against; a
        // later spawn in the drain rebuilds + renumbers the runtime, so a
        // runtime id would go stale. Decode values[0] (the name hash as a float
        // BIT PATTERN, mirroring the audio clip-name trick) and values[1..3]
        // (the offset). Drained by the caller at the frame boundary.
        for (const wz::engine::behavior::BehaviorCommand& command :
             commands)
        {
            if (command.kind
                != wz::engine::behavior::BehaviorCommandKind::SpawnPrefab)
            {
                continue;
            }
            if (command.entity
                >= behavior_scene_->runtime_to_authored.size())
            {
                continue;
            }
            uint32_t name_hash = 0u;
            std::memcpy(
                &name_hash, &command.values[0], sizeof(uint32_t));
            out_spawn_requests.push_back(DeferredSpawnRequest{
                .spawner_id =
                    behavior_scene_->runtime_to_authored[command.entity],
                .name_hash = name_hash,
                .offset = {
                    command.values[1],
                    command.values[2],
                    command.values[3],
                },
            });
        }
    }

    void WozzitsApp_v1::dispatch_scene_behaviors(
        const wz::input::InputState& input, float dt)
    {
        // One behavior tick, as a short sequence of named phases (#256 seam B).
        // The canonical per-tick order: build_collision_frame -> [behaviors] ->
        // integrate_motion -> apply_terrain_constraints. Behaviors are OPTIONAL -- a
        // motion-only / constraint-only actor still needs the integrate + constraint
        // phases to stick to its surface -- so only the dispatch phase is gated on
        // has_behaviors; the rest run whenever a runtime scene exists.
        if (!behavior_scene_) {
            return;
        }
        const bool has_behaviors = !behavior_scene_->behaviors.empty();

        // Scene-simulation start/stop gates (#258). run_behaviors also requires the
        // scene to actually have behaviors bound; advance_motion is the physics axis.
        // With both enabled (the default) every phase runs exactly as before.
        const bool run_behaviors = behaviors_enabled_ && has_behaviors;
        const bool advance_motion = simulation_enabled_;

        // World transforms must be current before dispatch: command application
        // (set_world_translation, motion integration) reads parent world matrices,
        // and behavior transform queries read self/other world. Always run -- it is
        // transform composition (it also settles a live edit), not simulation.
        wz::scene::propagate_all(behavior_scene_->storage.polytree);

        // Phase 1: refresh the per-frame "live?" mask and, when behaviors are
        // enabled, fire the SELF_ACTIVATED rising edges, returning the commands
        // those handlers produced (seeded into the dispatch phase's buffer below,
        // SAME frame). The mask + prev-active roll run regardless -- collision reads
        // the mask and a resumed dispatch must see it current, edge-free.
        const std::vector<wz::engine::behavior::BehaviorCommand>
            activation_commands =
                compute_active_mask_and_fire_edges(behaviors_enabled_);

        // Phase 2: build the collision / proximity / input event tables the dispatch
        // (behaviors) and constraint (motion) phases read. Skipped only when BOTH
        // axes are paused; whenever either resumes it is rebuilt this frame before
        // any consumer reads it, so there is no stale-table hazard.
        if (run_behaviors || advance_motion) {
            build_frame_event_tables(input);
        }

        // Per-frame scratch shared across the remaining phases: the transform-changed
        // entities (for the final re-propagate), the deferred-authoring sink, and the
        // frame-boundary prefab spawns collected during the command apply.
        std::vector<wz::scene::RuntimeEntityId> changed_entities;
        wz::engine::behavior::BehaviorAuthoringBuffer authoring;
        std::vector<DeferredSpawnRequest> spawn_requests;

        // Phase 3: run the behaviors + cognition tick and apply the produced command
        // buffer. Gated on behaviors being enabled AND the scene having any bound.
        if (run_behaviors) {
            dispatch_behaviors_and_apply(
                input, dt, activation_commands,
                changed_entities, authoring, spawn_requests);
        }

        // Phase 4: integrate motion + snap terrain constraints, then re-propagate if
        // anything moved. Gated on the simulation axis.
        if (advance_motion) {
            integrate_motion_and_constraints(dt, changed_entities);
        }

        // Phases 5-7: frame-boundary drains, safe only now -- each may rebuild the
        // behavior runtime out from under us, AFTER every read of behavior_scene_
        // this tick. These are behavior-produced, so gated with the behaviors axis
        // (the buffers are empty when dispatch did not run). Deferred authoring,
        // fire-and-forget prefab spawns, then the spawn-with-identity completions.
        if (behaviors_enabled_) {
            drain_deferred_authoring(authoring);
            drain_prefab_spawns(spawn_requests);
            drain_identity_spawns();
        }
    }

    std::vector<wz::engine::behavior::BehaviorCommand>
    WozzitsApp_v1::compute_active_mask_and_fire_edges(bool fire_edges)
    {
        // Refresh the "live?" mask (#252): a node is dispatched + collides only if it
        // AND every ancestor is `active`. Recomputed each frame from the authored
        // document_.nodes() (cheap O(scene)) and projected onto runtime entities via
        // runtime_to_authored, so a park/unpark flip, a reparent, or a spawn is
        // reflected immediately. build_collision_frame + dispatch_behaviors read
        // behavior_scene_->entity_active (empty => all live, so a caller that never
        // populates it is unaffected). Orthogonal to `visible`.
        std::vector<wz::engine::behavior::BehaviorCommand> activation_commands;
        const std::unordered_map<std::string, std::uint8_t> effective_active =
            compute_scene_node_effective_active(document_.nodes());
        const std::size_t node_count =
            wz::core::graph::node_count(behavior_scene_->storage.polytree);
        behavior_scene_->entity_active.assign(node_count, 1u);
        for (std::size_t entity = 0;
             entity < node_count
                 && entity < behavior_scene_->runtime_to_authored.size();
             ++entity)
        {
            const auto it = effective_active.find(
                behavior_scene_->runtime_to_authored[entity]);
            behavior_scene_->entity_active[entity] =
                (it != effective_active.end()) ? it->second : 1u;
        }

        // WZ_EVENT_SELF_ACTIVATED (#252 pooling): fire on the parked -> live rising
        // edge (an external unpark, e.g. a pool acquire) so a reused instance
        // self-resets. Diffed by authored id, since runtime ids renumber on rebuild.
        // Absent-in-prev counts as live, so a node's BIRTH (handled by self.start) is
        // not mistaken for an activation edge. Skipped wholesale unless a subscriber
        // exists AND there is a prior frame to diff.
        if (fire_edges && has_self_activated_subscriber_
            && !prev_active_by_id_.empty()) {
            wz::engine::behavior::BehaviorCommandBuffer activated_commands;
            wz::engine::behavior::BehaviorFrameContext activated_ctx{
                .scene = &*behavior_scene_,
                .behavior_state = &behavior_scene_->behavior_state,
                .commands = &activated_commands,
                .spawn_requests = &spawn_identity_buffer_,
                .logger = &ctx_.logger,
                .sim_time = behavior_sim_time_,
            };
            for (const auto& [node_id, current] : effective_active) {
                if (current == 0u) {
                    continue;
                }
                const auto prev_it = prev_active_by_id_.find(node_id);
                const std::uint8_t previous =
                    (prev_it != prev_active_by_id_.end())
                        ? prev_it->second
                        : std::uint8_t{ 1 };
                if (previous != 0u) {
                    continue;  // not a 0 -> 1 rising edge
                }
                const auto rt =
                    behavior_scene_->authored_to_runtime.find(node_id);
                if (rt == behavior_scene_->authored_to_runtime.end()) {
                    continue;
                }
                wz::engine::behavior::dispatch_behavior_event(
                    *behavior_scene_,
                    registry_,
                    activated_ctx,
                    wz::engine::behavior::BehaviorEvent{
                        .kind = WZ_EVENT_SELF_ACTIVATED,
                        .entity = rt->second,
                    });
            }
            // Return the produced commands to the orchestrator, which seeds them into
            // the dispatch phase's command buffer (injected just after the clear,
            // SAME frame) -- a SELF_ACTIVATED self-reset that sets visible / active /
            // spawns / plays audio then gets the full drain via
            // apply_all_behavior_commands rather than being silently dropped by an
            // apply-only pass (#252 audit). No id staleness: the edge pass ran this
            // frame, before the dispatch phase.
            activation_commands.insert(
                activation_commands.end(),
                activated_commands.commands.begin(),
                activated_commands.commands.end());
        }

        prev_active_by_id_ = effective_active;
        return activation_commands;
    }

    void WozzitsApp_v1::build_frame_event_tables(
        const wz::input::InputState& input)
    {
        // Build the collision frame (collision world + terrain constraint surfaces)
        // BEFORE motion/behaviors. apply_terrain_constraints reads
        // frame_storage_.collision to resolve the
        // surface a terrain_constrained Motion actor sticks to. Guard the asset
        // library; if absent, the collision frame is left whatever it was (empty),
        // which makes the constraint pass a no-op rather than fabricating a surface.
        if (ctx_.assets) {
            wz::engine::collision::build_collision_frame(
                *behavior_scene_,
                ctx_.assets->collisions(),
                frame_storage_.collision);
        }
        // Proximity events (proximity.* behaviors): same per-frame build +
        // enter/stay/exit advance, right after the collision world. Needs only the
        // scene's proximity components (no asset library).
        wz::engine::collision::build_proximity_frame(
            *behavior_scene_, frame_storage_.collision);
        // Input events (input.* behaviors, e.g. a controller-driven tank): convert
        // this frame's input into routed events so dispatch fires the behavior's
        // on_event. The runtime originally left these tables empty, so input-driven
        // behaviors never received events until this build was added.
        wz::engine::input_events::build_input_event_frame(
            input, *behavior_scene_, frame_storage_.input_events);
    }

    void WozzitsApp_v1::dispatch_behaviors_and_apply(
        const wz::input::InputState& input,
        float dt,
        const std::vector<wz::engine::behavior::BehaviorCommand>& activation_commands,
        std::vector<wz::scene::RuntimeEntityId>& changed_entities,
        wz::engine::behavior::BehaviorAuthoringBuffer& authoring,
        std::vector<DeferredSpawnRequest>& spawn_requests)
    {
        // Build a minimal FrameContext carrying time + input. The collision,
        // proximity and input-event tables were populated by build_frame_event_tables,
        // so the dispatch routes real collision/proximity/input events to behaviors.
        wz::engine::FrameContext frame_context{};
        frame_context.input = input;
        frame_context.frame.interval.start = 0;
        frame_context.frame.interval.end = static_cast<wz::time::Tick>(
            static_cast<double>(dt)
            * static_cast<double>(
                wz::time::TimeSource::ticks_per_second()));
        frame_context.frame.index = behavior_frame_index_++;

        frame_storage_.behavior_commands.clear();
        // Seed with the SELF_ACTIVATED pass's commands (collected earlier THIS frame,
        // before this clear) so a pooled instance's self-reset gets the SAME full
        // drain -- transforms AND host-handled kinds (visible / active / spawn /
        // audio / param) -- as per-frame behaviors, rather than the apply-only that
        // silently dropped every host-handled command it issued (#252 audit).
        if (!activation_commands.empty()) {
            frame_storage_.behavior_commands.commands.insert(
                frame_storage_.behavior_commands.commands.end(),
                activation_commands.begin(),
                activation_commands.end());
        }

        // Advance the monotonic sim clock the self-paced cognition scheduler stamps
        // against (the FrameContext interval is per-frame, not absolute).
        behavior_sim_time_ += static_cast<double>(dt);

        wz::engine::behavior::BehaviorFrameContext behavior_ctx{
            .frame_context = &frame_context,
            .frame_storage = &frame_storage_,
            .scene = &*behavior_scene_,
            .behavior_state = &behavior_scene_->behavior_state,
            .commands = &frame_storage_.behavior_commands,
            .events = &behavior_events_,
            .scalars = &behavior_scalars_,
            .gpu_compute = nullptr,
            .spawn_requests = &spawn_identity_buffer_,
            .authoring = &authoring,
            .logger = &ctx_.logger,
            .sim_time = behavior_sim_time_,
        };
        wz::engine::behavior::dispatch_behaviors(
            *behavior_scene_, registry_, behavior_ctx);

        // Self-paced cognition: fire cognition.tick to agents whose own scheduled
        // wake is due now, APPENDING any actuator commands to the same buffer (applied
        // just below). Not a per-frame call into every agent -- only those due at
        // behavior_sim_time_.
        wz::engine::behavior::dispatch_cognition_tick(
            *behavior_scene_, registry_, behavior_ctx);

        // Apply the produced command buffer through the single converged applier
        // (#256 seam A): the transform/velocity kinds AND every host-handled immediate
        // kind (audio / renderable-param / visible / active / active-camera), plus
        // COLLECT the deferred SPAWN_PREFAB requests into spawn_requests for the
        // frame-boundary drain.
        apply_all_behavior_commands(
            frame_storage_.behavior_commands.commands,
            changed_entities,
            spawn_requests);
    }

    void WozzitsApp_v1::integrate_motion_and_constraints(
        float dt, std::vector<wz::scene::RuntimeEntityId>& changed_entities)
    {
        std::vector<wz::scene::RuntimeEntityId> velocity_changed;
        (void)wz::engine::behavior::integrate_motion(
            *behavior_scene_, dt, &velocity_changed);
        changed_entities.insert(
            changed_entities.end(),
            velocity_changed.begin(),
            velocity_changed.end());

        // Snap terrain_constrained Motion actors to their constraint surface (from
        // frame_storage_.collision built above). Runs after integrate_motion so the
        // constraint corrects the just-integrated position.
        std::vector<wz::scene::RuntimeEntityId> constraint_changed;
        (void)wz::engine::behavior::apply_terrain_constraints(
            *behavior_scene_,
            frame_storage_.collision,
            dt,
            &constraint_changed);
        changed_entities.insert(
            changed_entities.end(),
            constraint_changed.begin(),
            constraint_changed.end());

        std::sort(changed_entities.begin(), changed_entities.end());
        changed_entities.erase(
            std::unique(changed_entities.begin(), changed_entities.end()),
            changed_entities.end());

        if (!changed_entities.empty()) {
            // Re-propagate so world matrices (and any next-frame world-space reads)
            // reflect the applied local changes. #221: the polytree is now the single
            // source of truth for render/audio/spawn world transforms (see
            // scene_world_transforms()), so there is no per-frame write-back into
            // document_.nodes() any more -- document_.nodes() transforms are derived from the
            // polytree only when actually needed (save + editor read-back). The lossy
            // Mat4->TRS->Mat4 round trip is gone from the frame path.
            wz::scene::propagate_all(behavior_scene_->storage.polytree);
        }

        // Motion Filter pass (secondary-motion camera damping): the LAST transform
        // step of the tick, AFTER the propagate above so it reads each filtered
        // node's clean world as the target, and overwrites that world IN PLACE
        // with the damped pose (never touching local -- see apply_motion_filters).
        // Runs every frame (unconditional on changed_entities) so a filtered node
        // keeps easing even when nothing else moved; the state map is app-owned so
        // it survives scene rebuilds. Downstream reads (camera view, render, audio)
        // all read the polytree, so they see the damped pose with no other wiring.
        if (!behavior_scene_->motion_filters.empty()) {
            (void)wz::engine::behavior::apply_motion_filters(
                *behavior_scene_,
                frame_storage_.collision,
                dt,
                motion_filter_states_);
        }
    }

    void WozzitsApp_v1::drain_deferred_authoring(
        const wz::engine::behavior::BehaviorAuthoringBuffer& authoring)
    {
        // Frame-boundary drain of behavior-issued deferred authoring (#204). This runs
        // AFTER the dispatch loop has finished iterating the scene, so mutating it
        // here is not reentrant. Each request goes through the SAME apply method the
        // host's add_child uses (add_child_node) -- the single converged apply path.
        // add_child_node re-materializes the behavior runtime (rebuild_behavior_scene)
        // on success, which is why the drains are among the LAST things the tick does:
        // every read of behavior_scene_ above has already happened, and behavior_scene_
        // may be rebuilt out from under us here safely. Fire-and-forget: no id flows
        // back to the behavior. The parents were resolved to authored ids at enqueue
        // time, so they remain valid even as a prior add in this same drain renumbers
        // runtime ids.
        for (const wz::scene::AuthoredEntityId& parent :
             authoring.spawn_child_parents)
        {
            const wz::engine::assets::SceneAddChildResult result =
                add_child_node(parent);
            if (!result.ok) {
                ctx_.logger.warn(
                    "behavior spawn_child rejected for parent '" + parent
                    + "': " + result.error);
            }
        }

        // remove_node drain: same frame-boundary, same single converged apply path the
        // host's remove uses (remove_node, which also rebuilds the behavior runtime on
        // success). Authored-id targets stay valid even as a prior add/remove in this
        // same drain renumbers runtime ids.
        for (const wz::scene::AuthoredEntityId& target :
             authoring.remove_node_targets)
        {
            if (!remove_node(target)) {
                ctx_.logger.warn(
                    "behavior remove_node rejected for node '" + target + "'");
            }
        }

        // set_renderable_asset drain: same frame-boundary, same apply method the host
        // uses (set_node_renderable_asset). This is a cheap field write + dirty flag
        // (no behavior-runtime rebuild, no asset-DAG recompile), so it is
        // order-independent of the structural drains above.
        for (const wz::engine::behavior::BehaviorSetRenderableRequest& request :
             authoring.set_renderable_requests)
        {
            if (!set_node_renderable_asset(
                    request.node_id, request.asset_graph_node_id)) {
                ctx_.logger.warn(
                    "behavior set_renderable_asset rejected for node '"
                    + request.node_id + "'");
            }
        }

        // reparent drain: same frame-boundary, same apply method the host's reparent
        // uses (reparent_node, which rebuilds the behavior runtime on success like
        // add_child/remove). Both ids were resolved to authored ids at enqueue time
        // (an empty new_parent_id = top level), so they stay valid even as a prior
        // structural drain entry renumbers runtime ids.
        for (const wz::engine::behavior::BehaviorReparentRequest& request :
             authoring.reparent_requests)
        {
            if (!reparent_node(request.node_id, request.new_parent_id)) {
                ctx_.logger.warn(
                    "behavior reparent_node rejected for node '"
                    + request.node_id + "'");
            }
        }

        // add/remove-component drains: same frame-boundary, same apply methods the
        // host uses (add_node_component / remove_node_component). These are cheap field
        // edits (no behavior-runtime rebuild, no asset-DAG recompile), so they are
        // order-independent of the structural drains above. The kind was copied into
        // each request at enqueue time (the behavior's transient const char* is long
        // gone by now).
        for (const wz::engine::behavior::BehaviorComponentRequest& request :
             authoring.add_component_requests)
        {
            if (!add_node_component(request.node_id, request.kind)) {
                ctx_.logger.warn(
                    "behavior add_node_component rejected for node '"
                    + request.node_id + "' kind '" + request.kind + "'");
            }
        }
        for (const wz::engine::behavior::BehaviorComponentRequest& request :
             authoring.remove_component_requests)
        {
            if (!remove_node_component(request.node_id, request.kind)) {
                ctx_.logger.warn(
                    "behavior remove_node_component rejected for node '"
                    + request.node_id + "' kind '" + request.kind + "'");
            }
        }
    }

    std::size_t WozzitsApp_v1::active_behavior_binding_count() const
    {
        return behavior_scene_ ? behavior_scene_->behaviors.size() : 0u;
    }

}
