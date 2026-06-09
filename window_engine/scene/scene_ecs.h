#pragma once

// wz/scene/scene_ecs.h
//
// Vocabulary for the authored scene ECS boundary.
//
// This is intentionally not a generic ECS framework. It names the scene-layer
// concepts that authored scene data and runtime scene projection share:
//
//   authored scene source -> runtime scene instance -> scene-render compile path
//
// The generic asset system remains a resource DAG. Scene/entity/component
// composition belongs to the scene model.

#include <graph/static_polytree.h>

#include <cstdint>
#include <string>

namespace wz::scene
{
    // Stable authored entity identity in serialized/source scene data.
    using AuthoredEntityId = std::string;

    // Runtime entity identity after authored data has compiled into SceneStorage.
    using RuntimeEntityId = wz::core::graph::NodeHandle;

    inline constexpr RuntimeEntityId INVALID_RUNTIME_ENTITY =
        wz::core::graph::INVALID_NODE;

    enum class SceneComponentDomain : uint8_t
    {
        CoreNode,
        RuntimeRelevant,
        EditorOnly,
        Exportable,
        EditorAuthoring,
    };

    enum class SceneAuthoredComponentKind : uint8_t
    {
        Transform,
        Visibility,
        MotionType,
        ParentLink,
        Renderable,
        SceneImportSource,
        MeshSource,
        MeshWaveletAnalysis,
        MeshRenderStyle,
        ScalarFieldSource,
        VectorFieldSource,
        Camera,
        Light,
        AmbientLighting,
        HDRIEnvironment,
        SkyVisual,
        SkySurface,
        InputReceiver,
        FlyingCameraController,
        ActorMovementController,
        GroundBoundary,
        Collision,
        Terrain,
        TerrainRenderStyle,
        TerrainMeshSource,
        TerrainHeightFieldSource,
        AudioListener,
        EventListener,
        EventTrigger,
        AuxiliaryVisual,
        EditorHandle,
        Proximity,
        Motion,
        Behavior,
        ComputeKernel,
    };

    constexpr SceneComponentDomain scene_component_domain(
        SceneAuthoredComponentKind kind) noexcept
    {
        switch (kind) {
        case SceneAuthoredComponentKind::Transform:
        case SceneAuthoredComponentKind::Visibility:
        case SceneAuthoredComponentKind::MotionType:
        case SceneAuthoredComponentKind::ParentLink:
            return SceneComponentDomain::CoreNode;

        case SceneAuthoredComponentKind::Renderable:
        case SceneAuthoredComponentKind::Camera:
        case SceneAuthoredComponentKind::Light:
        case SceneAuthoredComponentKind::AmbientLighting:
        case SceneAuthoredComponentKind::HDRIEnvironment:
        case SceneAuthoredComponentKind::SkyVisual:
        case SceneAuthoredComponentKind::SkySurface:
        case SceneAuthoredComponentKind::AuxiliaryVisual:
        case SceneAuthoredComponentKind::ComputeKernel:
            return SceneComponentDomain::Exportable;

        case SceneAuthoredComponentKind::SceneImportSource:
        case SceneAuthoredComponentKind::MeshSource:
        case SceneAuthoredComponentKind::MeshWaveletAnalysis:
        case SceneAuthoredComponentKind::MeshRenderStyle:
        case SceneAuthoredComponentKind::ScalarFieldSource:
        case SceneAuthoredComponentKind::VectorFieldSource:
        case SceneAuthoredComponentKind::TerrainRenderStyle:
        case SceneAuthoredComponentKind::TerrainMeshSource:
        case SceneAuthoredComponentKind::TerrainHeightFieldSource:
        case SceneAuthoredComponentKind::EventTrigger:
            return SceneComponentDomain::EditorAuthoring;

        case SceneAuthoredComponentKind::InputReceiver:
        case SceneAuthoredComponentKind::FlyingCameraController:
        case SceneAuthoredComponentKind::ActorMovementController:
        case SceneAuthoredComponentKind::GroundBoundary:
        case SceneAuthoredComponentKind::Collision:
        case SceneAuthoredComponentKind::Terrain:
        case SceneAuthoredComponentKind::AudioListener:
        case SceneAuthoredComponentKind::EventListener:
        case SceneAuthoredComponentKind::Proximity:
        case SceneAuthoredComponentKind::Motion:
        case SceneAuthoredComponentKind::Behavior:
            return SceneComponentDomain::RuntimeRelevant;

        case SceneAuthoredComponentKind::EditorHandle:
            return SceneComponentDomain::EditorOnly;
        }

        return SceneComponentDomain::RuntimeRelevant;
    }

