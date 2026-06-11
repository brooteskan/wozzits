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

TEST(SceneInstantiate, TerrainSurfaceRenderableCompilesToTerrainDrawRefs)
{
    using namespace wz::engine::assets;

    wz::asset::AssetKey renderable_key{};
    renderable_key.content_hash = { 0xAAu, 0xBBu };

    wz::asset::AssetKey proxy_key{};
    proxy_key.content_hash = { 0xCCu, 0xDDu };

    class SingleRenderableResolver final : public SceneRenderableResolver
    {
    public:
        explicit SingleRenderableResolver(RenderableAssetData data)
            : data_(std::move(data))
        {
        }

        const RenderableAssetData* get(wz::asset::AssetKey) const override
        {
            return &data_;
        }

    private:
        RenderableAssetData data_{};
    };

    class TerrainResourceResolver final : public SceneRenderResourceResolver
    {
    public:
        explicit TerrainResourceResolver(wz::asset::AssetKey proxy_key)
            : proxy_key_(proxy_key)
        {
            proxy_.compiler_version = 1u;
            proxy_.source_asset_key = proxy_key_;
            proxy_.terrain_proxy_id = TerrainProxyId{ proxy_key_ };
            proxy_.bounds.min[0] = -1.0f;
            proxy_.bounds.min[1] = -1.0f;
            proxy_.bounds.min[2] = 1.0f;
            proxy_.bounds.max[0] = 1.0f;
            proxy_.bounds.max[1] = 1.0f;
            proxy_.bounds.max[2] = 3.0f;
            for (uint32_t i = 0u; i < 2u; ++i) {
                TerrainVisualProxyChunkRecord chunk{};
                chunk.chunk_id = TerrainChunkId{ i };
                chunk.representation_id = TerrainRepresentationId{ i };
                chunk.bounds.min[0] = i == 0u ? -1.0f : 0.0f;
                chunk.bounds.min[1] = -1.0f;
                chunk.bounds.min[2] = 1.0f;
                chunk.bounds.max[0] = i == 0u ? 0.0f : 1.0f;
                chunk.bounds.max[1] = 1.0f;
                chunk.bounds.max[2] = 3.0f;
                chunk.triangle_count = 2u;
                chunk.vertex_count = 4u;
                TerrainVisualProxyLodRecord lod{};
                lod.lod_id = TerrainLodId{ 0u };
                lod.representation_id = chunk.representation_id;
                lod.bounds = chunk.bounds;
                lod.triangle_count = 2u;
                lod.vertex_count = 4u;
                chunk.lods.push_back(lod);
                proxy_.chunks.push_back(std::move(chunk));
            }
        }

        bool realize_renderable_descriptor(
            const RenderableAssetData&,
            wz::scene::RenderableDescriptor& descriptor) const override
        {
            descriptor.terrain_visual_proxy_asset = proxy_key_;
            descriptor.terrain_proxy_id =
                wz::scene::TerrainProxyId{ proxy_key_ };
            descriptor.terrain_visual_proxy_data = &proxy_;
            descriptor.terrain_visual_chunk_count = 2u;
            return true;
        }

    private:
        wz::asset::AssetKey proxy_key_{};
        TerrainVisualProxyData proxy_{};
    };

    RenderableAssetData renderable{};
    renderable.kind = RenderableKind::Mesh;
    renderable.program = BuiltinRenderProgram::TerrainMeshSurface;
    renderable.domain = RenderDomain::Opaque;
    renderable.bounds_min[0] = -1.0f;
    renderable.bounds_min[1] = -1.0f;
    renderable.bounds_min[2] = 1.0f;
    renderable.bounds_max[0] = 1.0f;
    renderable.bounds_max[1] = 1.0f;
    renderable.bounds_max[2] = 3.0f;

    SceneAssetData scene{};
    scene.name = "terrain_surface";

    SceneNodeAsset node{};
    node.id = "terrain";
    node.renderable_asset = renderable_key;
    scene.nodes.push_back(std::move(node));

    SingleRenderableResolver renderable_resolver{ renderable };
    TerrainResourceResolver resource_resolver{ proxy_key };
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const auto node_handle = result.instance.authored_to_runtime["terrain"];
    const auto& desc = result.instance.renderables[node_handle];
    EXPECT_EQ(
        desc.node_class.producer,
        wz::scene::ProducerKind::TerrainPatch);
    EXPECT_EQ(desc.terrain_visual_chunk_count, 2u);

    wz::scene::ViewData view{};
    view.camera_position = { 0.0f, 0.0f, -2.0f };
    view.view = wz::math::Mat4::identity();
    constexpr float Pi = 3.14159265358979323846f;
    view.projection = wz::math::projection_perspective_dx(
        70.0f * Pi / 180.0f,
        16.0f / 9.0f,
        0.1f,
        100.0f);
    view.view_projection = wz::math::mul(view.projection, view.view);

    wz::scene::CompiledSceneStorage compiled{};
    wz::scene::compile(
        compiled,
        result.instance.storage.polytree,
        result.instance.renderables,
        result.instance.lights,
        view);

    ASSERT_EQ(compiled.scene.terrain_instances.size(), 1u);
    ASSERT_EQ(compiled.scene.terrain_lod_choices.size(), 2u);

    wz::render::RenderIRStorage ir_storage;
    const wz::render::RenderIRView ir =
        wz::render::build_render_ir(ir_storage, compiled.scene);
    EXPECT_EQ(ir.terrain.size(), 2u);
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

TEST(RenderResourceResolver, CarriesTerrainChunksAndAccumulatesStats)
{
    using namespace wz::engine::assets;

    wz::engine::rendering::RenderResourceResolver resolver;

    TerrainVisualChunk chunks[2]{};
    chunks[0].first_index = 0u;
    chunks[0].index_count = 6u;
    chunks[0].replacement_first_index = 12u;
    chunks[0].replacement_index_count = 3u;
    chunks[0].aggregate.triangle_count = 2u;
    chunks[1].first_index = 6u;
    chunks[1].index_count = 3u;
    chunks[1].aggregate.triangle_count = 1u;

    wz::engine::rendering::TerrainFarSplatChunk far_chunks[2]{};
    far_chunks[0].chunk_id = TerrainChunkId{ 0u };
    far_chunks[0].density_id = TerrainLodId{ 0u };
    far_chunks[0].gpu_resource = wz::gpu::GPUHandle{
        .id = 8u,
        .epoch = 1u,
        .type = wz::gpu::GPUResourceType::GaussianSplatCloud,
    };
    far_chunks[0].splat_count = 16u;
    far_chunks[1].chunk_id = TerrainChunkId{ 1u };
    far_chunks[1].density_id = TerrainLodId{ 2u };
    far_chunks[1].gpu_resource = wz::gpu::GPUHandle{
        .id = 9u,
        .epoch = 1u,
        .type = wz::gpu::GPUResourceType::GaussianSplatCloud,
    };
    far_chunks[1].first_splat = 16u;
    far_chunks[1].splat_count = 24u;

    const wz::gpu::GPUHandle gpu_mesh{
        .id = 7u,
        .epoch = 1u,
        .type = wz::gpu::GPUResourceType::Mesh,
    };

    const auto mesh_handle = resolver.register_mesh(
        gpu_mesh,
        BuiltinRenderProgram::TerrainMeshSurface,
        {},
        {},
        4.0f,
        {},
        std::span<const TerrainVisualChunk>(chunks, 2u),
        std::span<const wz::engine::rendering::TerrainFarSplatChunk>(
            far_chunks,
            2u));

    const auto resolved = resolver.resolve_mesh(mesh_handle);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->gpu_resource, gpu_mesh);
    EXPECT_FLOAT_EQ(resolved->terrain_target_pixels_per_triangle, 4.0f);
    ASSERT_EQ(resolved->terrain_chunks.size(), 2u);
    EXPECT_EQ(resolved->terrain_chunks[0].first_index, 0u);
    EXPECT_EQ(resolved->terrain_chunks[0].index_count, 6u);
    EXPECT_EQ(resolved->terrain_chunks[0].replacement_first_index, 12u);
    EXPECT_EQ(resolved->terrain_chunks[0].replacement_index_count, 3u);
    EXPECT_EQ(resolved->terrain_chunks[1].first_index, 6u);
    EXPECT_EQ(resolved->terrain_chunks[1].index_count, 3u);
    ASSERT_EQ(resolved->terrain_far_splat_chunks.size(), 2u);
    EXPECT_EQ(
        resolved->terrain_far_splat_chunks[0].chunk_id,
        TerrainChunkId{ 0u });
    EXPECT_EQ(
        resolved->terrain_far_splat_chunks[0].density_id,
        TerrainLodId{ 0u });
    EXPECT_EQ(resolved->terrain_far_splat_chunks[0].first_splat, 0u);
    EXPECT_EQ(resolved->terrain_far_splat_chunks[0].splat_count, 16u);
    EXPECT_EQ(
        resolved->terrain_far_splat_chunks[1].chunk_id,
        TerrainChunkId{ 1u });
    EXPECT_EQ(
        resolved->terrain_far_splat_chunks[1].density_id,
        TerrainLodId{ 2u });
    EXPECT_EQ(resolved->terrain_far_splat_chunks[1].first_splat, 16u);
    EXPECT_EQ(resolved->terrain_far_splat_chunks[1].splat_count, 24u);

    resolver.record_terrain_render_stats(
        2u,
        1u,
        3u,
        2u,
        1u,
        2u,
        1u,
        2u,
        1u,
        1u,
        1u,
        16u,
        4.0f,
        1.5f,
        1.5f,
        3.0,
        0u,
        0u,
        2u,
        2u,
        2u,
        2u,
        2u,
        2u,
        2u,
        2u);
    resolver.record_terrain_render_stats(
        4u,
        3u,
        12u,
        9u,
        2u,
        7u,
        2u,
        7u,
        2u,
        3u,
        2u,
        24u,
        6.0f,
        0.5f,
        2.0f,
        12.0,
        3u,
        4u,
        9u,
        9u,
        9u,
        9u,
        9u,
        9u,
        9u,
        9u);

    auto stats = resolver.terrain_render_stats();
    EXPECT_EQ(stats.total_chunks, 6u);
    EXPECT_EQ(stats.submitted_chunks, 4u);
    EXPECT_EQ(stats.total_triangles, 15u);
    EXPECT_EQ(stats.submitted_triangles, 11u);
    EXPECT_EQ(stats.lod_candidate_chunks, 3u);
    EXPECT_EQ(stats.lod_candidate_triangles, 9u);
    EXPECT_EQ(stats.lod_replacement_available_chunks, 3u);
    EXPECT_EQ(stats.lod_replacement_available_triangles, 9u);
    EXPECT_EQ(stats.lod_replacement_drawn_chunks, 3u);
    EXPECT_EQ(stats.lod_replacement_drawn_triangles, 4u);
    EXPECT_EQ(stats.far_splat_chunks, 3u);
    EXPECT_EQ(stats.far_splats, 40u);
    EXPECT_FLOAT_EQ(stats.lod_target_pixels_per_triangle, 6.0f);
    EXPECT_FLOAT_EQ(stats.pixels_per_triangle_min, 0.5f);
    EXPECT_FLOAT_EQ(stats.pixels_per_triangle_max, 2.0f);
    EXPECT_FLOAT_EQ(
        stats.pixels_per_triangle_weighted_mean,
        15.0f / 11.0f);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_0_5, 3u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_1, 4u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_2, 11u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_4, 11u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_8, 11u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_16, 11u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_32, 11u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_64, 11u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_128, 11u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_256, 11u);

    resolver.reset_terrain_render_stats();
    stats = resolver.terrain_render_stats();
    EXPECT_EQ(stats.total_chunks, 0u);
    EXPECT_EQ(stats.submitted_chunks, 0u);
    EXPECT_EQ(stats.total_triangles, 0u);
    EXPECT_EQ(stats.submitted_triangles, 0u);
    EXPECT_EQ(stats.lod_candidate_chunks, 0u);
    EXPECT_EQ(stats.lod_candidate_triangles, 0u);
    EXPECT_EQ(stats.lod_replacement_available_chunks, 0u);
    EXPECT_EQ(stats.lod_replacement_available_triangles, 0u);
    EXPECT_EQ(stats.lod_replacement_drawn_chunks, 0u);
    EXPECT_EQ(stats.lod_replacement_drawn_triangles, 0u);
    EXPECT_EQ(stats.far_splat_chunks, 0u);
    EXPECT_EQ(stats.far_splats, 0u);
    EXPECT_FLOAT_EQ(stats.lod_target_pixels_per_triangle, 0.0f);
    EXPECT_FLOAT_EQ(stats.pixels_per_triangle_min, 0.0f);
    EXPECT_FLOAT_EQ(stats.pixels_per_triangle_max, 0.0f);
    EXPECT_FLOAT_EQ(stats.pixels_per_triangle_weighted_mean, 0.0f);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_0_5, 0u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_1, 0u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_2, 0u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_4, 0u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_8, 0u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_16, 0u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_32, 0u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_64, 0u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_128, 0u);
    EXPECT_EQ(stats.pixels_per_triangle_triangles_le_256, 0u);
}

