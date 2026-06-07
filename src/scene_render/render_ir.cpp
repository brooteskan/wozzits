#include <render/ir/render_ir.h>
#include <math/frustum.h>
#include <algo/next.h>

#include <new>
#include <ranges>

namespace wz::render {

    using namespace wz::scene;
    using namespace wz::math;

    namespace detail {

        inline uint64_t opaque_key(const OpaqueGeometryPrimitive& p)
        {
            return static_cast<uint64_t>(p.material) << 32
                | static_cast<uint64_t>(p.mesh);
        }

        inline uint64_t terrain_batch_key(
            const TerrainVisualInstance& instance,
            const TerrainLodChoice& choice)
        {
            return static_cast<uint64_t>(instance.material) << 32
                | (static_cast<uint64_t>(
                       choice.terrain_instance_index & 0xffffu) << 16)
                | static_cast<uint64_t>(choice.lod_id.value & 0xffffu);
        }

        inline uint64_t depth_key_back_to_front(float depth)
        {
            uint32_t bits;
            std::memcpy(&bits, &depth, sizeof(float));
            return static_cast<uint64_t>(~bits);
        }

        inline bool aabb_in_frustum(const Frustum& f, const AABB& aabb)
        {
            const std::span<const Plane> planes{ f.planes };
            return wz::core::algo::next::reduce(
                planes,
                true,
                [&aabb](bool inside, const Plane& p) {
                    if (!inside)
                        return false;

                    const float px = p.normal.x >= 0.f ? aabb.max.x : aabb.min.x;
                    const float py = p.normal.y >= 0.f ? aabb.max.y : aabb.min.y;
                    const float pz = p.normal.z >= 0.f ? aabb.max.z : aabb.min.z;

                    return p.normal.x * px + p.normal.y * py + p.normal.z * pz + p.distance >= 0.f;
                });
        }

        inline bool point_in_frustum(const Frustum& f, const Vec3& pt)
        {
            const std::span<const Plane> planes{ f.planes };
            return wz::core::algo::next::reduce(
                planes,
                true,
                [&pt](bool inside, const Plane& p) {
                    return inside
                        && p.normal.x * pt.x + p.normal.y * pt.y + p.normal.z * pt.z + p.distance >= 0.f;
                });
        }

        inline bool aabb_is_degenerate(const AABB& aabb) noexcept
        {
            return aabb.min.x == aabb.max.x
                && aabb.min.y == aabb.max.y
                && aabb.min.z == aabb.max.z;
        }

        inline bool splat_in_frustum(
            const Frustum& f,
            const AABB&    bounds,
            const Vec3&    center_fallback)
        {
            if (aabb_is_degenerate(bounds))
                return point_in_frustum(f, center_fallback);
            return aabb_in_frustum(f, bounds);
        }

        struct DrawRefCandidate {
            bool    visible{ false };
            DrawRef ref{};
        };

        struct DrawRefSink {
            DrawRef* ptr{ nullptr };
            uint32_t count{ 0 };

            explicit DrawRefSink(std::span<DrawRef> out) noexcept
                : ptr(out.data()) {}

            bool push(const DrawRefCandidate& candidate)
            {
                if (!candidate.visible)
                    return true;

                new (&ptr[count++]) DrawRef(candidate.ref);
                return true;
            }
        };

        struct TerrainDrawRefCandidate {
            bool visible{ false };
            TerrainDrawRef ref{};
        };

        struct TerrainDrawRefSink {
            TerrainDrawRef* ptr{ nullptr };
            uint32_t count{ 0 };

            explicit TerrainDrawRefSink(
                std::span<TerrainDrawRef> out) noexcept
                : ptr(out.data()) {}

            bool push(const TerrainDrawRefCandidate& candidate)
            {
                if (!candidate.visible)
                    return true;

                new (&ptr[count++]) TerrainDrawRef(candidate.ref);
                return true;
            }
        };

        template<typename MakeCandidate>
        uint32_t fill_sorted_refs(
            uint32_t           total,
            std::span<DrawRef> out,
            MakeCandidate&&    make_candidate)
        {
            using namespace wz::core::algo::next;

            DrawRefSink sink{ out };
            transform(
                std::views::iota(0u, total),
                sink,
                std::forward<MakeCandidate>(make_candidate));

            std::sort(out.begin(), out.begin() + sink.count,
                [](const DrawRef& a, const DrawRef& b) { return a.sort_key < b.sort_key; });

            return sink.count;
        }