    constexpr bool is_core_node_component(
        SceneAuthoredComponentKind kind) noexcept
    {
        return scene_component_domain(kind) == SceneComponentDomain::CoreNode;
    }

    constexpr bool is_editor_only_component(
        SceneAuthoredComponentKind kind) noexcept
    {
        return scene_component_domain(kind) == SceneComponentDomain::EditorOnly;
    }

    constexpr bool is_editor_authoring_component(
        SceneAuthoredComponentKind kind) noexcept
    {
        return scene_component_domain(kind)
            == SceneComponentDomain::EditorAuthoring;
    }

    constexpr bool is_exportable_component(
        SceneAuthoredComponentKind kind) noexcept
    {
        return scene_component_domain(kind) == SceneComponentDomain::Exportable;
    }

    constexpr bool is_runtime_relevant_component(
        SceneAuthoredComponentKind kind) noexcept
    {
        const SceneComponentDomain domain = scene_component_domain(kind);
        return domain == SceneComponentDomain::RuntimeRelevant
            || domain == SceneComponentDomain::Exportable;
    }

    struct SceneAuthoredComponentSummary
    {
        uint32_t nodes = 0;
        uint32_t transforms = 0;
        uint32_t visibility = 0;
        uint32_t motion_types = 0;
        uint32_t parent_links = 0;
        uint32_t renderables = 0;
        uint32_t scene_import_sources = 0;
        uint32_t mesh_sources = 0;
        uint32_t mesh_wavelet_analyses = 0;
        uint32_t mesh_render_styles = 0;
        uint32_t scalar_field_sources = 0;
        uint32_t vector_field_sources = 0;
        uint32_t cameras = 0;
        uint32_t lights = 0;
        uint32_t ambient_lighting = 0;
        uint32_t hdri_environments = 0;
        uint32_t sky_visuals = 0;
        uint32_t sky_surfaces = 0;
        uint32_t input_receivers = 0;
        uint32_t flying_camera_controllers = 0;
        uint32_t actor_movement_controllers = 0;
        uint32_t ground_boundaries = 0;
        uint32_t collisions = 0;
        uint32_t terrains = 0;
        uint32_t terrain_render_styles = 0;
        uint32_t terrain_mesh_sources = 0;
        uint32_t terrain_height_field_sources = 0;
        uint32_t audio_listeners = 0;
        uint32_t event_listeners = 0;
        uint32_t event_triggers = 0;
        uint32_t proximities = 0;
        uint32_t motions = 0;
        uint32_t behaviors = 0;
        uint32_t compute_kernels = 0;
        uint32_t auxiliary_visuals = 0;
        uint32_t editor_handles = 0;
    };

    struct SceneRuntimeComponentSummary
    {
        uint32_t runtime_entities = 0;
        uint32_t renderable_descriptor_slots = 0;
        uint32_t cameras = 0;
        uint32_t lights = 0;
        uint32_t ambient_lighting = 0;
        uint32_t hdri_environments = 0;
        uint32_t sky_draws = 0;
        uint32_t input_receivers = 0;
        uint32_t flying_camera_controllers = 0;
        uint32_t actor_movement_controllers = 0;
        uint32_t ground_boundaries = 0;
        uint32_t collisions = 0;
        uint32_t terrains = 0;
        uint32_t audio_listeners = 0;
        uint32_t event_listeners = 0;
        uint32_t proximities = 0;
        uint32_t motions = 0;
        uint32_t behaviors = 0;
        uint32_t auxiliary_visuals = 0;
    };

    // Runtime component record used by scene projections. The field remains
    // named node because the current runtime entity is a SceneStorage node.
    template <typename T>
    struct RuntimeComponentRecord
    {
        RuntimeEntityId node{};
        T component{};
    };
}
