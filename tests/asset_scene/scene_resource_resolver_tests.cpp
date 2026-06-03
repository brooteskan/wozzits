#include "scene_asset_module_test_support.h"

TEST(SceneInstantiate, RejectsRenderableAssetWithoutResolver)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "no_resolver";

    SceneNodeAsset node{};
    node.id = "obj";
    node.renderable_asset = wz::asset::AssetKey{};  // any key
    scene.nodes.push_back(std::move(node));

    auto result = instantiate_scene(scene);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::RenderableResolveFailed);
}

TEST(SceneInstantiate, RejectsUnresolvableRenderableAsset)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_bad_renderable_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    EngineAssetLibrary assets{ device, logger, root };

    ASSERT_TRUE(assets.commit());
    assets.resolve_all();

    SceneAssetData scene{};
    scene.name = "bad_ref";

    SceneNodeAsset node{};
    node.id = "missing";
    // Fabricate a key that doesn't exist in the renderable table
    wz::asset::AssetKey fake_key{};
    fake_key.content_hash = { 0xDEADBEEF, 0 };
    node.renderable_asset = fake_key;
    scene.nodes.push_back(std::move(node));

    TestRenderableResolver resolver(assets.renderables());
    SceneInstantiateContext context{ .renderable_resolver = &resolver };

    auto result = instantiate_scene(scene, context);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::RenderableResolveFailed);
}

TEST(SceneAssetModule, RealizedMeshHandlesFlowToDrawCommand)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_realize_mesh_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "debug/cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto renderable = assets.renderables().create_mesh_wireframe({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
    });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "realize_mesh_scene";

    SceneNodeAsset node{};
    node.id = "cube_node";
    node.local.translation[0] = 2.0f;
    node.local.translation[2] = 3.0f;
    node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(node));

    constexpr wz::scene::MeshHandle expected_mesh = 7;
    constexpr wz::scene::MaterialHandle expected_material = 3;

    TestRenderableResolver renderable_resolver(assets.renderables());
    TestRenderResourceResolver resource_resolver(expected_mesh, expected_material);
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto& inst = result.instance;
    auto cube_h = inst.authored_to_runtime["cube_node"];

    const auto& desc = inst.renderables[cube_h];
    EXPECT_EQ(desc.mesh, expected_mesh);
    EXPECT_EQ(desc.material, expected_material);
    EXPECT_EQ(desc.node_class.role, wz::scene::SceneRole::Renderable);
    EXPECT_EQ(desc.node_class.producer, wz::scene::ProducerKind::Mesh);

    // Full pipeline: compile → IR → frame
    wz::scene::ViewData view{};
    view.camera_position = { 0.f, 0.f, 0.f };
    view.view = wz::math::Mat4::identity();

    constexpr float Pi = 3.14159265358979323846f;
    const float fov = 70.0f * Pi / 180.0f;
    view.projection = wz::math::projection_perspective_dx(
        fov, 16.f / 9.f, 0.1f, 100.f);
    view.view_projection = wz::math::mul(view.projection, view.view);

    wz::scene::CompiledSceneStorage compiled{};
    wz::scene::compile(
        compiled,
        inst.storage.polytree,
        inst.renderables,
        inst.lights,
        view);

    ASSERT_EQ(compiled.scene.opaque.size(), 1u);
    EXPECT_EQ(compiled.scene.opaque[0].mesh, expected_mesh);
    EXPECT_EQ(compiled.scene.opaque[0].material, expected_material);

    wz::render::RenderIRStorage render_ir{};
    wz::render::build_render_ir(render_ir, compiled.scene);

    wz::render::RenderFrameStorage render_frame{};
    wz::render::build_frame(render_frame, render_ir.ir, compiled.scene);

    ASSERT_EQ(render_frame.frame.opaque.size(), 1u);

    const auto& cmd = render_frame.frame.opaque[0];
    EXPECT_EQ(cmd.mesh, expected_mesh);
    EXPECT_EQ(cmd.material, expected_material);
    EXPECT_FLOAT_EQ(cmd.world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(cmd.world.m[14], 3.0f);
}

TEST(SceneAssetModule, RealizedHandlesWithMixedNodes)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_realize_mixed_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "debug/cube",
        .kind = ProceduralMeshKind::Cube,
    });
    const auto renderable = assets.renderables().create_mesh_wireframe({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
    });

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "mixed_realize_scene";

    SceneNodeAsset render_node{};
    render_node.id = "cube";
    render_node.local.translation[0] = 5.0f;
    render_node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(render_node));

    SceneNodeAsset camera_node{};
    camera_node.id = "cam";
    camera_node.camera = SceneCameraAsset{};
    scene.nodes.push_back(std::move(camera_node));

    constexpr wz::scene::MeshHandle expected_mesh = 42;
    constexpr wz::scene::MaterialHandle expected_material = 11;

    TestRenderableResolver renderable_resolver(assets.renderables());
    TestRenderResourceResolver resource_resolver(expected_mesh, expected_material);
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto& inst = result.instance;
    auto cube_h = inst.authored_to_runtime["cube"];

    EXPECT_EQ(inst.renderables[cube_h].mesh, expected_mesh);
    EXPECT_EQ(inst.renderables[cube_h].material, expected_material);

    auto cam_h = inst.authored_to_runtime["cam"];
    EXPECT_EQ(inst.renderables[cam_h].node_class.role,
        wz::scene::SceneRole::None);
}

