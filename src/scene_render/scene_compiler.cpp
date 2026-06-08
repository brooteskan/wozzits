#include <scene/compile/scene_compiler.h>
#include <scene/compile/terrain_lod_selector.h>
#include <algo/next.h>

#include <algorithm>
#include <new>
#include <ranges>
#include <span>
#include <vector>

namespace wz::scene {

    using namespace wz::core::graph;
    using namespace wz::math;

    namespace detail {

        template<typename F>
        struct FunctionSink {
            F fn;

            template<typename T>
            bool push(T&& v)
            {
                fn(std::forward<T>(v));
                return true;
            }
        };

        template<typename F>
        FunctionSink<std::decay_t<F>> sink_fn(F&& fn)
        {
            return { std::forward<F>(fn) };
        }

        inline Vec3 aabb_center(const AABB& b)
        {
            return {
                (b.min.x + b.max.x) * 0.5f,
                (b.min.y + b.max.y) * 0.5f,
                (b.min.z + b.max.z) * 0.5f,
            };
        }

        inline float view_depth(const Mat4& view, const Vec3& world_pos)
        {
            Vec3 vs = mul_point(view, world_pos);
            return -vs.z;
        }

        struct PrimitiveCounts {
            uint32_t opaque{ 0 };
            uint32_t transparent{ 0 };
            uint32_t splats{ 0 };
            uint32_t particles{ 0 };
            uint32_t lights{ 0 };
            uint32_t terrain{ 0 };
            uint32_t terrain_lod_choices{ 0 };
        };

        inline PrimitiveCounts operator+(PrimitiveCounts a, PrimitiveCounts b)
        {
            return {
                .opaque      = a.opaque + b.opaque,
                .transparent = a.transparent + b.transparent,
                .splats      = a.splats + b.splats,
                .particles   = a.particles + b.particles,
                .lights      = a.lights + b.lights,
                .terrain     = a.terrain + b.terrain,
                .terrain_lod_choices =
                    a.terrain_lod_choices + b.terrain_lod_choices,
            };
        }

        PrimitiveCounts primitive_contribution(const RenderableDescriptor& d)
        {
            const auto& nc = d.node_class;
            if (!d.visible || nc.role != SceneRole::Renderable)
                return {};

            switch (nc.producer) {
            case ProducerKind::Mesh:
            case ProducerKind::InstancedMesh:
            case ProducerKind::SkinnedMesh:
                if (d.mesh == INVALID_MESH)
                    return {};
                if (nc.default_surface == SurfaceClass::Opaque ||
                    nc.default_surface == SurfaceClass::Masked)
                    return { .opaque = 1 };
                if (nc.default_surface != SurfaceClass::None)
                    return { .transparent = 1 };
                return {};
            case ProducerKind::SplatCloud:
                return { .splats = 1 };
            case ProducerKind::ParticleEmitter:
                return d.mesh != INVALID_MESH ? PrimitiveCounts{ .particles = 1 } : PrimitiveCounts{};
            case ProducerKind::TerrainPatch:
                return d.terrain_proxy_id.valid()
                    ? PrimitiveCounts{
                        .terrain = 1,
                        .terrain_lod_choices =
                            d.terrain_visual_chunk_count,
                    }
                    : PrimitiveCounts{};
            default:
                return {};
            }
        }

        PrimitiveCounts count_primitives(
            const SceneGraph& g,
            std::span<const RenderableDescriptor> descs,
            uint32_t                              light_count)
        {
            assert(descs.size() == node_count(g));
            auto counts = wz::core::algo::next::reduce(
                topo_order(g),
                PrimitiveCounts{},
                [&descs](PrimitiveCounts acc, NodeHandle n) {
                    return acc + primitive_contribution(descs[n]);
                });
            counts.lights = light_count;
            return counts;
        }

