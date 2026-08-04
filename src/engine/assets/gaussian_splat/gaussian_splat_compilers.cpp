// src/engine/assets/gaussian_splat/gaussian_splat_compilers.cpp

#include <engine/assets/gaussian_splat/gaussian_splat_compilers.h>
#include <engine/assets/gaussian_splat/gaussian_splat_ply_importer.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <wozzits/rhi/gpu_resource.h>
#include <wozzits/rhi/gpu_resource_registry.h>

#include <algorithm>
#include <any>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        GaussianSplatCloudData make_debug_sphere_cloud(
            const ProceduralGaussianSplatCloudCompileDesc& desc)
        {
            GaussianSplatCloudData cloud{};
            cloud.splats.reserve(desc.count);

            // 3DGS encoding constants.
            // opacity stored as logit; logit(0.9) ≈ 2.197 gives a clearly visible splat.
            constexpr float kLogitOpacity = 2.1972245773f;
            // scale stored as log.
            const float log_scale = std::log(desc.splat_scale);
            // color stored as SH DC coefficients: f_dc = (display - 0.5) / SH_C0
            constexpr float SH_C0 = 0.28209479177387814f;

            // Deterministic Fibonacci-ish sphere distribution. This is not random;
            // identical desc produces identical splat order and values.
            constexpr float pi = 3.14159265358979323846f;
            const float golden_angle = pi * (3.0f - std::sqrt(5.0f));

            for (uint32_t i = 0; i < desc.count; ++i) {
                const float t =
                    (desc.count > 1)
                    ? static_cast<float>(i) / static_cast<float>(desc.count - 1)
                    : 0.0f;

                const float y = 1.0f - 2.0f * t;
                const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
                const float theta = golden_angle * static_cast<float>(i);

                const float x = std::cos(theta) * r;
                const float z = std::sin(theta) * r;

                GaussianSplat splat{};
                splat.position[0] = x * desc.radius;
                splat.position[1] = y * desc.radius;
                splat.position[2] = z * desc.radius;

                splat.scale[0] = log_scale;
                splat.scale[1] = log_scale;
                splat.scale[2] = log_scale;

                // Identity quaternion: rot_0=w=1, rot_1=x=0, rot_2=y=0, rot_3=z=0.
                splat.rotation[0] = 1.0f;

                splat.opacity = kLogitOpacity;

                // Simple diagnostic color from normalized position, encoded as SH DC.
                const float display_r = 0.5f + 0.5f * x;
                const float display_g = 0.5f + 0.5f * y;
                const float display_b = 0.5f + 0.5f * z;
                splat.color_dc[0] = (display_r - 0.5f) / SH_C0;
                splat.color_dc[1] = (display_g - 0.5f) / SH_C0;
                splat.color_dc[2] = (display_b - 0.5f) / SH_C0;

                cloud.splats.push_back(splat);
            }

            const float b = desc.radius + desc.splat_scale;
            cloud.bounds.min[0] = -b;
            cloud.bounds.min[1] = -b;
            cloud.bounds.min[2] = -b;
            cloud.bounds.max[0] = b;
            cloud.bounds.max[1] = b;
            cloud.bounds.max[2] = b;
            cloud.bounds.valid = true;

            cloud.opacity_min = kLogitOpacity;
            cloud.opacity_max = kLogitOpacity;
            cloud.scale_min = log_scale;
            cloud.scale_max = log_scale;
            cloud.f_rest_count = 0;

            return cloud;
        }

        ProceduralGaussianSplatCloudCompileDesc
        procedural_gaussian_splat_cloud_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            ProceduralGaussianSplatCloudCompileDesc desc{};
            desc.count = params.get<uint32_t>("count", desc.count);
            desc.radius = params.get<float>("radius", desc.radius);
            desc.splat_scale =
                params.get<float>("splat_scale", desc.splat_scale);
            return desc;
        }

        GaussianSplatFromScalarFieldCompileDesc
        gaussian_splat_from_scalar_field_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            GaussianSplatFromScalarFieldCompileDesc desc{};
            desc.height_scale =
                params.get<float>("height_scale", desc.height_scale);
            desc.step_x = params.get<float>("step_x", desc.step_x);
            desc.step_z = params.get<float>("step_z", desc.step_z);
            desc.splat_scale =
                params.get<float>("splat_scale", desc.splat_scale);
            desc.opacity = params.get<float>("opacity", desc.opacity);
            desc.normalize_values =
                params.get<bool>(
                    "normalize_values",
                    desc.normalize_values);
            desc.use_threshold =
                params.get<bool>("use_threshold", desc.use_threshold);
            desc.emit_threshold =
                params.get<float>("emit_threshold", desc.emit_threshold);
            desc.subsample_step =
                params.get<uint32_t>("subsample_step", desc.subsample_step);
            return desc;
        }

        wz::asset::AssetNode compile_ply_gaussian_splat_cloud_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode* const> dep_nodes,
            wz::Logger& logger,
            GaussianSplatCloudTable& table)
        {
            if (dep_nodes.size() != 1) {
                logger.error("PLY gaussian splat cloud should have exactly one file dependency");
                return compile_failed_node(input);
            }

            const auto* bytes =
                std::get_if<std::vector<uint8_t>>(&dep_nodes[0]->payload);

            if (!bytes || bytes->empty()) {
                logger.error("PLY gaussian splat file dependency has no bytes");
                return compile_failed_node(input);
            }

            const GaussianSplatImportResult import_result =
                import_gaussian_splat_ply_bytes({ bytes->data(), bytes->size() });

            if (!import_result.ok) {
                logger.error("failed to import PLY gaussian splat cloud: " + import_result.error);
                return compile_failed_node(input);
            }

            GaussianSplatCloudData data = import_result.cloud;

            if (!data.valid()) {
                logger.error("PLY gaussian splat importer produced invalid cloud");
                return compile_failed_node(input);
            }

            wz::asset::ResourceHandle handle = table.add(std::move(data));
            if (!handle.valid()) {
                logger.error("failed to store PLY gaussian splat cloud");
                return compile_failed_node(input);
            }

            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }
        GaussianSplatCloudData make_splat_cloud_from_scalar_field(
            const GaussianSplatFromScalarFieldCompileDesc& desc,
            const ScalarFieldData& field,
            wz::Logger& /*logger*/)
        {
            constexpr float SH_C0 = 0.28209479177387814f;
            constexpr float kInf  = std::numeric_limits<float>::max();

            const float log_scale = std::log(std::max(desc.splat_scale, 1e-6f));

            const float logit_opacity = [&] {
                const float p = std::max(0.0001f, std::min(0.9999f, desc.opacity));
                return std::log(p / (1.0f - p));
            }();

            const float value_range  = field.max_value - field.min_value;
            const bool can_normalize = (value_range > 1e-12f);

            // Subsample stride (>= 1): emit one splat every Nth texel. The world
            // spacing between emitted columns/rows is step * stride, so the cloud
            // keeps the same footprint as the full-res cloud — just sparser.
            const uint32_t stride = desc.subsample_step == 0u
                ? 1u
                : desc.subsample_step;

            // Number of emitted columns/rows (ceil division of the field by the
            // stride); used only for reserve.
            const uint32_t cols = (field.width  + stride - 1u) / stride;
            const uint32_t rows = (field.height + stride - 1u) / stride;

            // Positions are authored in a canonical UNIT footprint: XZ in [0,1]
            // across the field, Y the raw normalized height in [0,1]. World size,
            // placement, and vertical scale come ENTIRELY from the scene-node
            // transform, so the field's pixel dimensions and vertical
            // exaggeration no longer bake into the coordinates (the cloud is
            // sized in one place, matching the clipmap it overlays).
            //
            // XZ normalizes the ORIGINAL texel index over DIMS (the texel-cell
            // convention): a sample at texel ix lands at ix/dims, exactly the
            // world->UV the clipmap VS uses when it floors uv*dims to a texel.
            // Because dims is a multiple of the lattice extent (e.g. 4096 =
            // 4*1024), one lattice cell then spans exactly N texels, so the cloud
            // sits flush on the clipmap vertices with no sub-texel drift. (The
            // earlier (dims-1) "point" convention left ~1 texel of drift edge to
            // edge because 4095/1024 != 4.)
            const float inv_width =
                field.width  > 0u ? 1.0f / static_cast<float>(field.width)  : 0.0f;
            const float inv_height =
                field.height > 0u ? 1.0f / static_cast<float>(field.height) : 0.0f;

            GaussianSplatCloudData cloud{};
            cloud.splats.reserve(static_cast<size_t>(cols) * rows);

            // Tight bounds accumulated only from emitted splats.
            float bmin[3] = { kInf,  kInf,  kInf};
            float bmax[3] = {-kInf, -kInf, -kInf};

            for (uint32_t iy = 0; iy < field.height; iy += stride) {
                for (uint32_t ix = 0; ix < field.width; ix += stride) {
                    const float raw = field.at(ix, iy);

                    const float normalized =
                        (desc.normalize_values && can_normalize)
                        ? (raw - field.min_value) / value_range
                        : raw;

                    if (desc.use_threshold && normalized < desc.emit_threshold)
                        continue;

                    const float wx = static_cast<float>(ix) * inv_width;
                    const float wy = normalized;
                    const float wz = static_cast<float>(iy) * inv_height;

                    GaussianSplat splat{};
                    splat.position[0] = wx;
                    splat.position[1] = wy;
                    splat.position[2] = wz;

                    splat.scale[0] = log_scale;
                    splat.scale[1] = log_scale;
                    splat.scale[2] = log_scale;

                    // Identity quaternion.
                    splat.rotation[0] = 1.0f;

                    splat.opacity = logit_opacity;

                    // Grayscale color derived from normalized value.
                    const float display = std::max(0.0f, std::min(1.0f, normalized));
                    const float sh_dc = (display - 0.5f) / SH_C0;
                    splat.color_dc[0] = sh_dc;
                    splat.color_dc[1] = sh_dc;
                    splat.color_dc[2] = sh_dc;

                    bmin[0] = std::min(bmin[0], wx);
                    bmin[1] = std::min(bmin[1], wy);
                    bmin[2] = std::min(bmin[2], wz);
                    bmax[0] = std::max(bmax[0], wx);
                    bmax[1] = std::max(bmax[1], wy);
                    bmax[2] = std::max(bmax[2], wz);

                    cloud.splats.push_back(splat);
                }
            }

            if (cloud.splats.empty())
                return {};

            const float r = desc.splat_scale;
            cloud.bounds.min[0] = bmin[0] - r;
            cloud.bounds.min[1] = bmin[1] - r;
            cloud.bounds.min[2] = bmin[2] - r;
            cloud.bounds.max[0] = bmax[0] + r;
            cloud.bounds.max[1] = bmax[1] + r;
            cloud.bounds.max[2] = bmax[2] + r;
            cloud.bounds.valid  = true;

            cloud.opacity_min = logit_opacity;
            cloud.opacity_max = logit_opacity;
            cloud.scale_min   = log_scale;
            cloud.scale_max   = log_scale;
            cloud.f_rest_count = 0;

            return cloud;
        }

        // #208: the decoded per-splat record uploaded into the resident
        // StructuredBuffer the SplatPull render path reads. 64-byte stride,
        // matching the legacy DX12GaussianSplatVertex layout AND the `Splat`
        // struct in gaussian_splat_field_cloud_vs.hlsl byte-for-byte. Unlike the
        // CPU GaussianSplat (which stores ENCODED log-scale / logit-opacity / SH
        // DC color), this stores values already DECODED for display so the VS
        // can consume them directly — the same decode the legacy
        // make_gpu_splat_vertex does, replicated here so #208 stays off the
        // legacy dx12 path (DX12 ONLY through rhi).
        struct ResidentSplat
        {
            float    position[3] = {};
            float    opacity     = 1.0f;
            float    scale[3]    = { 1.0f, 1.0f, 1.0f };
            float    pad0        = 0.0f;
            float    rotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            float    color[3]    = { 1.0f, 1.0f, 1.0f };
            uint32_t pad1        = 0u;
        };
        static_assert(sizeof(ResidentSplat) == 64,
            "ResidentSplat must be 64 bytes to match the SplatPull "
            "StructuredBuffer stride and the HLSL Splat struct");

        ResidentSplat decode_resident_splat(const GaussianSplat& splat)
        {
            constexpr float SH_C0 = 0.28209479177387814f;

            ResidentSplat out{};
            out.position[0] = splat.position[0];
            out.position[1] = splat.position[1];
            out.position[2] = splat.position[2];

            // Opacity stored as logit -> decode via sigmoid.
            out.opacity = 1.0f / (1.0f + std::exp(-splat.opacity));

            // Scale stored as log -> decode each axis to world-space size.
            out.scale[0] = std::exp(splat.scale[0]);
            out.scale[1] = std::exp(splat.scale[1]);
            out.scale[2] = std::exp(splat.scale[2]);

            // Rotation: stored rot_0=w, rot_1=x, rot_2=y, rot_3=z; emit x,y,z,w.
            float qw = splat.rotation[0];
            float qx = splat.rotation[1];
            float qy = splat.rotation[2];
            float qz = splat.rotation[3];
            const float q_len_sq = qx * qx + qy * qy + qz * qz + qw * qw;
            if (q_len_sq > 0.0f) {
                const float inv = 1.0f / std::sqrt(q_len_sq);
                qx *= inv; qy *= inv; qz *= inv; qw *= inv;
            }
            else {
                qx = 0.0f; qy = 0.0f; qz = 0.0f; qw = 1.0f;
            }
            out.rotation[0] = qx;
            out.rotation[1] = qy;
            out.rotation[2] = qz;
            out.rotation[3] = qw;

            // Color: SH DC coefficient -> linear display RGB.
            //
            // NOT clamped to [0,1], and this is the LIVE path -- the resident
            // buffer the SplatPull VS reads. The canonical `decode_splat`
            // clamps and pins both endpoints in a test; this decode was
            // replicated from the legacy `make_gpu_splat_vertex` after ITS
            // clamp had already been commented out (`6efca33b`, no reason
            // recorded), so the omission propagated rather than being chosen.
            // Whether the live path should clamp is #316 C3-Q2 -- it changes
            // what renders, so it is not a comment fix. Note there is no
            // tonemap in the engine either (#327 puts post-processing below
            // its own R1), so an out-of-range colour is not headroom for
            // anything downstream today; it clips at the backbuffer.
            out.color[0] = 0.5f + SH_C0 * splat.color_dc[0];
            out.color[1] = 0.5f + SH_C0 * splat.color_dc[1];
            out.color[2] = 0.5f + SH_C0 * splat.color_dc[2];
            return out;
        }

        // #208: publish the splat cloud's GPU residency onto the shared
        // wozzits-rhi registry as a decoded splat StructuredBuffer (the
        // surrogate pattern — the registry owns the GPU buffer, the asset is its
        // surrogate). Mirrors publish_resident_scalar_field /
        // publish_resident_gpu_sparse_mesh. Best-effort: a failure is logged and
        // does not fail the compile (the cloud still resolves on the CPU table;
        // the RHI splat renderable just stays unrealizable until residency
        // succeeds). The identity discriminator "splat_cloud" matches the one
        // RhiSceneRenderer::ensure_renderable looks the buffer up by.
        void publish_resident_gaussian_splat_cloud(
            const wz::asset::AssetKey& key,
            const GaussianSplatCloudData& cloud,
            wz::rhi::GpuResourceRegistry& gpu_resources,
            const RhiResourceTracker& rhi_resource_tracker,
            wz::Logger& logger)
        {
            if (cloud.empty()) {
                return;
            }

            const auto started = std::chrono::steady_clock::now();

            std::vector<ResidentSplat> resident;
            resident.reserve(cloud.splats.size());
            for (const GaussianSplat& splat : cloud.splats) {
                resident.push_back(decode_resident_splat(splat));
            }

            wz::rhi::GpuResourceDesc desc = wz::rhi::GpuResourceDesc::buffer(
                static_cast<uint64_t>(resident.size()) * sizeof(ResidentSplat),
                static_cast<uint32_t>(sizeof(ResidentSplat)),
                wz::rhi::ResourceUsage_Sampled,
                wz::rhi::ResourceCpuAccess::WriteOnce);
            desc.identity = wz::rhi::ResourceIdentity{
                rhi_asset_identity(key, "splat_cloud"),
                {},
            };

            const wz::rhi::GpuResourceHandle handle = gpu_resources.acquire(desc);
            const bool uploaded =
                handle.valid()
                && gpu_resources.update(
                    handle, resident.data(), desc.size_bytes);
            if (!uploaded) {
                if (handle.valid()) {
                    gpu_resources.release(handle);
                }
                logger.warn("gaussian splat cloud RHI resident upload failed");
                return;
            }

            if (rhi_resource_tracker) {
                rhi_resource_tracker(key, { desc.identity });
            }

            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
            logger.info(
                "asset compile: gaussian splat cloud RHI resident upload splats="
                + std::to_string(resident.size())
                + " upload_ms=" + std::to_string(elapsed_ms));
        }

    } // anonymous namespace

    void register_gaussian_splat_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        GaussianSplatCloudTable& table,
        ScalarFieldTable& scalar_field_table,
        wz::rhi::GpuResourceRegistry* gpu_resources,
        RhiResourceTracker rhi_resource_tracker)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kProceduralGaussianSplatCloudSchema,
            .output_type = kAssetTypeGaussianSplatCloud,
            .parameters = {
                {
                    .name = "count",
                    .type = wz::asset::ParamType::Int,
                    .label = "Count",
                    .default_num = 0,
                    .min = 0,
                    .max = 1000000,
                },
                {
                    .name = "radius",
                    .type = wz::asset::ParamType::Float,
                    .label = "Radius",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 100.0,
                },
                {
                    .name = "splat_scale",
                    .type = wz::asset::ParamType::Float,
                    .label = "Splat scale",
                    .default_num = 0.05,
                    .min = 0.0,
                    .max = 10.0,
                },
            },
            .compile = [&logger, &table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode* const> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                ProceduralGaussianSplatCloudCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<ProceduralGaussianSplatCloudCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(&input.meta))
                    {
                        param_desc =
                            procedural_gaussian_splat_cloud_desc_from_params(
                                *params);
                        desc = &param_desc;
                    }
                }

                if (!desc) {
                    logger.error("procedural gaussian splat cloud missing compile desc");
                    return compile_failed_node(input);
                }

                if (!dep_nodes.empty()) {
                    logger.error("procedural gaussian splat cloud should not have dependencies");
                    return compile_failed_node(input);
                }

                if (desc->count == 0) {
                    logger.error("procedural gaussian splat cloud has zero count");
                    return compile_failed_node(input);
                }

                if (desc->radius <= 0.0f) {
                    logger.error("procedural gaussian splat cloud has non-positive radius");
                    return compile_failed_node(input);
                }

                if (desc->splat_scale <= 0.0f) {
                    logger.error("procedural gaussian splat cloud has non-positive splat_scale");
                    return compile_failed_node(input);
                }

                GaussianSplatCloudData data = make_debug_sphere_cloud(*desc);

                if (!data.valid()) {
                    logger.error("procedural gaussian splat cloud produced invalid data");
                    return compile_failed_node(input);
                }

                wz::asset::ResourceHandle handle = table.add(std::move(data));

                if (!handle.valid()) {
                    logger.error("failed to store gaussian splat cloud");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kGaussianSplatFromPLYSchema,
            .output_type = kAssetTypeGaussianSplatCloud,
            .input_ports = {
                { "source_file", kAssetTypeRawFile },
            },
            .compile = [&logger, &table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode* const> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                return compile_ply_gaussian_splat_cloud_node(
                    input, dep_nodes, logger, table);
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kGaussianSplatFromFieldSchema,
            .output_type = kAssetTypeGaussianSplatCloud,
            .input_ports = {
                { "scalar_field", kAssetTypeScalarField },
            },
            .parameters = {
                {
                    .name = "height_scale",
                    .type = wz::asset::ParamType::Float,
                    .label = "Height scale",
                    .default_num = 1.0,
                    .min = -100.0,
                    .max = 100.0,
                },
                {
                    .name = "step_x",
                    .type = wz::asset::ParamType::Float,
                    .label = "Step X",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 100.0,
                },
                {
                    .name = "step_z",
                    .type = wz::asset::ParamType::Float,
                    .label = "Step Z",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 100.0,
                },
                {
                    .name = "splat_scale",
                    .type = wz::asset::ParamType::Float,
                    .label = "Splat scale",
                    .default_num = 0.05,
                    .min = 0.0,
                    .max = 10.0,
                },
                {
                    .name = "opacity",
                    .type = wz::asset::ParamType::Float,
                    .label = "Opacity",
                    .default_num = 0.9,
                    .min = 0.0,
                    .max = 1.0,
                },
                {
                    .name = "normalize_values",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Normalize values",
                    .default_num = 1.0,
                },
                {
                    .name = "use_threshold",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Use threshold",
                    .default_num = 0.0,
                },
                {
                    .name = "emit_threshold",
                    .type = wz::asset::ParamType::Float,
                    .label = "Emit threshold",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 1.0,
                },
                {
                    .name = "subsample_step",
                    .type = wz::asset::ParamType::Int,
                    .label = "Subsample step",
                    .default_num = 1.0,
                    .min = 1.0,
                    .max = 4096.0,
                },
            },
            .compile = [&logger, &table, &scalar_field_table,
                        gpu_resources, rhi_resource_tracker](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode* const> /*dep_nodes*/,
                std::span<const wz::asset::ResourceHandle> dep_handles) -> wz::asset::AssetNode
            {
                GaussianSplatFromScalarFieldCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<GaussianSplatFromScalarFieldCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(&input.meta))
                    {
                        param_desc =
                            gaussian_splat_from_scalar_field_desc_from_params(
                                *params);
                        desc = &param_desc;
                    }
                }

                if (!desc) {
                    logger.error("gaussian splat from scalar field missing compile desc");
                    return compile_failed_node(input);
                }

                if (dep_handles.size() != 1) {
                    logger.error("gaussian splat from scalar field expects exactly one compiled dependency");
                    return compile_failed_node(input);
                }

                const ScalarFieldData* field = scalar_field_table.get(dep_handles[0]);
                if (!field) {
                    logger.error("gaussian splat from scalar field: scalar field not found");
                    return compile_failed_node(input);
                }

                GaussianSplatCloudData data =
                    make_splat_cloud_from_scalar_field(*desc, *field, logger);

                if (!data.valid()) {
                    logger.error("gaussian splat from scalar field: produced empty or invalid cloud");
                    return compile_failed_node(input);
                }

                // #208: publish the decoded cloud as a resident StructuredBuffer
                // before the move into the CPU table, so the RHI splat path can
                // bind it. Best-effort; only when a shared registry is present.
                if (gpu_resources) {
                    publish_resident_gaussian_splat_cloud(
                        input.key,
                        data,
                        *gpu_resources,
                        rhi_resource_tracker,
                        logger);
                }

                wz::asset::ResourceHandle handle = table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("gaussian splat from scalar field: failed to store cloud");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage   = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }
}
