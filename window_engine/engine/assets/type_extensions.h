#pragma once

// engine/assets/type_extensions.h

#include <engine/assets/engine_asset_key_core.h>

namespace wz::engine::assets {

    // ─── Engine AssetType extensions ─────────────────────────────────────────────
    //
    // Cast through the underlying type so no change to the generic enum is needed.
    //
    // Values below are part of asset identity and may appear in persisted caches
    // or serialized asset metadata. Once external data depends on them, do not
    // renumber existing constants casually. Prefer adding new values in unused
    // ranges.
    //
    // Numeric range allocation:
    //
    //      [0,    63]   Generic wz::asset::AssetType values.
    //                   Values explicitly declared in the generic asset library
    //                   live here. Engine-specific extensions should not use this
    //                   range.
    //
    //     [64,   511]   Engine foundation and CPU/intermediate asset types.
    //                   Examples: raw files, text files, scalar fields, textures,
    //                   meshes, splat clouds, audio clips, fonts, terrain data,
    //                   animation clips, collision data.
    //
    //    [512,  1023]   Renderer/backend GPU-resident asset types.
    //                   Examples: GPU textures, GPU meshes, GPU buffers,
    //                   GPU pipelines, descriptor tables, material bindings.
    //
    //   [1024,  2047]   Shader, pipeline-layout, render-state, and material-system
    //                   asset types that are not necessarily direct GPU resources.
    //                   Examples: shader variants, material definitions, binding
    //                   layouts, render pass definitions, material graphs.
    //
    //   [2048,  4095]   Scene, prefab, world, gameplay, UI, audio-behavior, and
    //                   project-level authored content.
    //                   Examples: scenes, prefabs, levels, worlds, quests,
    //                   dialogue graphs, UI screens, sound cues.
    //
    //   [4096,  8191]   Tooling, editor, build, import, and cooked/package assets.
    //                   Examples: import recipes, thumbnails, validation reports,
    //                   cooked assets, build manifests, asset bundles.
    //
    //   [8192, 65535]   Unallocated extension range.
    //                   Available for future engine modules, plugins,
    //                   experiments, game-specific ranges, or third-party
    //                   integrations.
    //
    // AssetType uses uint16_t as its underlying type, so 65535 is the maximum
    // representable value.

    // ─── Foundation / carrier asset types: 64–127 ───────────────────────────────

    // Implemented: generic raw byte file carrier.
    inline constexpr wz::asset::AssetType kAssetTypeRawFile =
        static_cast<wz::asset::AssetType>(64);

    // Reserved foundation asset type. Not implemented yet:
    // no public registration API, schema, compiler, or runtime table exists.
    inline constexpr wz::asset::AssetType kAssetTypeTextFile =
        static_cast<wz::asset::AssetType>(65);

    // Reserved foundation asset type. Not implemented yet:
    // no public registration API, schema, compiler, or runtime table exists.
    inline constexpr wz::asset::AssetType kAssetTypeBinaryBlob =
        static_cast<wz::asset::AssetType>(66);

    // Reserved foundation asset type. Not implemented yet:
    // no public registration API, schema, compiler, or runtime table exists.
    inline constexpr wz::asset::AssetType kAssetTypeManifest =
        static_cast<wz::asset::AssetType>(67);

    // Reserved foundation asset type. Not implemented yet:
    // no public registration API, schema, compiler, or runtime table exists.
    inline constexpr wz::asset::AssetType kAssetTypeAssetBundle =
        static_cast<wz::asset::AssetType>(68);

    // Reserved foundation asset type. Not implemented yet:
    // no public registration API, schema, compiler, or runtime table exists.
    inline constexpr wz::asset::AssetType kAssetTypePackage =
        static_cast<wz::asset::AssetType>(69);

    // Reserved foundation asset type. Not implemented yet:
    // no public registration API, schema, compiler, or runtime table exists.
    inline constexpr wz::asset::AssetType kAssetTypeImportedSourceFile =
        static_cast<wz::asset::AssetType>(70);

    // Reserved foundation asset type. Not implemented yet:
    // no public registration API, schema, compiler, or runtime table exists.
    inline constexpr wz::asset::AssetType kAssetTypeExternalReference =
        static_cast<wz::asset::AssetType>(71);

    // Reserved foundation asset type. Not implemented yet:
    // no public registration API, schema, compiler, or runtime table exists.
    inline constexpr wz::asset::AssetType kAssetTypeDirectory =
        static_cast<wz::asset::AssetType>(72);

    // Reserved foundation asset type. Not implemented yet:
    // no public registration API, schema, compiler, or runtime table exists.
    inline constexpr wz::asset::AssetType kAssetTypeArchive =
        static_cast<wz::asset::AssetType>(73);


    // ─── CPU data asset types: 128–191 ───────────────────────────────────────────
    // Decoded, engine-readable, non-GPU runtime data.
    //
    // Unless marked "Implemented", these are reserved only:
    // no public registration API, schema, compiler, or runtime table exists yet.

    // Implemented: immutable CPU-side sampled scalar field.
    // Runtime data is owned by ScalarFieldTable.
    inline constexpr wz::asset::AssetType kAssetTypeScalarField =
        static_cast<wz::asset::AssetType>(128);

    // Reserved CPU data asset type. Not implemented yet:
    // intended runtime representation: TextureData owned by TextureTable.
    inline constexpr wz::asset::AssetType kAssetTypeTexture =
        static_cast<wz::asset::AssetType>(129);

    // Reserved CPU data asset type. Not implemented yet:
    // intended runtime representation: MeshData owned by MeshTable.
    inline constexpr wz::asset::AssetType kAssetTypeMesh =
        static_cast<wz::asset::AssetType>(130);

    // Reserved CPU data asset type. Not implemented yet:
    // key/schema support may exist, but no full public API, compiler, or runtime
    // table exists.
    inline constexpr wz::asset::AssetType kAssetTypeGaussianSplatCloud =
        static_cast<wz::asset::AssetType>(131);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypePointCloud =
        static_cast<wz::asset::AssetType>(132);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeVoxelGrid =
        static_cast<wz::asset::AssetType>(133);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeCurve =
        static_cast<wz::asset::AssetType>(134);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeSpline =
        static_cast<wz::asset::AssetType>(135);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeSignedDistanceField =
        static_cast<wz::asset::AssetType>(136);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeHeightField =
        static_cast<wz::asset::AssetType>(137);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeDistanceFieldVolume =
        static_cast<wz::asset::AssetType>(138);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeNavigationMesh =
        static_cast<wz::asset::AssetType>(139);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeNavigationGrid =
        static_cast<wz::asset::AssetType>(140);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeCollisionMesh =
        static_cast<wz::asset::AssetType>(141);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypePhysicsShape =
        static_cast<wz::asset::AssetType>(142);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeAnimationClip =
        static_cast<wz::asset::AssetType>(143);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeSkeleton =
        static_cast<wz::asset::AssetType>(144);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeShaderReflection =
        static_cast<wz::asset::AssetType>(145);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeAudioClip =
        static_cast<wz::asset::AssetType>(146);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeFontFace =
        static_cast<wz::asset::AssetType>(147);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeLocalizationTable =
        static_cast<wz::asset::AssetType>(148);