        struct CompileFillState {
            std::span<OpaqueGeometryPrimitive>      opaque;
            std::span<SplatPrimitive>               splats;
            std::span<TransparentGeometryPrimitive> transparent;
            std::span<ParticlePrimitive>            particles;
            std::span<TerrainVisualInstance>         terrain_instances;
            std::span<TerrainLodChoice>              terrain_lod_choices;
            std::span<CompiledOutputRef>            node_to_output;
            std::span<NodeHandle>                   opaque_source_nodes;
            std::span<NodeHandle>                   transparent_source_nodes;
            std::span<NodeHandle>                   splat_source_nodes;
            std::span<NodeHandle>                   particle_source_nodes;
            std::span<NodeHandle>                   terrain_source_nodes;

            uint32_t opaque_count{ 0 };
            uint32_t splat_count{ 0 };
            uint32_t transparent_count{ 0 };
            uint32_t particle_count{ 0 };
            uint32_t terrain_count{ 0 };
            uint32_t terrain_lod_choice_count{ 0 };
        };

        struct CompileNodeSink {
            CompileFillState&                    state;
            const SceneGraph&                    g;
            std::span<const RenderableDescriptor> descs;

            bool push(NodeHandle n)
            {
                if (n >= descs.size())
                    return true;

                const auto& d = descs[n];
                const auto& node = node_data(g, n);
                if (!d.visible)
                    return true;

                const auto& nc = d.node_class;
                if (nc.role != SceneRole::Renderable)
                    return true;

                switch (nc.producer) {
                case ProducerKind::Mesh:
                case ProducerKind::InstancedMesh:
                case ProducerKind::SkinnedMesh:
                    push_surface(n, d, node, nc.default_surface);
                    break;
                case ProducerKind::SplatCloud:
                    push_splat(n, d, node);
                    break;
                case ProducerKind::ParticleEmitter:
                    push_particle(n, d, node);
                    break;
                case ProducerKind::TerrainPatch:
                    push_terrain(n, d, node);
                    break;
                default:
                    break;
                }

                return true;
            }

            void push_surface(
                NodeHandle                  n,
                const RenderableDescriptor& d,
                const TransformNode&        node,
                SurfaceClass                surface)
            {
                if (d.mesh == INVALID_MESH)
                    return;

                const AABB bounds = transform_aabb(d.local_bounds, node.world);
                if (surface == SurfaceClass::Opaque || surface == SurfaceClass::Masked) {
                    const uint32_t out = state.opaque_count++;
                    new (&state.opaque[out]) OpaqueGeometryPrimitive{
                        .world    = node.world,
                        .bounds   = bounds,
                        .mesh     = d.mesh,
                        .material = d.material,
                    };
                    state.node_to_output[n] = { CompiledOutputKind::SurfacePrimitive, out };
                    new (&state.opaque_source_nodes[out]) NodeHandle{ n };
                }
                else if (surface != SurfaceClass::None) {
                    const uint32_t out = state.transparent_count++;
                    new (&state.transparent[out]) TransparentGeometryPrimitive{
                        .world    = node.world,
                        .bounds   = bounds,
                        .mesh     = d.mesh,
                        .material = d.material,
                        .depth    = 0.f,
                    };
                    state.node_to_output[n] = { CompiledOutputKind::SurfacePrimitive, out };
                    new (&state.transparent_source_nodes[out]) NodeHandle{ n };
                }
            }

            void push_splat(
                NodeHandle                  n,
                const RenderableDescriptor& d,
                const TransformNode&        node)
            {
                const uint32_t out = state.splat_count++;
                const Vec3 pos{ node.world.m[12], node.world.m[13], node.world.m[14] };
                new (&state.splats[out]) SplatPrimitive{
                    .position          = pos,
                    .scale             = d.splat_data.scale,
                    .rotation          = d.splat_data.rotation,
                    .color             = d.splat_data.color,
                    .opacity           = d.splat_data.opacity,
                    .cloud_handle      = d.splat_data.cloud_handle,
                    .cloud_local_index = d.splat_data.cloud_local_index,
                    .bounds            = transform_aabb(d.local_bounds, node.world),
                };
                state.node_to_output[n] = { CompiledOutputKind::Splat, out };
                new (&state.splat_source_nodes[out]) NodeHandle{ n };
            }