        template<typename MakeCandidate>
        uint32_t fill_sorted_terrain_refs(
            uint32_t                    total,
            std::span<TerrainDrawRef>   out,
            MakeCandidate&&             make_candidate)
        {
            using namespace wz::core::algo::next;

            TerrainDrawRefSink sink{ out };
            transform(
                std::views::iota(0u, total),
                sink,
                std::forward<MakeCandidate>(make_candidate));

            std::sort(out.begin(), out.begin() + sink.count,
                [](const TerrainDrawRef& a, const TerrainDrawRef& b)
                {
                    return a.sort_key < b.sort_key;
                });

            return sink.count;
        }

        CullingStats recull_sections(
            RenderIRStorage&         storage,
            const CompiledSceneView& scene)
        {
            const Frustum frustum = frustum_from_view_projection(scene.view.view_projection);

            auto opaque_w = std::span<DrawRef>(storage.opaque_base, storage.opaque_capacity);
            auto transparent_w = std::span<DrawRef>(
                storage.transparent_base, storage.transparent_capacity);
            auto splats_w = std::span<DrawRef>(storage.splat_base, storage.splat_capacity);
            auto particles_w = std::span<DrawRef>(
                storage.particle_base, storage.particle_capacity);
            auto terrain_w = std::span<TerrainDrawRef>(
                storage.terrain_base, storage.terrain_capacity);

            const uint32_t visible_opaque = fill_sorted_refs(
                static_cast<uint32_t>(scene.opaque.size()),
                opaque_w,
                [&scene, &frustum](uint32_t i) -> DrawRefCandidate {
                    const auto& p = scene.opaque[i];
                    return {
                        .visible = aabb_in_frustum(frustum, p.bounds),
                        .ref     = { i, opaque_key(p) },
                    };
                });

            const uint32_t visible_transparent = fill_sorted_refs(
                static_cast<uint32_t>(scene.transparent.size()),
                transparent_w,
                [&scene, &frustum](uint32_t i) -> DrawRefCandidate {
                    const auto& p = scene.transparent[i];
                    return {
                        .visible = aabb_in_frustum(frustum, p.bounds),
                        .ref     = { i, depth_key_back_to_front(p.depth) },
                    };
                });

            const uint32_t visible_splats = fill_sorted_refs(
                static_cast<uint32_t>(scene.splats.size()),
                splats_w,
                [&scene, &frustum](uint32_t i) -> DrawRefCandidate {
                    const auto& p = scene.splats[i];
                    return {
                        .visible = splat_in_frustum(frustum, p.bounds, p.position),
                        .ref     = { i, depth_key_back_to_front(scene.splat_depths[i]) },
                    };
                });

            const uint32_t visible_particles = fill_sorted_refs(
                static_cast<uint32_t>(scene.particles.size()),
                particles_w,
                [&scene, &frustum](uint32_t i) -> DrawRefCandidate {
                    const auto& p = scene.particles[i];
                    const Vec3 pos{ p.world.m[12], p.world.m[13], p.world.m[14] };
                    return {
                        .visible = point_in_frustum(frustum, pos),
                        .ref     = { i, depth_key_back_to_front(p.depth) },
                    };
                });

            const uint32_t visible_terrain = fill_sorted_terrain_refs(
                static_cast<uint32_t>(scene.terrain_lod_choices.size()),
                terrain_w,
                [&scene, &frustum](uint32_t i) -> TerrainDrawRefCandidate {
                    const TerrainLodChoice& choice =
                        scene.terrain_lod_choices[i];
                    if (choice.terrain_instance_index
                        >= scene.terrain_instances.size())
                    {
                        return {};
                    }

                    const TerrainVisualInstance& instance =
                        scene.terrain_instances[choice.terrain_instance_index];
                    const uint64_t batch =
                        terrain_batch_key(instance, choice);
                    return {
                        .visible = instance.visible
                            && aabb_in_frustum(frustum, instance.bounds),
                        .ref = TerrainDrawRef{
                            .terrain_instance_index =
                                choice.terrain_instance_index,
                            .chunk_id = choice.chunk_id,
                            .representation_kind =
                                choice.representation_kind,
                            .lod_id = choice.lod_id,
                            .batch_key = batch,
                            .sort_key = batch,
                        },
                    };
                });

            storage.ir.opaque      = { opaque_w.data(),      visible_opaque };
            storage.ir.transparent = { transparent_w.data(), visible_transparent };
            storage.ir.splats      = { splats_w.data(),      visible_splats };
            storage.ir.particles   = { particles_w.data(),   visible_particles };
            storage.ir.terrain     = { terrain_w.data(),     visible_terrain };

            return CullingStats{
                .visible_opaque      = visible_opaque,
                .culled_opaque       = static_cast<uint32_t>(scene.opaque.size()) - visible_opaque,
                .visible_transparent = visible_transparent,
                .culled_transparent  = static_cast<uint32_t>(scene.transparent.size()) - visible_transparent,
                .visible_splats      = visible_splats,
                .culled_splats       = static_cast<uint32_t>(scene.splats.size()) - visible_splats,
                .visible_particles   = visible_particles,
                .culled_particles    = static_cast<uint32_t>(scene.particles.size()) - visible_particles,
                .visible_terrain     = visible_terrain,
                .culled_terrain      =
                    static_cast<uint32_t>(scene.terrain_lod_choices.size())
                    - visible_terrain,
            };
        }

    } // namespace detail