    // Reserved CPU data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeTerrainLayer =
        static_cast<wz::asset::AssetType>(149);

    // Implemented: parsed CSV document (headers + ragged rows of raw strings).
    // Runtime data is owned by CSVTable.
    inline constexpr wz::asset::AssetType kAssetTypeCSVTable =
        static_cast<wz::asset::AssetType>(150);

    // Implemented soon: parsed JSON document.
    // Runtime data is owned by JSONTable.
    inline constexpr wz::asset::AssetType kAssetTypeJSONDocument =
        static_cast<wz::asset::AssetType>(151);

    // Implemented: parsed TOML document.
    // Runtime data is owned by TOMLTable.
    inline constexpr wz::asset::AssetType kAssetTypeTOMLDocument =
        static_cast<wz::asset::AssetType>(152);

    // ─── Texture / image CPU and intermediate asset types: 160–191 ──────────────
    // Texture specializations that may need distinct recipe/runtime paths.
    //
    // Do not create separate AssetTypes for every material-map usage such as
    // albedo, normal, roughness, metalness, AO, emissive, opacity, etc. Those
    // should usually be TextureRole / TextureUsage metadata on TextureData or
    // MaterialDefinition, not distinct AssetTypes.

    // Reserved texture atlas asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeTextureAtlas =
        static_cast<wz::asset::AssetType>(160);

    // Reserved sprite-sheet asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeSpriteSheet =
        static_cast<wz::asset::AssetType>(161);

    // Reserved virtual texture asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeVirtualTexture =
        static_cast<wz::asset::AssetType>(162);

    // Reserved streaming texture asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeStreamingTexture =
        static_cast<wz::asset::AssetType>(163);

    // Reserved compressed texture asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeCompressedTexture =
        static_cast<wz::asset::AssetType>(164);

    // Reserved mip-chain asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeMipChain =
        static_cast<wz::asset::AssetType>(165);

    // Reserved procedural texture recipe/data asset type. Not implemented yet.
    // This may later be represented as kAssetTypeTexture + kTextureProceduralSchema
    // instead of a distinct AssetType; reserved only if procedural textures need
    // to be queryable as their own category.
    inline constexpr wz::asset::AssetType kAssetTypeProceduralTexture =
        static_cast<wz::asset::AssetType>(166);


    // ─── Geometry / terrain CPU and intermediate asset types: 192–255 ───────────
    // These remain inside the broader [64, 511] engine CPU/intermediate range.
    //
    // Mesh variants such as StaticMesh, SkinnedMesh, ProceduralMesh, TerrainMesh,
    // and ImpostorMesh should usually be represented as kAssetTypeMesh plus
    // MeshKind / MeshUsage metadata, not separate AssetTypes.

    // Reserved mesh LOD-group asset type. Not implemented yet.
    // Represents a CPU-side grouping of mesh LODs, not a single mesh.
    inline constexpr wz::asset::AssetType kAssetTypeMeshLODGroup =
        static_cast<wz::asset::AssetType>(192);

    // Reserved terrain asset type. Not implemented yet.
    // Represents one terrain tile, usually referencing height fields, terrain
    // layers, material data, and generated mesh/texture data.
    inline constexpr wz::asset::AssetType kAssetTypeTerrainTile =
        static_cast<wz::asset::AssetType>(193);

    // Reserved terrain asset type. Not implemented yet.
    // Represents the material/layer set used by terrain rendering or terrain baking.
    inline constexpr wz::asset::AssetType kAssetTypeTerrainMaterialSet =
        static_cast<wz::asset::AssetType>(194);

    // Reserved terrain asset type. Not implemented yet.
    // Represents a procedural recipe for producing terrain data from scalar fields,
    // noise, erosion, masks, or authored parameters.
    inline constexpr wz::asset::AssetType kAssetTypeProceduralTerrainRecipe =
        static_cast<wz::asset::AssetType>(195);

    // Reserved terrain asset type. Not implemented yet.
    // Represents a streamable terrain chunk, usually derived from TerrainTile or
    // WorldPartition data.
    inline constexpr wz::asset::AssetType kAssetTypeTerrainChunk =
        static_cast<wz::asset::AssetType>(196);

    // Reserved terrain asset type. Not implemented yet.
    // Represents terrain LOD grouping or terrain LOD metadata.
    inline constexpr wz::asset::AssetType kAssetTypeTerrainLOD =
        static_cast<wz::asset::AssetType>(197);

    // Reserved terrain asset type. Not implemented yet.
    // Represents voxel-based terrain data. May later derive from VoxelGrid.
    inline constexpr wz::asset::AssetType kAssetTypeVoxelTerrain =
        static_cast<wz::asset::AssetType>(198);

    // Reserved terrain asset type. Not implemented yet.
    // Represents terrain defined by signed distance data. May later derive from
    // SignedDistanceField or DistanceFieldVolume.
    inline constexpr wz::asset::AssetType kAssetTypeSignedDistanceTerrain =
        static_cast<wz::asset::AssetType>(199);


    // ─── Renderer/backend GPU-resident asset types: 512–1023 ────────────────────
    // Backend-owned or backend-facing resources.
    //
    // Unless marked "Implemented", these are reserved only:
    // no public registration API, schema, compiler, or backend upload path exists
    // yet.
    //
    // Note:
    // The current shader asset path uses the generic wz::asset::AssetType::Shader.
    // kAssetTypeGPUShader is reserved for a future engine/backend-specific shader
    // asset type if we decide to move compiled shaders out of the generic enum.