            void push_particle(
                NodeHandle                  n,
                const RenderableDescriptor& d,
                const TransformNode&        node)
            {
                if (d.mesh == INVALID_MESH)
                    return;

                const uint32_t out = state.particle_count++;
                new (&state.particles[out]) ParticlePrimitive{
                    .world    = node.world,
                    .color    = { 1.f, 1.f, 1.f, 1.f },
                    .mesh     = d.mesh,
                    .material = d.material,
                    .depth    = 0.f,
                };
                state.node_to_output[n] = { CompiledOutputKind::Particle, out };
                new (&state.particle_source_nodes[out]) NodeHandle{ n };
            }

            void push_terrain(
                NodeHandle                  n,
                const RenderableDescriptor& d,
                const TransformNode&        node)
            {
                if (!d.terrain_proxy_id.valid())
                    return;

                const uint32_t out = state.terrain_count++;
                new (&state.terrain_instances[out]) TerrainVisualInstance{
                    .world              = node.world,
                    .bounds             = transform_aabb(d.local_bounds, node.world),
                    .terrain_proxy_id   = d.terrain_proxy_id,
                    .visual_proxy_asset = d.terrain_visual_proxy_asset,
                    .visual_proxy_data  = d.terrain_visual_proxy_data,
                    .material           = d.material,
                    .visible            = d.visible,
                };
                state.node_to_output[n] = {
                    CompiledOutputKind::TerrainVisualInstance,
                    out,
                };
                new (&state.terrain_source_nodes[out]) NodeHandle{ n };
            }
        };

        AABB terrain_proxy_bounds_to_aabb(
            const wz::engine::assets::TerrainVisualProxyBounds& bounds)
        {
            return AABB{
                .min = { bounds.min[0], bounds.min[1], bounds.min[2] },
                .max = { bounds.max[0], bounds.max[1], bounds.max[2] },
            };
        }

        void select_view_terrain_lods(
            CompiledSceneStorage& storage,
            const ViewData& view)
        {
            CompiledSceneView& cs = storage.scene;
            std::span<TerrainLodChoice> choice_capacity =
                storage.terrain_lod_choice_capacity;
            if (choice_capacity.empty()) {
                cs.terrain_lod_choices = {};
                return;
            }

            const std::vector<TerrainLodChoice> previous(
                cs.terrain_lod_choices.begin(),
                cs.terrain_lod_choices.end());

            std::vector<TerrainChunkInfo> chunks;
            chunks.reserve(choice_capacity.size());
            auto chunk_asset_density = [](const auto& chunk) noexcept
            {
                const float extent_x =
                    chunk.bounds.max[0] - chunk.bounds.min[0];
                const float extent_z =
                    chunk.bounds.max[2] - chunk.bounds.min[2];
                const float area = extent_x * extent_z;
                if (area <= 0.0f) {
                    return 0.0f;
                }
                return static_cast<float>(chunk.triangle_count) / area;
            };
            for (uint32_t instance_index = 0u;
                 instance_index < cs.terrain_instances.size();
                 ++instance_index)
            {
                const TerrainVisualInstance& instance =
                    cs.terrain_instances[instance_index];
                if (!instance.visible || !instance.visual_proxy_data) {
                    continue;
                }

                const auto& proxy = *instance.visual_proxy_data;
                for (const auto& chunk : proxy.chunks) {
                    chunks.push_back(TerrainChunkInfo{
                        .terrain_instance_index = instance_index,
                        .chunk_id = chunk.chunk_id,
                        .representation_kind = chunk.representation_kind,
                        .world_bounds = transform_aabb(
                            terrain_proxy_bounds_to_aabb(chunk.bounds),
                            instance.world),
                        .asset_triangle_density =
                            chunk_asset_density(chunk),
                        .boundary = chunk.boundary,
                        .surfel_density_levels =
                            chunk.surfel_density_levels,
                        .lods = chunk.lods,
                    });
                }
            }

            std::vector<TerrainLodChoice> selected =
                select_terrain_lods(chunks, view.terrain_lod, view, previous);
            const uint32_t write_count =
                (std::min)(
                    static_cast<uint32_t>(selected.size()),
                    static_cast<uint32_t>(choice_capacity.size()));
            for (uint32_t i = 0u; i < write_count; ++i) {
                new (&choice_capacity[i]) TerrainLodChoice(selected[i]);
            }

            cs.terrain_lod_choices = std::span<const TerrainLodChoice>(
                choice_capacity.data(),
                write_count);
        }

