#include <engine/assets/scene/scene_fingerprint.h>

#include <iomanip>
#include <sstream>
#include <string_view>

namespace wz::engine::assets
{
    namespace
    {
        struct SceneFingerprintBuilder
        {
            uint64_t value = 14695981039346656037ull;

            void mix_byte(uint8_t byte)
            {
                value ^= byte;
                value *= 1099511628211ull;
            }

            void mix_bytes(const void* data, std::size_t size)
            {
                const auto* bytes = static_cast<const uint8_t*>(data);
                for (std::size_t i = 0; i < size; ++i) {
                    mix_byte(bytes[i]);
                }
            }

            void mix_string(std::string_view text)
            {
                mix_bytes(text.data(), text.size());
                mix_byte(0xffu);
            }

            template <typename T>
            void mix_value(const T& value_in)
            {
                mix_bytes(&value_in, sizeof(value_in));
            }
        };

        void mix_asset_key(
            SceneFingerprintBuilder& fp,
            const wz::asset::AssetKey& key)
        {
            fp.mix_value(key.content_hash.lo);
            fp.mix_value(key.content_hash.hi);
            fp.mix_value(key.schema_hash.lo);
            fp.mix_value(key.schema_hash.hi);
            fp.mix_value(key.compiler_hash.lo);
            fp.mix_value(key.compiler_hash.hi);
            fp.mix_value(key.deps_hash.lo);
            fp.mix_value(key.deps_hash.hi);
        }

        void mix_behavior(
            SceneFingerprintBuilder& fp,
            const SceneBehaviorAsset& behavior)
        {
            fp.mix_string(behavior.id);
            fp.mix_string(behavior.label);
            fp.mix_string(behavior.module);
            fp.mix_string(behavior.name);
            fp.mix_value(behavior.enabled);
            fp.mix_value(behavior.apply_in_editor);
            fp.mix_value(behavior.events.size());
            for (const auto& event : behavior.events) {
                fp.mix_string(event);
            }
            fp.mix_value(behavior.config.size());
            for (const auto& entry : behavior.config) {
                fp.mix_string(entry.key);
                fp.mix_value(entry.kind);
                switch (entry.kind) {
                case SceneBehaviorConfigValueKind::Bool:
                    fp.mix_value(entry.bool_value);
                    break;
                case SceneBehaviorConfigValueKind::Number:
                    fp.mix_value(entry.number_value);
                    break;
                case SceneBehaviorConfigValueKind::String:
                    fp.mix_string(entry.string_value);
                    break;
                }
            }
        }
    }