    // Reserved GPU/backend asset type. Not implemented yet.
    // Current shader compilation uses wz::asset::AssetType::Shader.
    inline constexpr wz::asset::AssetType kAssetTypeGPUShader =
        static_cast<wz::asset::AssetType>(512);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUPipeline =
        static_cast<wz::asset::AssetType>(513);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPURootSignature =
        static_cast<wz::asset::AssetType>(514);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUBuffer =
        static_cast<wz::asset::AssetType>(515);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUVertexBuffer =
        static_cast<wz::asset::AssetType>(516);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUIndexBuffer =
        static_cast<wz::asset::AssetType>(517);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUTexture =
        static_cast<wz::asset::AssetType>(518);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUTextureView =
        static_cast<wz::asset::AssetType>(519);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUSampler =
        static_cast<wz::asset::AssetType>(520);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUMesh =
        static_cast<wz::asset::AssetType>(521);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUSplatCloud =
        static_cast<wz::asset::AssetType>(522);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUVoxelVolume =
        static_cast<wz::asset::AssetType>(523);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUAccelerationStructure =
        static_cast<wz::asset::AssetType>(524);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUComputePipeline =
        static_cast<wz::asset::AssetType>(525);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPURenderTarget =
        static_cast<wz::asset::AssetType>(526);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUDepthStencil =
        static_cast<wz::asset::AssetType>(527);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUDescriptorSet =
        static_cast<wz::asset::AssetType>(528);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUDescriptorTable =
        static_cast<wz::asset::AssetType>(529);

    // Reserved GPU/backend asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUMaterialBinding =
        static_cast<wz::asset::AssetType>(530);

    // Reserved GPU/backend pipeline state object asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPUPipelineStateObject =
        static_cast<wz::asset::AssetType>(531);

    // Reserved GPU/backend graphics pipeline asset type. Not implemented yet.
    // This may later specialize or replace kAssetTypeGPUPipeline.
    inline constexpr wz::asset::AssetType kAssetTypeGPUGraphicsPipeline =
        static_cast<wz::asset::AssetType>(532);

    // Reserved GPU/backend ray tracing pipeline asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGPURayTracingPipeline =
        static_cast<wz::asset::AssetType>(533);

    // Reserved GPU/backend instance-buffer asset type. Not implemented yet.
    // Represents backend-owned per-instance data, usually derived from scene/render data.
    inline constexpr wz::asset::AssetType kAssetTypeGPUInstanceBuffer =
        static_cast<wz::asset::AssetType>(534);

    // Reserved GPU/backend splat render pipeline asset type. Not implemented yet.
    // This is a backend-facing render pipeline, not the splat cloud data itself.
    inline constexpr wz::asset::AssetType kAssetTypeGPUSplatRenderPipeline =
        static_cast<wz::asset::AssetType>(535);


    // ─── Shader / pipeline / render-state descriptions: 1024–1059 ───────────────
    // Shader and pipeline descriptions that are not necessarily backend-owned GPU
    // resources.
    //
    // Note:
    // The current HLSL path uses the generic wz::asset::AssetType::ShaderSource
    // and wz::asset::AssetType::Shader. These constants are reserved for a future
    // engine-level shader model if/when we migrate away from the generic enum.

    // Reserved shader/source asset type. Not implemented here.
    // Current source shader files use wz::asset::AssetType::ShaderSource.
    inline constexpr wz::asset::AssetType kAssetTypeShaderSource =
        static_cast<wz::asset::AssetType>(1024);

    // Reserved shader module asset type. Not implemented yet.
    // Current compiled shaders use wz::asset::AssetType::Shader.
    inline constexpr wz::asset::AssetType kAssetTypeShaderModule =
        static_cast<wz::asset::AssetType>(1025);

    // Reserved shader program asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeShaderProgram =
        static_cast<wz::asset::AssetType>(1026);

    // Reserved shader permutation asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeShaderPermutation =
        static_cast<wz::asset::AssetType>(1027);

    // Reserved shader variant asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeShaderVariant =
        static_cast<wz::asset::AssetType>(1028);

    // Reserved shader library asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeShaderLibrary =
        static_cast<wz::asset::AssetType>(1029);

    // Reserved render pass definition asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeRenderPassDefinition =
        static_cast<wz::asset::AssetType>(1040);

    // Reserved blend-state description asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeBlendState =
        static_cast<wz::asset::AssetType>(1041);

    // Reserved depth/stencil-state description asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeDepthStencilState =
        static_cast<wz::asset::AssetType>(1042);

    // Reserved rasterizer-state description asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeRasterizerState =
        static_cast<wz::asset::AssetType>(1043);

    // Reserved input-layout description asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeInputLayout =
        static_cast<wz::asset::AssetType>(1044);

    // Reserved vertex-layout description asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeVertexLayout =
        static_cast<wz::asset::AssetType>(1045);

    // Reserved descriptor-layout description asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeDescriptorLayout =
        static_cast<wz::asset::AssetType>(1046);

    // Reserved binding-layout description asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeBindingLayout =
        static_cast<wz::asset::AssetType>(1047);


    // ─── Material asset types: 1060–1099 ─────────────────────────────────────────
    // CPU-side authored material definitions and instances.
    //
    // These are authored content that reference engine/render resources such as
    // shaders, textures, samplers, and pipeline layouts.
    //
    // Unless marked "Implemented", these are reserved only:
    // no public registration API, schema, compiler, material table, or GPU binding
    // path exists yet.

    // Reserved material definition asset type. Not implemented yet.
    // Describes a material recipe/template: shader references, expected parameters,
    // texture slots, sampler slots, render-state expectations, etc.
    inline constexpr wz::asset::AssetType kAssetTypeMaterialDefinition =
        static_cast<wz::asset::AssetType>(1060);

    // Reserved material instance asset type. Not implemented yet.
    // Represents concrete parameter values and texture/sampler bindings for a
    // MaterialDefinition.
    inline constexpr wz::asset::AssetType kAssetTypeMaterialInstance =
        static_cast<wz::asset::AssetType>(1061);

    // Reserved material template asset type. Not implemented yet.
    // Represents reusable high-level material families such as PBR, unlit,
    // terrain, decal, particle, or splat templates.
    inline constexpr wz::asset::AssetType kAssetTypeMaterialTemplate =
        static_cast<wz::asset::AssetType>(1062);

    // Reserved material graph asset type. Not implemented yet.
    // Represents node-graph authored material logic before lowering to shader or
    // material definitions.
    inline constexpr wz::asset::AssetType kAssetTypeMaterialGraph =
        static_cast<wz::asset::AssetType>(1063);

    // Reserved material function asset type. Not implemented yet.
    // Represents reusable subgraphs/functions used by MaterialGraph assets.
    inline constexpr wz::asset::AssetType kAssetTypeMaterialFunction =
        static_cast<wz::asset::AssetType>(1064);

    // Reserved material parameter block asset type. Not implemented yet.
    // Represents named CPU-side parameter data that can be shared across material
    // instances or uploaded to GPU constant/uniform buffers later.
    inline constexpr wz::asset::AssetType kAssetTypeMaterialParameterBlock =
        static_cast<wz::asset::AssetType>(1065);

    // Reserved material variant asset type. Not implemented yet.
    // Represents a named variant of a material definition or instance.
    inline constexpr wz::asset::AssetType kAssetTypeMaterialVariant =
        static_cast<wz::asset::AssetType>(1066);