        void patch_node_transform(
            CompiledSceneStorage&                 storage,
            const SceneGraph&                     g,
            std::span<const RenderableDescriptor> descs,
            const ViewData&                       view,
            bool                                  view_changed,
            NodeHandle                            n)
        {
            CompiledSceneView& cs = storage.scene;
            const CompiledSceneMetadataView& meta = storage.metadata;

            if (n >= static_cast<NodeHandle>(meta.node_to_output.size()))
                return;

            const CompiledOutputRef ref = meta.node_to_output[n];
            if (ref.kind == CompiledOutputKind::None || ref.index == INVALID_COMPILED_OUTPUT)
                return;

            const auto& node = node_data(g, n);

            switch (ref.kind) {
            case CompiledOutputKind::SurfacePrimitive: {
                const SurfaceClass surface = descs[n].node_class.default_surface;
                const AABB bounds = transform_aabb(descs[n].local_bounds, node.world);

                if (surface == SurfaceClass::Opaque || surface == SurfaceClass::Masked) {
                    auto& rec = const_cast<OpaqueGeometryPrimitive&>(cs.opaque[ref.index]);
                    rec.world = node.world;
                    rec.bounds = bounds;
                }
                else {
                    auto& rec = const_cast<TransparentGeometryPrimitive&>(cs.transparent[ref.index]);
                    rec.world = node.world;
                    rec.bounds = bounds;
                    if (!view_changed)
                        rec.depth = view_depth(view.view, aabb_center(bounds));
                }
                break;
            }
            case CompiledOutputKind::Splat: {
                const Vec3 pos{ node.world.m[12], node.world.m[13], node.world.m[14] };
                auto& rec = const_cast<SplatPrimitive&>(cs.splats[ref.index]);
                rec.position = pos;
                rec.bounds = transform_aabb(descs[n].local_bounds, node.world);
                if (!view_changed)
                    cs.splat_depths[ref.index] = view_depth(view.view, pos);
                break;
            }
            case CompiledOutputKind::Particle: {
                auto& rec = const_cast<ParticlePrimitive&>(cs.particles[ref.index]);
                rec.world = node.world;
                if (!view_changed) {
                    const Vec3 pos{ node.world.m[12], node.world.m[13], node.world.m[14] };
                    rec.depth = view_depth(view.view, pos);
                }
                break;
            }
            case CompiledOutputKind::TerrainVisualInstance: {
                auto& rec = const_cast<TerrainVisualInstance&>(
                    cs.terrain_instances[ref.index]);
                rec.world = node.world;
                rec.bounds = transform_aabb(descs[n].local_bounds, node.world);
                break;
            }
            default:
                break;
            }
        }

    } // namespace detail

