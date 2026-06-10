// tests/asset_scene/gpu_scene_resource_resolver_tests.cpp
//
// GpuSceneRenderResourceResolver coverage with a null gpu::Device. The
// resolver's asset-lookup and validation branches all run before the first
// GPU call, and the cache-backed realize path must fail cleanly on an
// invalid device instead of fabricating handles.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/scene/scene_instance.h>
#include <engine/rendering/gpu_scene_render_resource_resolver.h>
#include <engine/rendering/render_resource_resolver.h>
#include <engine/rendering/renderable_gpu_cache.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <gpu/gpu_resource_lifecycle.h>
#include <logging/logger.h>

namespace
{
    wz::fs::Path make_test_root(const char* name)
    {
        const wz::fs::Path root =
            wz::fs::join(wz::fs::temp_directory_path(), name);
        wz::fs::create_directories(root);
        return root;
    }

    class GpuResolverFixture
    {
    public:
        explicit GpuResolverFixture(const char* root_name)
            : assets_(device_, logger_, make_test_root(root_name))
        {
        }

        wz::Logger logger_;
        wz::gpu::Device device_{};
        wz::engine::assets::EngineAssetLibrary assets_;
        wz::engine::rendering::RenderResourceResolver render_resolver_;
    };
}

TEST(GpuSceneRenderResourceResolver, RejectsUnsupportedRenderableKind)
{
    using namespace wz::engine::assets;

    GpuResolverFixture fx("wz_gpu_resolver_kind_test");
    ASSERT_TRUE(fx.assets_.commit());

    wz::engine::rendering::GpuSceneRenderResourceResolver resolver(
        fx.device_, fx.assets_, fx.render_resolver_);

    RenderableAssetData splat{};
    splat.kind = RenderableKind::GaussianSplatCloud;
    splat.source_asset.content_hash = { 0xBEEFu, 0u };

    wz::scene::RenderableDescriptor descriptor{};
    EXPECT_FALSE(resolver.realize_renderable_descriptor(splat, descriptor));
}

TEST(GpuSceneRenderResourceResolver, RejectsUnknownMeshSourceAsset)
{
    using namespace wz::engine::assets;

    GpuResolverFixture fx("wz_gpu_resolver_unknown_mesh_test");
    ASSERT_TRUE(fx.assets_.commit());

    wz::engine::rendering::GpuSceneRenderResourceResolver resolver(
        fx.device_, fx.assets_, fx.render_resolver_);

    RenderableAssetData renderable{};
    renderable.kind = RenderableKind::Mesh;
    renderable.source_asset.content_hash = { 0xDEADu, 0xBEEFu };

    wz::scene::RenderableDescriptor descriptor{};
    EXPECT_FALSE(
        resolver.realize_renderable_descriptor(renderable, descriptor));
}

TEST(GpuSceneRenderResourceResolver, RejectsTerrainSurfaceWithMissingProxy)
{
    using namespace wz::engine::assets;

    GpuResolverFixture fx("wz_gpu_resolver_missing_proxy_test");
    ASSERT_TRUE(fx.assets_.commit());

    wz::engine::rendering::GpuSceneRenderResourceResolver resolver(
        fx.device_, fx.assets_, fx.render_resolver_);

    RenderableAssetData renderable{};
    renderable.kind = RenderableKind::Mesh;
    renderable.program = BuiltinRenderProgram::TerrainMeshSurface;
    renderable.source_asset.content_hash = { 0x1111u, 0u };
    renderable.companion_asset.content_hash = { 0x2222u, 0u };

    wz::scene::RenderableDescriptor descriptor{};
    EXPECT_FALSE(
        resolver.realize_renderable_descriptor(renderable, descriptor));
}

TEST(GpuSceneRenderResourceResolver, RejectsScalarFieldWithoutCompanion)
{
    using namespace wz::engine::assets;

    GpuResolverFixture fx("wz_gpu_resolver_scalar_no_companion_test");
    ASSERT_TRUE(fx.assets_.commit());

    wz::engine::rendering::GpuSceneRenderResourceResolver resolver(
        fx.device_, fx.assets_, fx.render_resolver_);

    RenderableAssetData renderable{};
    renderable.kind = RenderableKind::ScalarField;
    renderable.source_asset.content_hash = { 0x3333u, 0u };

    wz::scene::RenderableDescriptor descriptor{};
    EXPECT_FALSE(
        resolver.realize_renderable_descriptor(renderable, descriptor));
}