    // Reserved material permutation asset type. Not implemented yet.
    // Represents a compiled or selected permutation of shader/material features.
    inline constexpr wz::asset::AssetType kAssetTypeMaterialPermutation =
        static_cast<wz::asset::AssetType>(1067);

    // Reserved shader binding-set asset type. Not implemented yet.
    // CPU-side description of shader resource bindings.
    inline constexpr wz::asset::AssetType kAssetTypeShaderBindingSet =
        static_cast<wz::asset::AssetType>(1068);

    // Reserved texture binding-set asset type. Not implemented yet.
    // CPU-side description of texture bindings used by a material or render pass.
    inline constexpr wz::asset::AssetType kAssetTypeTextureBindingSet =
        static_cast<wz::asset::AssetType>(1069);

    // Reserved sampler binding-set asset type. Not implemented yet.
    // CPU-side description of sampler bindings used by a material or render pass.
    inline constexpr wz::asset::AssetType kAssetTypeSamplerBindingSet =
        static_cast<wz::asset::AssetType>(1070);


    // ─── Scene / prefab / world asset types: 2048–2079 ──────────────────────────
    // High-level authored scene, prefab, and world-organization assets.
    //
    // These are reserved only unless explicitly marked implemented:
    // no public registration API, schema, compiler, runtime table, streaming system,
    // or scene loader exists yet.

    // Reserved scene/world asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeWorldPartition =
        static_cast<wz::asset::AssetType>(2048);

    // Reserved scene/world asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeScene =
        static_cast<wz::asset::AssetType>(2049);

    // Reserved scene/world asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypePrefab =
        static_cast<wz::asset::AssetType>(2050);

    // Reserved scene/world asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeEntityArchetype =
        static_cast<wz::asset::AssetType>(2051);

    // Reserved game/project-level asset type. Not implemented yet.
    // Represents a complete playable/loadable level.
    inline constexpr wz::asset::AssetType kAssetTypeLevel =
        static_cast<wz::asset::AssetType>(2052);

    // Reserved game/project-level asset type. Not implemented yet.
    // Represents a complete world definition.
    inline constexpr wz::asset::AssetType kAssetTypeWorld =
        static_cast<wz::asset::AssetType>(2053);

    // Reserved scene chunk asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeSceneChunk =
        static_cast<wz::asset::AssetType>(2054);

    // Reserved entity template asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeEntityTemplate =
        static_cast<wz::asset::AssetType>(2055);

    // Reserved component blob asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeComponentBlob =
        static_cast<wz::asset::AssetType>(2056);

    // Reserved transform hierarchy asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeTransformHierarchy =
        static_cast<wz::asset::AssetType>(2057);

    // Reserved render scene data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeRenderSceneData =
        static_cast<wz::asset::AssetType>(2058);

    // Reserved lighting scene data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeLightingSceneData =
        static_cast<wz::asset::AssetType>(2059);

    // Reserved physics scene data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypePhysicsSceneData =
        static_cast<wz::asset::AssetType>(2060);

    // Reserved navigation scene data asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeNavigationSceneData =
        static_cast<wz::asset::AssetType>(2061);

    // Reserved streaming cell asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeStreamingCell =
        static_cast<wz::asset::AssetType>(2062);

    // Reserved portal zone asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypePortalZone =
        static_cast<wz::asset::AssetType>(2063);

    // Reserved occlusion zone asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeOcclusionZone =
        static_cast<wz::asset::AssetType>(2064);

    // Reserved spawn table asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeSpawnTable =
        static_cast<wz::asset::AssetType>(2065);

    // Reserved placement set asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypePlacementSet =
        static_cast<wz::asset::AssetType>(2066);


    // ─── Animation authored asset types: 2080–2099 ──────────────────────────────

    // Reserved animation asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeBoneHierarchy =
        static_cast<wz::asset::AssetType>(2080);

    // Reserved animation asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeAnimationCurve =
        static_cast<wz::asset::AssetType>(2081);

    // Reserved animation asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeAnimationTrack =
        static_cast<wz::asset::AssetType>(2082);

    // Reserved animation asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeAnimationSet =
        static_cast<wz::asset::AssetType>(2083);

    // Reserved animation asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeBlendTree =
        static_cast<wz::asset::AssetType>(2084);

    // Reserved animation asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeAnimationStateMachine =
        static_cast<wz::asset::AssetType>(2085);

    // Reserved animation asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeAnimationController =
        static_cast<wz::asset::AssetType>(2086);

    // Reserved animation asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeRetargetingMap =
        static_cast<wz::asset::AssetType>(2087);

    // Reserved animation asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeIKRig =
        static_cast<wz::asset::AssetType>(2088);

    // Reserved animation asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeIKPose =
        static_cast<wz::asset::AssetType>(2089);

    // Reserved animation asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeMorphTargetAnimation =
        static_cast<wz::asset::AssetType>(2090);

    // Reserved animation asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeFacialAnimation =
        static_cast<wz::asset::AssetType>(2091);

    // Reserved animation asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeMotionMatchingDatabase =
        static_cast<wz::asset::AssetType>(2092);

    // Reserved animation asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeAnimationCompressionTable =
        static_cast<wz::asset::AssetType>(2093);


    // ─── Physics authored asset types: 2110–2129 ────────────────────────────────

    // Reserved physics asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeCollider =
        static_cast<wz::asset::AssetType>(2110);

    // Reserved physics asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeBoxCollider =
        static_cast<wz::asset::AssetType>(2111);

    // Reserved physics asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeSphereCollider =
        static_cast<wz::asset::AssetType>(2112);

    // Reserved physics asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeCapsuleCollider =
        static_cast<wz::asset::AssetType>(2113);

    // Reserved physics asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeConvexHullCollider =
        static_cast<wz::asset::AssetType>(2114);

    // Reserved physics asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeTriangleMeshCollider =
        static_cast<wz::asset::AssetType>(2115);

    // Reserved physics asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeCompoundCollider =
        static_cast<wz::asset::AssetType>(2116);

    // Reserved physics asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeRigidBodyDefinition =
        static_cast<wz::asset::AssetType>(2117);

    // Reserved physics asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypePhysicsMaterial =
        static_cast<wz::asset::AssetType>(2118);

    // Reserved physics asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeJointDefinition =
        static_cast<wz::asset::AssetType>(2119);

    // Reserved physics asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeConstraintDefinition =
        static_cast<wz::asset::AssetType>(2120);

    // Reserved physics asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeRagdollDefinition =
        static_cast<wz::asset::AssetType>(2121);

    // Reserved physics asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeClothSimulation =
        static_cast<wz::asset::AssetType>(2122);