    void update_view(CompiledSceneStorage& storage, const ViewData& view)
    {
        using namespace wz::core::algo::next;

        CompiledSceneView& cs = storage.scene;

        auto splat_depth_sink = detail::sink_fn([&](uint32_t i) {
            cs.splat_depths[i] = detail::view_depth(view.view, cs.splats[i].position);
        });
        transform(
            std::views::iota(0u, static_cast<uint32_t>(cs.splats.size())),
            splat_depth_sink,
            [](uint32_t i) { return i; });

        auto transparent_sink = detail::sink_fn([&](const TransparentGeometryPrimitive& p) {
            const Vec3 center = detail::aabb_center(p.bounds);
            const_cast<TransparentGeometryPrimitive&>(p).depth =
                detail::view_depth(view.view, center);
        });
        transform(cs.transparent, transparent_sink,
            [](const TransparentGeometryPrimitive& p) -> const TransparentGeometryPrimitive& { return p; });

        auto particle_sink = detail::sink_fn([&](const ParticlePrimitive& p) {
            const Vec3 pos{ p.world.m[12], p.world.m[13], p.world.m[14] };
            const_cast<ParticlePrimitive&>(p).depth = detail::view_depth(view.view, pos);
        });
        transform(cs.particles, particle_sink,
            [](const ParticlePrimitive& p) -> const ParticlePrimitive& { return p; });

        detail::select_view_terrain_lods(storage, view);

        cs.view = view;
    }

