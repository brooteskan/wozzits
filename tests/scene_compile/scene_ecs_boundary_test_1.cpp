#include <gtest/gtest.h>

#include <scene/scene_ecs.h>

#include <type_traits>

using namespace wz::scene;

TEST(SceneECSBoundary, IdentityVocabularyIsSceneOwned)
{
    static_assert(std::is_same_v<AuthoredEntityId, std::string>);
    static_assert(std::is_same_v<RuntimeEntityId, wz::core::graph::NodeHandle>);
    static_assert(std::is_same_v<
        decltype(RuntimeComponentRecord<int>{}.node),
        RuntimeEntityId>);

    RuntimeComponentRecord<int> record{};
    record.node = INVALID_RUNTIME_ENTITY;
    record.component = 42;

    EXPECT_EQ(record.node, INVALID_RUNTIME_ENTITY);
    EXPECT_EQ(record.component, 42);
}

TEST(SceneECSBoundary, RuntimeSummaryDefaultsToZero)
{
    const SceneRuntimeComponentSummary summary{};

    EXPECT_EQ(summary.runtime_entities, 0u);
    EXPECT_EQ(summary.renderable_descriptor_slots, 0u);
    EXPECT_EQ(summary.cameras, 0u);
    EXPECT_EQ(summary.lights, 0u);
    EXPECT_EQ(summary.hdri_environments, 0u);
    EXPECT_EQ(summary.sky_draws, 0u);
    EXPECT_EQ(summary.input_receivers, 0u);
    EXPECT_EQ(summary.flying_camera_controllers, 0u);
    EXPECT_EQ(summary.actor_movement_controllers, 0u);
    EXPECT_EQ(summary.ground_boundaries, 0u);
    EXPECT_EQ(summary.collisions, 0u);
    EXPECT_EQ(summary.terrains, 0u);
    EXPECT_EQ(summary.audio_listeners, 0u);
    EXPECT_EQ(summary.event_listeners, 0u);
    EXPECT_EQ(summary.proximities, 0u);
    EXPECT_EQ(summary.motions, 0u);
    EXPECT_EQ(summary.behaviors, 0u);
    EXPECT_EQ(summary.auxiliary_visuals, 0u);
}

TEST(SceneECSBoundary, ClassifiesCoreNodeComponents)
{
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::Transform),
        SceneComponentDomain::CoreNode);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::Visibility),
        SceneComponentDomain::CoreNode);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::MotionType),
        SceneComponentDomain::CoreNode);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::ParentLink),
        SceneComponentDomain::CoreNode);

    EXPECT_TRUE(is_core_node_component(SceneAuthoredComponentKind::Transform));
    EXPECT_FALSE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::Transform));
    EXPECT_FALSE(is_exportable_component(SceneAuthoredComponentKind::Transform));
    EXPECT_FALSE(is_editor_only_component(
        SceneAuthoredComponentKind::Transform));
}

TEST(SceneECSBoundary, ClassifiesExportableSceneComponents)
{
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::Renderable),
        SceneComponentDomain::Exportable);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::Camera),
        SceneComponentDomain::Exportable);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::Light),
        SceneComponentDomain::Exportable);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::AmbientLighting),
        SceneComponentDomain::Exportable);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::HDRIEnvironment),
        SceneComponentDomain::Exportable);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::AuxiliaryVisual),
        SceneComponentDomain::Exportable);
    EXPECT_TRUE(is_exportable_component(
        SceneAuthoredComponentKind::Renderable));
    EXPECT_TRUE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::Renderable));
    EXPECT_TRUE(is_exportable_component(
        SceneAuthoredComponentKind::AmbientLighting));
    EXPECT_TRUE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::AmbientLighting));
    EXPECT_TRUE(is_exportable_component(
        SceneAuthoredComponentKind::HDRIEnvironment));
    EXPECT_TRUE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::HDRIEnvironment));
    EXPECT_FALSE(is_core_node_component(
        SceneAuthoredComponentKind::Renderable));
    EXPECT_FALSE(is_editor_only_component(
        SceneAuthoredComponentKind::Renderable));
    EXPECT_TRUE(is_exportable_component(
        SceneAuthoredComponentKind::AuxiliaryVisual));
    EXPECT_TRUE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::AuxiliaryVisual));
    EXPECT_FALSE(is_editor_only_component(
        SceneAuthoredComponentKind::AuxiliaryVisual));
}

