// src/engine/assets/scene/scene_compilers.cpp

#include <engine/assets/scene/scene_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/json/json.h>

#include <scene/compile/scene_node_class.h>
#include <scene/compile/compiled_scene.h>

#include <external/json/json_document.h>
#include <external/json/json_read_helpers.h>

#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace wz::engine::assets::internal
{
    namespace
    {
        using wz::json::find_member;
        using wz::json::read_string;
        using wz::json::read_number;
        using wz::json::read_bool;
        using wz::json::read_float3;
        using wz::json::read_float4;

        AuthoredTransform parse_transform(const wz::json::JSONValue& obj)
        {
            AuthoredTransform t{};
            read_float3(obj, "translation", t.translation);
            read_float4(obj, "rotation_quat", t.rotation_quat);
            read_float3(obj, "scale", t.scale);
            return t;
        }

        wz::scene::SceneNodeClass parse_node_class_for_pipeline(
            std::string_view pipeline)
        {
            namespace sc = wz::scene;

            if (pipeline == "OpaqueGeometry") {
                return {
                    .role = sc::SceneRole::Renderable,
                    .producer = sc::ProducerKind::Mesh,
                    .default_surface = sc::SurfaceClass::Opaque,
                    .spatial = sc::SpatialKind::MeshBounds,
                    .compile = sc::CompileBehavior::Static,
                    .domains = sc::RenderDomain::Surface | sc::RenderDomain::Shadow,
                };
            }
            if (pipeline == "TransparentGeometry") {
                return {
                    .role = sc::SceneRole::Renderable,
                    .producer = sc::ProducerKind::Mesh,
                    .default_surface = sc::SurfaceClass::Transparent,
                    .spatial = sc::SpatialKind::MeshBounds,
                    .compile = sc::CompileBehavior::Static,
                    .domains = sc::RenderDomain::Surface | sc::RenderDomain::Transparent,
                };
            }
            return {};
        }

        std::optional<uint64_t> parse_hex_u64(std::string_view text)
        {
            uint64_t value = 0;
            const char* first = text.data();
            const char* last = text.data() + text.size();
            const auto result = std::from_chars(first, last, value, 16);
            if (result.ec != std::errc{} || result.ptr != last) {
                return std::nullopt;
            }
            return value;
        }

        std::optional<wz::asset::AssetKey> parse_asset_key_string(
            std::string_view text)
        {
            // Transitional concrete AssetKey syntax for renderable.asset.
            // Symbolic asset://renderables/... lookup belongs to a later
            // registry/naming layer, not the scene JSON compiler.
            constexpr std::string_view kPrefix = "asset-key:";
            if (!text.starts_with(kPrefix)) {
                return std::nullopt;
            }

            text.remove_prefix(kPrefix.size());

            std::array<uint64_t, 8> parts{};
            for (std::size_t i = 0; i < parts.size(); ++i) {
                const std::size_t end = text.find(':');
                const std::string_view part =
                    end == std::string_view::npos
                        ? text
                        : text.substr(0, end);

                auto value = parse_hex_u64(part);
                if (!value) {
                    return std::nullopt;
                }
                parts[i] = *value;

                if (i + 1 == parts.size()) {
                    if (end != std::string_view::npos) {
                        return std::nullopt;
                    }
                }
                else {
                    if (end == std::string_view::npos) {
                        return std::nullopt;
                    }
                    text.remove_prefix(end + 1);
                }
            }

            return wz::asset::AssetKey{
                .content_hash = { parts[0], parts[1] },
                .schema_hash = { parts[2], parts[3] },
                .compiler_hash = { parts[4], parts[5] },
                .deps_hash = { parts[6], parts[7] },
            };
        }

        std::optional<SceneRenderableBinding> parse_debug_renderable(
            const wz::json::JSONValue& obj)
        {
            const auto* dr = find_member(obj, "debug_renderable");
            if (!dr || dr->kind != wz::json::JSONValueKind::Object)
                return std::nullopt;

            SceneRenderableBinding binding{};

            auto pipeline = read_string(*dr, "pipeline");
            if (pipeline) {
                binding.node_class = parse_node_class_for_pipeline(*pipeline);
            }

            auto mesh_val = read_number(*dr, "mesh");
            if (mesh_val)
                binding.mesh = static_cast<wz::scene::MeshHandle>(
                    static_cast<uint32_t>(*mesh_val));

            auto mat_val = read_number(*dr, "material");
            if (mat_val)
                binding.material = static_cast<wz::scene::MaterialHandle>(
                    static_cast<uint32_t>(*mat_val));

            const auto* bounds = find_member(*dr, "bounds");
            if (bounds && bounds->kind == wz::json::JSONValueKind::Object) {
                float mn[3]{}, mx[3]{};
                if (read_float3(*bounds, "min", mn) &&
                    read_float3(*bounds, "max", mx)) {
                    binding.local_bounds = wz::scene::AABB{
                        .min = { mn[0], mn[1], mn[2] },
                        .max = { mx[0], mx[1], mx[2] },
                    };
                }
            }

            auto vis = read_bool(*dr, "visible");
            if (vis) binding.visible = *vis;

            return binding;
        }

        bool parse_renderable_asset_reference(
            const wz::json::JSONValue& obj,
            const std::string& node_id,
            wz::Logger& logger,
            std::optional<wz::asset::AssetKey>& out)
        {
            const auto* renderable = find_member(obj, "renderable");
            if (!renderable) {
                return true;
            }
            if (renderable->kind != wz::json::JSONValueKind::Object) {
                logger.error("renderable on node '" + node_id
                    + "' is not an object");
                return false;
            }

            auto asset = read_string(*renderable, "asset");
            if (!asset || asset->empty()) {
                logger.error("renderable on node '" + node_id
                    + "' missing 'asset'");
                return false;
            }

            auto key = parse_asset_key_string(*asset);
            if (!key) {
                logger.error("renderable.asset on node '" + node_id
                    + "' is not a serialized AssetKey");
                return false;
            }
            if (*key == wz::asset::AssetKey{}) {
                logger.error("renderable.asset on node '" + node_id
                    + "' is empty");
                return false;
            }

            out = *key;
            return true;
        }

        std::optional<SceneNodeAsset> parse_node(
            const wz::json::JSONValue& node_val,
            wz::Logger& logger)
        {
            if (node_val.kind != wz::json::JSONValueKind::Object)
                return std::nullopt;

            SceneNodeAsset node{};

            auto id = read_string(node_val, "id");
            if (!id) {
                logger.error("scene node missing 'id' field");
                return std::nullopt;
            }
            node.id = *id;
            node.name = read_string(node_val, "name").value_or(*id);

            const auto* parent_v = find_member(node_val, "parent");
            if (parent_v && parent_v->kind == wz::json::JSONValueKind::String) {
                node.parent_id = parent_v->string_value;
            }

            const auto* transform = find_member(node_val, "transform");
            if (transform && transform->kind == wz::json::JSONValueKind::Object) {
                node.local = parse_transform(*transform);
            }

            auto vis = read_bool(node_val, "visible");
            if (vis) node.visible = *vis;

            auto motion = read_string(node_val, "motion_type");
            if (motion && *motion == "Animated") {
                node.motion_type = wz::scene::TransformNode::MotionType::Animated;
            }

            node.renderable = parse_debug_renderable(node_val);
            if (!parse_renderable_asset_reference(
                    node_val,
                    node.id,
                    logger,
                    node.renderable_asset))
            {
                return std::nullopt;
            }

            const auto* cam = find_member(node_val, "camera");
            if (cam && cam->kind == wz::json::JSONValueKind::Object) {
                SceneCameraAsset camera{};
                auto fov = read_number(*cam, "fov_y");
                if (fov) camera.fov_y = static_cast<float>(*fov);
                auto near_p = read_number(*cam, "near");
                if (near_p) camera.near_plane = static_cast<float>(*near_p);
                auto far_p = read_number(*cam, "far");
                if (far_p) camera.far_plane = static_cast<float>(*far_p);
                auto asp = read_number(*cam, "aspect");
                if (asp) camera.aspect = static_cast<float>(*asp);
                node.camera = camera;
            }

            // ── Non-render component descriptors ──────────────────────

            const auto* ir = find_member(node_val, "input_receiver");
            if (ir && ir->kind == wz::json::JSONValueKind::Object) {
                auto map_uri = read_string(*ir, "input_map");
                if (!map_uri || map_uri->empty()) {
                    logger.error("input_receiver on node '" + node.id
                        + "' missing 'input_map'");
                    return std::nullopt;
                }
                auto log_input = read_bool(*ir, "log_input");
                node.input_receiver = SceneInputReceiverAsset{
                    .input_map = std::string(*map_uri),
                    .log_input = log_input.value_or(false),
                };
            }

            const auto* fcc = find_member(node_val, "flying_camera_controller");
            if (fcc && fcc->kind == wz::json::JSONValueKind::Object) {
                SceneFlyingCameraControllerAsset ctrl{};
                auto ms = read_number(*fcc, "move_speed");
                if (ms) ctrl.move_speed = static_cast<float>(*ms);
                auto ls = read_number(*fcc, "look_speed");
                if (ls) ctrl.look_speed = static_cast<float>(*ls);
                auto bm = read_number(*fcc, "boost_multiplier");
                if (bm) ctrl.boost_multiplier = static_cast<float>(*bm);
                auto rs = read_number(*fcc, "roll_speed");
                if (rs) ctrl.roll_speed = static_cast<float>(*rs);

                if (ctrl.move_speed < 0.0f || ctrl.look_speed < 0.0f
                    || ctrl.boost_multiplier < 0.0f || ctrl.roll_speed < 0.0f)
                {
                    logger.error("flying_camera_controller on node '"
                        + node.id + "' has negative speed value");
                    return std::nullopt;
                }

                node.flying_camera_controller = ctrl;
            }

            const auto* al = find_member(node_val, "audio_listener");
            if (al && al->kind == wz::json::JSONValueKind::Object) {
                SceneAudioListenerAsset listener{};
                auto active = read_bool(*al, "active");
                if (active) listener.active = *active;
                node.audio_listener = listener;
            }

            const auto* el = find_member(node_val, "event_listener");
            if (el && el->kind == wz::json::JSONValueKind::Object) {
                const auto* channels = find_member(*el, "channels");
                if (channels
                    && channels->kind == wz::json::JSONValueKind::Array)
                {
                    SceneEventListenerAsset evt{};
                    for (const auto& ch : channels->array_values) {
                        if (ch->kind == wz::json::JSONValueKind::String
                            && !ch->string_value.empty())
                        {
                            evt.channels.push_back(ch->string_value);
                        }
                    }
                    if (evt.channels.empty()) {
                        logger.error("event_listener on node '" + node.id
                            + "' has no valid channel names");
                        return std::nullopt;
                    }
                    node.event_listener = evt;
                }
            }

            // ── Debug/editor visual descriptor ───────────────────────

            const auto* dv = find_member(node_val, "auxiliary_visual");
            const char* visual_field = "auxiliary_visual";
            if (!dv) {
                dv = find_member(node_val, "debug_visual");
                visual_field = "debug_visual";
            }
            if (dv && dv->kind == wz::json::JSONValueKind::Object) {
                auto kind_str = read_string(*dv, "kind");
                if (!kind_str) {
                    logger.error(std::string(visual_field) + " on node '"
                        + node.id + "' missing 'kind'");
                    return std::nullopt;
                }

                SceneAuxiliaryVisualAsset dbg{};

                if (*kind_str == "axes") {
                    dbg.kind = SceneAuxiliaryVisualKind::Axes;
                }
                else {
                    logger.error(std::string(visual_field) + " on node '"
                        + node.id + "' has unknown kind '"
                        + std::string(*kind_str) + "'");
                    return std::nullopt;
                }

                auto scale = read_number(*dv, "scale");
                if (scale) {
                    float s = static_cast<float>(*scale);
                    if (s < 0.0f || !std::isfinite(s)) {
                        logger.error(std::string(visual_field) + " on node '"
                            + node.id + "' has invalid scale");
                        return std::nullopt;
                    }
                    dbg.scale = s;
                }

                auto vis = read_bool(*dv, "visible");
                if (vis) dbg.visible = *vis;

                node.debug_visual = dbg;
            }

            const auto* eh = find_member(node_val, "editor_handle");
            if (eh && eh->kind == wz::json::JSONValueKind::Object) {
                auto kind_str = read_string(*eh, "kind");
                if (!kind_str) {
                    logger.error("editor_handle on node '" + node.id
                        + "' missing 'kind'");
                    return std::nullopt;
                }

                SceneEditorHandleAsset handle{};

                if (*kind_str == "translate") {
                    handle.kind = SceneEditorHandleKind::Translate;
                }
                else if (*kind_str == "rotate") {
                    handle.kind = SceneEditorHandleKind::Rotate;
                }
                else if (*kind_str == "scale") {
                    handle.kind = SceneEditorHandleKind::Scale;
                }
                else if (*kind_str == "transform") {
                    handle.kind = SceneEditorHandleKind::Transform;
                }
                else {
                    logger.error("editor_handle on node '" + node.id
                        + "' has unknown kind '" + std::string(*kind_str) + "'");
                    return std::nullopt;
                }

                auto enabled = read_bool(*eh, "enabled");
                if (enabled) handle.enabled = *enabled;

                auto visible = read_bool(*eh, "visible");
                if (visible) handle.visible = *visible;

                auto size = read_number(*eh, "size");
                if (size) {
                    float s = static_cast<float>(*size);
                    if (s < 0.0f || !std::isfinite(s)) {
                        logger.error("editor_handle on node '" + node.id
                            + "' has invalid size");
                        return std::nullopt;
                    }
                    handle.size = s;
                }

                node.editor_handle = handle;
            }

            return node;
        }

        std::optional<SceneAssetData> parse_scene_json(
            const wz::json::JSONDocument& doc,
            wz::Logger& logger)
        {
            if (!doc.root || doc.root->kind != wz::json::JSONValueKind::Object) {
                logger.error("scene JSON root is not an object");
                return std::nullopt;
            }

            const auto& root = *doc.root;

            auto schema = read_string(root, "schema");
            if (!schema || *schema != "wozzits.scene.v0") {
                logger.error("scene JSON missing or unrecognized 'schema' field");
                return std::nullopt;
            }

            SceneAssetData scene{};
            scene.name = read_string(root, "name").value_or("unnamed_scene");

            const auto* nodes = find_member(root, "nodes");
            if (!nodes || nodes->kind != wz::json::JSONValueKind::Array) {
                logger.error("scene JSON missing 'nodes' array");
                return std::nullopt;
            }

            for (const auto& nv : nodes->array_values) {
                auto node = parse_node(*nv, logger);
                if (!node) return std::nullopt;
                scene.nodes.push_back(std::move(*node));
            }

            const auto* lights = find_member(root, "lights");
            if (lights && lights->kind == wz::json::JSONValueKind::Array) {
                for (const auto& lv : lights->array_values) {
                    if (lv->kind != wz::json::JSONValueKind::Object)
                        continue;
                    SceneLightAsset light{};
                    light.node_id = read_string(*lv, "node_id").value_or("");
                    auto* lr = find_member(*lv, "light");
                    if (lr && lr->kind == wz::json::JSONValueKind::Object) {
                        float pos[3]{}, dir[3]{}, col[3]{ 1.f, 1.f, 1.f };
                        read_float3(*lr, "position", pos);
                        read_float3(*lr, "direction", dir);
                        read_float3(*lr, "color", col);
                        light.light.position = { pos[0], pos[1], pos[2] };
                        light.light.direction = { dir[0], dir[1], dir[2] };
                        light.light.color = { col[0], col[1], col[2] };
                        auto intens = read_number(*lr, "intensity");
                        if (intens) light.light.intensity = static_cast<float>(*intens);
                        auto range = read_number(*lr, "range");
                        if (range) light.light.range = static_cast<float>(*range);
                    }
                    scene.lights.push_back(std::move(light));
                }
            }

            const auto* defaults = find_member(root, "defaults");
            if (defaults && defaults->kind == wz::json::JSONValueKind::Object) {
                auto cam = read_string(*defaults, "active_camera");
                if (cam) scene.defaults.active_camera_node = std::string(*cam);
            }

            return scene;
        }
    }

    void register_scene_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        JSONTable& json_table,
        SceneAssetTable& scene_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kSceneFromJSONSchema,
            .output_type = kAssetTypeScene,
            .compile = [&logger, &json_table, &scene_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                if (dep_handles.empty()) {
                    logger.error("scene node has no JSON document dependency");
                    return compile_failed_node(input);
                }

                const JSONData* json_data = json_table.get(dep_handles[0]);

                if (!json_data) {
                    logger.error("scene JSON document dependency is invalid");
                    return compile_failed_node(input);
                }

                auto scene = parse_scene_json(json_data->document, logger);
                if (!scene) {
                    return compile_failed_node(input);
                }

                wz::asset::ResourceHandle handle =
                    scene_table.add(std::move(*scene));

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }

} // namespace wz::engine::assets::internal
