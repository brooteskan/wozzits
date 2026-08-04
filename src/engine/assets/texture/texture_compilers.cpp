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

        // Indexed layer names for the composite material (#285). Static storage
        // so the ParamDecl string_views outlive the registry -- the same reason
        // the option lists above are constexpr.
        const std::array<std::string, kMaxCompositeMaterialLayers>&
        composite_layer_prefixes()
        {
            static const std::array<std::string, kMaxCompositeMaterialLayers>
                prefixes = []
                {
                    std::array<std::string, kMaxCompositeMaterialLayers> out{};
                    for (std::size_t i = 0; i < out.size(); ++i) {
                        out[i] = "layer" + std::to_string(i) + "_";
                    }
                    return out;
                }();
            return prefixes;
        }

        // Storage for every generated param name/label, kept alive for the
        // process. ParamDecl holds string_views.
        const std::vector<std::string>& composite_param_storage()
        {
            static const std::vector<std::string> names = []
            {
                std::vector<std::string> out;
                for (const std::string& prefix : composite_layer_prefixes()) {
                    for (const char* suffix : {
                             "centre_u", "centre_v", "half_width",
                             "half_height", "rotation", "opacity" })
                    {
                        out.push_back(prefix + suffix);
                    }
                }
                return out;
            }();
            return names;
        }

        std::vector<wz::asset::InputPort> composite_material_ports()
        {
            static const std::vector<std::string> port_names = []
            {
                std::vector<std::string> out;
                for (std::size_t i = 0; i < kMaxCompositeMaterialLayers; ++i) {
                    out.push_back("layer" + std::to_string(i));
                }
                return out;
            }();

            std::vector<wz::asset::InputPort> ports;
            ports.reserve(port_names.size());
            for (const std::string& name : port_names) {
                // Optional and typed: a layer's source is a texture, which the
                // render-target and composite recipes also produce -- so a
                // composite can layer another composite, or a live render
                // target (#287), with no special case.
                ports.push_back(wz::asset::InputPort{
                    name,
                    kAssetTypeTexture,
                    wz::asset::InputPortRequirement::Optional,
                });
            }
            return ports;
        }

        std::vector<wz::asset::ParamDecl> composite_material_parameters()
        {
            std::vector<wz::asset::ParamDecl> params;
            params.push_back({
                // Same reason as the render-target recipe: a composited texture
                // is a SINK, and two of the same size would otherwise derive
                // one key and be rejected as a duplicate at commit.
                .name = "name",
                .type = wz::asset::ParamType::String,
                .label = "Name",
            });
            params.push_back({
                .name = "width",
                .type = wz::asset::ParamType::Int,
                .label = "Width",
                .default_num = 512,
            });
            params.push_back({
                .name = "height",
                .type = wz::asset::ParamType::Int,
                .label = "Height",
                .default_num = 512,
            });
            params.push_back({
                .name = "color_space",
                .type = wz::asset::ParamType::Enum,
                .label = "Color space",
                .default_num = 0,
                .options = kColorSpaceOptions,
            });
            params.push_back({
                // What the material is where no layer covers it.
                .name = "base_colour",
                .type = wz::asset::ParamType::Color,
                .label = "Base colour",
            });
            params.push_back({
                .name = "base_alpha",
                .type = wz::asset::ParamType::Float,
                .label = "Base alpha",
                .default_num = 1.0,
                .min = 0.0,
                .max = 1.0,
            });

            const std::vector<std::string>& names = composite_param_storage();
            std::size_t next = 0;
            for (std::size_t i = 0; i < kMaxCompositeMaterialLayers; ++i) {
                // Placement in UV space, so it is resolution-independent: this
                // IS the "move the art on the material" control.
                params.push_back({
                    .name = names[next++],
                    .type = wz::asset::ParamType::Float,
                    .label = "Centre U",
                    .default_num = 0.5, .min = 0.0, .max = 1.0 });
                params.push_back({
                    .name = names[next++],
                    .type = wz::asset::ParamType::Float,
                    .label = "Centre V",
                    .default_num = 0.5, .min = 0.0, .max = 1.0 });
                params.push_back({
                    .name = names[next++],
                    .type = wz::asset::ParamType::Float,
                    .label = "Half width",
                    .default_num = 0.35, .min = 0.0, .max = 1.0 });
                params.push_back({
                    .name = names[next++],
                    .type = wz::asset::ParamType::Float,
                    .label = "Half height",
                    .default_num = 0.35, .min = 0.0, .max = 1.0 });
                params.push_back({
                    .name = names[next++],
                    .type = wz::asset::ParamType::Float,
                    .label = "Rotation (radians)",
                    .default_num = 0.0 });
                params.push_back({
                    .name = names[next++],
                    .type = wz::asset::ParamType::Float,
                    .label = "Opacity",
                    .default_num = 1.0, .min = 0.0, .max = 1.0 });
            }
            return params;
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
        // Residency for a render-target texture (#281): no pixels to upload, and
        // the usage flags are what make it different -- RenderTarget so a pass
        // can draw into it, Sampled so a material can read it back. Published
        // under the SAME "texture" variant a file-backed texture uses, which is
        // what lets an authored binding name it wherever a texture is accepted.
        bool publish_resident_render_target(
            const wz::asset::AssetKey& key,
            std::uint32_t width,
            std::uint32_t height,
            wz::rhi::GpuResourceRegistry& gpu_resources,
            const RhiResourceTracker& rhi_resource_tracker,
            wz::Logger& logger)
        {
            wz::rhi::GpuResourceDesc desc = wz::rhi::GpuResourceDesc::texture_2d(
                width,
                height,
                wz::rhi::TextureFormat::RGBA8Unorm,
                wz::rhi::ResourceUsage_Sampled
                    | wz::rhi::ResourceUsage_RenderTarget);
            desc.identity = wz::rhi::ResourceIdentity{
                rhi_asset_identity(key, "texture"), {} };

            const wz::rhi::GpuResourceHandle handle = gpu_resources.acquire(desc);
            if (!handle.valid()) {
                logger.warn("render target texture: RHI acquire failed");
                return false;
            }

            logger.info(
                "asset compile: render target texture "
                + std::to_string(width) + "x" + std::to_string(height));

            if (rhi_resource_tracker) {
                std::vector<wz::rhi::ResourceIdentity> tracked{ desc.identity };
                rhi_resource_tracker(key, std::move(tracked));
            }
            return true;
        }
    } // anonymous namespace

    void register_texture_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        TextureTable& table,
        wz::rhi::GpuResourceRegistry* gpu_resources,
        RhiResourceTracker rhi_resource_tracker,
        const wz::asset::AssetSystem* asset_system)
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
                std::span<const wz::asset::AssetNode* const> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                if (dep_nodes.size() != 1) {
                    logger.error(
                        "texture: expected exactly one source file dependency");
                    return compile_failed_node(input);
                }
                const auto* bytes =
                    std::get_if<std::vector<uint8_t>>(&dep_nodes[0]->payload);
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

        // Render-target texture (#281): the same kAssetTypeTexture, with no
        // source file. Its dimensions are authored rather than decoded, and it
        // is made resident Sampled | RenderTarget so a pass can render into it
        // and a material can sample it. Deviceless (no registry) it still
        // compiles to valid metadata -- residency is the only thing gated.
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kRenderTargetTextureSchema,
            .output_type  = kAssetTypeTexture,
            .input_ports = {},
            .parameters = {
                {
                    // Nothing reads this at compile time -- it exists to make
                    // two targets DISTINCT. An authored node's key is derived
                    // from its params, and a render target is a SINK, not a
                    // value: two 512x512 targets are two different places to
                    // draw. Without a name they derive the same key and the
                    // commit batch rejects the graph outright (one key = one
                    // node), which is what happens the moment a project wants a
                    // second target of a size it already uses. The typed
                    // create_render_target folds a name for the same reason.
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "width",
                    .type = wz::asset::ParamType::Int,
                    .label = "Width",
                    .default_num = 512,
                },
                {
                    .name = "height",
                    .type = wz::asset::ParamType::Int,
                    .label = "Height",
                    .default_num = 512,
                },
                {
                    // A render target is written by a pass, so its texels are
                    // whatever that pass wrote -- there is no decode step to
                    // infer a colour space from. Authored, defaulting to the
                    // sRGB the compositor and the backbuffer both work in.
                    .name = "color_space",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Color space",
                    .default_num = 0,   // auto -> sRGB for a colour usage
                    .options = kColorSpaceOptions,
                },
            },
            .compile = [&logger, &table, gpu_resources, rhi_resource_tracker](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode* const>,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                int64_t width = 512;
                int64_t height = 512;
                int64_t color_space_index = 0;
                if (const auto* pb =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta)) {
                    width = pb->get<int64_t>("width", 512);
                    height = pb->get<int64_t>("height", 512);
                    color_space_index = pb->get<int64_t>("color_space", 0);
                }
                // A zero/negative or absurd extent is an authoring mistake, and
                // acquiring on it would either fail opaquely or reserve a
                // gigabyte. Reject it with a reason the inspector can show.
                constexpr int64_t kMaxExtent = 8192;
                if (width <= 0 || height <= 0
                    || width > kMaxExtent || height > kMaxExtent)
                {
                    const std::string reason =
                        "render target texture: dimensions must be within 1.."
                        + std::to_string(kMaxExtent) + " (got "
                        + std::to_string(width) + "x"
                        + std::to_string(height) + ")";
                    logger.error(reason);
                    return compile_failed_node(input, reason);
                }

                TextureData data;
                data.width       = static_cast<std::uint32_t>(width);
                data.height      = static_cast<std::uint32_t>(height);
                data.format      = TexturePixelFormat::RGBA8;
                data.usage       = TextureUsage::Color;
                data.color_space =
                    color_space_from(color_space_index, TextureUsage::Color);

                if (gpu_resources) {
                    if (!publish_resident_render_target(
                            input.key, data.width, data.height, *gpu_resources,
                            rhi_resource_tracker, logger))
                    {
                        return compile_failed_node(
                            input, "render target texture residency failed");
                    }
                }

                wz::asset::ResourceHandle handle = table.add(data);
                if (!handle.valid()) {
                    logger.error(
                        "render target texture: failed to store metadata");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage   = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
        });

        // Composite material (#285): the render-target texture above, with its
        // CONTENT authored -- a base colour plus placed layers. Each layer
        // sources another texture through an optional port; the pass that fills
        // it clears to the base colour and draws the layers over it in order.
        //
        // Everything here was hardcoded in app C++ before: which art, the base
        // colour, and the layer placement. Those are now inspector dials, which
        // is the difference between #281 working and #281 being usable.
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kCompositeMaterialTextureSchema,
            .output_type  = kAssetTypeTexture,
            .input_ports = composite_material_ports(),
            .parameters = composite_material_parameters(),
            .compile = [&logger, &table, gpu_resources, rhi_resource_tracker,
                        asset_system](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode* const>,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                const auto* pb =
                    std::any_cast<wz::asset::ParamBlock>(&input.meta);
                if (!pb) {
                    return compile_failed_node(
                        input, "composite material has no parameters");
                }

                const int64_t width = pb->get<int64_t>("width", 512);
                const int64_t height = pb->get<int64_t>("height", 512);
                constexpr int64_t kMaxExtent = 8192;
                if (width <= 0 || height <= 0
                    || width > kMaxExtent || height > kMaxExtent)
                {
                    const std::string reason =
                        "composite material: dimensions must be within 1.."
                        + std::to_string(kMaxExtent) + " (got "
                        + std::to_string(width) + "x"
                        + std::to_string(height) + ")";
                    logger.error(reason);
                    return compile_failed_node(input, reason);
                }

                TextureData data;
                data.width       = static_cast<std::uint32_t>(width);
                data.height      = static_cast<std::uint32_t>(height);
                data.format      = TexturePixelFormat::RGBA8;
                data.usage       = TextureUsage::Color;
                data.color_space = color_space_from(
                    pb->get<int64_t>("color_space", 0), TextureUsage::Color);
                data.is_composite = true;

                const std::array<float, 3> base =
                    pb->get<std::array<float, 3>>(
                        "base_colour", std::array<float, 3>{ 0.0f, 0.0f, 0.0f });
                data.base_colour[0] = base[0];
                data.base_colour[1] = base[1];
                data.base_colour[2] = base[2];
                data.base_colour[3] =
                    static_cast<float>(pb->get<double>("base_alpha", 1.0));

                // Layer sources come from the node's PORT-ORDERED dep keys
                // (empty key = unwired port), not from the dep node order:
                // optional ports shift dep positions, which is the documented
                // hazard the custom-renderable compiler hit first. That list is
                // part of the node's identity, so the compile stays
                // content-addressed.
                std::span<const wz::asset::AssetKey> port_keys{};
                if (asset_system) {
                    for (const wz::asset::AssetSystem::RegistrationEntry& entry :
                         asset_system->registered_assets())
                    {
                        if (entry.node.key == input.key) {
                            port_keys = entry.dep_keys;
                            break;
                        }
                    }
                }

                for (std::size_t i = 0; i < kMaxCompositeMaterialLayers; ++i) {
                    if (i >= port_keys.size()
                        || port_keys[i] == wz::asset::AssetKey{})
                    {
                        continue;  // unwired layer port
                    }
                    const std::string prefix =
                        "layer" + std::to_string(i) + "_";
                    CompositeMaterialLayer layer{};
                    layer.source = port_keys[i];
                    layer.centre_uv[0] = static_cast<float>(
                        pb->get<double>(prefix + "centre_u", 0.5));
                    layer.centre_uv[1] = static_cast<float>(
                        pb->get<double>(prefix + "centre_v", 0.5));
                    layer.half_size_uv[0] = static_cast<float>(
                        pb->get<double>(prefix + "half_width", 0.35));
                    layer.half_size_uv[1] = static_cast<float>(
                        pb->get<double>(prefix + "half_height", 0.35));
                    layer.rotation = static_cast<float>(
                        pb->get<double>(prefix + "rotation", 0.0));
                    layer.opacity = static_cast<float>(
                        pb->get<double>(prefix + "opacity", 1.0));
                    data.composite_layers.push_back(layer);
                }

                if (gpu_resources) {
                    if (!publish_resident_render_target(
                            input.key, data.width, data.height, *gpu_resources,
                            rhi_resource_tracker, logger))
                    {
                        return compile_failed_node(
                            input, "composite material residency failed");
                    }
                }

                wz::asset::ResourceHandle handle = table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("composite material: failed to store metadata");
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
