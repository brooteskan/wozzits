#pragma once
// engine/assets/shader/shader_file_asset.h

namespace wz::asset::shader {

    // Enum representing different shader stages
    enum class ShaderStage {
        Vertex,
        Fragment,
        Geometry,
        Compute
    };

    // ShaderFileAsset represents the raw shader source code
    struct ShaderFileAsset {
        AssetKey key;                   // Unique identifier for the shader file asset
        ShaderStage stage;               // The shader stage this file corresponds to
        std::string source_code;         // The raw shader source code (e.g., HLSL, GLSL)
        std::string entry_point;         // Entry point of the shader (e.g., main)
        std::string language;            // Shader language (e.g., HLSL, GLSL, etc.)
        std::vector<std::string> includes; // List of included shader files, if any

        // Constructor
        ShaderFileAsset(AssetKey key, ShaderStage stage, std::string source_code)
            : key(key), stage(stage), source_code(std::move(source_code)) {
        }
    };
}