TEST(SceneInstantiate, RejectsFailedRealization)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_realize_fail_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "debug/cube",
        .kind = ProceduralMeshKind::Cube,
    });
    const auto renderable = assets.renderables().create_mesh_wireframe({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
    });

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "fail_realize";

    SceneNodeAsset node{};
    node.id = "obj";
    node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(node));

    // Resolver that always fails
    class FailingResourceResolver final
        : public SceneRenderResourceResolver
    {
    public:
        bool realize_renderable_descriptor(
            const RenderableAssetData&,
            wz::scene::RenderableDescriptor&) const override
        {
            return false;
        }
    };

    TestRenderableResolver renderable_resolver(assets.renderables());
    FailingResourceResolver resource_resolver;
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::RenderableRealizeFailed);
}

TEST(SceneInstantiate, MeshWithoutResourceResolverUsesPlaceholder)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_placeholder_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "debug/cube",
        .kind = ProceduralMeshKind::Cube,
    });
    const auto renderable = assets.renderables().create_mesh_wireframe({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
    });

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "mesh_placeholder";

    SceneNodeAsset node{};
    node.id = "cube";
    node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(node));

    TestRenderableResolver renderable_resolver(assets.renderables());
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
    };

    auto result = instantiate_scene(scene, context);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto cube_h = result.instance.authored_to_runtime["cube"];
    EXPECT_EQ(result.instance.renderables[cube_h].mesh, 0u);
    EXPECT_EQ(result.instance.renderables[cube_h].material, 0u);
    EXPECT_EQ(result.instance.renderables[cube_h].node_class.role,
        wz::scene::SceneRole::Renderable);
}

TEST(SceneAssetModule, ConcreteMeshResolverFlowsHandlesToDrawCommand)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_concrete_resolver_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "debug/cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    MeshRenderStyleData style{};
    style.wireframe.color[0] = 1.0f;
    style.wireframe.color[1] = 0.25f;
    style.wireframe.color[2] = 0.0f;
    style.wireframe.color[3] = 1.0f;
    style.wireframe.emissive_strength = 1.5f;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/orange_wire",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable = assets.renderables().create_mesh_styled({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
        .style = render_style,
    });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());
    const auto renderable_handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(renderable_handle.valid());
    const auto* renderable_data =
        assets.renderables().get_renderable_data(renderable_handle);
    ASSERT_NE(renderable_data, nullptr);
    EXPECT_FLOAT_EQ(renderable_data->mesh_style.wireframe.color[0], 1.0f);
    EXPECT_FLOAT_EQ(renderable_data->mesh_style.wireframe.color[1], 0.25f);
    EXPECT_FLOAT_EQ(
        renderable_data->mesh_style.wireframe.emissive_strength,
        1.5f);

    // The concrete resolver uses RenderResourceResolver::register_mesh()
    // to allocate a scene-render MeshHandle.
    wz::engine::rendering::RenderResourceResolver render_resolver;
    wz::engine::rendering::MeshSceneRenderResourceResolver resource_resolver(
        assets.meshes(), render_resolver);

    SceneAssetData scene{};
    scene.name = "concrete_resolver_scene";

    SceneNodeAsset node{};
    node.id = "cube_node";
    node.local.translation[0] = 4.0f;
    node.local.translation[2] = 6.0f;
    node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(node));

    TestRenderableResolver renderable_resolver(assets.renderables());
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto& inst = result.instance;
    auto cube_h = inst.authored_to_runtime["cube_node"];

    const auto& desc = inst.renderables[cube_h];
    EXPECT_EQ(desc.node_class.role, wz::scene::SceneRole::Renderable);
    EXPECT_EQ(desc.node_class.producer, wz::scene::ProducerKind::Mesh);

    // The concrete resolver registered the mesh with RenderResourceResolver,
    // so the handle should be a valid index (first registration = 0).
    EXPECT_NE(desc.mesh, wz::scene::INVALID_MESH);
    EXPECT_EQ(desc.material, wz::scene::INVALID_MATERIAL);

    // Verify the handle resolves back through RenderResourceResolver.
    auto resolved = render_resolver.resolve_mesh(desc.mesh);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_TRUE(resolved->gpu_resource.valid());
    EXPECT_FLOAT_EQ(resolved->mesh_style.wireframe.color[0], 1.0f);
    EXPECT_FLOAT_EQ(resolved->mesh_style.wireframe.color[1], 0.25f);
    EXPECT_FLOAT_EQ(
        resolved->mesh_style.wireframe.emissive_strength,
        1.5f);

    // Full pipeline: compile → IR → frame
    wz::scene::ViewData view{};
    view.camera_position = { 0.f, 0.f, 0.f };
    view.view = wz::math::Mat4::identity();

    constexpr float Pi = 3.14159265358979323846f;
    const float fov = 70.0f * Pi / 180.0f;
    view.projection = wz::math::projection_perspective_dx(
        fov, 16.f / 9.f, 0.1f, 100.f);
    view.view_projection = wz::math::mul(view.projection, view.view);

    wz::scene::CompiledSceneStorage compiled{};
    wz::scene::compile(
        compiled,
        inst.storage.polytree,
        inst.renderables,
        inst.lights,
        view);

    ASSERT_EQ(compiled.scene.opaque.size(), 1u);
    EXPECT_EQ(compiled.scene.opaque[0].mesh, desc.mesh);
    EXPECT_EQ(compiled.scene.opaque[0].material, desc.material);

    wz::render::RenderIRStorage render_ir{};
    wz::render::build_render_ir(render_ir, compiled.scene);

    wz::render::RenderFrameStorage render_frame{};
    wz::render::build_frame(render_frame, render_ir.ir, compiled.scene);

    ASSERT_EQ(render_frame.frame.opaque.size(), 1u);

    const auto& cmd = render_frame.frame.opaque[0];
    EXPECT_EQ(cmd.mesh, desc.mesh);
    EXPECT_EQ(cmd.material, desc.material);
    EXPECT_FLOAT_EQ(cmd.world.m[12], 4.0f);
    EXPECT_FLOAT_EQ(cmd.world.m[14], 6.0f);
}

