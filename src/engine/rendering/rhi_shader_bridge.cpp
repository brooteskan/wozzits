#include <engine/rendering/rhi_shader_bridge.h>

#include <engine/rendering/rhi_render_program_bridge.h>

#include <wozzits/rhi/shader_module.h>

#include <string>
#include <vector>

namespace wz::engine::rendering
{
    bool register_program_shaders(
        wz::rhi::ShaderModuleRegistry& shaders,
        const wz::engine::assets::CustomRenderProgramDesc& src,
        std::span<const uint8_t> vertex_bytecode,
        std::span<const uint8_t> pixel_bytecode)
    {
        const std::string vertex_ref = shader_ref(src.vertex_shader);
        const std::string pixel_ref = shader_ref(src.pixel_shader);
        if (vertex_ref.empty() || pixel_ref.empty()) {
            return false;
        }

        const wz::rhi::Tag vertex_tag = shaders.register_program(
            wz::rhi::ShaderModuleDesc{
                vertex_ref,
                wz::rhi::ShaderStage::Vertex,
                std::vector<uint8_t>{
                    vertex_bytecode.begin(),
                    vertex_bytecode.end() } });
        const wz::rhi::Tag pixel_tag = shaders.register_program(
            wz::rhi::ShaderModuleDesc{
                pixel_ref,
                wz::rhi::ShaderStage::Pixel,
                std::vector<uint8_t>{
                    pixel_bytecode.begin(),
                    pixel_bytecode.end() } });

        return vertex_tag.valid() && pixel_tag.valid();
    }
}