    // Reserved physics asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeSoftBody =
        static_cast<wz::asset::AssetType>(2123);

    // Reserved physics asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypePhysicsSceneSettings =
        static_cast<wz::asset::AssetType>(2124);


    // ─── Audio behavior asset types: 2140–2159 ──────────────────────────────────

    // Reserved audio asset type. Not implemented yet.
    // Represents a higher-level authored sound event/cue.
    inline constexpr wz::asset::AssetType kAssetTypeSoundCue =
        static_cast<wz::asset::AssetType>(2140);

    // Reserved audio asset type. Not implemented yet.
    // Represents a packed or grouped collection of audio assets.
    inline constexpr wz::asset::AssetType kAssetTypeAudioBank =
        static_cast<wz::asset::AssetType>(2141);


    // ─── UI / text authored asset types: 2160–2199 ──────────────────────────────
    //
    // UITexture should usually be represented as kAssetTypeTexture plus
    // TextureRole::UI, not as a distinct AssetType.

    // Reserved UI/text asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeFontAtlas =
        static_cast<wz::asset::AssetType>(2160);

    // Reserved UI/text asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeGlyphSet =
        static_cast<wz::asset::AssetType>(2161);

    // Reserved UI/text asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeRichTextStyle =
        static_cast<wz::asset::AssetType>(2162);

    // Reserved UI/text asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeNineSliceImage =
        static_cast<wz::asset::AssetType>(2163);

    // Reserved UI/text asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeUILayout =
        static_cast<wz::asset::AssetType>(2164);

    // Reserved UI/text asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeUIScreen =
        static_cast<wz::asset::AssetType>(2165);

    // Reserved UI/text asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeUITheme =
        static_cast<wz::asset::AssetType>(2166);

    // Reserved UI/text asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeIconSet =
        static_cast<wz::asset::AssetType>(2167);

    // Reserved UI/text asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeCursor =
        static_cast<wz::asset::AssetType>(2168);

    // Reserved UI/text asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeSubtitleTrack =
        static_cast<wz::asset::AssetType>(2169);

    // Reserved UI/text asset type. Not implemented yet.
    inline constexpr wz::asset::AssetType kAssetTypeDialogueText =
        static_cast<wz::asset::AssetType>(2170);

    // ─── Gameplay / data authored asset types: 2200–2299 ───────────────────────
    //
    // Gameplay definitions, progression data, economy data, dialogue/quest data,
    // and reusable authored game-content records.
    //
    // These are reserved only unless explicitly marked implemented:
    // no public registration API, schema, compiler, runtime table, gameplay
    // database, editor integration, or serialization path exists yet.
    //
    // Existing related reservation:
    //   kAssetTypeSpawnTable = 2065
    //
    // Do not duplicate SpawnTable here unless the old value is intentionally
    // migrated.

    // Reserved gameplay asset type. Not implemented yet.
    // Represents a generic item definition: display data, stats, tags, icons,
    // inventory behavior, stack rules, and references to other assets.
    inline constexpr wz::asset::AssetType kAssetTypeItemDefinition =
        static_cast<wz::asset::AssetType>(2200);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents a weapon definition: damage model, fire/use behavior, animations,
    // effects, audio, UI data, and tuning parameters.
    inline constexpr wz::asset::AssetType kAssetTypeWeaponDefinition =
        static_cast<wz::asset::AssetType>(2201);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents an ability definition: activation rules, costs, cooldowns,
    // effects, targeting rules, and presentation references.
    inline constexpr wz::asset::AssetType kAssetTypeAbilityDefinition =
        static_cast<wz::asset::AssetType>(2202);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents a spell-like ability definition. May later collapse into
    // AbilityDefinition plus metadata if no separate runtime path is needed.
    inline constexpr wz::asset::AssetType kAssetTypeSpellDefinition =
        static_cast<wz::asset::AssetType>(2203);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents a skill tree, progression graph, unlock graph, or ability tree.
    inline constexpr wz::asset::AssetType kAssetTypeSkillTree =
        static_cast<wz::asset::AssetType>(2204);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents a player/character archetype definition.
    inline constexpr wz::asset::AssetType kAssetTypeCharacterDefinition =
        static_cast<wz::asset::AssetType>(2205);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents an enemy archetype definition.
    inline constexpr wz::asset::AssetType kAssetTypeEnemyDefinition =
        static_cast<wz::asset::AssetType>(2206);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents an NPC archetype definition.
    inline constexpr wz::asset::AssetType kAssetTypeNPCDefinition =
        static_cast<wz::asset::AssetType>(2207);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents weighted or rule-driven loot generation data.
    inline constexpr wz::asset::AssetType kAssetTypeLootTable =
        static_cast<wz::asset::AssetType>(2208);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents a quest definition: objectives, stages, rewards, conditions,
    // dialogue links, triggers, and completion rules.
    inline constexpr wz::asset::AssetType kAssetTypeQuestDefinition =
        static_cast<wz::asset::AssetType>(2209);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents authored dialogue graph data.
    inline constexpr wz::asset::AssetType kAssetTypeDialogueGraph =
        static_cast<wz::asset::AssetType>(2210);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents factions, relationships, reputation data, hostility rules, etc.
    inline constexpr wz::asset::AssetType kAssetTypeFactionDefinition =
        static_cast<wz::asset::AssetType>(2211);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents prices, currencies, vendors, drop rates, or economic tuning data.
    inline constexpr wz::asset::AssetType kAssetTypeEconomyTable =
        static_cast<wz::asset::AssetType>(2212);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents crafting inputs, outputs, stations, requirements, and rules.
    inline constexpr wz::asset::AssetType kAssetTypeCraftingRecipe =
        static_cast<wz::asset::AssetType>(2213);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents inventory layout, slots, filters, item constraints, and rules.
    inline constexpr wz::asset::AssetType kAssetTypeInventoryDefinition =
        static_cast<wz::asset::AssetType>(2214);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents a reusable progression/tuning curve.
    inline constexpr wz::asset::AssetType kAssetTypeProgressionCurve =
        static_cast<wz::asset::AssetType>(2215);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents named stats and values for characters, items, enemies, etc.
    inline constexpr wz::asset::AssetType kAssetTypeStatBlock =
        static_cast<wz::asset::AssetType>(2216);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents damage types, resistances, vulnerabilities, tags, and rules.
    inline constexpr wz::asset::AssetType kAssetTypeDamageTypeTable =
        static_cast<wz::asset::AssetType>(2217);

    // Reserved gameplay asset type. Not implemented yet.
    // Represents buffs, debuffs, conditions, timed effects, and gameplay modifiers.
    inline constexpr wz::asset::AssetType kAssetTypeStatusEffectDefinition =
        static_cast<wz::asset::AssetType>(2218);