    CompiledSceneView compile(
        CompiledSceneStorage&                 storage,
        const SceneGraph&                     g,
        std::span<const RenderableDescriptor> descs,
        std::span<const LightRecord>          lights,
        const ViewData&                       view)
    {
        using namespace wz::core::algo::next;

        assert(descs.size() == node_count(g));

        auto counts = detail::count_primitives(
            g, descs, static_cast<uint32_t>(lights.size()));

        const size_t stable_size =
            sizeof(OpaqueGeometryPrimitive) * counts.opaque
            + alignof(OpaqueGeometryPrimitive)
            + sizeof(SplatPrimitive) * counts.splats
            + alignof(SplatPrimitive)
            + sizeof(LightRecord) * counts.lights
            + alignof(LightRecord)
            + sizeof(TerrainVisualInstance) * counts.terrain
            + alignof(TerrainVisualInstance);

        storage.stable_stats.reset_build_counters();
        storage.stable_stats.record_owned(storage.stable_bytes);

        if (stable_size > storage.stable_bytes)
        {
            storage.stable_buffer = std::make_unique<std::byte[]>(stable_size);
            storage.stable_bytes = stable_size;

            storage.stable_stats.record_reallocation(stable_size);
        }
        else
        {
            storage.stable_stats.record_owned(storage.stable_bytes);
        }
        std::byte* sp = storage.stable_buffer.get();
        std::byte* se = sp + stable_size;

        std::span<OpaqueGeometryPrimitive> opaque_w;
        std::span<SplatPrimitive>          splats_w;
        std::span<LightRecord>             lights_w;
        std::span<TerrainVisualInstance>   terrain_w;

        sp = wz::core::graph::detail::carve<OpaqueGeometryPrimitive>(
            sp, se, counts.opaque, opaque_w);
        sp = wz::core::graph::detail::carve<SplatPrimitive>(
            sp, se, counts.splats, splats_w);
        sp = wz::core::graph::detail::carve<LightRecord>(
            sp, se, counts.lights, lights_w);
        sp = wz::core::graph::detail::carve<TerrainVisualInstance>(
            sp, se, counts.terrain, terrain_w);

        const uint32_t n_nodes = node_count(g);

        const size_t metadata_size =
            sizeof(CompiledOutputRef) * n_nodes
            + alignof(CompiledOutputRef)
            + sizeof(NodeHandle) * counts.opaque
            + sizeof(NodeHandle) * counts.transparent
            + sizeof(NodeHandle) * counts.splats
            + sizeof(NodeHandle) * counts.particles
            + sizeof(NodeHandle) * counts.lights
            + sizeof(NodeHandle) * counts.terrain
            + alignof(NodeHandle);

        storage.metadata_stats.reset_build_counters();
        storage.metadata_stats.record_owned(storage.metadata_bytes);

        if (metadata_size > storage.metadata_bytes)
        {
            storage.metadata_buffer = std::make_unique<std::byte[]>(metadata_size);
            storage.metadata_bytes = metadata_size;

            storage.metadata_stats.record_reallocation(metadata_size);
        }
        else
        {
            storage.metadata_stats.record_owned(storage.metadata_bytes);
        }

        std::byte* mp = storage.metadata_buffer.get();
        std::byte* me = mp + metadata_size;

        std::span<CompiledOutputRef> node_to_output_w;
        std::span<NodeHandle>        opaque_src_w;
        std::span<NodeHandle>        transparent_src_w;
        std::span<NodeHandle>        splat_src_w;
        std::span<NodeHandle>        particle_src_w;
        std::span<NodeHandle>        light_src_w;
        std::span<NodeHandle>        terrain_src_w;

        mp = wz::core::graph::detail::carve<CompiledOutputRef>(
            mp, me, n_nodes, node_to_output_w);
        mp = wz::core::graph::detail::carve<NodeHandle>(
            mp, me, counts.opaque, opaque_src_w);
        mp = wz::core::graph::detail::carve<NodeHandle>(
            mp, me, counts.transparent, transparent_src_w);
        mp = wz::core::graph::detail::carve<NodeHandle>(
            mp, me, counts.splats, splat_src_w);
        mp = wz::core::graph::detail::carve<NodeHandle>(
            mp, me, counts.particles, particle_src_w);
        mp = wz::core::graph::detail::carve<NodeHandle>(
            mp, me, counts.lights, light_src_w);
        mp = wz::core::graph::detail::carve<NodeHandle>(
            mp, me, counts.terrain, terrain_src_w);

        auto init_node_output_sink = detail::sink_fn([&](uint32_t i) {
            new (&node_to_output_w[i]) CompiledOutputRef{};
        });
        transform(
            std::views::iota(0u, n_nodes),
            init_node_output_sink,
            [](uint32_t i) { return i; });

        const size_t view_size =
            sizeof(TransparentGeometryPrimitive) * counts.transparent
            + alignof(TransparentGeometryPrimitive)
            + sizeof(ParticlePrimitive) * counts.particles
            + alignof(ParticlePrimitive)
            + sizeof(float) * counts.splats
            + alignof(float)
            + sizeof(TerrainLodChoice) * counts.terrain_lod_choices
            + alignof(TerrainLodChoice);

        storage.view_stats.reset_build_counters();
        storage.view_stats.record_owned(storage.view_bytes);

        if (view_size > storage.view_bytes)
        {
            storage.view_buffer = std::make_unique<std::byte[]>(view_size);
            storage.view_bytes = view_size;

            storage.view_stats.record_reallocation(view_size);
        }
        else
        {
            storage.view_stats.record_owned(storage.view_bytes);
        }

        std::byte* vp = storage.view_buffer.get();
        std::byte* ve = vp + view_size;

        std::span<TransparentGeometryPrimitive> transparent_w;
        std::span<ParticlePrimitive>            particles_w;
        std::span<float>                        splat_depths_w;
        std::span<TerrainLodChoice>             terrain_lod_choices_w;

        vp = wz::core::graph::detail::carve<TransparentGeometryPrimitive>(
            vp, ve, counts.transparent, transparent_w);
        vp = wz::core::graph::detail::carve<ParticlePrimitive>(
            vp, ve, counts.particles, particles_w);
        vp = wz::core::graph::detail::carve<float>(
            vp, ve, counts.splats, splat_depths_w);
        vp = wz::core::graph::detail::carve<TerrainLodChoice>(
            vp, ve, counts.terrain_lod_choices, terrain_lod_choices_w);

        detail::CompileFillState fill_state{
            .opaque                  = opaque_w,
            .splats                  = splats_w,
            .transparent             = transparent_w,
            .particles               = particles_w,
            .terrain_instances       = terrain_w,
            .terrain_lod_choices     = terrain_lod_choices_w,
            .node_to_output          = node_to_output_w,
            .opaque_source_nodes     = opaque_src_w,
            .transparent_source_nodes = transparent_src_w,
            .splat_source_nodes      = splat_src_w,
            .particle_source_nodes   = particle_src_w,
            .terrain_source_nodes    = terrain_src_w,
        };
        detail::CompileNodeSink compile_sink{ fill_state, g, descs };
        transform(topo_order(g), compile_sink, [](NodeHandle n) { return n; });

        auto light_sink = detail::sink_fn([&](uint32_t i) {
            new (&lights_w[i]) LightRecord(lights[i]);
            new (&light_src_w[i]) NodeHandle{ INVALID_NODE };
        });
        transform(
            std::views::iota(0u, counts.lights),
            light_sink,
            [](uint32_t i) { return i; });

        auto zero_splat_depth_sink = detail::sink_fn([&](uint32_t i) {
            splat_depths_w[i] = 0.f;
        });
        transform(
            std::views::iota(0u, counts.splats),
            zero_splat_depth_sink,
            [](uint32_t i) { return i; });

        storage.scene = CompiledSceneView{
            .opaque       = opaque_w,
            .splats       = splats_w,
            .lights       = lights_w,
            .terrain_instances = terrain_w,
            .transparent  = transparent_w,
            .particles    = particles_w,
            .splat_depths = splat_depths_w,
            .terrain_lod_choices = terrain_lod_choices_w,
            .view         = {},
        };
        storage.terrain_lod_choice_capacity = terrain_lod_choices_w;

        storage.metadata = CompiledSceneMetadataView{
            .node_to_output           = node_to_output_w,
            .opaque_source_nodes      = opaque_src_w,
            .transparent_source_nodes = transparent_src_w,
            .splat_source_nodes       = splat_src_w,
            .particle_source_nodes    = particle_src_w,
            .light_source_nodes       = light_src_w,
            .terrain_source_nodes     = terrain_src_w,
        };

        update_view(storage, view);
        return storage.scene;
    }

