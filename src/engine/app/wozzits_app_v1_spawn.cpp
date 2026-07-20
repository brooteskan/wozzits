// src/engine/app/wozzits_app_v1_spawn.cpp
//
// WozzitsApp_v1 -- the runtime prefab spawn + scene_source graft subsystem
// (#256 seam C). Split out of wozzits_app_v1.cpp as a partial-class TU: the
// class is still declared once in engine/app/wozzits_app_v1.h; these are just
// its spawn/graft method definitions, co-located. Covers prefab registration,
// spawn_prefab (clone + graft + incremental render assembly + rebuild), the
// scene_source graft (full + per-host), the grafted-child override capture, and
// the frame-boundary spawn drains (fire-and-forget + spawn-with-identity). See
// the parallel behavior track #257 for making the rebuild incremental.

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
        // FNV-1a/32 over a prefab name. Matches wz_prefab_hash (the behavior-side
        // constexpr) bit-for-bit so a SPAWN_PREFAB command's hash resolves to the
        // prefab registered under the same name.
        uint32_t prefab_name_hash(std::string_view name) noexcept
        {
            uint32_t h = 2166136261u;
            for (const unsigned char c : name) {
                h ^= static_cast<uint32_t>(c);
                h *= 16777619u;
            }
            return h;
        }
    }

    std::size_t WozzitsApp_v1::register_scenelet_prefabs()
    {
        // Rebuilt from scratch on every (re)load, so a reload reflects added/removed
        // scenelet files.
        scenelet_catalog_.clear();
        if (!ctx_.assets) {
            return 0;
        }

        // Scenelets live in a "scenelets" folder next to the scene file (the
        // project root), so derive the folder from the scene's directory and
        // resolve it through the asset file system the way load_behavior_modules
        // resolves the module folder (resolve_path joins a relative path onto the
        // absolute resource root). A missing/empty folder is a silent no-op —
        // projects without scenelets register nothing.
        const wz::fs::Path project_dir = wz::fs::parent_path(scene_source_path_);
        const wz::fs::Path scenelets_rel =
            project_dir.empty()
                ? wz::fs::Path{ "scenelets" }
                : wz::fs::join(project_dir, "scenelets");
        const wz::fs::Path scenelets_dir =
            ctx_.assets->files().resolve_path(scenelets_rel);

        const wz::fs::FileResult<std::vector<wz::fs::DirEntry>> listing =
            wz::fs::list_directory(scenelets_dir);
        if (!listing) {
            return 0;  // no scenelets folder (or unreadable): nothing to register
        }

        std::size_t registered = 0;
        for (const wz::fs::DirEntry& entry : listing.value) {
            if (entry.is_directory) {
                continue;
            }
            // Match "*.scene.json" only (the scene-file convention). The prefab
            // name is the filename stem with the ".scene" suffix stripped, so
            // tank.scene.json -> "tank". wz::fs::extension returns the suffix
            // WITHOUT a leading dot.
            const wz::fs::Path full = wz::fs::join(scenelets_dir, entry.name);
            if (wz::fs::extension(full) != "json") {
                continue;
            }
            wz::fs::Path name = wz::fs::stem(full);  // "tank.scene"
            if (wz::fs::extension(name) != "scene") {
                continue;
            }
            name = wz::fs::stem(name);  // "tank"

            const wz::fs::FileResult<std::string> text =
                wz::fs::read_file_text(full);
            if (!text) {
                ctx_.logger.warn(
                    "register_scenelet_prefabs: could not read '"
                    + entry.name + "' (skipped)");
                continue;
            }

            wz::json::JSONParseResult parsed =
                wz::json::parse_json_string(text.value);
            if (!parsed.ok || !parsed.document.root) {
                ctx_.logger.warn(
                    "register_scenelet_prefabs: '" + entry.name
                    + "' is not valid JSON (skipped)");
                continue;
            }

            std::optional<wz::engine::assets::SceneAssetData> scene_data =
                wz::engine::assets::internal::parse_scene_data_from_json(
                    parsed.document, ctx_.logger);
            if (!scene_data) {
                ctx_.logger.warn(
                    "register_scenelet_prefabs: '" + entry.name
                    + "' did not parse as a scene (skipped)");
                continue;
            }

            register_prefab(name, std::move(scene_data->nodes));
            // Record it for the editor's scenelet menu (name + resource-relative
            // path, so the editor can open the file directly).
            scenelet_catalog_.push_back(SceneletCatalogEntry{
                .name = name,
                .path = wz::fs::join(scenelets_rel, entry.name),
            });
            ++registered;
        }

        if (registered > 0) {
            ctx_.logger.info(
                "register_scenelet_prefabs: registered "
                + std::to_string(registered) + " prefab(s) from "
                + scenelets_dir);
        }
        return registered;
    }

    std::size_t WozzitsApp_v1::graft_scene_sources()
    {
        if (!ctx_.assets) {
            return 0;
        }

        // Idempotent re-graft: drop the previously grafted children first so a
        // re-bind/re-resolve (new Scene keys) or a scene-source edit re-expands
        // cleanly rather than accumulating duplicates.
        if (!document_.grafted_ids().empty()) {
            std::unordered_set<std::string> stale(
                document_.grafted_ids().begin(), document_.grafted_ids().end());
            document_.nodes().erase(
                std::remove_if(
                    document_.nodes().begin(),
                    document_.nodes().end(),
                    [&stale](const wz::engine::assets::SceneNodeAsset& n) {
                        return stale.count(n.id) != 0;
                    }),
                document_.nodes().end());
            document_.grafted_ids().clear();
        }

        // Snapshot the host nodes carrying a resolved scene_source up front: we
        // mutate document_.nodes() (append children) while iterating, and grafted
        // children never themselves carry a scene_source (the expander clears
        // it), so a single non-recursive pass over the current hosts is enough.
        struct HostRef
        {
            wz::engine::assets::SceneNodeAsset host;  // copy (stable across append)
            wz::asset::AssetKey scene_source;
        };
        std::vector<HostRef> hosts;
        for (const wz::engine::assets::SceneNodeAsset& node : document_.nodes()) {
            if (node.scene_source) {
                hosts.push_back(HostRef{ node, *node.scene_source });
            }
        }

        std::size_t grafted = 0;
        for (const HostRef& ref : hosts) {
            grafted +=
                graft_host_scene_source(ref.host, ref.scene_source, nullptr);
        }

        if (grafted > 0) {
            ctx_.logger.info(
                "graft_scene_sources: grafted "
                + std::to_string(grafted) + " scene-source child node(s) from "
                + std::to_string(hosts.size()) + " host(s)");
        }
        return grafted;
    }

    std::vector<std::string> WozzitsApp_v1::graft_scene_sources_for_hosts(
        const std::vector<std::string>& host_ids)
    {
        std::vector<std::string> new_children;
        if (!ctx_.assets || host_ids.empty()) {
            return new_children;
        }

        // Snapshot the target hosts up front (copies stable across the document_.nodes()
        // appends graft_host_scene_source does): only those present AND carrying a
        // resolved scene_source. Unlike graft_scene_sources() there is NO drop-all --
        // existing grafted children stay put, so a spawn touches only its own subtree
        // (#252). A later full graft_scene_sources() still drop-alls + re-expands
        // everything (the spawned host carries a scene_source), so the two compose.
        struct HostRef
        {
            wz::engine::assets::SceneNodeAsset host;  // copy (stable across append)
            wz::asset::AssetKey scene_source;
        };
        const std::unordered_set<std::string> wanted(
            host_ids.begin(), host_ids.end());
        std::vector<HostRef> hosts;
        for (const wz::engine::assets::SceneNodeAsset& node : document_.nodes()) {
            if (node.scene_source && wanted.count(node.id) != 0) {
                hosts.push_back(HostRef{ node, *node.scene_source });
            }
        }

        std::size_t grafted = 0;
        for (const HostRef& ref : hosts) {
            grafted += graft_host_scene_source(
                ref.host, ref.scene_source, &new_children);
        }

        if (grafted > 0) {
            ctx_.logger.info(
                "graft_scene_sources_for_hosts: grafted "
                + std::to_string(grafted) + " scene-source child node(s) from "
                + std::to_string(hosts.size()) + " host(s)");
        }
        return new_children;
    }

    std::size_t WozzitsApp_v1::graft_host_scene_source(
        const wz::engine::assets::SceneNodeAsset& host,
        const wz::asset::AssetKey& scene_source,
        std::vector<std::string>* out_new_children)
    {
        const wz::engine::assets::SceneHandle handle =
            ctx_.assets->scenes().get_scene(
                wz::engine::assets::SceneAsset{ .output = scene_source });
        const wz::engine::assets::SceneAssetData* sub =
            ctx_.assets->scenes().get_scene_data(handle);
        if (!sub) {
            ctx_.logger.warn(
                "graft_scene_sources: scene_source for node '" + host.id
                + "' did not resolve to a Scene asset (skipped)");
            return 0;
        }

        std::vector<wz::engine::assets::SceneNodeAsset> children =
            wz::engine::assets::expand_scene_source_children(host, *sub);
        // Re-apply this host's sticky per-child component overrides (issue #213)
        // onto the freshly expanded children, keyed by the child's sub-scene id
        // ("<host>/" suffix). Applied AFTER expansion so the authored program rides
        // on the runtime child without being folded into the GLB Scene key.
        const std::string host_prefix = host.id + "/";
        std::unordered_set<std::string> matched_overrides;
        std::size_t grafted = 0;
        for (wz::engine::assets::SceneNodeAsset& child : children) {
            if (child.id.size() > host_prefix.size()
                && child.id.compare(
                       0, host_prefix.size(), host_prefix) == 0)
            {
                const std::string sub_id =
                    child.id.substr(host_prefix.size());
                for (const wz::engine::assets::SceneSourceChildOverride& ov :
                     host.scene_source_child_overrides)
                {
                    if (ov.child_id != sub_id) {
                        continue;
                    }
                    if (ov.render_program_node_id) {
                        child.render_program_node_id =
                            ov.render_program_node_id;
                    }
                    matched_overrides.insert(sub_id);
                    break;
                }
            }
            document_.grafted_ids().push_back(child.id);
            if (out_new_children) {
                out_new_children->push_back(child.id);
            }
            document_.nodes().push_back(std::move(child));
            ++grafted;
        }
        // Sticky policy: an override whose child_id no longer expands (renamed
        // / removed source node, or a transient resolve failure) is RETAINED,
        // never deleted — only logged, so a later source fix re-attaches it.
        for (const wz::engine::assets::SceneSourceChildOverride& ov :
             host.scene_source_child_overrides)
        {
            if (matched_overrides.count(ov.child_id) == 0) {
                ctx_.logger.info(
                    "graft_scene_sources: child override '" + ov.child_id
                    + "' under host '" + host.id
                    + "' matched no expanded node (retained)");
            }
        }
        return grafted;
    }

    void WozzitsApp_v1::capture_grafted_child_override(
        const wz::scene::AuthoredEntityId& child_id)
    {
        // Only runtime-only grafted children need a sticky override; authored
        // nodes persist their components directly.
        if (std::find(
                document_.grafted_ids().begin(), document_.grafted_ids().end(), child_id)
            == document_.grafted_ids().end())
        {
            return;
        }
        const wz::engine::assets::SceneNodeAsset* child =
            wz::engine::assets::find_scene_node(document_.nodes(), child_id);
        if (!child) {
            return;
        }

        // Walk up to the owning host: the first non-grafted ancestor (the node
        // that carries the scene source this child was expanded from). All grafted
        // children are descendants of their host, and the host is not grafted.
        const std::unordered_set<std::string> grafted(
            document_.grafted_ids().begin(), document_.grafted_ids().end());
        wz::engine::assets::SceneNodeAsset* host = nullptr;
        const wz::engine::assets::SceneNodeAsset* cursor = child;
        for (std::size_t guard = 0;
             cursor && guard <= document_.nodes().size();
             ++guard)
        {
            if (!cursor->parent_id || cursor->parent_id->empty()) {
                break;
            }
            wz::engine::assets::SceneNodeAsset* parent =
                wz::engine::assets::find_scene_node(
                    document_.nodes(), *cursor->parent_id);
            if (!parent) {
                break;
            }
            if (grafted.count(parent->id) == 0) {
                host = parent;  // first non-grafted ancestor = host
                break;
            }
            cursor = parent;
        }
        if (!host) {
            return;
        }

        // Sub-scene-relative key: strip the "<host>/" prefix the graft namespaced
        // it with (child.id == host.id + "/" + src.id).
        const std::string prefix = host->id + "/";
        if (child_id.size() <= prefix.size()
            || child_id.compare(0, prefix.size(), prefix) != 0)
        {
            return;
        }
        const std::string sub_id = child_id.substr(prefix.size());

        // Upsert the override from the child's CURRENT authored state; an override
        // that would carry nothing is erased (so clearing a child's program drops
        // the entry rather than persisting an empty one).
        std::vector<wz::engine::assets::SceneSourceChildOverride>& overrides =
            host->scene_source_child_overrides;
        const auto it = std::find_if(
            overrides.begin(),
            overrides.end(),
            [&sub_id](const wz::engine::assets::SceneSourceChildOverride& ov) {
                return ov.child_id == sub_id;
            });

        wz::engine::assets::SceneSourceChildOverride next{};
        next.child_id = sub_id;
        next.render_program_node_id = child->render_program_node_id;

        const bool carries_nothing = !next.render_program_node_id.has_value();
        if (carries_nothing) {
            if (it != overrides.end()) {
                overrides.erase(it);
                document_.dirty() = true;
            }
            return;
        }
        if (it != overrides.end()) {
            *it = std::move(next);
        }
        else {
            overrides.push_back(std::move(next));
        }
        document_.dirty() = true;
    }

    void WozzitsApp_v1::register_prefab(
        const std::string& prefab_name,
        std::vector<wz::engine::assets::SceneNodeAsset> nodes)
    {
        // Key by the same FNV-1a/32 hash the behavior helpers use, so a
        // SPAWN_PREFAB command naming this prefab resolves here. A re-register
        // replaces the prior nodes (the cache is the live prefab set).
        const uint32_t hash = prefab_name_hash(prefab_name);
        prefab_by_hash_[hash] = std::move(nodes);
        ctx_.logger.info(
            "register_prefab: '" + prefab_name + "' ("
            + std::to_string(prefab_by_hash_[hash].size()) + " nodes)");
    }

    std::size_t WozzitsApp_v1::spawned_prefab_node_count() const
    {
        // The spawn graft mints ids with a "spawn:" prefix (instantiate_prefab_-
        // nodes); count them so a test can observe a prefab landing in document_.nodes().
        std::size_t count = 0;
        for (const wz::engine::assets::SceneNodeAsset& node : document_.nodes()) {
            if (node.id.rfind("spawn:", 0) == 0) {
                ++count;
            }
        }
        return count;
    }

    const wz::scene::AuthoredEntityId&
    WozzitsApp_v1::active_scene_camera_id() const
    {
        return view_.active_scene_camera_id();
    }

    WozzitsApp_v1::SpawnPrefabResult WozzitsApp_v1::spawn_prefab(
        const wz::scene::AuthoredEntityId& spawner_id,
        uint32_t prefab_name_hash,
        float offset_x,
        float offset_y,
        float offset_z,
        bool spawn_parked)
    {
        // Resolve the prefab once (the registered scenelet for this name hash).
        const auto prefab_it = prefab_by_hash_.find(prefab_name_hash);
        if (prefab_it == prefab_by_hash_.end()) {
            ctx_.logger.warn(
                "spawn_prefab: unknown prefab hash "
                + std::to_string(prefab_name_hash) + " — skipped");
            return {};
        }
        if (prefab_it->second.empty()) {
            ctx_.logger.warn("spawn_prefab: prefab has no nodes — skipped");
            return {};
        }

        // Resolve the spawner's stable authored id -> its index in document_.nodes() so
        // the spawn anchor is the spawner's live world transform. #221:
        // scene_world_transforms() is index-aligned with document_.nodes() and, with a
        // live behavior scene, reads the sim-current pose from the polytree (this
        // drain runs after propagate_all). Addressing by authored id (not runtime
        // entity) keeps this valid across the rebuild a prior spawn in this drain
        // may have done.
        const std::vector<wz::math::Mat4> world_transforms =
            scene_world_transforms();
        wz::math::Mat4 spawner_world = wz::math::Mat4::identity();
        bool found_spawner = false;
        for (std::size_t i = 0; i < document_.nodes().size(); ++i) {
            if (document_.nodes()[i].id == spawner_id) {
                spawner_world = world_transforms[i];
                found_spawner = true;
                break;
            }
        }
        if (!found_spawner) {
            ctx_.logger.warn(
                "spawn_prefab: spawner node '" + spawner_id
                + "' not in scene — skipped");
            return {};
        }

        // T = spawner world transform × the offset. mul(a, b) = a * b
        // (column-major), so the offset is applied in the spawner's local frame
        // and then carried by the spawner's world transform — i.e. the prefab is
        // placed `offset` away from the spawner IN the spawner's frame. Decompose
        // to a TRS for the re-rooted prefab root's local (it becomes a top-level
        // node, so its local == this world transform).
        const wz::math::Mat4 spawn_world = wz::math::mul(
            spawner_world,
            wz::math::translation(
                wz::math::Vec3{ offset_x, offset_y, offset_z }));

        wz::math::Transform trs{};
        if (!wz::math::decompose_trs(spawn_world, trs)) {
            trs = wz::math::rigid_pose_from_matrix(spawn_world);
        }

        // Spawn LEVEL: replace the inherited rotation with an upright one facing
        // the spawn heading (yaw about world +Y). The spawner's full rotation
        // carries its OWN terrain-induced tilt (pitch/roll for ITS surface); at
        // the spawn location that tilt is wrong -- it tips the tank onto a side
        // edge and aims its local-forward partly into the ground, so it drives
        // under the surface. Keep only the heading (the spawn matrix's +Z axis
        // projected onto the horizontal plane) and let per-frame terrain
        // alignment tilt the tank correctly for its own surface.
        {
            const float fwd_x = spawn_world.m[8];   // local +Z in world (column 2)
            const float fwd_z = spawn_world.m[10];
            const float horiz = std::sqrt(fwd_x * fwd_x + fwd_z * fwd_z);
            if (horiz > 1.0e-4f) {
                const float yaw = std::atan2(fwd_x, fwd_z);  // +Z at yaw 0
                trs.rotation = wz::math::Quaternion{
                    0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f) };
            }
        }
        ctx_.logger.info(
            "spawn_prefab: spawn at ("
            + std::to_string(trs.position.x) + ", "
            + std::to_string(trs.position.y) + ", "
            + std::to_string(trs.position.z) + ") scale "
            + std::to_string(trs.scale.x));

        wz::engine::assets::AuthoredTransform root_transform{};
        root_transform.translation[0] = trs.position.x;
        root_transform.translation[1] = trs.position.y;
        root_transform.translation[2] = trs.position.z;
        root_transform.rotation_quat[0] = trs.rotation.x;
        root_transform.rotation_quat[1] = trs.rotation.y;
        root_transform.rotation_quat[2] = trs.rotation.z;
        root_transform.rotation_quat[3] = trs.rotation.w;
        root_transform.scale[0] = trs.scale.x;
        root_transform.scale[1] = trs.scale.y;
        root_transform.scale[2] = trs.scale.z;

        // Clone the prefab with conflict-free ids under the spawn transform, then
        // graft: append to document_.nodes() (the renderer's + behavior runtime's
        // source of truth), rebuild the behavior runtime (now state-preserving, so
        // pre-existing bindings keep their state and the spawned subtree's
        // behaviors initialize fresh), and re-assemble render bindings (so a
        // spawned renderable draws). Mirrors add_child_node's append->rebuild.
        const std::size_t nodes_before = document_.nodes().size();
        std::vector<wz::engine::assets::SceneNodeAsset> spawned =
            wz::engine::assets::instantiate_prefab_nodes(
                prefab_it->second, ++spawn_counter_, root_transform);
        // Capture the spawned root's STABLE authored id (the re-rooted node, the one
        // instantiate_prefab_nodes left parent-less) so the identity-spawn path
        // (#252 pooling) can address the instance after the rebuild renumbers the
        // runtime. Fall back to the first spawned node if none is parent-less.
        wz::scene::AuthoredEntityId root_authored_id =
            spawned.empty() ? wz::scene::AuthoredEntityId{} : spawned.front().id;
        for (const wz::engine::assets::SceneNodeAsset& node : spawned) {
            if (!node.parent_id.has_value()) {
                root_authored_id = node.id;
                break;
            }
        }
        // spawn_parked (#252 pooling): a prewarmed pool instance grafts INERT --
        // active=0 parks its dispatch + collision, visible=0 hides it (no one-frame
        // flash). Both axes are HIERARCHICAL, so set them ONLY on the re-rooted
        // top-level node(s): the whole subtree inherits parked+hidden. Setting every
        // node would be WRONG -- an acquire unparks the root (wz_set_active on the
        // one handle it has), and a child that carried its OWN active=0 would stay
        // effectively inactive, so its collider (hitbox / projectile) would never
        // rejoin the collision frame. Root-only means the acquire revives the whole
        // instance. The render/collision bindings still materialize, so the acquire
        // (active=1 + visible=1) brings it up with no re-spawn.
        if (spawn_parked) {
            for (wz::engine::assets::SceneNodeAsset& node : spawned) {
                if (!node.parent_id.has_value()) {
                    node.active = false;
                    node.visible = false;
                }
            }
        }
        document_.nodes().insert(
            document_.nodes().end(),
            std::make_move_iterator(spawned.begin()),
            std::make_move_iterator(spawned.end()));
        // A runtime spawn is ephemeral, NOT an authoring edit -- do not mark the
        // scene dirty (and save_scene excludes "spawn:" nodes regardless), so
        // spawned instances are never written back into scene.json.

        // Materialize the spawned subtree's render/collision bindings INCREMENTALLY
        // -- the same bridge + graft + assemble the load/bind path runs, but scoped
        // to just the spawned nodes + their grafted children instead of the whole
        // scene (#252, A1). A prefab whose geometry comes from a scene_source (e.g. a
        // GLB tank) carries only the host node; its meshes are the grafted children,
        // so without the graft the host appends but nothing draws.
        if (ctx_.assets) {
            // Capture the spawned block's node ids (the contiguous "spawn:" span just
            // appended) BEFORE graft appends its children -- these plus the grafted
            // children are the ONLY nodes this spawn changed, so render assembly can
            // touch just them instead of re-assembling the whole scene (#252, A1).
            std::vector<std::string> spawned_ids;
            spawned_ids.reserve(document_.nodes().size() - nodes_before);
            for (std::size_t i = nodes_before; i < document_.nodes().size(); ++i) {
                spawned_ids.push_back(document_.nodes()[i].id);
            }

            wz::engine::assets::bridge_scene_source_keys(
                document_.nodes(), graph_draft_);
            // Incremental graft: expand ONLY the spawned host(s)' scene_source into
            // grafted children, without the full re-graft's drop-all. A prefab whose
            // geometry comes from a scene_source (e.g. a GLB tank) carries only the
            // host node; its meshes are the grafted children, so without this the host
            // appends but nothing draws. Child ids are prefixed with the (per-spawn
            // unique) host id, so each instance gets its own children.
            const std::vector<std::string> new_children =
                graft_scene_sources_for_hosts(spawned_ids);

            // Bridge the spawned subtree's PRE-BUILT renderables (renderable_asset_
            // node_id -> key) -- e.g. a tank's muzzle_beam / trajectory / impact_flash
            // / projectile, which carry a renderable but no geometry, so the assemble
            // path below (geometry-only) never touches them. Without this bridge those
            // nodes keep an unresolved key and draw nothing. Cheap (key lookups); it
            // used to ride on the trailing rematerialize, now removed.
            wz::engine::assets::bridge_scene_renderable_keys(
                document_.nodes(), graph_draft_);

            // Assemble ONLY the spawned subtree (spawned block + its grafted
            // children). Resolves each node's render_program + AudioSource anchors
            // (node id -> key) on document_.nodes(). MUST run BEFORE rebuild_behavior_scene,
            // which materializes the runtime AudioSource from
            // node.audio_source->audio_renderable -- an unresolved key there makes a
            // spawned tank's cannon silent (mirrors the load path).
            std::unordered_set<std::string> spawned_subtree(
                spawned_ids.begin(), spawned_ids.end());
            spawned_subtree.insert(new_children.begin(), new_children.end());
            const std::size_t assembled = assemble_render_bindings(
                graph_draft_, nullptr, &spawned_subtree);

            // Bridge the spawned subtree's collision_asset_node_id -> key (mirrors
            // the load/bind path). Without this a spawned prefab's collision component
            // keeps an unresolved key, so build_collision_frame skips it and the
            // collider never exists -- the same spawn-gap class as the audio auto-play
            // + active-camera passes. Runs BEFORE rebuild_behavior_scene, which
            // materializes the runtime collider from the resolved key.
            wz::engine::assets::bridge_scene_collision_keys(
                document_.nodes(), graph_draft_);

            // Same for any atmosphere the spawned subtree carries. Frame-global
            // state in a prefab is an authoring error render_scene warns about,
            // but bridging it here keeps the spawn path a faithful bind: the
            // component resolves however the node entered the document, so the
            // error surfaces as a duplicate warning rather than as silence.
            wz::engine::assets::bridge_scene_atmosphere_keys(
                document_.nodes(), graph_draft_);

            // Same for any FrameEnvironment the spawned subtree carries.
            wz::engine::assets::bridge_scene_environment_keys(
                document_.nodes(), graph_draft_);

            // Compile the freshly registered spawn renderables. This commit()+
            // resolve_all() is the INCREMENTAL replacement for the old full-scene
            // trailing rematerialize_render_bindings(): commit finalizes only the new
            // registrations and resolve_all cache-hits every pre-existing key (ms=1),
            // so a spawn no longer re-materializes the whole scene (#252). Mirrors
            // rematerialize_node_render_binding (#253). resolve_all (not a targeted
            // resolve_roots) because the targeted paths evict everything not in the
            // root set -- they would tear down the rest of the live scene.
            if (assembled > 0) {
                ctx_.assets->commit();
                const wz::engine::assets::ResolveReport resolve =
                    ctx_.assets->resolve_all();
                if (!resolve.ok()) {
                    ctx_.logger.warn(
                        "spawn_prefab: resolved with errors="
                        + std::to_string(resolve.failures.size()));
                }
            }
        }

        // A spawn only APPENDS to document_.nodes() (the spawned block + its grafted
        // scene-source children, all at the tail), so survivor runtime ids stay
        // stable and only the newly materialized bindings need their on_init run
        // (#257 B1 init-scoping) -- the rest of the runtime is preserved as-is.
        rebuild_behavior_scene(/*append_only=*/true);
        // Loud, greppable spawn marker so a play log unambiguously records WHEN a
        // graft landed and its node delta -- correlate with a frame_profile CSV row
        // (scene_nodes jump + rematerialize/rebuild) to confirm a spawn (#252).
        ctx_.logger.info(
            "[perf] SPAWN instance " + std::to_string(spawn_counter_)
            + " from '" + spawner_id + "' -- scene_nodes "
            + std::to_string(nodes_before) + " -> "
            + std::to_string(document_.nodes().size()) + " (frame "
            + std::to_string(behavior_frame_index_) + ")");
        ctx_.logger.info(
            "spawn_prefab: grafted prefab as instance "
            + std::to_string(spawn_counter_));

        // Start the spawned subtree's auto_play AudioSources. The scene-load
        // auto-play pass already ran, before this instance existed, so without this
        // a spawned tank's engine grain cloud (auto_play) would never start.
        start_spawned_audio();
        return { true, root_authored_id };
    }

    void WozzitsApp_v1::drain_prefab_spawns(
        const std::vector<DeferredSpawnRequest>& spawn_requests)
    {
        // SPAWN_PREFAB drain (runtime prefab spawning): same frame-boundary as the
        // deferred-authoring edits, AFTER every read of behavior_scene_ this tick --
        // each spawn grafts nodes + rebuilds the behavior runtime out from under us, so
        // it is safe only now. The spawner is addressed by its stable authored id
        // (resolved during the command pass), so it stays valid even as a prior spawn
        // here rebuilds + renumbers the runtime. State preservation in
        // rebuild_behavior_scene leaves pre-existing bindings' state untouched.
        for (const DeferredSpawnRequest& request : spawn_requests) {
            spawn_prefab(
                request.spawner_id,
                request.name_hash,
                request.offset[0],
                request.offset[1],
                request.offset[2]);
        }
    }

    void WozzitsApp_v1::drain_identity_spawns()
    {
        // SPAWN-WITH-IDENTITY drain (#252 pooling): same frame boundary as the
        // fire-and-forget SPAWN_PREFAB drain, but each spawn's identity flows BACK to
        // the spawner via a SPAWN_COMPLETED (or _FAILED) event. Materialize each
        // (spawn_prefab rebuilds + renumbers the runtime), collecting the completion,
        // THEN dispatch all completions against the FINAL behavior_scene_ -- resolving
        // each spawner + spawned root from its stable authored id, so a prior spawn's
        // renumbering doesn't strand a live handle.
        // Swap the pending requests out first: a spawn below rebuilds the behavior
        // runtime, which fires self.start for the new subtree -- if one of those
        // submits, it must land in the (now-empty) buffer for NEXT frame, not re-enter
        // this drain. next_ticket stays on the member, so tickets remain monotonic
        // across frames.
        std::vector<wz::engine::behavior::BehaviorSpawnRequest> pending_spawns;
        pending_spawns.swap(spawn_identity_buffer_.requests);
        if (pending_spawns.empty()) {
            return;
        }
        struct SpawnCompletion
        {
            WzSpawnTicket ticket{};
            uint64_t request_tag = 0u;
            wz::scene::AuthoredEntityId spawner_id;
            wz::scene::AuthoredEntityId root_id;  // empty on failure
            WzSpawnStatus status = WZ_SPAWN_STATUS_NONE;
        };
        std::vector<SpawnCompletion> completions;
        completions.reserve(pending_spawns.size());
        for (const wz::engine::behavior::BehaviorSpawnRequest& request :
             pending_spawns)
        {
            const SpawnPrefabResult result = spawn_prefab(
                request.spawner_id,
                request.prefab_name_hash,
                request.offset[0],
                request.offset[1],
                request.offset[2],
                request.spawn_parked != 0u);
            completions.push_back(SpawnCompletion{
                .ticket = request.ticket,
                .request_tag = request.request_tag,
                .spawner_id = request.spawner_id,
                .root_id = result.ok
                    ? result.root_authored_id
                    : wz::scene::AuthoredEntityId{},
                .status = static_cast<WzSpawnStatus>(result.ok
                    ? WZ_SPAWN_STATUS_COMPLETED
                    : WZ_SPAWN_STATUS_FAILED),
            });
        }

        if (!behavior_scene_) {
            return;
        }
        wz::engine::behavior::BehaviorCommandBuffer spawn_commands;
        wz::engine::behavior::BehaviorFrameContext spawn_ctx{
            .scene = &*behavior_scene_,
            .behavior_state = &behavior_scene_->behavior_state,
            .commands = &spawn_commands,
            .spawn_requests = &spawn_identity_buffer_,
            .logger = &ctx_.logger,
        };
        for (const SpawnCompletion& completion : completions) {
            const auto spawner_it =
                behavior_scene_->authored_to_runtime.find(
                    completion.spawner_id);
            if (spawner_it
                == behavior_scene_->authored_to_runtime.end())
            {
                continue;  // spawner gone (removed) -- drop the completion
            }
            wz::scene::RuntimeEntityId root_entity =
                wz::scene::INVALID_RUNTIME_ENTITY;
            if (!completion.root_id.empty()) {
                const auto root_it =
                    behavior_scene_->authored_to_runtime.find(
                        completion.root_id);
                if (root_it
                    != behavior_scene_->authored_to_runtime.end())
                {
                    root_entity = root_it->second;
                }
            }
            WzSpawnEventPayload payload{};
            payload.ticket = completion.ticket;
            payload.status = completion.status;
            payload.request_tag = completion.request_tag;
            payload.root_entity = root_entity;
            payload.root_authored_id = completion.root_id.empty()
                ? nullptr
                : completion.root_id.c_str();
            wz::engine::behavior::dispatch_spawn_event(
                *behavior_scene_,
                registry_,
                spawn_ctx,
                spawner_it->second,
                payload);
        }
        // Apply the completion handlers' TRANSFORM commands same-frame (a completion
        // handler's primary job is free-list bookkeeping + an optional next-spawn via
        // the wired spawn_requests, both same-frame). NOTE: this pass runs AFTER the
        // frame's host-command drain, so host-handled kinds (visible/active/audio/
        // param) from a completion handler are NOT applied here -- a host-state reset
        // belongs on the manager's acquire, not the async completion (#252 audit).
        if (!spawn_commands.commands.empty()) {
            std::vector<wz::scene::RuntimeEntityId> spawn_changed;
            (void)wz::engine::behavior::apply_behavior_commands(
                *behavior_scene_,
                spawn_commands.commands,
                &spawn_changed);
        }
    }

}