TEST(RenderResourceResolver, ResolvesTerrainProxyResources)
{
    using namespace wz::engine::assets;

    wz::engine::rendering::RenderResourceResolver resolver;

    TerrainVisualChunk chunks[1]{};
    chunks[0].first_index = 12u;
    chunks[0].index_count = 6u;
    chunks[0].replacement_first_index = 30u;
    chunks[0].replacement_index_count = 3u;
    chunks[0].aggregate.triangle_count = 2u;

    const wz::gpu::GPUHandle gpu_mesh{
        .id = 17u,
        .epoch = 1u,
        .type = wz::gpu::GPUResourceType::Mesh,
    };

    wz::asset::AssetKey proxy_key{};
    proxy_key.content_hash = { 0x1234u, 0x5678u };
    const TerrainProxyId proxy_id{ proxy_key };

    wz::engine::rendering::TerrainTransitionDrawRange transition_ranges[1]{};
    transition_ranges[0].chunk_id = TerrainChunkId{ 0u };
    transition_ranges[0].neighbor_chunk_id = TerrainChunkId{ 1u };
    transition_ranges[0].lod_id = TerrainLodId{ 0u };
    transition_ranges[0].neighbor_lod_id = TerrainLodId{ 1u };
    transition_ranges[0].edge = TerrainVisualProxyBoundaryEdge::PositiveX;
    transition_ranges[0].first_index = 36u;
    transition_ranges[0].index_count = 6u;

    wz::engine::rendering::TerrainFarSplatChunk far_chunks[1]{};
    far_chunks[0].chunk_id = TerrainChunkId{ 0u };
    far_chunks[0].density_id = TerrainLodId{ 2u };
    far_chunks[0].gpu_resource = wz::gpu::GPUHandle{
        .id = 18u,
        .epoch = 1u,
        .type = wz::gpu::GPUResourceType::GaussianSplatCloud,
    };
    far_chunks[0].first_splat = 12u;
    far_chunks[0].splat_count = 5u;

    ASSERT_TRUE(resolver.register_terrain_proxy(
        proxy_id,
        gpu_mesh,
        BuiltinRenderProgram::TerrainMeshSurface,
        {},
        {},
        8.0f,
        {},
        std::span<const TerrainVisualChunk>(chunks, 1u),
        std::span<const wz::engine::rendering::TerrainFarSplatChunk>(
            far_chunks,
            1u),
        std::span<const wz::engine::rendering::TerrainTransitionDrawRange>(
            transition_ranges,
            1u)));

    auto resolved = resolver.resolve_terrain_proxy(proxy_id);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->gpu_resource, gpu_mesh);
    EXPECT_FLOAT_EQ(resolved->terrain_target_pixels_per_triangle, 8.0f);
    ASSERT_EQ(resolved->terrain_chunks.size(), 1u);
    EXPECT_EQ(resolved->terrain_chunks[0].first_index, 12u);
    EXPECT_EQ(resolved->terrain_chunks[0].index_count, 6u);

    wz::render::TerrainDrawRef base_ref{};
    base_ref.chunk_id = wz::scene::TerrainChunkId{ 0u };
    base_ref.representation_kind =
        wz::scene::TerrainVisualRepresentationKind::MeshChunks;
    base_ref.lod_id = wz::scene::TerrainLodId{ 0u };

    auto draw = resolver.resolve_terrain_draw(proxy_id, base_ref);
    ASSERT_TRUE(draw.has_value());
    EXPECT_EQ(draw->gpu_resource, gpu_mesh);
    EXPECT_EQ(draw->first_index, 12u);
    EXPECT_EQ(draw->index_count, 6u);
    EXPECT_EQ(draw->source_triangle_count, 2u);
    EXPECT_TRUE(draw->lod_replacement_available);
    EXPECT_FALSE(draw->lod_replacement_selected);

    wz::render::TerrainDrawRef replacement_ref = base_ref;
    replacement_ref.lod_id = wz::scene::TerrainLodId{ 1u };

    draw = resolver.resolve_terrain_draw(proxy_id, replacement_ref);
    ASSERT_TRUE(draw.has_value());
    EXPECT_EQ(draw->first_index, 30u);
    EXPECT_EQ(draw->index_count, 3u);
    EXPECT_TRUE(draw->lod_replacement_available);
    EXPECT_TRUE(draw->lod_replacement_selected);

    wz::render::TerrainDrawRef transition_ref{};
    transition_ref.kind = wz::render::TerrainDrawRefKind::LodTransition;
    transition_ref.chunk_id = wz::scene::TerrainChunkId{ 0u };
    transition_ref.neighbor_chunk_id = wz::scene::TerrainChunkId{ 1u };
    transition_ref.lod_id = wz::scene::TerrainLodId{ 0u };
    transition_ref.neighbor_lod_id = wz::scene::TerrainLodId{ 1u };
    transition_ref.transition_edge =
        wz::scene::TerrainVisualProxyBoundaryEdge::PositiveX;

    draw = resolver.resolve_terrain_draw(proxy_id, transition_ref);
    ASSERT_TRUE(draw.has_value());
    EXPECT_EQ(draw->first_index, 36u);
    EXPECT_EQ(draw->index_count, 6u);
    EXPECT_TRUE(draw->transition_selected);
    EXPECT_FALSE(draw->lod_replacement_available);

    wz::render::TerrainDrawRef surfel_ref{};
    surfel_ref.chunk_id = wz::scene::TerrainChunkId{ 0u };
    surfel_ref.representation_kind =
        wz::scene::TerrainVisualRepresentationKind::SurfelCloud;
    surfel_ref.lod_id = wz::scene::TerrainLodId{ 2u };

    draw = resolver.resolve_terrain_draw(proxy_id, surfel_ref);
    ASSERT_TRUE(draw.has_value());
    EXPECT_EQ(draw->gpu_resource, far_chunks[0].gpu_resource);
    EXPECT_EQ(draw->source_triangle_count, 2u);
    EXPECT_EQ(draw->first_splat, 12u);
    EXPECT_EQ(draw->far_splat_count, 5u);
    EXPECT_TRUE(draw->far_splat_selected);
    EXPECT_FALSE(draw->transition_selected);

    auto fallback = resolver.resolve_terrain_draw_mesh_fallback(
        proxy_id,
        surfel_ref);
    ASSERT_TRUE(fallback.has_value());
    EXPECT_EQ(fallback->gpu_resource, gpu_mesh);
    EXPECT_EQ(fallback->first_index, 30u);
    EXPECT_EQ(fallback->index_count, 3u);
    EXPECT_TRUE(fallback->lod_replacement_available);
    EXPECT_TRUE(fallback->lod_replacement_selected);
    EXPECT_FALSE(fallback->far_splat_selected);

    TerrainVisualChunk updated[1]{};
    updated[0].first_index = 24u;
    updated[0].index_count = 3u;
    updated[0].aggregate.triangle_count = 1u;

    ASSERT_TRUE(resolver.register_terrain_proxy(
        proxy_id,
        gpu_mesh,
        BuiltinRenderProgram::TerrainMeshSurface,
        {},
        {},
        2.0f,
        {},
        std::span<const TerrainVisualChunk>(updated, 1u)));

    resolved = resolver.resolve_terrain_proxy(proxy_id);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_FLOAT_EQ(resolved->terrain_target_pixels_per_triangle, 2.0f);
    ASSERT_EQ(resolved->terrain_chunks.size(), 1u);
    EXPECT_EQ(resolved->terrain_chunks[0].first_index, 24u);
    EXPECT_EQ(resolved->terrain_chunks[0].index_count, 3u);
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