    // ─── AI authored asset types: 2230–2249 ─────────────────────────────────────
    //
    // AI behavior definitions, decision systems, tactical data, and navigation-
    // related authored data.
    //
    // These are reserved only unless explicitly marked implemented:
    // no public registration API, schema, compiler, runtime table, AI runtime,
    // planner, behavior evaluator, or navigation integration exists yet.
    //
    // Related existing CPU/intermediate reservations:
    //   kAssetTypeNavigationMesh
    //   kAssetTypeNavigationGrid
    //
    // Do not duplicate NavMesh/NavGrid here unless those values are intentionally
    // migrated.

    // Reserved AI asset type. Not implemented yet.
    // Represents an authored behavior tree.
    inline constexpr wz::asset::AssetType kAssetTypeBehaviorTree =
        static_cast<wz::asset::AssetType>(2230);

    // Reserved AI asset type. Not implemented yet.
    // Represents a decision tree or branching decision model.
    inline constexpr wz::asset::AssetType kAssetTypeDecisionTree =
        static_cast<wz::asset::AssetType>(2231);

    // Reserved AI asset type. Not implemented yet.
    // Represents utility-AI scoring configuration.
    inline constexpr wz::asset::AssetType kAssetTypeUtilityAIConfig =
        static_cast<wz::asset::AssetType>(2232);

    // Reserved AI asset type. Not implemented yet.
    // Represents a GOAP action/state/planning domain.
    inline constexpr wz::asset::AssetType kAssetTypeGOAPDomain =
        static_cast<wz::asset::AssetType>(2233);

    // Reserved AI asset type. Not implemented yet.
    // Represents blackboard key/type/schema definitions.
    inline constexpr wz::asset::AssetType kAssetTypeBlackboardSchema =
        static_cast<wz::asset::AssetType>(2234);

    // Reserved AI asset type. Not implemented yet.
    // Represents a navigation graph distinct from mesh/grid navigation data.
    inline constexpr wz::asset::AssetType kAssetTypeNavigationGraph =
        static_cast<wz::asset::AssetType>(2235);

    // Reserved AI asset type. Not implemented yet.
    // Represents authored or generated cover point data.
    inline constexpr wz::asset::AssetType kAssetTypeCoverPointSet =
        static_cast<wz::asset::AssetType>(2236);

    // Reserved AI asset type. Not implemented yet.
    // Represents a patrol route, waypoint chain, or route graph.
    inline constexpr wz::asset::AssetType kAssetTypePatrolRoute =
        static_cast<wz::asset::AssetType>(2237);

    // Reserved AI asset type. Not implemented yet.
    // Represents tactical/strategic map annotations used by AI systems.
    inline constexpr wz::asset::AssetType kAssetTypeTacticalMap =
        static_cast<wz::asset::AssetType>(2238);

    // Reserved AI asset type. Not implemented yet.
    // Represents crowd simulation tuning/configuration data.
    inline constexpr wz::asset::AssetType kAssetTypeCrowdSimulationConfig =
        static_cast<wz::asset::AssetType>(2239);

    // Reserved AI asset type. Not implemented yet.
    // Represents a high-level NPC brain definition that references behavior,
    // perception, blackboard, dialogue, combat, and navigation assets.
    inline constexpr wz::asset::AssetType kAssetTypeNPCBrainDefinition =
        static_cast<wz::asset::AssetType>(2240);

    // ─── VFX / particle authored asset types: 2250–2269 ─────────────────────────
    //
    // Particle systems, VFX graphs, procedural effect definitions, and effect-
    // oriented renderer data.
    //
    // These are reserved only unless explicitly marked implemented:
    // no public registration API, schema, compiler, runtime table, particle
    // runtime, VFX graph evaluator, or renderer integration exists yet.
    //
    // Related existing CPU/intermediate reservations:
    //   kAssetTypeTexture
    //   kAssetTypeScalarField
    //   kAssetTypeSignedDistanceField
    //
    // Do not duplicate FlipbookTexture, NoiseField, or SignedDistanceField here
    // unless they become distinct runtime products rather than texture/field roles.

    // Reserved VFX asset type. Not implemented yet.
    // Represents a particle emitter definition: spawn rules, rates, shapes,
    // lifetime, velocity, color/size curves, and references to materials/textures.
    inline constexpr wz::asset::AssetType kAssetTypeParticleEmitter =
        static_cast<wz::asset::AssetType>(2250);

    // Reserved VFX asset type. Not implemented yet.
    // Represents a complete particle system made of one or more emitters.
    inline constexpr wz::asset::AssetType kAssetTypeParticleSystem =
        static_cast<wz::asset::AssetType>(2251);

    // Reserved VFX asset type. Not implemented yet.
    // Represents a particle-specific node graph.
    inline constexpr wz::asset::AssetType kAssetTypeParticleGraph =
        static_cast<wz::asset::AssetType>(2252);

    // Reserved VFX asset type. Not implemented yet.
    // Represents a general visual-effects graph.
    inline constexpr wz::asset::AssetType kAssetTypeVFXGraph =
        static_cast<wz::asset::AssetType>(2253);

    // Reserved VFX asset type. Not implemented yet.
    // Represents trail renderer settings and authored trail behavior.
    inline constexpr wz::asset::AssetType kAssetTypeTrailRenderer =
        static_cast<wz::asset::AssetType>(2254);

    // Reserved VFX asset type. Not implemented yet.
    // Represents beam effect settings: endpoints, noise, width, material, timing.
    inline constexpr wz::asset::AssetType kAssetTypeBeam =
        static_cast<wz::asset::AssetType>(2255);

    // Reserved VFX asset type. Not implemented yet.
    // Represents ribbon effect settings.
    inline constexpr wz::asset::AssetType kAssetTypeRibbon =
        static_cast<wz::asset::AssetType>(2256);

    // Reserved VFX asset type. Not implemented yet.
    // Represents decal projection/rendering metadata.
    // The visual image itself should usually be a Texture asset.
    inline constexpr wz::asset::AssetType kAssetTypeDecal =
        static_cast<wz::asset::AssetType>(2257);

    // Reserved VFX asset type. Not implemented yet.
    // Represents a vector field used by particles, flow, wind, fluids, or motion.
    inline constexpr wz::asset::AssetType kAssetTypeVectorField =
        static_cast<wz::asset::AssetType>(2258);

    // Reserved VFX asset type. Not implemented yet.
    // Represents cached simulation data for particles, cloth-like effects,
    // fluids, destruction, or offline-baked VFX.
    inline constexpr wz::asset::AssetType kAssetTypeSimulationCache =
        static_cast<wz::asset::AssetType>(2259);