    void update_compiled_descriptors(
        CompiledSceneStorage&                 storage,
        std::span<const RenderableDescriptor> descs)
    {
        using namespace wz::core::algo::next;

        CompiledSceneView& cs = storage.scene;
        const CompiledSceneMetadataView& meta = storage.metadata;

        auto opaque_sink = detail::sink_fn([&](uint32_t i) {
            const NodeHandle n = meta.opaque_source_nodes[i];
            auto& rec = const_cast<OpaqueGeometryPrimitive&>(cs.opaque[i]);
            rec.mesh = descs[n].mesh;
            rec.material = descs[n].material;
        });
        transform(std::views::iota(0u, static_cast<uint32_t>(meta.opaque_source_nodes.size())),
            opaque_sink, [](uint32_t i) { return i; });

        auto transparent_sink = detail::sink_fn([&](uint32_t i) {
            const NodeHandle n = meta.transparent_source_nodes[i];
            auto& rec = const_cast<TransparentGeometryPrimitive&>(cs.transparent[i]);
            rec.mesh = descs[n].mesh;
            rec.material = descs[n].material;
        });
        transform(std::views::iota(0u, static_cast<uint32_t>(meta.transparent_source_nodes.size())),
            transparent_sink, [](uint32_t i) { return i; });

        auto splat_sink = detail::sink_fn([&](uint32_t i) {
            const NodeHandle n = meta.splat_source_nodes[i];
            auto& rec = const_cast<SplatPrimitive&>(cs.splats[i]);
            rec.scale = descs[n].splat_data.scale;
            rec.rotation = descs[n].splat_data.rotation;
            rec.color = descs[n].splat_data.color;
            rec.opacity = descs[n].splat_data.opacity;
            rec.cloud_handle = descs[n].splat_data.cloud_handle;
            rec.cloud_local_index = descs[n].splat_data.cloud_local_index;
        });
        transform(std::views::iota(0u, static_cast<uint32_t>(meta.splat_source_nodes.size())),
            splat_sink, [](uint32_t i) { return i; });

        auto particle_sink = detail::sink_fn([&](uint32_t i) {
            const NodeHandle n = meta.particle_source_nodes[i];
            auto& rec = const_cast<ParticlePrimitive&>(cs.particles[i]);
            rec.mesh = descs[n].mesh;
            rec.material = descs[n].material;
        });
        transform(std::views::iota(0u, static_cast<uint32_t>(meta.particle_source_nodes.size())),
            particle_sink, [](uint32_t i) { return i; });

        auto terrain_sink = detail::sink_fn([&](uint32_t i) {
            const NodeHandle n = meta.terrain_source_nodes[i];
            auto& rec = const_cast<TerrainVisualInstance&>(
                cs.terrain_instances[i]);
            rec.terrain_proxy_id = descs[n].terrain_proxy_id;
            rec.visual_proxy_asset = descs[n].terrain_visual_proxy_asset;
            rec.visual_proxy_data = descs[n].terrain_visual_proxy_data;
            rec.material = descs[n].material;
            rec.visible = descs[n].visible;
        });
        transform(std::views::iota(0u, static_cast<uint32_t>(meta.terrain_source_nodes.size())),
            terrain_sink, [](uint32_t i) { return i; });
    }

