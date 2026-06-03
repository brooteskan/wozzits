#include "renderable_asset_module_test_support.h"

TEST(RenderableAssetModule, ResolvesMeshWireframeRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_mesh_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/cube",
            .kind = ProceduralMeshKind::Cube,
            });

    ASSERT_TRUE(mesh.valid());

    const auto renderable =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/cube_wireframe",
            .mesh = mesh,
            });

    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle =
        assets.renderables().get_renderable(renderable);

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.handle.type, kAssetTypeRenderable);

    const auto* data =
        assets.renderables().get_renderable_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->kind, RenderableKind::Mesh);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshWireframeDebug);
    EXPECT_EQ(data->domain, RenderDomain::Debug);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_Wireframe) != 0);
    EXPECT_FLOAT_EQ(data->bounds_min[0], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[1], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[2], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[0], 1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[1], 1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[2], 1.0f);
}

TEST(RenderableAssetModule, DuplicateMeshWireframeRegistrationReturnsSameAsset)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_duplicate_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/cube",
            .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(mesh.valid());

    const auto first =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/cube_wireframe",
            .mesh = mesh,
        });
    const auto second =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/cube_wireframe",
            .mesh = mesh,
        });

    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    EXPECT_EQ(second.output, first.output);
}

TEST(RenderableAssetModule, ResolvesDepthTestedMeshWireframeRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_depth_mesh_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/depth_cube",
            .kind = ProceduralMeshKind::Cube,
            });

    ASSERT_TRUE(mesh.valid());

    const auto renderable =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/depth_cube_wireframe",
            .mesh = mesh,
            .program = BuiltinRenderProgram::MeshWireframeDepthDebug,
            .domain = RenderDomain::Debug,
            .policy_flags =
                RenderPolicy_Wireframe
                | RenderPolicy_DepthTest
                | RenderPolicy_DepthWrite,
            });

    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle =
        assets.renderables().get_renderable(renderable);

    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->kind, RenderableKind::Mesh);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshWireframeDepthDebug);
    EXPECT_EQ(data->domain, RenderDomain::Debug);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_Wireframe) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthTest) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthWrite) != 0);
}

TEST(RenderableAssetModule, ResolvesStyledMeshWireframeRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_styled_mesh_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/styled_cube",
            .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(mesh.valid());

    MeshRenderStyleData style{};
    style.wireframe.color[0] = 0.05f;
    style.wireframe.color[1] = 0.9f;
    style.wireframe.color[2] = 0.2f;
    style.wireframe.color[3] = 0.75f;
    style.wireframe.emissive_strength = 3.0f;
    style.depth_test = true;
    style.depth_write = true;
    style.double_sided = false;
    style.hidden_line_prepass = true;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/battlezone_green",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/styled_cube_renderable",
            .mesh = mesh,
            .style = render_style,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 3u);

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const MeshHandle mesh_handle = assets.meshes().get_mesh(mesh);
    ASSERT_TRUE(mesh_handle.valid());
    const MeshData* mesh_data = assets.meshes().get_mesh_data(mesh_handle);
    ASSERT_NE(mesh_data, nullptr);
    EXPECT_TRUE(mesh_data->has_normals);
    EXPECT_FALSE(mesh_data->has_uv0);

    const auto* data =
        assets.renderables().get_renderable_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->kind, RenderableKind::Mesh);
    EXPECT_EQ(data->source_asset, mesh.output);
    EXPECT_EQ(data->companion_asset, render_style.output);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshWireframeDepthDebug);
    EXPECT_EQ(data->domain, RenderDomain::Opaque);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_Wireframe) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthTest) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthWrite) != 0);
    EXPECT_TRUE(data->mesh_style.wireframe.enabled);
    EXPECT_FALSE(data->mesh_style.surface.enabled);
    EXPECT_FLOAT_EQ(data->mesh_style.wireframe.color[1], 0.9f);
    EXPECT_FLOAT_EQ(data->mesh_style.wireframe.emissive_strength, 3.0f);
    EXPECT_FALSE(data->mesh_style.double_sided);
}

TEST(RenderableAssetModule, StyledMeshWireframeDepthTestSelectsDepthProgram)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_hidden_line_mesh_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/hidden_line_cube",
            .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(mesh.valid());

    MeshRenderStyleData style{};
    style.depth_test = true;
    style.depth_write = false;
    style.hidden_line_prepass = true;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/hidden_line",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/hidden_line_cube_renderable",
            .mesh = mesh,
            .style = render_style,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshWireframeDepthDebug);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthTest) != 0);
    EXPECT_FALSE((data->policy_flags & RenderPolicy_DepthWrite) != 0);
    EXPECT_FALSE(data->mesh_style.hidden_line_prepass);
}

TEST(RenderableAssetModule, TransparentSurfaceLayerResolvesSurfaceRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_surface_style_fallback_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/surface_style_cube",
            .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(mesh.valid());

    MeshRenderStyleData style{};
    style.wireframe.enabled = false;
    style.surface.enabled = true;
    style.surface.color[0] = 0.2f;
    style.surface.color[1] = 0.6f;
    style.surface.color[2] = 1.0f;
    style.alpha = 0.35f;
    style.depth_test = true;
    style.depth_write = false;
    style.hidden_line_prepass = false;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/transparent_surface_fallback",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/surface_style_cube_renderable",
            .mesh = mesh,
            .style = render_style,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->kind, RenderableKind::Mesh);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshSurfaceAlpha);
    EXPECT_EQ(data->domain, RenderDomain::Transparent);
    EXPECT_FALSE((data->policy_flags & RenderPolicy_Wireframe) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_AlphaBlend) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthTest) != 0);
    EXPECT_FALSE((data->policy_flags & RenderPolicy_DepthWrite) != 0);
    EXPECT_TRUE(data->mesh_style.surface.enabled);
    EXPECT_FLOAT_EQ(data->mesh_style.alpha, 0.35f);
}

