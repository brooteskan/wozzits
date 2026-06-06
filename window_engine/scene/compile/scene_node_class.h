#pragma once

// wz/scene/scene_node_class.h
//
// Orthogonal classification axes for scene nodes.
//
// Each enum represents one independent question the compiler can ask.
// SceneNodeClass is a named product of those axes — it describes what a node
// IS, not what changed or what was emitted.
//
// SceneDirtyBits is separate: it describes what changed this frame.
// CompiledOutputRef (compiled_scene.h) describes where compiled output lives.

#include <cstdint>

namespace wz::scene {

    // ─── SceneRole ────────────────────────────────────────────────────────────────
    //
    // The high-level role of a node in the scene.
    // Answers: is this a renderable, a light, a camera, or something organisational?

    enum class SceneRole : uint8_t
    {
        None = 0,

        Empty,
        Renderable,
        Light,
        Camera,
        Volume,
        Probe,
        Occluder,
        Marker,
        Constraint,
        Custom,
    };


    // ─── ProducerKind ─────────────────────────────────────────────────────────────
    //
    // What kind of renderable payload the node produces, if any.
    // Answers: does this emit mesh primitives, splats, particles, terrain?

    enum class ProducerKind : uint8_t
    {
        None = 0,

        Mesh,
        InstancedMesh,
        SkinnedMesh,
        SplatCloud,
        ParticleEmitter,
        TerrainPatch,
        Decal,
        VolumeRenderable,
        Line,
        Trail,
        Sprite,
        Custom,
    };


    // ─── SurfaceClass ─────────────────────────────────────────────────────────────
    //
    // Material/surface behaviour relevant to visibility, ordering, and pass routing.
    // Answers: opaque pass? needs depth sorting? additive blend?
    //
    // Placement rule: SurfaceClass usually belongs to emitted primitives or material
    // slots, not to the source node. SceneNodeClass.default_surface is a convenience
    // for simple single-material producers only. It is not authoritative for
    // multi-primitive producers.

    enum class SurfaceClass : uint8_t
    {
        None = 0,

        Opaque,
        Masked,
        Transparent,
        Additive,
        AlphaComposite,
        Overlay,
    };


    // ─── SpatialKind ─────────────────────────────────────────────────────────────
    //
    // The spatial representation used for transforms, bounds, culling, and
    // visibility queries.
    // Answers: does this node have bounds? what shape? can it be frustum culled?

    enum class SpatialKind : uint8_t
    {
        None = 0,

        Point,
        Sphere,
        Box,
        OrientedBox,
        MeshBounds,
        Frustum,
        Infinite,
        Procedural,
    };


    // ─── CompileBehavior ──────────────────────────────────────────────────────────
    //
    // How the compiler should expect this node to change over time.
    // Answers: stable, transform-only dynamic, fully dynamic, procedural, streaming?
    //
    // This is NOT dirty state. It describes expected behaviour and informs
    // allocation/update strategy. Dirty bits (SceneDirtyBits) describe what
    // actually changed this frame.

    enum class CompileBehavior : uint8_t
    {
        None = 0,

        Static,
        DynamicTransform,
        DynamicBounds,
        DynamicMaterial,
        DynamicGeometry,
        DynamicTopology,
        Procedural,
        Streaming,
    };


    // ─── RenderDomain ─────────────────────────────────────────────────────────────
    //
    // Pass/domain membership. A node may participate in multiple domains.
    // Answers: does this produce surface packets, shadow packets, splat packets?
    //
    // RenderDomainMask is an OR combination of RenderDomain values.
    // These are routing facts, not source identity or material class.

    enum class RenderDomain : uint16_t
    {
        None         = 0,

        Surface      = 1 << 0,
        Shadow       = 1 << 1,
        Transparent  = 1 << 2,
        Lighting     = 1 << 3,
        Decal        = 1 << 4,
        Volume       = 1 << 5,
        Particle     = 1 << 6,
        Splat        = 1 << 7,
        MotionVector = 1 << 8,
        Debug        = 1 << 9,
    };

    using RenderDomainMask = uint16_t;

    inline RenderDomainMask operator|(RenderDomain a, RenderDomain b) noexcept
    {
        return static_cast<RenderDomainMask>(a) | static_cast<RenderDomainMask>(b);
    }

    inline RenderDomainMask operator|(RenderDomainMask a, RenderDomain b) noexcept
    {
        return a | static_cast<RenderDomainMask>(b);
    }

    inline bool domain_has(RenderDomainMask mask, RenderDomain d) noexcept
    {
        return (mask & static_cast<RenderDomainMask>(d)) != 0;
    }


    // ─── SceneDirtyBits ──────────────────────────────────────────────────────────
    //
    // What changed this frame on a node or in the scene.
    // Separate from SceneNodeClass — a static node can have a dirty transform
    // without "becoming" dynamic.

    enum class SceneDirtyBits : uint32_t
    {
        None       = 0,

        View       = 1 << 0,
        Transform  = 1 << 1,
        Bounds     = 1 << 2,
        Material   = 1 << 3,
        Geometry   = 1 << 4,
        Topology   = 1 << 5,
        Visibility = 1 << 6,
        Lighting   = 1 << 7,
        Metadata   = 1 << 8,

        // Future evaluated-scene concerns
        Animation  = 1 << 9,
        Constraint = 1 << 10,
        Procedural = 1 << 11,
    };

    inline SceneDirtyBits operator|(SceneDirtyBits a, SceneDirtyBits b) noexcept
    {
        return static_cast<SceneDirtyBits>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline SceneDirtyBits operator&(SceneDirtyBits a, SceneDirtyBits b) noexcept
    {
        return static_cast<SceneDirtyBits>(
            static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline SceneDirtyBits& operator|=(SceneDirtyBits& a, SceneDirtyBits b) noexcept
    {
        a = a | b;
        return a;
    }

    inline bool any(SceneDirtyBits bits) noexcept
    {
        return static_cast<uint32_t>(bits) != 0;
    }

    inline bool dirty(SceneDirtyBits bits, SceneDirtyBits flag) noexcept
    {
        return any(bits & flag);
    }


    // ─── SceneNodeClass ───────────────────────────────────────────────────────────
    //
    // A named product of orthogonal classification axes describing what a node IS.
    //
    // Compiler phases query only the axes they care about. No phase should switch
    // on a monolithic node-kind value.
    //
    // default_surface is a convenience for simple single-material producers.
    // It is NOT authoritative for nodes that emit multiple primitives with
    // different surface classes — those belong on emitted CompiledSurfacePrimitive
    // records, not here.

    struct SceneNodeClass
    {
        SceneRole        role            = SceneRole::None;
        ProducerKind     producer        = ProducerKind::None;
        SurfaceClass     default_surface = SurfaceClass::None;
        SpatialKind      spatial         = SpatialKind::None;
        CompileBehavior  compile         = CompileBehavior::None;
        RenderDomainMask domains         = 0;
    };

} // namespace wz::scene