TEST(SceneECSBoundary, ClassifiesRuntimeRelevantComponents)
{
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::InputReceiver),
        SceneComponentDomain::RuntimeRelevant);
    EXPECT_EQ(
        scene_component_domain(
            SceneAuthoredComponentKind::FlyingCameraController),
        SceneComponentDomain::RuntimeRelevant);
    EXPECT_EQ(
        scene_component_domain(
            SceneAuthoredComponentKind::ActorMovementController),
        SceneComponentDomain::RuntimeRelevant);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::GroundBoundary),
        SceneComponentDomain::RuntimeRelevant);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::Collision),
        SceneComponentDomain::RuntimeRelevant);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::Terrain),
        SceneComponentDomain::RuntimeRelevant);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::AudioListener),
        SceneComponentDomain::RuntimeRelevant);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::EventListener),
        SceneComponentDomain::RuntimeRelevant);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::Proximity),
        SceneComponentDomain::RuntimeRelevant);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::Motion),
        SceneComponentDomain::RuntimeRelevant);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::Behavior),
        SceneComponentDomain::RuntimeRelevant);

    EXPECT_TRUE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::InputReceiver));
    EXPECT_FALSE(is_exportable_component(
        SceneAuthoredComponentKind::InputReceiver));
    EXPECT_FALSE(is_editor_only_component(
        SceneAuthoredComponentKind::InputReceiver));
    EXPECT_TRUE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::Collision));
    EXPECT_FALSE(is_exportable_component(
        SceneAuthoredComponentKind::Collision));
    EXPECT_FALSE(is_editor_only_component(
        SceneAuthoredComponentKind::Collision));
    EXPECT_TRUE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::Proximity));
    EXPECT_TRUE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::Motion));
    EXPECT_TRUE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::Behavior));
    EXPECT_FALSE(is_exportable_component(
        SceneAuthoredComponentKind::Behavior));
}

TEST(SceneECSBoundary, ClassifiesEditorAuthoringComponents)
{
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::SceneImportSource),
        SceneComponentDomain::EditorAuthoring);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::MeshSource),
        SceneComponentDomain::EditorAuthoring);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::MeshRenderStyle),
        SceneComponentDomain::EditorAuthoring);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::ScalarFieldSource),
        SceneComponentDomain::EditorAuthoring);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::VectorFieldSource),
        SceneComponentDomain::EditorAuthoring);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::TerrainRenderStyle),
        SceneComponentDomain::EditorAuthoring);
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::TerrainMeshSource),
        SceneComponentDomain::EditorAuthoring);
    EXPECT_EQ(
        scene_component_domain(
            SceneAuthoredComponentKind::TerrainHeightFieldSource),
        SceneComponentDomain::EditorAuthoring);

    EXPECT_FALSE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::SceneImportSource));
    EXPECT_TRUE(is_editor_authoring_component(
        SceneAuthoredComponentKind::SceneImportSource));
    EXPECT_FALSE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::MeshSource));
    EXPECT_TRUE(is_editor_authoring_component(
        SceneAuthoredComponentKind::MeshSource));
    EXPECT_FALSE(is_exportable_component(
        SceneAuthoredComponentKind::MeshRenderStyle));
    EXPECT_TRUE(is_editor_authoring_component(
        SceneAuthoredComponentKind::MeshRenderStyle));
    EXPECT_FALSE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::ScalarFieldSource));
    EXPECT_TRUE(is_editor_authoring_component(
        SceneAuthoredComponentKind::ScalarFieldSource));
    EXPECT_FALSE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::VectorFieldSource));
    EXPECT_TRUE(is_editor_authoring_component(
        SceneAuthoredComponentKind::VectorFieldSource));
    EXPECT_FALSE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::TerrainRenderStyle));
    EXPECT_TRUE(is_editor_authoring_component(
        SceneAuthoredComponentKind::TerrainRenderStyle));
    EXPECT_FALSE(is_editor_only_component(
        SceneAuthoredComponentKind::TerrainMeshSource));
    EXPECT_TRUE(is_editor_authoring_component(
        SceneAuthoredComponentKind::TerrainMeshSource));
    EXPECT_FALSE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::TerrainHeightFieldSource));
    EXPECT_TRUE(is_editor_authoring_component(
        SceneAuthoredComponentKind::TerrainHeightFieldSource));
}

TEST(SceneECSBoundary, ClassifiesEditorOnlyComponents)
{
    EXPECT_EQ(
        scene_component_domain(SceneAuthoredComponentKind::EditorHandle),
        SceneComponentDomain::EditorOnly);

    EXPECT_TRUE(is_editor_only_component(
        SceneAuthoredComponentKind::EditorHandle));
    EXPECT_FALSE(is_runtime_relevant_component(
        SceneAuthoredComponentKind::EditorHandle));
    EXPECT_FALSE(is_exportable_component(
        SceneAuthoredComponentKind::EditorHandle));
}