    // ─── Lighting / rendering environment asset types: 2270–2299 ────────────────
    //
    // Lighting probes, environment descriptions, sky/fog/atmosphere settings,
    // baked lighting products, and post-process profiles.
    //
    // These are reserved only unless explicitly marked implemented:
    // no public registration API, schema, compiler, runtime table, lighting bake
    // pipeline, renderer integration, or probe system exists yet.
    //
    // Related existing reservations:
    //   kAssetTypeTexture
    //   kAssetTypeGPUTexture
    //   kAssetTypeRenderSceneData
    //   kAssetTypeLightingSceneData
    //
    // EnvironmentMap, Skybox, BakedLightmap, and color-grading LUT data may
    // internally reference Texture assets. Do not duplicate raw image data here.

    // Reserved lighting/environment asset type. Not implemented yet.
    // Represents a sampled light probe used for local lighting approximation.
    inline constexpr wz::asset::AssetType kAssetTypeLightProbe =
        static_cast<wz::asset::AssetType>(2270);

    // Reserved lighting/environment asset type. Not implemented yet.
    // Represents a reflection probe, usually referencing cubemap/environment data.
    inline constexpr wz::asset::AssetType kAssetTypeReflectionProbe =
        static_cast<wz::asset::AssetType>(2271);

    // Reserved lighting/environment asset type. Not implemented yet.
    // Represents a volume of irradiance probes or baked lighting samples.
    inline constexpr wz::asset::AssetType kAssetTypeIrradianceVolume =
        static_cast<wz::asset::AssetType>(2272);

    // Reserved lighting/environment asset type. Not implemented yet.
    // Represents environment lighting data, usually referencing HDR/cubemap textures.
    inline constexpr wz::asset::AssetType kAssetTypeEnvironmentMap =
        static_cast<wz::asset::AssetType>(2273);

    // Reserved lighting/environment asset type. Not implemented yet.
    // Represents skybox settings and texture references.
    inline constexpr wz::asset::AssetType kAssetTypeSkybox =
        static_cast<wz::asset::AssetType>(2274);

    // Reserved lighting/environment asset type. Not implemented yet.
    // Represents physically-inspired sky/atmosphere rendering settings.
    inline constexpr wz::asset::AssetType kAssetTypeSkyAtmosphereSettings =
        static_cast<wz::asset::AssetType>(2275);

    // Reserved lighting/environment asset type. Not implemented yet.
    // Represents fog volume settings, density fields, and visual behavior.
    inline constexpr wz::asset::AssetType kAssetTypeFogVolume =
        static_cast<wz::asset::AssetType>(2276);

    // Reserved lighting/environment asset type. Not implemented yet.
    // Represents volumetric cloud settings, density data, and render parameters.
    inline constexpr wz::asset::AssetType kAssetTypeVolumetricCloud =
        static_cast<wz::asset::AssetType>(2277);

    // Reserved lighting/render-cache asset type. Not implemented yet.
    // Represents cached shadow-map data or shadow-atlas metadata.
    inline constexpr wz::asset::AssetType kAssetTypeShadowMapCache =
        static_cast<wz::asset::AssetType>(2278);

    // Reserved baked-lighting asset type. Not implemented yet.
    // Represents baked lightmap metadata and texture references.
    inline constexpr wz::asset::AssetType kAssetTypeBakedLightmap =
        static_cast<wz::asset::AssetType>(2279);

    // Reserved baked-lighting asset type. Not implemented yet.
    // Represents baked irradiance data, possibly texture/probe/volume-backed.
    inline constexpr wz::asset::AssetType kAssetTypeBakedIrradiance =
        static_cast<wz::asset::AssetType>(2280);

    // Reserved baked-lighting asset type. Not implemented yet.
    // Represents secondary UV data or metadata used for lightmap baking.
    inline constexpr wz::asset::AssetType kAssetTypeLightmapUVData =
        static_cast<wz::asset::AssetType>(2281);

    // Reserved lighting/render-cache asset type. Not implemented yet.
    // Represents cached radiance data for GI, probes, or environment lighting.
    inline constexpr wz::asset::AssetType kAssetTypeRadianceCache =
        static_cast<wz::asset::AssetType>(2282);

    // Reserved post-process asset type. Not implemented yet.
    // Represents a collection of post-process settings.
    inline constexpr wz::asset::AssetType kAssetTypePostProcessProfile =
        static_cast<wz::asset::AssetType>(2283);

    // Reserved post-process asset type. Not implemented yet.
    // Represents color grading settings and LUT references.
    inline constexpr wz::asset::AssetType kAssetTypeColorGradingProfile =
        static_cast<wz::asset::AssetType>(2284);

    // Reserved post-process asset type. Not implemented yet.
    // Represents exposure/auto-exposure settings.
    inline constexpr wz::asset::AssetType kAssetTypeExposureProfile =
        static_cast<wz::asset::AssetType>(2285);

    // Reserved post-process asset type. Not implemented yet.
    // Represents tone-mapping settings.
    inline constexpr wz::asset::AssetType kAssetTypeTonemapProfile =
        static_cast<wz::asset::AssetType>(2286);

    // ─── Cinematic asset types: 2300–2319 ───────────────────────────────────────
    //
    // Camera rigs, camera paths, timelines, cutscenes, sequencer tracks, and
    // cinematic playback data.
    //
    // These are reserved only unless explicitly marked implemented:
    // no public registration API, schema, compiler, runtime table, cinematic
    // sequencer, camera system integration, or editor integration exists yet.
    //
    // Related existing reservations:
    //   kAssetTypeAnimationClip
    //   kAssetTypeAudioClip
    //   kAssetTypeSubtitleTrack
    //
    // AnimationSequence, AudioSequence, and SubtitleSequence should usually
    // reference existing animation/audio/subtitle assets rather than owning
    // their data directly.

    // Reserved cinematic asset type. Not implemented yet.
    // Represents a camera rig: camera offsets, constraints, targets, lenses,
    // shake layers, follow behavior, or authored camera setup.
    inline constexpr wz::asset::AssetType kAssetTypeCameraRig =
        static_cast<wz::asset::AssetType>(2300);

    // Reserved cinematic asset type. Not implemented yet.
    // Represents an authored camera path, spline, rail, or keyframed camera route.
    inline constexpr wz::asset::AssetType kAssetTypeCameraPath =
        static_cast<wz::asset::AssetType>(2301);

    // Reserved cinematic asset type. Not implemented yet.
    // Represents a generic time-based sequence of tracks.
    inline constexpr wz::asset::AssetType kAssetTypeTimeline =
        static_cast<wz::asset::AssetType>(2302);