TEST(RenderableAssetModule, OpaqueSurfaceMeshStyleResolvesSurfaceRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_opaque_surface_style_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/opaque_surface_cube",
            .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(mesh.valid());

    MeshRenderStyleData style{};
    style.wireframe.enabled = false;
    style.surface.enabled = true;
    style.surface.color[0] = 0.8f;
    style.surface.color[1] = 0.2f;
    style.surface.color[2] = 0.1f;
    style.depth_test = true;
    style.depth_write = true;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/opaque_surface",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/opaque_surface_cube_renderable",
            .mesh = mesh,
            .style = render_style,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->kind, RenderableKind::Mesh);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshSurface);
    EXPECT_EQ(data->domain, RenderDomain::Opaque);
    EXPECT_FALSE((data->policy_flags & RenderPolicy_Wireframe) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthTest) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthWrite) != 0);
    EXPECT_TRUE(data->mesh_style.surface.enabled);
    EXPECT_FLOAT_EQ(data->mesh_style.surface.color[0], 0.8f);
}

TEST(RenderableAssetModule, NearOpaqueSurfaceAlphaResolvesOpaqueDepthWritingRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_near_opaque_surface_style_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/near_opaque_surface_cube",
            .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(mesh.valid());

    MeshRenderStyleData style{};
    style.wireframe.enabled = false;
    style.surface.enabled = true;
    style.alpha = 0.9995f;
    style.depth_test = false;
    style.depth_write = false;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/near_opaque_surface",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/near_opaque_surface_cube_renderable",
            .mesh = mesh,
            .style = render_style,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshSurface);
    EXPECT_EQ(data->domain, RenderDomain::Opaque);
    EXPECT_FALSE((data->policy_flags & RenderPolicy_AlphaBlend) != 0);
    EXPECT_FALSE((data->policy_flags & RenderPolicy_DepthTest) != 0);
    EXPECT_FALSE((data->policy_flags & RenderPolicy_DepthWrite) != 0);
    EXPECT_FLOAT_EQ(data->mesh_style.alpha, 1.0f);
}

TEST(RenderableAssetModule, StyledMeshWithNoEnabledLayersDoesNotEmitRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_empty_mesh_style_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/empty_style_cube",
            .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(mesh.valid());

    MeshRenderStyleData style{};
    style.wireframe.enabled = false;
    style.surface.enabled = false;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/empty",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable =
        assets.renderables().create_mesh_styled({
            .name = "debug/empty_style_cube_renderable",
            .mesh = mesh,
            .style = render_style,
        });
    ASSERT_TRUE(renderable.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_FALSE(report.ok());
    int renderable_failures = 0;
    for (const auto& failure : report.failures) {
        if (failure.key == renderable.output
            && failure.error == wz::asset::ResolveError::CompileFailed)
        {
            ++renderable_failures;
        }
    }
    EXPECT_EQ(renderable_failures, 1);
    EXPECT_FALSE(assets.renderables().get_renderable(renderable).valid());
}

TEST(RenderableAssetModule, MeshWireframeRenderableDomainParticipatesInIdentity)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_mesh_domain_identity_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/shared_quad",
            .kind = ProceduralMeshKind::Quad,
        });

    ASSERT_TRUE(mesh.valid());

    const auto debug_renderable =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/shared_quad_wireframe",
            .mesh = mesh,
            .domain = RenderDomain::Debug,
        });

    const auto opaque_renderable =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/shared_quad_wireframe",
            .mesh = mesh,
            .domain = RenderDomain::Opaque,
        });

    ASSERT_TRUE(debug_renderable.valid());
    ASSERT_TRUE(opaque_renderable.valid());
    EXPECT_FALSE(debug_renderable.output == opaque_renderable.output);

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 3u);

    const auto debug_handle =
        assets.renderables().get_renderable(debug_renderable);
    const auto opaque_handle =
        assets.renderables().get_renderable(opaque_renderable);

    ASSERT_TRUE(debug_handle.valid());
    ASSERT_TRUE(opaque_handle.valid());

    const auto* debug_data =
        assets.renderables().get_renderable_data(debug_handle);
    const auto* opaque_data =
        assets.renderables().get_renderable_data(opaque_handle);

    ASSERT_NE(debug_data, nullptr);
    ASSERT_NE(opaque_data, nullptr);
    EXPECT_EQ(debug_data->domain, RenderDomain::Debug);
    EXPECT_EQ(opaque_data->domain, RenderDomain::Opaque);
}

TEST(RenderableAssetModule, MeshWireframeRenderableBoundsComeFromMeshVertices)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_mesh_bounds_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "debug/quad",
            .kind = ProceduralMeshKind::Quad,
        });

    ASSERT_TRUE(mesh.valid());

    const auto renderable =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/quad_wireframe",
            .mesh = mesh,
        });

    ASSERT_TRUE(renderable.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle =
        assets.renderables().get_renderable(renderable);

    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_FLOAT_EQ(data->bounds_min[0], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[1], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[2], 0.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[0], 1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[1], 1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[2], 0.0f);
}