TEST(SceneInstantiate, ConcreteMeshResolverRejectsNonMeshKind)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_concrete_non_mesh_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    // Create a RenderableAssetData that looks like a splat
    RenderableAssetData splat_data{};
    splat_data.kind = RenderableKind::GaussianSplatCloud;
    splat_data.source_asset.content_hash = { 0xBEEF, 0 };

    wz::engine::rendering::RenderResourceResolver render_resolver;
    wz::engine::rendering::MeshSceneRenderResourceResolver resource_resolver(
        assets.meshes(), render_resolver);

    wz::scene::RenderableDescriptor desc{};
    EXPECT_FALSE(resource_resolver.realize_renderable_descriptor(splat_data, desc));
}

TEST(SceneInstantiate, ConcreteMeshResolverRejectsSplatRenderableDuringInstantiation)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_concrete_splat_unsupported_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    const auto cloud =
        assets.gaussian_splats().create_procedural_cloud({
            .name = "debug/splat_sphere",
            .count = 64,
            .radius = 2.0f,
            .splat_scale = 1.0f,
        });
    ASSERT_TRUE(cloud.valid());

    const auto renderable =
        assets.renderables().create_gaussian_splat_debug({
            .name = "debug/splat_sphere_debug",
            .splat_cloud = cloud,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    wz::engine::rendering::RenderResourceResolver render_resolver;
    wz::engine::rendering::MeshSceneRenderResourceResolver resource_resolver(
        assets.meshes(), render_resolver);

    SceneAssetData scene{};
    scene.name = "splat_unsupported_scene";

    SceneNodeAsset node{};
    node.id = "splat_node";
    node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(node));

    TestRenderableResolver renderable_resolver(assets.renderables());
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::RenderableRealizeFailed);
}

TEST(SceneInstantiate, ConcreteMeshResolverRejectsScalarFieldRenderableDuringInstantiation)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_concrete_scalar_unsupported_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    const auto field =
        assets.scalar_fields().create_procedural_scalar_field({
            .name = "debug/scalar_gradient",
            .width = 16,
            .height = 16,
            .depth = 1,
            .generator = ScalarFieldGenerator::GradientX,
            .frequency = 1.0f,
            .amplitude = 1.0f,
            .format = ScalarFieldFormat::Float32,
            .domain_kind = ScalarFieldDomainKind::Spatial2D,
        });
    ASSERT_TRUE(field.valid());

    const auto renderable =
        assets.renderables().create_scalar_field_debug({
            .name = "debug/scalar_gradient_debug",
            .scalar_field = field,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    wz::engine::rendering::RenderResourceResolver render_resolver;
    wz::engine::rendering::MeshSceneRenderResourceResolver resource_resolver(
        assets.meshes(), render_resolver);

    SceneAssetData scene{};
    scene.name = "scalar_unsupported_scene";

    SceneNodeAsset node{};
    node.id = "scalar_node";
    node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(node));

    TestRenderableResolver renderable_resolver(assets.renderables());
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::RenderableRealizeFailed);
}