    RenderIRView build_render_ir(RenderIRStorage& storage, CompiledSceneView scene)
    {
        const CompiledSceneView& cs = scene;
        const uint32_t opaque_total      = static_cast<uint32_t>(cs.opaque.size());
        const uint32_t transparent_total = static_cast<uint32_t>(cs.transparent.size());
        const uint32_t splat_total       = static_cast<uint32_t>(cs.splats.size());
        const uint32_t particle_total    = static_cast<uint32_t>(cs.particles.size());
        const uint32_t terrain_total     =
            static_cast<uint32_t>(cs.terrain_lod_choices.size());

        const size_t buf_size =
            sizeof(DrawRef) * opaque_total        + alignof(DrawRef)
            + sizeof(DrawRef) * transparent_total + alignof(DrawRef)
            + sizeof(DrawRef) * splat_total       + alignof(DrawRef)
            + sizeof(DrawRef) * particle_total    + alignof(DrawRef)
            + sizeof(TerrainDrawRef) * terrain_total
            + alignof(TerrainDrawRef);

        storage.stats.reset_build_counters();
        storage.stats.record_owned(storage.buffer_bytes);

        if (buf_size > storage.buffer_bytes)
        {
            storage.buffer = std::make_unique<std::byte[]>(buf_size);
            storage.buffer_bytes = buf_size;

            storage.stats.record_reallocation(buf_size);
        }
        else
        {
            storage.stats.record_owned(storage.buffer_bytes);
        }

        std::byte* ptr = storage.buffer.get();
        std::byte* end = ptr + buf_size;

        std::span<DrawRef> opaque_w;
        std::span<DrawRef> transparent_w;
        std::span<DrawRef> splats_w;
        std::span<DrawRef> particles_w;
        std::span<TerrainDrawRef> terrain_w;

        ptr = wz::core::graph::detail::carve<DrawRef>(ptr, end, opaque_total,      opaque_w);
        ptr = wz::core::graph::detail::carve<DrawRef>(ptr, end, transparent_total, transparent_w);
        ptr = wz::core::graph::detail::carve<DrawRef>(ptr, end, splat_total,       splats_w);
        ptr = wz::core::graph::detail::carve<DrawRef>(ptr, end, particle_total,    particles_w);
        ptr = wz::core::graph::detail::carve<TerrainDrawRef>(
            ptr, end, terrain_total, terrain_w);

        storage.opaque_base          = opaque_w.data();
        storage.transparent_base     = transparent_w.data();
        storage.splat_base           = splats_w.data();
        storage.particle_base        = particles_w.data();
        storage.terrain_base         = terrain_w.data();
        storage.opaque_capacity      = opaque_total;
        storage.transparent_capacity = transparent_total;
        storage.splat_capacity       = splat_total;
        storage.particle_capacity    = particle_total;
        storage.terrain_capacity     = terrain_total;

        const CullingStats culling = detail::recull_sections(storage, cs);

        storage.ir = RenderIRView{
            .opaque      = storage.ir.opaque,
            .transparent = storage.ir.transparent,
            .splats      = storage.ir.splats,
            .particles   = storage.ir.particles,
            .terrain     = storage.ir.terrain,
            .source      = scene,
            .culling     = culling,
        };

        return storage.ir;
    }

    void update_render_ir(RenderIRStorage& storage, CompiledSceneView scene)
    {
        storage.ir.source = scene;
        storage.ir.culling = detail::recull_sections(storage, scene);
    }

} // namespace wz::render