TEST(GpuSceneRenderResourceResolver, RejectsScalarFieldWithUnknownTerrain)
{
    using namespace wz::engine::assets;

    GpuResolverFixture fx("wz_gpu_resolver_scalar_bad_terrain_test");
    ASSERT_TRUE(fx.assets_.commit());

    wz::engine::rendering::GpuSceneRenderResourceResolver resolver(
        fx.device_, fx.assets_, fx.render_resolver_);

    RenderableAssetData renderable{};
    renderable.kind = RenderableKind::ScalarField;
    renderable.source_asset.content_hash = { 0x4444u, 0u };
    renderable.companion_asset.content_hash = { 0x5555u, 0u };

    wz::scene::RenderableDescriptor descriptor{};
    EXPECT_FALSE(
        resolver.realize_renderable_descriptor(renderable, descriptor));
}

TEST(GpuSceneRenderResourceResolver, CacheBackedRealizeFailsOnInvalidDevice)
{
    using namespace wz::engine::assets;

    GpuResolverFixture fx("wz_gpu_resolver_null_device_cache_test");

    const auto mesh = fx.assets_.meshes().create_procedural_mesh({
        .name = "debug/cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto renderable = fx.assets_.renderables().create_mesh_wireframe({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
    });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(fx.assets_.commit());
    ASSERT_TRUE(fx.assets_.resolve_all().ok());

    const auto handle = fx.assets_.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());
    const RenderableAssetData* data =
        fx.assets_.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);

    wz::gpu::DeferredReleaseQueue release_queue{};
    wz::engine::rendering::RenderableGpuCache cache(release_queue);
    wz::engine::rendering::GpuSceneRenderResourceResolver resolver(
        fx.device_, fx.assets_, fx.render_resolver_, &cache);

    // The asset side is fully resolvable, but the null device must stop the
    // realize path without fabricating a descriptor handle.
    wz::scene::RenderableDescriptor descriptor{};
    EXPECT_FALSE(resolver.realize_renderable_descriptor(*data, descriptor));
}

TEST(GpuSceneRenderResourceResolver, InstantiationFailsRealizeOnNullDevice)
{
    using namespace wz::engine::assets;

    GpuResolverFixture fx("wz_gpu_resolver_instantiate_test");

    const auto mesh = fx.assets_.meshes().create_procedural_mesh({
        .name = "debug/cube",
        .kind = ProceduralMeshKind::Cube,
    });
    const auto renderable = fx.assets_.renderables().create_mesh_wireframe({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
    });

    ASSERT_TRUE(fx.assets_.commit());
    ASSERT_TRUE(fx.assets_.resolve_all().ok());

    class LibraryRenderableResolver final : public SceneRenderableResolver
    {
    public:
        explicit LibraryRenderableResolver(RenderableAssetModule& module)
            : module_(module)
        {
        }

        const RenderableAssetData* get(
            wz::asset::AssetKey key) const override
        {
            const auto handle =
                module_.get_renderable(RenderableAsset{ .output = key });
            return handle.valid()
                ? module_.get_renderable_data(handle)
                : nullptr;
        }

    private:
        RenderableAssetModule& module_;
    };

    wz::gpu::DeferredReleaseQueue release_queue{};
    wz::engine::rendering::RenderableGpuCache cache(release_queue);
    wz::engine::rendering::GpuSceneRenderResourceResolver resource_resolver(
        fx.device_, fx.assets_, fx.render_resolver_, &cache);
    LibraryRenderableResolver renderable_resolver(fx.assets_.renderables());

    SceneAssetData scene{};
    scene.name = "gpu_resolver_null_device";

    SceneNodeAsset node{};
    node.id = "cube";
    node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(node));

    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::RenderableRealizeFailed);
}