    uint64_t scene_asset_fingerprint(const SceneAssetData& scene)
    {
        SceneFingerprintBuilder fp{};
        fp.mix_string(scene.name);
        fp.mix_value(scene.nodes.size());
        fp.mix_value(scene.lights.size());

        for (const auto& node : scene.nodes) {
            fp.mix_string(node.id);
            fp.mix_string(node.name);
            fp.mix_value(node.visible);
            fp.mix_value(node.active);
            fp.mix_value(node.render_order);
            fp.mix_value(node.motion_type);

            const bool has_parent = node.parent_id.has_value();
            fp.mix_value(has_parent);
            if (node.parent_id) {
                fp.mix_string(*node.parent_id);
            }

            fp.mix_bytes(node.local.translation, sizeof(node.local.translation));
            fp.mix_bytes(node.local.rotation_quat, sizeof(node.local.rotation_quat));
            fp.mix_bytes(node.local.scale, sizeof(node.local.scale));

            const bool has_inline_renderable = node.renderable.has_value();
            const bool has_renderable_asset =
                node.renderable_asset_node_id.has_value();
            const bool has_geometry_binding =
                node.geometry_asset_node_id.has_value();
            const bool has_render_program =
                node.render_program_node_id.has_value();
            const bool has_scene_source =
                node.scene_source_node_id.has_value();
            const bool has_glb_scene_source =
                node.glb_scene_source.has_value();
            const bool has_render_to_texture =
                node.render_to_texture.has_value();
            const bool has_mesh_index = node.mesh_index.has_value();
            const bool has_asset_reference = node.asset_reference.has_value();
            const bool has_camera = node.camera.has_value();
            const bool has_direct_light_source =
                node.direct_light_source.has_value();
            const bool has_ambient_lighting =
                node.ambient_lighting.has_value();
            const bool has_hdri_environment =
                node.hdri_environment.has_value();
            const bool has_atmosphere = node.atmosphere.has_value();
            const bool has_environment = node.environment.has_value();
            const bool has_sky_visual = node.sky_visual.has_value();
            const bool has_sky_surface = node.sky_surface.has_value();
            const bool has_scene_import_source =
                node.scene_import_source.has_value();
            const bool has_imported_node = node.imported_node.has_value();
            const bool has_mesh_source = node.mesh_source.has_value();
            const bool has_mesh_processing =
                node.mesh_processing.has_value();
            const bool has_mesh_wavelet_analysis =
                node.mesh_wavelet_analysis.has_value();
            const bool has_mesh_compute_field =
                node.mesh_compute_field.has_value();
            const bool has_mesh_render_style =
                node.mesh_render_style.has_value();
            const bool has_mesh_mask_render_style =
                node.mesh_mask_render_style.has_value();
            const bool has_mesh_region_set =
                node.mesh_region_set.has_value();
            const bool has_mesh_sparse_operator_source =
                node.mesh_sparse_operator_source.has_value();
            const bool has_mesh_level_mask_source =
                node.mesh_level_mask_source.has_value();
            const bool has_scalar_field_source =
                node.scalar_field_source.has_value();
            const bool has_vector_field_source =
                node.vector_field_source.has_value();
            const bool has_collision = node.collision.has_value();
            const bool has_proximity = node.proximity.has_value();
            const bool has_motion = node.motion.has_value();
            const bool has_motion_filter = node.motion_filter.has_value();
            const bool has_terrain = node.terrain.has_value();
            const bool has_terrain_render_style =
                node.terrain_render_style.has_value();
            const bool has_terrain_mesh_source =
                node.terrain_mesh_source.has_value();
            const bool has_terrain_height_field_source =
                node.terrain_height_field_source.has_value();
            const bool has_behavior = node.behavior.has_value();
            const bool has_behaviors = !node.behaviors.empty();
            const bool has_compute_kernel = node.compute_kernel.has_value();
            const bool has_debug_visual = node.debug_visual.has_value();
            const bool has_editor_handle = node.editor_handle.has_value();
            fp.mix_value(has_inline_renderable);
            fp.mix_value(has_renderable_asset);
            fp.mix_value(has_geometry_binding);
            fp.mix_value(has_render_program);
            fp.mix_value(has_scene_source);
            fp.mix_value(has_glb_scene_source);
            fp.mix_value(has_render_to_texture);
            fp.mix_value(has_mesh_index);
            fp.mix_value(has_asset_reference);
            fp.mix_value(has_camera);
            fp.mix_value(has_direct_light_source);
            fp.mix_value(has_ambient_lighting);
            fp.mix_value(has_hdri_environment);
            fp.mix_value(has_atmosphere);
            fp.mix_value(has_environment);
            fp.mix_value(has_sky_visual);
            fp.mix_value(has_sky_surface);
            fp.mix_value(has_scene_import_source);
            fp.mix_value(has_imported_node);
            fp.mix_value(has_mesh_source);
            fp.mix_value(has_mesh_processing);
            fp.mix_value(has_mesh_wavelet_analysis);
            fp.mix_value(has_mesh_compute_field);
            fp.mix_value(has_mesh_render_style);
            fp.mix_value(has_mesh_mask_render_style);
            fp.mix_value(has_mesh_region_set);
            fp.mix_value(has_mesh_sparse_operator_source);
            fp.mix_value(has_mesh_level_mask_source);
            fp.mix_value(has_scalar_field_source);
            fp.mix_value(has_vector_field_source);
            fp.mix_value(has_collision);
            fp.mix_value(has_proximity);
            fp.mix_value(has_motion);
            fp.mix_value(has_motion_filter);
            fp.mix_value(has_terrain);
            fp.mix_value(has_terrain_render_style);
            fp.mix_value(has_terrain_mesh_source);
            fp.mix_value(has_terrain_height_field_source);
            fp.mix_value(has_behavior);
            fp.mix_value(has_behaviors);
            fp.mix_value(has_compute_kernel);
            fp.mix_value(has_debug_visual);
            fp.mix_value(has_editor_handle);

            if (node.renderable_asset_node_id) {
                fp.mix_value(*node.renderable_asset_node_id);
            }

            if (node.geometry_asset_node_id) {
                fp.mix_value(*node.geometry_asset_node_id);
            }

            // Indexed GLB-part geometry (issue #213 increment 3): the part name
            // changes the assembled mesh, so it must re-key the scene.
            if (node.geometry_glb_node_id) {
                fp.mix_string(*node.geometry_glb_node_id);
            }

            if (node.render_program_node_id) {
                fp.mix_value(*node.render_program_node_id);
            }

            // Custom-renderable ingredients (issue #229): both the semantic
            // bindings and the constant overrides are authored look data, so
            // either changing re-fingerprints the scene. Only the authored
            // intent is mixed (semantic + graph anchor, name + value) — the
            // resolved binding keys are bridge products, not authored state.
            fp.mix_value(static_cast<uint64_t>(node.renderable_bindings.size()));
            for (const auto& binding : node.renderable_bindings) {
                fp.mix_string(binding.semantic);
                fp.mix_value(binding.asset_graph_node_id.has_value());
                if (binding.asset_graph_node_id) {
                    fp.mix_value(*binding.asset_graph_node_id);
                }
            }
            fp.mix_value(
                static_cast<uint64_t>(node.renderable_constants.size()));
            for (const auto& constant : node.renderable_constants) {
                fp.mix_string(constant.name);
                fp.mix_bytes(constant.value, sizeof(constant.value));
            }

            if (node.scene_source_node_id) {
                fp.mix_value(*node.scene_source_node_id);
            }

            // GLB scene-source descriptor (issue #213): mix the persisted
            // authored intent (path + scene_index + consume_mode + the per-
            // component style mapping's persisted visual fields), so an edit to
            // any of them re-fingerprints the scene.
            if (node.glb_scene_source) {
                const auto& glb = *node.glb_scene_source;
                const auto mix_render_style =
                    [&fp](const MeshRenderStyleData& style) {
                        const auto mix_layer =
                            [&fp](const MeshRenderLayerStyle& layer) {
                                fp.mix_value(layer.enabled);
                                fp.mix_bytes(layer.color, sizeof(layer.color));
                                fp.mix_value(layer.emissive_strength);
                            };
                        mix_layer(style.wireframe);
                        mix_layer(style.surface);
                        fp.mix_value(style.alpha);
                        fp.mix_value(style.depth_test);
                        fp.mix_value(style.depth_write);
                        fp.mix_value(style.double_sided);
                        fp.mix_value(style.hidden_line_prepass);
                    };
                fp.mix_string(glb.path);
                fp.mix_value(glb.scene_index);
                fp.mix_value(glb.consume_mode);
                fp.mix_value(glb.base_style.has_value());
                if (glb.base_style) {
                    mix_render_style(*glb.base_style);
                }
                fp.mix_value(static_cast<uint64_t>(glb.style_overrides.size()));
                for (const auto& ov : glb.style_overrides) {
                    fp.mix_value(ov.mesh_index);
                    mix_render_style(ov.style);
                }
            }

            // Per-child authored-component overrides (issue #213): an edit re-keys
            // the scene so the graft re-applies the new override set.
            fp.mix_value(static_cast<uint64_t>(
                node.scene_source_child_overrides.size()));
            for (const auto& ov : node.scene_source_child_overrides) {
                fp.mix_string(ov.child_id);
                fp.mix_value(ov.render_program_node_id.has_value());
                if (ov.render_program_node_id) {
                    fp.mix_value(*ov.render_program_node_id);
                }
            }

            // Render-to-texture source (issue #287). The authored anchor is the
            // target node id; `target` is a bridge product re-resolved on every
            // bind, so it stays out for the same reason renderable_asset does.
            if (node.render_to_texture) {
                const auto& rtt = *node.render_to_texture;
                fp.mix_value(rtt.target_node_id.has_value());
                if (rtt.target_node_id) {
                    fp.mix_value(*rtt.target_node_id);
                }
                fp.mix_value(rtt.include_descendants);
                fp.mix_value(rtt.also_draw_in_scene);
                fp.mix_value(rtt.enabled);
            }

            if (node.mesh_index) {
                fp.mix_value(*node.mesh_index);
            }

            if (node.asset_reference) {
                mix_asset_key(fp, node.asset_reference->asset);
                fp.mix_string(node.asset_reference->stable_asset_id);
                fp.mix_value(node.asset_reference->expected_type);
                fp.mix_string(node.asset_reference->label);
            }

            if (node.renderable) {
                const auto& renderable = *node.renderable;
                fp.mix_value(renderable.node_class);
                fp.mix_value(renderable.mesh);
                fp.mix_value(renderable.material);
                fp.mix_bytes(&renderable.local_bounds.min,
                    sizeof(renderable.local_bounds.min));
                fp.mix_bytes(&renderable.local_bounds.max,
                    sizeof(renderable.local_bounds.max));
                fp.mix_value(renderable.visible);
            }

            if (node.camera) {
                const auto& camera = *node.camera;
                fp.mix_value(camera.fov_y);
                fp.mix_value(camera.near_plane);
                fp.mix_value(camera.far_plane);
                fp.mix_value(camera.aspect);
            }

            if (node.direct_light_source) {
                const auto& light = *node.direct_light_source;
                mix_asset_key(fp, light.light_asset);
                fp.mix_value(light.kind);
                fp.mix_bytes(light.color, sizeof(light.color));
                fp.mix_value(light.intensity);
                fp.mix_value(light.range);
                fp.mix_value(light.inner_cone_radians);
                fp.mix_value(light.outer_cone_radians);
            }

            if (node.ambient_lighting) {
                const auto& lighting = *node.ambient_lighting;
                mix_asset_key(fp, lighting.lighting_asset);
                fp.mix_value(lighting.mode);
                fp.mix_bytes(lighting.color, sizeof(lighting.color));
                fp.mix_value(lighting.intensity);
                mix_asset_key(fp, lighting.intensity_field);
                mix_asset_key(fp, lighting.color_field);
                fp.mix_value(lighting.domain_mapping);
            }

            if (node.hdri_environment) {
                const auto& environment = *node.hdri_environment;
                mix_asset_key(fp, environment.environment_asset);
                fp.mix_string(environment.path);
                fp.mix_value(environment.format);
                fp.mix_value(environment.exposure);
                fp.mix_value(environment.rotation_x_radians);
                fp.mix_value(environment.rotation_y_radians);
                fp.mix_value(environment.rotation_z_radians);
                fp.mix_value(environment.lighting_intensity);
                fp.mix_value(environment.reflection_intensity);
                fp.mix_value(environment.background_intensity);
                fp.mix_value(environment.lighting_sample_resolution);
                fp.mix_bytes(
                    environment.environment_light_color,
                    sizeof(environment.environment_light_color));
                fp.mix_value(environment.environment_light_intensity);
                fp.mix_bytes(
                    environment.dominant_light_direction,
                    sizeof(environment.dominant_light_direction));
                fp.mix_bytes(
                    environment.dominant_light_color,
                    sizeof(environment.dominant_light_color));
                fp.mix_value(environment.dominant_light_intensity);
                fp.mix_value(environment.dominant_light_confidence);
            }

            if (node.atmosphere) {
                const auto& atmosphere = *node.atmosphere;
                mix_asset_key(fp, atmosphere.atmosphere_asset);
                fp.mix_value(atmosphere.atmosphere_asset_node_id);
                fp.mix_value(atmosphere.enabled);
            }

            if (node.environment) {
                const auto& env = *node.environment;
                mix_asset_key(fp, env.environment_asset);
                fp.mix_value(env.environment_asset_node_id);
                fp.mix_value(env.enabled);
            }

            if (node.sky_visual) {
                const auto& visual = *node.sky_visual;
                fp.mix_value(visual.kind);
                fp.mix_bytes(visual.solid_color, sizeof(visual.solid_color));
                fp.mix_bytes(
                    visual.gradient_top_color,
                    sizeof(visual.gradient_top_color));
                fp.mix_bytes(
                    visual.gradient_bottom_color,
                    sizeof(visual.gradient_bottom_color));
                mix_asset_key(fp, visual.texture_asset);
                fp.mix_string(visual.texture_path);
                fp.mix_value(visual.texture_format);
                mix_asset_key(fp, visual.scalar_field_asset);
                fp.mix_string(visual.scalar_field_node);
                mix_asset_key(fp, visual.vector_field_asset);
                fp.mix_string(visual.vector_field_node);
                fp.mix_value(visual.exposure);
                fp.mix_value(visual.rotation_x_radians);
                fp.mix_value(visual.rotation_y_radians);
                fp.mix_value(visual.rotation_z_radians);
            }

            if (node.sky_surface) {
                const auto& surface = *node.sky_surface;
                fp.mix_string(surface.visual_node);
                fp.mix_value(surface.projection);
                fp.mix_value(surface.radius);
                fp.mix_value(surface.visible_to_camera);
            }

            if (node.scene_import_source) {
                const auto& source = *node.scene_import_source;
                fp.mix_value(source.kind);
                fp.mix_string(source.path);
                fp.mix_string(source.import_prefix);
                const bool has_scene_index = source.scene_index.has_value();
                fp.mix_value(has_scene_index);
                if (source.scene_index) {
                    fp.mix_value(*source.scene_index);
                }
            }

            if (node.imported_node) {
                const auto& imported = *node.imported_node;
                fp.mix_string(imported.anchor_node);
                fp.mix_string(imported.import_prefix);
                fp.mix_string(imported.source_node_id);
                fp.mix_value(imported.missing_source);
            }

            if (node.mesh_source) {
                const auto& source = *node.mesh_source;
                fp.mix_value(source.kind);
                fp.mix_string(source.path);
                fp.mix_value(source.mesh_index);
            }

            if (node.mesh_processing) {
                const auto& processing = *node.mesh_processing;
                fp.mix_value(processing.enabled);
                fp.mix_value(processing.operation);
                fp.mix_string(processing.region_set_ref);
                fp.mix_value(processing.target_vertex_count);
                fp.mix_value(processing.target_triangle_count);
                fp.mix_value(processing.target_ratio);
                fp.mix_value(processing.preserve_boundary);
                fp.mix_value(processing.aspect_ratio);
                fp.mix_value(processing.edge_length);
                fp.mix_value(processing.max_valence);
                fp.mix_value(processing.normal_deviation);
                fp.mix_value(processing.hausdorff_error);
                fp.mix_value(processing.preview_level_index);
                // These three are authored AND written to disk (they have no
                // *_node_id anchor, so the resolved-key exclusion does not apply),
                // yet were absent from the mix -- two mesh_processing components
                // differing only in a source/processed/hierarchy key hashed
                // identically. (A3-C1-F1, #77 visit 2)
                mix_asset_key(fp, processing.source_mesh_asset);
                mix_asset_key(fp, processing.processed_mesh_asset);
                mix_asset_key(fp, processing.hierarchy_asset);
            }

            if (node.mesh_wavelet_analysis) {
                const auto& analysis = *node.mesh_wavelet_analysis;
                fp.mix_value(analysis.enabled);
                fp.mix_value(analysis.function);
                fp.mix_value(analysis.scale_count);
                fp.mix_value(analysis.lambda_max_estimate);
                fp.mix_value(analysis.gamma);
            }

            if (node.mesh_compute_field) {
                const auto& field = *node.mesh_compute_field;
                fp.mix_value(field.enabled);
                fp.mix_string(field.kernel_id);
                fp.mix_string(field.hlsl_path);
                fp.mix_string(field.entry);
                fp.mix_string(field.target);
                fp.mix_value(field.thread_group_size_x);
                fp.mix_value(field.thread_group_size_y);
                fp.mix_value(field.thread_group_size_z);
                fp.mix_value(field.inputs.size());
                for (const MeshComputeInput input : field.inputs) {
                    fp.mix_value(input);
                }
                fp.mix_value(field.channels.size());
                for (const auto& channel : field.channels) {
                    fp.mix_value(channel.channel_id);
                    fp.mix_value(channel.value_type);
                }
                fp.mix_value(field.params.size());
                for (const uint32_t param : field.params) {
                    fp.mix_value(param);
                }
            }

            if (node.mesh_derived_field_source) {
                const auto& source = *node.mesh_derived_field_source;
                fp.mix_value(source.enabled);
                fp.mix_string(source.field_id);
                fp.mix_value(source.domain);
                fp.mix_value(source.channel_id);
                fp.mix_value(source.value_type);
                fp.mix_value(source.source_kind);
                fp.mix_value(source.component);
                fp.mix_value(source.normalize);
                fp.mix_value(source.constant_value);
            }

            // The operator the two consumers below reference by name. Its
            // resolved_operator_asset is a materialization product, so only the
            // authored recipe is mixed.
            if (node.mesh_sparse_operator_source) {
                const auto& source = *node.mesh_sparse_operator_source;
                fp.mix_value(source.enabled);
                fp.mix_string(source.operator_id);
                fp.mix_value(source.kind);
                fp.mix_value(source.domain);
                fp.mix_value(source.value_convention);
            }

            if (node.mesh_sparse_apply_field) {
                const auto& field = *node.mesh_sparse_apply_field;
                fp.mix_value(field.enabled);
                fp.mix_string(field.operator_ref);
                fp.mix_string(field.input_field_ref);
                fp.mix_value(field.input_channel_id);
                fp.mix_value(field.output_channel_id);
                fp.mix_value(field.apply_mode);
            }

            if (node.mesh_sparse_diffusion_bands) {
                const auto& bands = *node.mesh_sparse_diffusion_bands;
                fp.mix_value(bands.enabled);
                fp.mix_string(bands.operator_ref);
                fp.mix_string(bands.input_field_ref);
                fp.mix_value(bands.input_channel_id);
                fp.mix_value(bands.output_base_channel_id);
                fp.mix_value(bands.band_count);
                fp.mix_value(bands.iterations_per_band);
                fp.mix_value(bands.mode);
                fp.mix_value(bands.tau);
            }

            if (node.mesh_level_mask_source) {
                const auto& source = *node.mesh_level_mask_source;
                fp.mix_value(source.enabled);
                fp.mix_string(source.input_field_ref);
                fp.mix_string(source.output_field_id);
                fp.mix_value(source.domain);
                fp.mix_value(
                    static_cast<uint64_t>(source.regions.size()));
                for (const auto& region : source.regions) {
                    fp.mix_value(region.input_channel_id);
                    fp.mix_value(region.output_channel_id);
                    fp.mix_value(region.min_value);
                    fp.mix_value(region.max_value);
                }
            }

            if (node.mesh_render_style) {
                const auto& style = *node.mesh_render_style;
                mix_asset_key(fp, style.style_asset);
                const auto mix_layer =
                    [&fp](const SceneMeshRenderLayerAsset& layer) {
                        fp.mix_value(layer.enabled);
                        for (float channel : layer.color) {
                            fp.mix_value(channel);
                        }
                        fp.mix_value(layer.emissive_strength);
                };
                mix_layer(style.wireframe);
                mix_layer(style.surface);
                fp.mix_value(style.alpha);
                fp.mix_value(style.depth_test);
                fp.mix_value(style.depth_write);
                fp.mix_value(style.double_sided);
                fp.mix_value(style.hidden_line_prepass);
                fp.mix_value(style.field_visualization_enabled);
                fp.mix_value(style.field_visualization_channel_id);
                fp.mix_value(style.field_visualization_value_min);
                fp.mix_value(style.field_visualization_value_max);
                fp.mix_value(style.field_visualization_gamma);
                fp.mix_value(style.field_visualization_palette);
                fp.mix_string(style.field_visualization_field_ref);
                mix_asset_key(fp, style.field_visualization_asset);
            }

            if (node.mesh_mask_render_style) {
                const auto& style = *node.mesh_mask_render_style;
                const auto mix_layer =
                    [&fp](const SceneMeshRenderLayerAsset& layer) {
                        fp.mix_value(layer.enabled);
                        for (float channel : layer.color) {
                            fp.mix_value(channel);
                        }
                        fp.mix_value(layer.emissive_strength);
                };
                fp.mix_value(style.enabled);
                fp.mix_value(style.mesh_input);
                mix_layer(style.wireframe);
                fp.mix_value(style.mask.enabled);
                fp.mix_value(style.mask.domain);
                fp.mix_value(style.mask.projection_mode);
                fp.mix_value(style.mask.overlap_mode);
                for (float channel : style.mask.unmatched_color) {
                    fp.mix_value(channel);
                }
                fp.mix_value(style.mask.show_unmatched);
                fp.mix_value(style.mask.rules.size());
                for (const MeshMaskRule& rule : style.mask.rules) {
                    fp.mix_value(rule.enabled);
                    fp.mix_value(rule.input_channel_id);
                    fp.mix_value(rule.lo);
                    fp.mix_value(rule.hi);
                    for (float channel : rule.color) {
                        fp.mix_value(channel);
                    }
                    fp.mix_value(rule.priority);
                }
                fp.mix_string(style.source_field_ref);
                mix_asset_key(fp, style.source_field_asset);
            }

            if (node.mesh_region_set) {
                const auto& region_set = *node.mesh_region_set;
                fp.mix_value(region_set.enabled);
                fp.mix_string(region_set.region_set_id);
                fp.mix_value(region_set.intent);
                fp.mix_value(region_set.mesh_input);
                fp.mix_value(region_set.mask.enabled);
                fp.mix_value(region_set.mask.domain);
                fp.mix_value(region_set.mask.projection_mode);
                fp.mix_value(region_set.mask.overlap_mode);
                for (float channel : region_set.mask.unmatched_color) {
                    fp.mix_value(channel);
                }
                fp.mix_value(region_set.mask.show_unmatched);
                fp.mix_value(region_set.mask.rules.size());
                for (const MeshMaskRule& rule : region_set.mask.rules) {
                    fp.mix_value(rule.enabled);
                    fp.mix_value(rule.input_channel_id);
                    fp.mix_value(rule.lo);
                    fp.mix_value(rule.hi);
                    for (float channel : rule.color) {
                        fp.mix_value(channel);
                    }
                    fp.mix_value(rule.priority);
                }
                fp.mix_string(region_set.source_field_ref);
                mix_asset_key(fp, region_set.source_field_asset);
            }

            if (node.scalar_field_source) {
                const auto& source = *node.scalar_field_source;
                fp.mix_value(source.kind);
                mix_asset_key(fp, source.scalar_field_asset);
                fp.mix_string(source.path);
                fp.mix_value(source.width);
                fp.mix_value(source.height);
                fp.mix_value(source.depth);
                fp.mix_value(source.frequency);
                fp.mix_value(source.amplitude);
            }

            if (node.vector_field_source) {
                const auto& source = *node.vector_field_source;
                fp.mix_value(source.kind);
                mix_asset_key(fp, source.vector_field_asset);
                fp.mix_string(source.path);
                fp.mix_value(source.width);
                fp.mix_value(source.height);
                fp.mix_value(source.depth);
                fp.mix_value(source.components_per_channel);
                fp.mix_value(source.channels.size());
                for (const auto& channel : source.channels) {
                    fp.mix_string(channel.name);
                }
            }

            if (node.input_receiver) {
                fp.mix_string(node.input_receiver->input_map);
                fp.mix_value(node.input_receiver->log_input);
            }

            if (node.flying_camera_controller) {
                const auto& controller = *node.flying_camera_controller;
                fp.mix_value(controller.move_speed);
                fp.mix_value(controller.look_speed);
                fp.mix_value(controller.boost_multiplier);
                fp.mix_value(controller.roll_speed);
            }

            if (node.actor_movement_controller) {
                const auto& controller = *node.actor_movement_controller;
                fp.mix_value(controller.move_speed);
                fp.mix_value(controller.boost_multiplier);
                fp.mix_value(controller.movement_space);
            }

            if (node.ground_boundary) {
                const auto& boundary = *node.ground_boundary;
                fp.mix_bytes(boundary.min, sizeof(boundary.min));
                fp.mix_bytes(boundary.max, sizeof(boundary.max));
                fp.mix_value(boundary.constrain_vertical);
                fp.mix_value(boundary.enabled);
            }

            if (node.collision) {
                const auto& collision = *node.collision;
                mix_asset_key(fp, collision.collision_asset);
                fp.mix_value(collision.layer_mask);
                fp.mix_value(collision.collides_with_mask);
                fp.mix_value(collision.is_trigger);
                fp.mix_value(collision.enabled);
                fp.mix_value(collision.constrain_movement);
                fp.mix_value(collision.collision_asset_node_id.has_value());
                if (collision.collision_asset_node_id) {
                    fp.mix_value(*collision.collision_asset_node_id);
                }
                fp.mix_value(collision.height_field_source.has_value());
                if (collision.height_field_source) {
                    const auto& source = *collision.height_field_source;
                    mix_asset_key(fp, source.scalar_field_asset);
                    fp.mix_value(source.origin[0]);
                    fp.mix_value(source.origin[1]);
                    fp.mix_value(source.size[0]);
                    fp.mix_value(source.size[1]);
                    fp.mix_value(source.vertical_scale);
                    fp.mix_value(source.base_height);
                    fp.mix_value(source.projection_resolution_x);
                    fp.mix_value(source.projection_resolution_y);
                }
            }

            if (node.proximity) {
                const auto& proximity = *node.proximity;
                fp.mix_value(proximity.radius);
                fp.mix_value(proximity.layer_mask);
                fp.mix_value(proximity.detects_with_mask);
                fp.mix_value(proximity.enabled);
            }

            if (node.motion) {
                const auto& motion = *node.motion;
                // linear_velocity / angular_velocity / space are runtime state,
                // no longer read from or written to disk (#312), so they are not
                // part of authored identity and are excluded here. This is the
                // header's completeness contract working as intended: the rule is
                // every AUTHORED member, and these stopped being authored.
                fp.mix_value(motion.terrain_constrained);
                fp.mix_value(motion.terrain_ride_height);
                fp.mix_value(motion.terrain_footprint_radius);
                fp.mix_value(motion.terrain_align_to_surface);
                fp.mix_value(motion.terrain_alignment_strength);
                fp.mix_value(motion.enabled);
            }

            if (node.motion_filter) {
                const auto& filter = *node.motion_filter;
                fp.mix_bytes(
                    filter.translation_smoothing,
                    sizeof(filter.translation_smoothing));
                fp.mix_value(filter.terrain_floor);
                fp.mix_value(filter.terrain_floor_offset);
                for (const SceneMotionFilterRotationAxis& axis :
                     { filter.roll, filter.pitch, filter.yaw })
                {
                    fp.mix_value(axis.smoothing_time);
                    fp.mix_value(axis.level);
                    fp.mix_value(axis.limit);
                    fp.mix_value(axis.limit_min_degrees);
                    fp.mix_value(axis.limit_max_degrees);
                }
                fp.mix_value(filter.enabled);
            }

            if (node.terrain) {
                const auto& terrain = *node.terrain;
                mix_asset_key(fp, terrain.terrain_asset);
                mix_asset_key(fp, terrain.visual_proxy_asset);
                mix_asset_key(fp, terrain.constraint_surface_asset);
                fp.mix_value(terrain.calculate_constraint_surface);
                fp.mix_value(terrain.visible);
                fp.mix_value(terrain.queryable);
                fp.mix_value(terrain.constrain_movement);
            }

            if (node.terrain_render_style) {
                const auto& style = *node.terrain_render_style;
                fp.mix_value(style.path);
                fp.mix_value(style.depth_test);
                fp.mix_value(style.depth_write);
                fp.mix_value(style.lighting_source);
                fp.mix_string(style.directional_light_node);
                fp.mix_string(style.ambient_light_node);
                fp.mix_string(style.environment_node);
                fp.mix_value(style.ambient_strength);
                fp.mix_value(style.sky_visibility_strength);
                fp.mix_value(style.normal_lighting_strength);
                fp.mix_value(style.terrain_bounce_strength);
                fp.mix_value(style.target_pixels_per_triangle);
                fp.mix_value(style.enable_surfel_lods);
                fp.mix_value(style.surfel_target_coverage_px);
                fp.mix_value(style.max_asset_triangle_density);
                fp.mix_value(style.max_screen_triangle_density);
                fp.mix_value(style.visual_chunk_count);
            }

            if (node.terrain_mesh_source) {
                const auto& source = *node.terrain_mesh_source;
                fp.mix_value(source.mode);
                fp.mix_string(source.source_node);
                mix_asset_key(fp, source.mesh_asset);
                fp.mix_value(source.height_policy);
                fp.mix_value(source.min_surface_normal_y);
                fp.mix_value(source.include_backfaces);
            }

            if (node.terrain_height_field_source) {
                const auto& source = *node.terrain_height_field_source;
                fp.mix_value(source.mode);
                fp.mix_string(source.source_node);
                mix_asset_key(fp, source.scalar_field_asset);
                fp.mix_bytes(source.origin, sizeof(source.origin));
                fp.mix_bytes(source.size, sizeof(source.size));
                fp.mix_value(source.vertical_scale);
                fp.mix_value(source.base_height);
            }

            if (node.audio_listener) {
                fp.mix_value(node.audio_listener->active);
            }

            if (node.audio_source) {
                // Authored identity is the stable node id when present; the
                // resolved key is transient (re-derived at bind), so it only
                // contributes when no node id anchors the reference.
                const bool has_node_id =
                    node.audio_source->audio_renderable_node_id.has_value();
                fp.mix_value(has_node_id);
                if (has_node_id) {
                    fp.mix_value(*node.audio_source->audio_renderable_node_id);
                }
                else {
                    mix_asset_key(fp, node.audio_source->audio_renderable);
                }
                fp.mix_value(node.audio_source->auto_play);
                fp.mix_value(node.audio_source->enabled);
            }

            if (node.event_listener) {
                fp.mix_value(node.event_listener->channels.size());
                for (const auto& channel : node.event_listener->channels) {
                    fp.mix_string(channel);
                }
            }
            if (node.event_trigger) {
                fp.mix_string(node.event_trigger->event);
            }

            if (node.behavior) {
                mix_behavior(fp, *node.behavior);
            }
            fp.mix_value(node.behaviors.size());
            for (const auto& behavior : node.behaviors) {
                mix_behavior(fp, behavior);
            }

            if (node.compute_kernel) {
                const auto& kernel = *node.compute_kernel;
                fp.mix_string(kernel.kernel_id);
                fp.mix_string(kernel.hlsl_path);
                fp.mix_string(kernel.entry);
                fp.mix_string(kernel.target);
                fp.mix_value(kernel.thread_group_size_x);
                fp.mix_value(kernel.thread_group_size_y);
                fp.mix_value(kernel.thread_group_size_z);
                fp.mix_value(kernel.ports.size());
                for (const auto& port : kernel.ports) {
                    fp.mix_string(port.name);
                    fp.mix_value(port.kind);
                    fp.mix_value(port.direction);
                    fp.mix_value(port.binding_kind);
                    fp.mix_value(port.shader_register);
                    fp.mix_value(port.register_space);
                    fp.mix_value(port.stride_bytes);
                    fp.mix_value(port.root_constant_offset);
                    fp.mix_value(port.root_constant_dwords);
                }
            }

            if (node.debug_visual) {
                const auto& visual = *node.debug_visual;
                fp.mix_value(visual.kind);
                fp.mix_value(visual.scale);
                fp.mix_value(visual.visible);
            }

            if (node.editor_handle) {
                const auto& handle = *node.editor_handle;
                fp.mix_value(handle.kind);
                fp.mix_value(handle.enabled);
                fp.mix_value(handle.visible);
                fp.mix_value(handle.size);
            }
        }

        for (const auto& light : scene.lights) {
            fp.mix_string(light.node_id);
            fp.mix_value(light.light.position.x);
            fp.mix_value(light.light.position.y);
            fp.mix_value(light.light.position.z);
            fp.mix_value(light.light.direction.x);
            fp.mix_value(light.light.direction.y);
            fp.mix_value(light.light.direction.z);
            fp.mix_value(light.light.color.x);
            fp.mix_value(light.light.color.y);
            fp.mix_value(light.light.color.z);
            fp.mix_value(light.light.type);
            fp.mix_value(light.light.intensity);
            fp.mix_value(light.light.range);
        }

        // Sky draws are derived from the sky_surface/sky_visual node pair at
        // materialize time, but they are the form the renderer consumes, so a
        // scene that resolved a different set is a different scene.
        fp.mix_value(scene.sky_draws.size());
        for (const auto& sky : scene.sky_draws) {
            fp.mix_string(sky.surface_node);
            fp.mix_string(sky.visual_node);
            fp.mix_value(sky.visual_kind);
            fp.mix_value(sky.projection);
            fp.mix_value(sky.radius);
            fp.mix_value(sky.visible_to_camera);
            fp.mix_bytes(sky.solid_color, sizeof(sky.solid_color));
            fp.mix_bytes(
                sky.gradient_top_color, sizeof(sky.gradient_top_color));
            fp.mix_bytes(
                sky.gradient_bottom_color, sizeof(sky.gradient_bottom_color));
            mix_asset_key(fp, sky.texture_asset);
            fp.mix_string(sky.texture_path);
            fp.mix_value(sky.texture_format);
            mix_asset_key(fp, sky.scalar_field_asset);
            mix_asset_key(fp, sky.vector_field_asset);
            fp.mix_value(sky.exposure);
            fp.mix_value(sky.rotation_x_radians);
            fp.mix_value(sky.rotation_y_radians);
            fp.mix_value(sky.rotation_z_radians);
        }

        const bool has_active_camera =
            scene.defaults.active_camera_node.has_value();
        fp.mix_value(has_active_camera);
        if (scene.defaults.active_camera_node) {
            fp.mix_string(*scene.defaults.active_camera_node);
        }

        return fp.value;
    }

    std::string scene_asset_fingerprint_string(const SceneAssetData& scene)
    {
        std::ostringstream out;
        out << "0x"
            << std::hex << std::setw(16) << std::setfill('0')
            << scene_asset_fingerprint(scene);
        return out.str();
    }
}
