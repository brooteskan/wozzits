#pragma once

// wz/scene/legacy_classification.h
//
// Bridge from the legacy RenderPipeline enum to the orthogonal SceneNodeClass
// axes. Used by existing call sites (tests, tools) that predate the
// RenderableDescriptor::node_class field.
//
// classify_legacy_renderable() is a one-way conversion: given a RenderPipeline
// value, it returns the equivalent SceneNodeClass. New code should populate
// RenderableDescriptor::node_class directly without going through this helper.

#include <scene/compile/scene_node_class.h>
#include <scene/compile/compiled_scene.h>

namespace wz::scene {

    // Maps a legacy RenderPipeline value to a provisional SceneNodeClass.
    //
    //   OpaqueGeometry      -> Renderable / Mesh / Opaque      / MeshBounds / Static / Surface|Shadow
    //   TransparentGeometry -> Renderable / Mesh / Transparent / MeshBounds / Static / Surface|Transparent
    //   Splat               -> Renderable / SplatCloud / Transparent / Box / Static / Splat|Transparent
    //   Particle            -> Renderable / ParticleEmitter / Transparent / Procedural / Procedural / Particle|Transparent
    //   None                -> None (empty/non-renderable node)

    inline SceneNodeClass classify_legacy_renderable(RenderPipeline pipeline) noexcept
    {
        switch (pipeline) {

        case RenderPipeline::OpaqueGeometry:
            return {
                .role            = SceneRole::Renderable,
                .producer        = ProducerKind::Mesh,
                .default_surface = SurfaceClass::Opaque,
                .spatial         = SpatialKind::MeshBounds,
                .compile         = CompileBehavior::Static,
                .domains         = RenderDomain::Surface | RenderDomain::Shadow,
            };

        case RenderPipeline::TransparentGeometry:
            return {
                .role            = SceneRole::Renderable,
                .producer        = ProducerKind::Mesh,
                .default_surface = SurfaceClass::Transparent,
                .spatial         = SpatialKind::MeshBounds,
                .compile         = CompileBehavior::Static,
                .domains         = RenderDomain::Surface | RenderDomain::Transparent,
            };

        case RenderPipeline::Splat:
            return {
                .role            = SceneRole::Renderable,
                .producer        = ProducerKind::SplatCloud,
                .default_surface = SurfaceClass::Transparent,
                .spatial         = SpatialKind::Box,
                .compile         = CompileBehavior::Static,
                .domains         = RenderDomain::Splat | RenderDomain::Transparent,
            };

        case RenderPipeline::Particle:
            return {
                .role            = SceneRole::Renderable,
                .producer        = ProducerKind::ParticleEmitter,
                .default_surface = SurfaceClass::Transparent,
                .spatial         = SpatialKind::Procedural,
                .compile         = CompileBehavior::Procedural,
                .domains         = RenderDomain::Particle | RenderDomain::Transparent,
            };

        case RenderPipeline::None:
        default:
            return {};
        }
    }

} // namespace wz::scene