    void update_compiled_transforms(
        CompiledSceneStorage&                 storage,
        const SceneGraph&                     g,
        std::span<const RenderableDescriptor> descs,
        const ViewData&                       view,
        std::span<const NodeHandle>           dirty_nodes,
        bool                                  view_changed)
    {
        using namespace wz::core::algo::next;

        auto patch_sink = detail::sink_fn([&](NodeHandle n) {
            detail::patch_node_transform(storage, g, descs, view, view_changed, n);
        });

        if (dirty_nodes.empty()) {
            transform(storage.metadata.opaque_source_nodes, patch_sink, [](NodeHandle n) { return n; });
            transform(storage.metadata.transparent_source_nodes, patch_sink, [](NodeHandle n) { return n; });
            transform(storage.metadata.splat_source_nodes, patch_sink, [](NodeHandle n) { return n; });
            transform(storage.metadata.particle_source_nodes, patch_sink, [](NodeHandle n) { return n; });
            transform(storage.metadata.terrain_source_nodes, patch_sink, [](NodeHandle n) { return n; });
        }
        else {
            transform(dirty_nodes, patch_sink, [](NodeHandle n) { return n; });
        }

        if (view_changed)
            update_view(storage, view);
        else
            storage.scene.view = view;
    }

    CompiledSceneView refresh_compiled_scene(
        CompiledSceneStorage&                 storage,
        const SceneGraph&                     g,
        std::span<const RenderableDescriptor> descs,
        std::span<const LightRecord>          lights,
        const ViewData&                       view,
        SceneDirtyBits                        dirty,
        std::span<const NodeHandle>           dirty_nodes)
    {
        if (!any(dirty))
            return storage.scene;

        const SceneDirtyBits compile_mask =
            SceneDirtyBits::Topology |
            SceneDirtyBits::Geometry |
            SceneDirtyBits::Visibility |
            SceneDirtyBits::Metadata |
            SceneDirtyBits::Lighting |
            SceneDirtyBits::Animation |
            SceneDirtyBits::Constraint |
            SceneDirtyBits::Procedural;

        if (any(dirty & compile_mask))
            return compile(storage, g, descs, lights, view);

        const SceneDirtyBits descriptor_mask = SceneDirtyBits::Material;
        const SceneDirtyBits transform_mask =
            SceneDirtyBits::Transform |
            SceneDirtyBits::Bounds;

        if (any(dirty & transform_mask)) {
            const bool vd = any(dirty & SceneDirtyBits::View);
            update_compiled_transforms(storage, g, descs, view, dirty_nodes, vd);
            if (any(dirty & descriptor_mask))
                update_compiled_descriptors(storage, descs);
            return storage.scene;
        }

        if (any(dirty & descriptor_mask)) {
            update_compiled_descriptors(storage, descs);
            update_view(storage, view);
            return storage.scene;
        }

        update_view(storage, view);
        return storage.scene;
    }

} // namespace wz::scene