    // Reserved cinematic asset type. Not implemented yet.
    // Represents a complete cutscene definition, usually referencing timelines,
    // cameras, animation, audio, subtitles, and scene objects.
    inline constexpr wz::asset::AssetType kAssetTypeCutscene =
        static_cast<wz::asset::AssetType>(2303);

    // Reserved cinematic asset type. Not implemented yet.
    // Represents one sequencer track inside a Timeline or Cutscene.
    inline constexpr wz::asset::AssetType kAssetTypeSequencerTrack =
        static_cast<wz::asset::AssetType>(2304);

    // Reserved cinematic asset type. Not implemented yet.
    // Represents animation clip references arranged in cinematic time.
    inline constexpr wz::asset::AssetType kAssetTypeAnimationSequence =
        static_cast<wz::asset::AssetType>(2305);

    // Reserved cinematic asset type. Not implemented yet.
    // Represents audio clip/cue references arranged in cinematic time.
    inline constexpr wz::asset::AssetType kAssetTypeAudioSequence =
        static_cast<wz::asset::AssetType>(2306);

    // Reserved cinematic asset type. Not implemented yet.
    // Represents subtitle track references arranged in cinematic time.
    inline constexpr wz::asset::AssetType kAssetTypeSubtitleSequence =
        static_cast<wz::asset::AssetType>(2307);

    // Reserved cinematic asset type. Not implemented yet.
    // Represents authored camera shake behavior or procedural shake settings.
    inline constexpr wz::asset::AssetType kAssetTypeCameraShake =
        static_cast<wz::asset::AssetType>(2308);

    // Reserved cinematic asset type. Not implemented yet.
    // Represents one shot: camera choice, framing, timing, transitions, and
    // references into a larger cutscene/timeline.
    inline constexpr wz::asset::AssetType kAssetTypeShotDefinition =
        static_cast<wz::asset::AssetType>(2309);

    // ─── Editor / tooling / build asset types: 4096–4119 ───────────────────────
    //
    // Import settings, editor-only metadata, preview/debug assets, build reports,
    // cooked outputs, and hot-reload records.
    //
    // These are reserved only unless explicitly marked implemented:
    // no public registration API, schema, compiler, runtime table, editor tooling,
    // build pipeline, import pipeline, or hot-reload system exists yet.
    //
    // These are generally not gameplay/runtime asset types. Some may exist only
    // in editor builds, asset-build tools, or offline cook steps.
    //
    // Related existing reservations:
    //   kAssetTypeMesh
    //   kAssetTypeScene
    //   kAssetTypeTexture
    //   kAssetTypeAssetBundle
    //   kAssetTypePackage
    //
    // PreviewMesh, PreviewScene, EditorIcon, and Thumbnail should usually
    // reference ordinary Mesh/Scene/Texture assets rather than owning duplicate
    // runtime data directly.

    // Reserved tooling asset type. Not implemented yet.
    // Represents an import recipe: source path, importer choice, options,
    // dependency policy, output targets, and cook instructions.
    inline constexpr wz::asset::AssetType kAssetTypeImportRecipe =
        static_cast<wz::asset::AssetType>(4096);

    // Reserved tooling asset type. Not implemented yet.
    // Represents importer-specific settings shared across many import recipes.
    inline constexpr wz::asset::AssetType kAssetTypeImporterSettings =
        static_cast<wz::asset::AssetType>(4097);

    // Reserved tooling asset type. Not implemented yet.
    // Represents editor/build metadata associated with an asset: labels, tags,
    // source info, authoring notes, GUIDs, thumbnails, or import history.
    inline constexpr wz::asset::AssetType kAssetTypeAssetMetadata =
        static_cast<wz::asset::AssetType>(4098);

    // Reserved tooling asset type. Not implemented yet.
    // Represents a generated preview thumbnail, usually referencing Texture data.
    inline constexpr wz::asset::AssetType kAssetTypeThumbnail =
        static_cast<wz::asset::AssetType>(4099);

    // Reserved tooling asset type. Not implemented yet.
    // Represents a mesh used only for preview/editor visualization.
    inline constexpr wz::asset::AssetType kAssetTypePreviewMesh =
        static_cast<wz::asset::AssetType>(4100);

    // Reserved tooling asset type. Not implemented yet.
    // Represents a scene used only for preview/editor visualization.
    inline constexpr wz::asset::AssetType kAssetTypePreviewScene =
        static_cast<wz::asset::AssetType>(4101);

    // Reserved tooling asset type. Not implemented yet.
    // Represents debug visualization configuration or generated debug-view data.
    inline constexpr wz::asset::AssetType kAssetTypeDebugVisualization =
        static_cast<wz::asset::AssetType>(4102);

    // Reserved tooling asset type. Not implemented yet.
    // Represents validation errors/warnings produced by import, compile, cook,
    // scene analysis, or build steps.
    inline constexpr wz::asset::AssetType kAssetTypeValidationReport =
        static_cast<wz::asset::AssetType>(4103);

    // Reserved tooling/build asset type. Not implemented yet.
    // Represents a cooked runtime-ready asset product.
    inline constexpr wz::asset::AssetType kAssetTypeCookedAsset =
        static_cast<wz::asset::AssetType>(4104);

    // Reserved tooling/build asset type. Not implemented yet.
    // Represents a build manifest: cooked files, package contents, versions,
    // dependency hashes, and output layout.
    inline constexpr wz::asset::AssetType kAssetTypeBuildManifest =
        static_cast<wz::asset::AssetType>(4105);

    // Reserved tooling/build asset type. Not implemented yet.
    // Represents a dependency report for inspection, debugging, or build tools.
    inline constexpr wz::asset::AssetType kAssetTypeAssetDependencyReport =
        static_cast<wz::asset::AssetType>(4106);

    // Reserved tooling asset type. Not implemented yet.
    // Represents hot-reload state, watched files, invalidation records, or
    // last-known reload results.
    inline constexpr wz::asset::AssetType kAssetTypeHotReloadRecord =
        static_cast<wz::asset::AssetType>(4107);

    // Reserved editor asset type. Not implemented yet.
    // Represents editor gizmo configuration or visualization data.
    inline constexpr wz::asset::AssetType kAssetTypeEditorGizmo =
        static_cast<wz::asset::AssetType>(4108);

    // Reserved editor asset type. Not implemented yet.
    // Represents editor-only icon metadata, usually referencing Texture data.
    inline constexpr wz::asset::AssetType kAssetTypeEditorIcon =
        static_cast<wz::asset::AssetType>(4109);

    // for creating diagnostic output consumable by AI agents
    // intended target is CSV files on disc
    inline constexpr wz::asset::AssetType kAssetTypeDiagnosticTable =
        static_cast<wz::asset::AssetType>(4110);


} // namespace wz::engine::assets
