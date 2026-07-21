#pragma once

// engine/assets/renderable/render_binding_sources.h
//
// THE map from a bindable source asset type to the GPU resource its compiler
// publishes: the resource kind a layout row must declare for it, and the
// rhi_asset_identity variant string it is resident under (issue #228). These
// pairs are the publisher contracts that were previously scattered
// conventions between the asset compilers and RhiSceneRenderer's hardcoded
// per-recipe tables:
//
//   ScalarField        → TextureSrv,    "field_texture"         (scalar_field_compilers.cpp)
//   VectorField        → TextureSrv,    "vector_field_texture"  (vector_field_compilers.cpp)
//   GaussianSplatCloud → StructuredSrv, "splat_cloud"           (gaussian_splat_compilers.cpp)
//   SkyGaussian        → StructuredSrv, "sky_gaussian"          (sky_gaussian_compilers.cpp)
//                        StructuredSrv, "sky_gaussian_points"     — variant selected by semantic (present only when the fit has point sources)
//   StarCatalog        → StructuredSrv, "star_catalog"           (star_catalog_compilers.cpp)
//   GpuSparseMesh      → StructuredSrv, "pull_positions"        (gpu_sparse_mesh_compilers.cpp)
//                        StructuredSrv, "pull_indices"            — variant selected by semantic
//                        StructuredSrv, "pull_normals"            — present only when the mesh has normals
//
// The custom renderable compiler (0x70A) consults this to validate an
// authored binding's kind against the wired program's layout row and to bake
// the resolved variant into the recipe; the renderer then binds with
// rhi_asset_identity(key, variant) and needs no type knowledge. A compiler
// that publishes a NEW bindable resident resource extends this table (and
// nothing else). Mesh is deliberately absent: a CPU mesh has no resident
// published buffers (the renderer uploads pull buffers itself), so a mesh can
// only feed the 0x70A geometry port. The HDRI environment map
// ("environment_map_texture", light_compilers.cpp) stays off the table until
// a descriptor semantic exists for it.

#include <asset/types.h>
#include <engine/assets/render_binding_layout/render_binding_layout.h>
#include <engine/assets/type_extensions.h>

#include <optional>
#include <string_view>

namespace wz::engine::assets
{
    struct RenderBindingSource
    {
        RenderBindingKind kind{};
        // rhi_asset_identity discriminator the source is resident under.
        std::string_view variant;
    };

    // The published resource a `source_type` asset offers for binding at
    // `semantic`. nullopt = that asset type cannot back a layout binding row
    // (nothing resident is published for it, or the semantic does not select
    // one of its published variants).
    [[nodiscard]] inline std::optional<RenderBindingSource>
    render_binding_source_for(
        wz::asset::AssetType source_type,
        DescriptorSemantic semantic) noexcept
    {
        if (source_type == kAssetTypeScalarField) {
            return RenderBindingSource{
                RenderBindingKind::TextureSrv, "field_texture" };
        }
        if (source_type == kAssetTypeTexture) {
            // An imported image (2D overlays / materials). Resident under the
            // "texture" discriminator (texture_compilers.cpp).
            return RenderBindingSource{
                RenderBindingKind::TextureSrv, "texture" };
        }
        if (source_type == kAssetTypeVectorField) {
            return RenderBindingSource{
                RenderBindingKind::TextureSrv, "vector_field_texture" };
        }
        if (source_type == kAssetTypeGaussianSplatCloud) {
            return RenderBindingSource{
                RenderBindingKind::StructuredSrv, "splat_cloud" };
        }
        if (source_type == kAssetTypeSkyGaussian) {
            // Two published variants; the semantic picks the dome vs the points.
            if (semantic == DescriptorSemantic::SkyGaussianPoints) {
                return RenderBindingSource{
                    RenderBindingKind::StructuredSrv, "sky_gaussian_points" };
            }
            return RenderBindingSource{
                RenderBindingKind::StructuredSrv, "sky_gaussian" };
        }
        if (source_type == kAssetTypeStarCatalog) {
            return RenderBindingSource{
                RenderBindingKind::StructuredSrv, "star_catalog" };
        }
        if (source_type == kAssetTypeGpuSparseMesh) {
            // Two published variants; the semantic picks which buffer.
            if (semantic == DescriptorSemantic::PulledMeshPositions) {
                return RenderBindingSource{
                    RenderBindingKind::StructuredSrv, "pull_positions" };
            }
            if (semantic == DescriptorSemantic::PulledMeshIndices) {
                return RenderBindingSource{
                    RenderBindingKind::StructuredSrv, "pull_indices" };
            }
            if (semantic == DescriptorSemantic::PulledMeshNormals) {
                return RenderBindingSource{
                    RenderBindingKind::StructuredSrv, "pull_normals" };
            }
            return std::nullopt;
        }
        return std::nullopt;
    }
}
