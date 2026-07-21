// src/engine/assets/texture/texture_compilers.cpp
//
// Compiler for the Texture-from-file recipe.
//
// Input (one dep):
//   source_file -- a kAssetTypeRawFile whose payload is the image bytes.
//
// Output: kAssetTypeTexture -- a handle into the TextureTable (dimensions +
// colour space + usage). When a shared wozzits-rhi registry is present the
// decoded RGBA8 pixels are ALSO published as a resident Texture2D (variant
// "texture") under rhi_asset_identity, the surrogate pattern -- so an SRV bind
// finds it exactly like the star/sky resident buffers.

#include <engine/assets/texture/texture_compilers.h>

#include <engine/assets/texture/image_decode.h>
#include <engine/assets/engine_asset_library_internal.h>   // compile_failed_node
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <wozzits/rhi/gpu_resource.h>
#include <wozzits/rhi/gpu_resource_registry.h>

#include <any>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        // The Enum-param option lists. Static storage so the ParamDecl's span
        // outlives the registry. Order defines the authored index the compiler
        // reads back (and matches TextureUsage / TextureColorSpaceChoice).
        constexpr std::array<std::string_view, 4> kUsageOptions{
            "color", "normal", "data", "ui" };
        constexpr std::array<std::string_view, 3> kColorSpaceOptions{
            "auto", "srgb", "linear" };

        TextureUsage usage_from_index(int64_t i)
        {
            switch (i) {
            case 1:  return TextureUsage::Normal;
            case 2:  return TextureUsage::Data;
            case 3:  return TextureUsage::UI;
            default: return TextureUsage::Color;
            }
        }

        // Resolve the authored colour space. "auto" (0) derives from usage:
        // colour / UI are display-referred (sRGB), normal / mask / LUT are DATA
        // (linear). 1 = srgb, 2 = linear force it.
        TextureColorSpace color_space_from(int64_t index, TextureUsage usage)
        {
            if (index == 1) return TextureColorSpace::Srgb;
            if (index == 2) return TextureColorSpace::Linear;
            return (usage == TextureUsage::Color || usage == TextureUsage::UI)
                ? TextureColorSpace::Srgb
                : TextureColorSpace::Linear;
        }

        // Publish the decoded pixels as a resident Texture2D under
        // rhi_asset_identity(key, "texture"). Mirror of
        // publish_resident_star_catalog: best-effort (a failure is logged, the
        // handle released, the compile still succeeds -- the CPU metadata is
        // valid, only the GPU bind is missing), and records (key -> identity) so
        // the library releases it on de-registration.
        void publish_resident_texture(
            const wz::asset::AssetKey& key,
            const DecodedImage& image,
            wz::rhi::GpuResourceRegistry& gpu_resources,
            const RhiResourceTracker& rhi_resource_tracker,
            wz::Logger& logger)
        {
            wz::rhi::GpuResourceDesc desc = wz::rhi::GpuResourceDesc::texture_2d(
                image.width,
                image.height,
                wz::rhi::TextureFormat::RGBA8Unorm,
                wz::rhi::ResourceUsage_Sampled);
            // texture_2d leaves cpu_access None; update_mip needs a writable
            // resource. WriteOnce: uploaded once at compile, never refreshed.
            desc.cpu_access = wz::rhi::ResourceCpuAccess::WriteOnce;
            desc.identity = wz::rhi::ResourceIdentity{
                rhi_asset_identity(key, "texture"), {} };

            const wz::rhi::GpuResourceHandle handle = gpu_resources.acquire(desc);
            const bool uploaded = handle.valid()
                && gpu_resources.update_mip(
                    handle, /*mip_level*/ 0, image.rgba8.data(),
                    static_cast<uint64_t>(image.rgba8.size()));
            if (!uploaded) {
                if (handle.valid()) {
                    gpu_resources.release(handle);
                }
                logger.warn("texture RHI resident upload failed");
                return;
            }

            logger.info(
                "asset compile: texture RHI resident upload "
                + std::to_string(image.width) + "x"
                + std::to_string(image.height));

            if (rhi_resource_tracker) {
                std::vector<wz::rhi::ResourceIdentity> tracked{ desc.identity };
                rhi_resource_tracker(key, std::move(tracked));
            }
        }
    } // anonymous namespace

    void register_texture_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        TextureTable& table,
        wz::rhi::GpuResourceRegistry* gpu_resources,
        RhiResourceTracker rhi_resource_tracker)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kTextureFromFileSchema,
            .output_type  = kAssetTypeTexture,
            .input_ports = {
                { "source_file", kAssetTypeRawFile },
            },
            // Declaring the Enum params here is what surfaces them in the editor
            // (a dropdown) AND makes it store the authored INDEX -- without the
            // schema the value is a string and get<int64_t> silently falls back,
            // so the dial is a no-op (see the compiler-params-need-schema rule).
            .parameters = {
                {
                    .name = "usage",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Usage",
                    .default_num = 0,   // color
                    .options = kUsageOptions,
                },
                {
                    // The load-bearing semantic. Auto derives from usage.
                    .name = "color_space",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Color space",
                    .default_num = 0,   // auto
                    .options = kColorSpaceOptions,
                },
            },
            .compile = [&logger, &table, gpu_resources, rhi_resource_tracker](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                if (dep_nodes.size() != 1) {
                    logger.error(
                        "texture: expected exactly one source file dependency");
                    return compile_failed_node(input);
                }
                const auto* bytes =
                    std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);
                if (!bytes || bytes->empty()) {
                    logger.error(
                        "texture: source file dependency has no bytes");
                    return compile_failed_node(input);
                }

                int64_t usage_index = 0;
                int64_t color_space_index = 0;
                if (const auto* pb =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta)) {
                    usage_index = pb->get<int64_t>("usage", 0);
                    color_space_index = pb->get<int64_t>("color_space", 0);
                }
                const TextureUsage usage = usage_from_index(usage_index);

                const DecodedImage image =
                    decode_image_rgba8({ bytes->data(), bytes->size() });
                if (!image.ok) {
                    logger.error("texture: decode failed: " + image.error);
                    return compile_failed_node(input, image.error);
                }

                TextureData data;
                data.width       = image.width;
                data.height      = image.height;
                data.format      = TexturePixelFormat::RGBA8;
                data.usage       = usage;
                data.color_space = color_space_from(color_space_index, usage);

                // Publish residency BEFORE the store, mirroring the star path.
                if (gpu_resources) {
                    publish_resident_texture(
                        input.key, image, *gpu_resources,
                        rhi_resource_tracker, logger);
                }

                wz::asset::ResourceHandle handle = table.add(data);
                if (!handle.valid()) {
                    logger.error("texture: failed to store texture metadata");
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
