#pragma once

// engine/assets/engine_asset_library.h

#include <asset/system.h>
#include <asset/compiler.h>
#include <asset/types.h>

#include <file/filesystem.h>

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>
#include <gpu/shader.h>

#include <logging/logger.h>

#include <engine/assets/scalar_field/scalar_field.h>
#include <engine/assets/file_carrier_asset_module.h>
#include <engine/assets/shader_asset_module.h>
#include <engine/assets/scalar_field_asset_module.h>

#include <string>
#include <vector>

namespace wz::engine::assets
{
    // ─── ResolveReport ────────────────────────────────────────────────────────────
    //
    // Returned by EngineAssetLibrary::resolve_all(). Carries the success count
    // and a structured list of failures for diagnostic use.

    struct ResolveFailure
    {
        wz::asset::AssetKey     key;
        wz::asset::ResolveError error;
    };

    struct ResolveReport
    {
        uint32_t                  resolved_count = 0;
        std::vector<ResolveFailure> failures;

        bool ok() const noexcept { return failures.empty(); }
    };


    // ─── Shader pair ──────────────────────────────────────────────────────────────

    struct ShaderPairDesc
    {
        std::string name;

        wz::fs::Path vertex_path;
        wz::fs::Path pixel_path;

        std::string vertex_entry = "main";
        std::string pixel_entry = "main";

        std::string vertex_target = "vs_5_0";
        std::string pixel_target = "ps_5_0";
    };

    struct ShaderPairAsset
    {
        wz::asset::AssetKey vertex_shader{};
        wz::asset::AssetKey pixel_shader{};

        bool valid() const noexcept
        {
            return vertex_shader != wz::asset::AssetKey{} &&
                pixel_shader != wz::asset::AssetKey{};
        }
    };

    struct ShaderPairHandles
    {
        wz::gpu::GPUHandle vertex{};
        wz::gpu::GPUHandle pixel{};

        bool valid() const noexcept
        {
            return vertex.valid() && pixel.valid();
        }
    };


    // ─── Scalar field ─────────────────────────────────────────────────────────────

    // Describes a file-backed scalar field asset to register.
    // path is relative to the EngineAssetLibrary's resource_root.
    struct ScalarFieldFileDesc
    {
        std::string name;

        wz::fs::Path path;          // relative to resource_root

        uint32_t width = 0;
        uint32_t height = 1;
        uint32_t depth = 1;

        ScalarFieldFormat     format = ScalarFieldFormat::Float32;
        ScalarFieldDomainKind domain_kind = ScalarFieldDomainKind::Spatial2D;
    };

    // Describes a procedural scalar field asset to register.
    // No file path is required — values are generated from the parameters alone.
    // name contributes to the asset key so differently-named procedural fields
    // with identical parameters are treated as distinct assets.
    struct ProceduralScalarFieldDesc
    {
        std::string name;

        uint32_t width = 0;
        uint32_t height = 1;
        uint32_t depth = 1;   // must be 1 for V1

        ScalarFieldGenerator  generator = ScalarFieldGenerator::GradientX;

        float frequency = 1.0f;
        float amplitude = 1.0f;

        ScalarFieldFormat     format = ScalarFieldFormat::Float32;
        ScalarFieldDomainKind domain_kind = ScalarFieldDomainKind::Spatial2D;
    };

    // Returned by create_scalar_field(). Wraps the DAG output node key.
    struct ScalarFieldAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    // Returned by get_scalar_field(). Wraps the ResourceHandle into ScalarFieldTable.
    struct ScalarFieldHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };


    // ─── EngineAssetLibrary ───────────────────────────────────────────────────────

    class EngineAssetLibrary
    {
    public:
        EngineAssetLibrary(
            wz::gpu::Device& device,
            wz::Logger& logger,
            wz::fs::Path      resource_root
        );

        EngineAssetLibrary(const EngineAssetLibrary&) = delete;
        EngineAssetLibrary& operator=(const EngineAssetLibrary&) = delete;

        EngineAssetLibrary(EngineAssetLibrary&&) = delete;
        EngineAssetLibrary& operator=(EngineAssetLibrary&&) = delete;

        // ── Shader pair ───────────────────────────────────────────────────────────

        ShaderPairAsset   create_shader_pair(const ShaderPairDesc& desc);
        ShaderPairHandles get_shader_pair(const ShaderPairAsset& asset) const;

        // ── Scalar field ──────────────────────────────────────────────────────────

        // Register a file-backed scalar field asset in the DAG.
        // Call commit() and resolve_all() before querying handles.
        ScalarFieldAsset create_scalar_field(const ScalarFieldFileDesc& desc);

        // Register a procedural scalar field asset in the DAG.
        // Values are generated entirely from the descriptor parameters; no file
        // dependency is required. Call commit() and resolve_all() before querying.
        ScalarFieldAsset create_procedural_scalar_field(
            const ProceduralScalarFieldDesc& desc);

        // Retrieve the ResourceHandle for a resolved scalar field asset.
        // Returns an invalid handle if the asset has not been resolved.
        ScalarFieldHandle get_scalar_field(const ScalarFieldAsset& asset) const;

        // Retrieve the resolved data for a scalar field by handle.
        // Returns nullptr if the handle is invalid or stale.
        const ScalarFieldData* get_scalar_field_data(ScalarFieldHandle handle) const;

        // ── Graph lifecycle ───────────────────────────────────────────────────────

        bool          commit();
        ResolveReport resolve_all();

        // ── Module accessors ──────────────────────────────────────────────────────

        FileCarrierAssetModule&       files()         { return files_; }
        ShaderAssetModule&            shaders()       { return shaders_; }
        ScalarFieldAssetModule&       scalar_fields() { return scalar_fields_; }

        const FileCarrierAssetModule&  files()         const { return files_; }
        const ShaderAssetModule&       shaders()       const { return shaders_; }
        const ScalarFieldAssetModule&  scalar_fields() const { return scalar_fields_; }

        // ── Direct access ─────────────────────────────────────────────────────────

        wz::asset::AssetSystem&       system()       { return system_; }
        const wz::asset::AssetSystem& system() const { return system_; }

    private:
        wz::asset::AssetKey register_hlsl_shader_node(
            std::string_view           name,
            wz::asset::AssetKey        source_file,
            const wz::gpu::HLSLCompileDesc& desc
        );

        wz::asset::AssetKey register_scalar_field_node(
            wz::asset::AssetKey        source_file,
            const ScalarFieldCompileDesc& desc
        );

        wz::asset::AssetKey register_procedural_scalar_field_node(
            const ProceduralScalarFieldCompileDesc& desc,
            std::string_view name
        );

    private:
        // Member declaration order is load-bearing — C++ initialises in this order.
        //
        // scalar_fields_table_ before system_: the compiler registry lambda
        //   captures a reference to the table; it must be alive when system_
        //   is constructed.
        //
        // system_ before the modules: modules hold a reference to system_.
        //
        // files_ before shaders_ and scalar_fields_: both modules hold a
        //   reference to files_.

        wz::gpu::Device& device_;
        wz::Logger&      logger_;
        wz::fs::Path     resource_root_;

        ScalarFieldTable       scalar_fields_table_;
        wz::asset::AssetSystem system_;

        FileCarrierAssetModule  files_;
        ShaderAssetModule       shaders_;
        ScalarFieldAssetModule  scalar_fields_;
    };

} // namespace wz::engine::assets
