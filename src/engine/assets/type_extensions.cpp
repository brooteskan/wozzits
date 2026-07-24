#include <engine/assets/type_extensions.h>

#include <engine/assets/schema_ids.h>

#include <cassert>
#include <cstdlib>
#include <string>

namespace wz::engine::assets
{
    namespace
    {
        void add_asset_type_name(
            AssetTypeDisplayNameMap& names,
            wz::asset::AssetType type,
            std::string_view display_name,
            const char* symbol)
        {
            const auto [_, inserted] = names.emplace(type, display_name);
            if (!inserted) {
                assert(false && "duplicate engine AssetType code");
                std::abort();
            }
            (void)symbol;
        }

        void set_asset_type_name(
            AssetTypeDisplayNameMap& names,
            wz::asset::AssetType type,
            std::string_view display_name)
        {
            const auto it = names.find(type);
            if (it == names.end()) {
                assert(false && "missing engine AssetType registry entry");
                std::abort();
            }
            it->second = display_name;
        }

        AssetTypeDisplayNameMap build_asset_type_display_names()
        {
            AssetTypeDisplayNameMap names;

#define WZ_ADD_TYPE(symbol) \
            add_asset_type_name(names, symbol, #symbol, #symbol)
#define WZ_SET_TYPE_NAME(symbol, label) \
            set_asset_type_name(names, symbol, label)

            WZ_ADD_TYPE(wz::asset::AssetType::Unknown);
            WZ_ADD_TYPE(wz::asset::AssetType::Mesh);
            WZ_ADD_TYPE(wz::asset::AssetType::Texture);
            WZ_ADD_TYPE(wz::asset::AssetType::Material);
            WZ_ADD_TYPE(wz::asset::AssetType::Shader);
            WZ_ADD_TYPE(wz::asset::AssetType::Audio);
            WZ_ADD_TYPE(wz::asset::AssetType::Font);
            WZ_ADD_TYPE(wz::asset::AssetType::ShaderSource);
            WZ_ADD_TYPE(wz::asset::AssetType::GaussianSplatCloud);
            WZ_ADD_TYPE(wz::asset::AssetType::Any);

            WZ_ADD_TYPE(kAssetTypeRawFile);
            WZ_ADD_TYPE(kAssetTypeTextFile);
            WZ_ADD_TYPE(kAssetTypeBinaryBlob);
            WZ_ADD_TYPE(kAssetTypeManifest);
            WZ_ADD_TYPE(kAssetTypeAssetBundle);
            WZ_ADD_TYPE(kAssetTypePackage);
            WZ_ADD_TYPE(kAssetTypeImportedSourceFile);
            WZ_ADD_TYPE(kAssetTypeExternalReference);
            WZ_ADD_TYPE(kAssetTypeDirectory);
            WZ_ADD_TYPE(kAssetTypeArchive);
            WZ_ADD_TYPE(kAssetTypeScalarField);
            WZ_ADD_TYPE(kAssetTypeTexture);
            WZ_ADD_TYPE(kAssetTypeMesh);
            WZ_ADD_TYPE(kAssetTypeGaussianSplatCloud);
            WZ_ADD_TYPE(kAssetTypeTerrain);
            WZ_ADD_TYPE(kAssetTypeCollisionAsset);
            WZ_ADD_TYPE(kAssetTypeTerrainVisualProxy);
            WZ_ADD_TYPE(kAssetTypeMeshDerivedField);
            WZ_ADD_TYPE(kAssetTypeMeshSparseOperator);
            WZ_ADD_TYPE(kAssetTypeGpuSparseMesh);
            WZ_ADD_TYPE(kAssetTypeMeshClusterHierarchy);
            WZ_ADD_TYPE(kAssetTypePlacement);
            WZ_ADD_TYPE(kAssetTypePlacedField);
            WZ_ADD_TYPE(kAssetTypeClipmapLatticeSchedule);
            WZ_ADD_TYPE(kAssetTypeAtmosphere);
            WZ_ADD_TYPE(kAssetTypeFrameEnvironment);
            WZ_ADD_TYPE(kAssetTypePointCloud);
            WZ_ADD_TYPE(kAssetTypeVoxelGrid);
            WZ_ADD_TYPE(kAssetTypeCurve);
            WZ_ADD_TYPE(kAssetTypeSpline);
            WZ_ADD_TYPE(kAssetTypeSignedDistanceField);
            WZ_ADD_TYPE(kAssetTypeHeightField);
            WZ_ADD_TYPE(kAssetTypeDistanceFieldVolume);
            WZ_ADD_TYPE(kAssetTypeNavigationMesh);
            WZ_ADD_TYPE(kAssetTypeNavigationGrid);
            WZ_ADD_TYPE(kAssetTypeCollisionMesh);
            WZ_ADD_TYPE(kAssetTypePhysicsShape);
            WZ_ADD_TYPE(kAssetTypeAnimationClip);
            WZ_ADD_TYPE(kAssetTypeSkeleton);
            WZ_ADD_TYPE(kAssetTypeShaderReflection);
            WZ_ADD_TYPE(kAssetTypeAudioClip);
            WZ_ADD_TYPE(kAssetTypeFontFace);
            WZ_ADD_TYPE(kAssetTypeLocalizationTable);
            WZ_ADD_TYPE(kAssetTypeTerrainLayer);
            WZ_ADD_TYPE(kAssetTypeCSVTable);
            WZ_ADD_TYPE(kAssetTypeJSONDocument);
            WZ_ADD_TYPE(kAssetTypeTOMLDocument);
            WZ_ADD_TYPE(kAssetTypeTextureAtlas);
            WZ_ADD_TYPE(kAssetTypeSpriteSheet);
            WZ_ADD_TYPE(kAssetTypeVirtualTexture);
            WZ_ADD_TYPE(kAssetTypeStreamingTexture);
            WZ_ADD_TYPE(kAssetTypeCompressedTexture);
            WZ_ADD_TYPE(kAssetTypeMipChain);
            WZ_ADD_TYPE(kAssetTypeProceduralTexture);
            WZ_ADD_TYPE(kAssetTypeMeshLODGroup);
            WZ_ADD_TYPE(kAssetTypeTerrainTile);
            WZ_ADD_TYPE(kAssetTypeTerrainMaterialSet);
            WZ_ADD_TYPE(kAssetTypeProceduralTerrainRecipe);
            WZ_ADD_TYPE(kAssetTypeTerrainChunk);
            WZ_ADD_TYPE(kAssetTypeTerrainLOD);
            WZ_ADD_TYPE(kAssetTypeVoxelTerrain);
            WZ_ADD_TYPE(kAssetTypeSignedDistanceTerrain);
            WZ_ADD_TYPE(kAssetTypeGPUShader);
            WZ_ADD_TYPE(kAssetTypeGPUPipeline);
            WZ_ADD_TYPE(kAssetTypeGPURootSignature);
            WZ_ADD_TYPE(kAssetTypeGPUBuffer);
            WZ_ADD_TYPE(kAssetTypeGPUVertexBuffer);
            WZ_ADD_TYPE(kAssetTypeGPUIndexBuffer);
            WZ_ADD_TYPE(kAssetTypeGPUTexture);
            WZ_ADD_TYPE(kAssetTypeGPUTextureView);
            WZ_ADD_TYPE(kAssetTypeGPUSampler);
            WZ_ADD_TYPE(kAssetTypeGPUMesh);
            WZ_ADD_TYPE(kAssetTypeGPUSplatCloud);
            WZ_ADD_TYPE(kAssetTypeGPUVoxelVolume);
            WZ_ADD_TYPE(kAssetTypeGPUAccelerationStructure);
            WZ_ADD_TYPE(kAssetTypeGPUComputePipeline);
            WZ_ADD_TYPE(kAssetTypeGPURenderTarget);
            WZ_ADD_TYPE(kAssetTypeGPUDepthStencil);
            WZ_ADD_TYPE(kAssetTypeGPUDescriptorSet);
            WZ_ADD_TYPE(kAssetTypeGPUDescriptorTable);
            WZ_ADD_TYPE(kAssetTypeGPUMaterialBinding);
            WZ_ADD_TYPE(kAssetTypeGPUPipelineStateObject);
            WZ_ADD_TYPE(kAssetTypeGPUGraphicsPipeline);
            WZ_ADD_TYPE(kAssetTypeGPURayTracingPipeline);
            WZ_ADD_TYPE(kAssetTypeGPUInstanceBuffer);
            WZ_ADD_TYPE(kAssetTypeGPUSplatRenderPipeline);
            WZ_ADD_TYPE(kAssetTypeGPUMeshFieldBuffer);
            WZ_ADD_TYPE(kAssetTypeShaderSource);
            WZ_ADD_TYPE(kAssetTypeShaderModule);
            WZ_ADD_TYPE(kAssetTypeShaderProgram);
            WZ_ADD_TYPE(kAssetTypeShaderPermutation);
            WZ_ADD_TYPE(kAssetTypeShaderVariant);
            WZ_ADD_TYPE(kAssetTypeShaderLibrary);
            WZ_ADD_TYPE(kAssetTypeRenderPassDefinition);
            WZ_ADD_TYPE(kAssetTypeBlendState);
            WZ_ADD_TYPE(kAssetTypeDepthStencilState);
            WZ_ADD_TYPE(kAssetTypeRasterizerState);
            WZ_ADD_TYPE(kAssetTypeInputLayout);
            WZ_ADD_TYPE(kAssetTypeVertexLayout);
            WZ_ADD_TYPE(kAssetTypeDescriptorLayout);
            WZ_ADD_TYPE(kAssetTypeBindingLayout);
            WZ_ADD_TYPE(kAssetTypeRenderable);
            WZ_ADD_TYPE(kAssetTypeRenderProgram);
            WZ_ADD_TYPE(kAssetTypeComputePipeline);
            WZ_ADD_TYPE(kAssetTypeRhiRenderableRecipe);
            WZ_ADD_TYPE(kAssetTypeRenderBindingLayout);
            WZ_ADD_TYPE(kAssetTypeMaterialDefinition);
            WZ_ADD_TYPE(kAssetTypeMaterialInstance);
            WZ_ADD_TYPE(kAssetTypeMaterialTemplate);
            WZ_ADD_TYPE(kAssetTypeMaterialGraph);
            WZ_ADD_TYPE(kAssetTypeMaterialFunction);
            WZ_ADD_TYPE(kAssetTypeMaterialParameterBlock);
            WZ_ADD_TYPE(kAssetTypeMaterialVariant);
            WZ_ADD_TYPE(kAssetTypeMaterialPermutation);
            WZ_ADD_TYPE(kAssetTypeShaderBindingSet);
            WZ_ADD_TYPE(kAssetTypeTextureBindingSet);
            WZ_ADD_TYPE(kAssetTypeSamplerBindingSet);
            WZ_ADD_TYPE(kAssetTypeMeshRenderStyle);
            WZ_ADD_TYPE(kAssetTypeWorldPartition);
            WZ_ADD_TYPE(kAssetTypeScene);
            WZ_ADD_TYPE(kAssetTypePrefab);
            WZ_ADD_TYPE(kAssetTypeEntityArchetype);
            WZ_ADD_TYPE(kAssetTypeLevel);
            WZ_ADD_TYPE(kAssetTypeWorld);
            WZ_ADD_TYPE(kAssetTypeSceneChunk);
            WZ_ADD_TYPE(kAssetTypeEntityTemplate);
            WZ_ADD_TYPE(kAssetTypeComponentBlob);
            WZ_ADD_TYPE(kAssetTypeTransformHierarchy);
            WZ_ADD_TYPE(kAssetTypeRenderSceneData);
            WZ_ADD_TYPE(kAssetTypeLightingSceneData);
            WZ_ADD_TYPE(kAssetTypePhysicsSceneData);
            WZ_ADD_TYPE(kAssetTypeNavigationSceneData);
            WZ_ADD_TYPE(kAssetTypeStreamingCell);
            WZ_ADD_TYPE(kAssetTypePortalZone);
            WZ_ADD_TYPE(kAssetTypeOcclusionZone);
            WZ_ADD_TYPE(kAssetTypeSpawnTable);
            WZ_ADD_TYPE(kAssetTypePlacementSet);
            WZ_ADD_TYPE(kAssetTypeBoneHierarchy);
            WZ_ADD_TYPE(kAssetTypeAnimationCurve);
            WZ_ADD_TYPE(kAssetTypeAnimationTrack);
            WZ_ADD_TYPE(kAssetTypeAnimationSet);
            WZ_ADD_TYPE(kAssetTypeBlendTree);
            WZ_ADD_TYPE(kAssetTypeAnimationStateMachine);
            WZ_ADD_TYPE(kAssetTypeAnimationController);
            WZ_ADD_TYPE(kAssetTypeRetargetingMap);
            WZ_ADD_TYPE(kAssetTypeIKRig);
            WZ_ADD_TYPE(kAssetTypeIKPose);
            WZ_ADD_TYPE(kAssetTypeMorphTargetAnimation);
            WZ_ADD_TYPE(kAssetTypeFacialAnimation);
            WZ_ADD_TYPE(kAssetTypeMotionMatchingDatabase);
            WZ_ADD_TYPE(kAssetTypeAnimationCompressionTable);
            WZ_ADD_TYPE(kAssetTypeCollider);
            WZ_ADD_TYPE(kAssetTypeBoxCollider);
            WZ_ADD_TYPE(kAssetTypeSphereCollider);
            WZ_ADD_TYPE(kAssetTypeCapsuleCollider);
            WZ_ADD_TYPE(kAssetTypeConvexHullCollider);
            WZ_ADD_TYPE(kAssetTypeTriangleMeshCollider);
            WZ_ADD_TYPE(kAssetTypeCompoundCollider);
            WZ_ADD_TYPE(kAssetTypeRigidBodyDefinition);
            WZ_ADD_TYPE(kAssetTypePhysicsMaterial);
            WZ_ADD_TYPE(kAssetTypeJointDefinition);
            WZ_ADD_TYPE(kAssetTypeConstraintDefinition);
            WZ_ADD_TYPE(kAssetTypeRagdollDefinition);
            WZ_ADD_TYPE(kAssetTypeClothSimulation);
            WZ_ADD_TYPE(kAssetTypeSoftBody);
            WZ_ADD_TYPE(kAssetTypePhysicsSceneSettings);
            WZ_ADD_TYPE(kAssetTypeSoundCue);
            WZ_ADD_TYPE(kAssetTypeAudioBank);
            WZ_ADD_TYPE(kAssetTypeAudioRenderable);
            WZ_ADD_TYPE(kAssetTypeFontAtlas);
            WZ_ADD_TYPE(kAssetTypeGlyphSet);
            WZ_ADD_TYPE(kAssetTypeRichTextStyle);
            WZ_ADD_TYPE(kAssetTypeNineSliceImage);
            WZ_ADD_TYPE(kAssetTypeUILayout);
            WZ_ADD_TYPE(kAssetTypeUIScreen);
            WZ_ADD_TYPE(kAssetTypeUITheme);
            WZ_ADD_TYPE(kAssetTypeIconSet);
            WZ_ADD_TYPE(kAssetTypeCursor);
            WZ_ADD_TYPE(kAssetTypeSubtitleTrack);
            WZ_ADD_TYPE(kAssetTypeDialogueText);
            WZ_ADD_TYPE(kAssetTypePuppet);
            WZ_ADD_TYPE(kAssetTypeItemDefinition);
            WZ_ADD_TYPE(kAssetTypeWeaponDefinition);
            WZ_ADD_TYPE(kAssetTypeAbilityDefinition);
            WZ_ADD_TYPE(kAssetTypeSpellDefinition);
            WZ_ADD_TYPE(kAssetTypeSkillTree);
            WZ_ADD_TYPE(kAssetTypeCharacterDefinition);
            WZ_ADD_TYPE(kAssetTypeEnemyDefinition);
            WZ_ADD_TYPE(kAssetTypeNPCDefinition);
            WZ_ADD_TYPE(kAssetTypeLootTable);
            WZ_ADD_TYPE(kAssetTypeQuestDefinition);
            WZ_ADD_TYPE(kAssetTypeDialogueGraph);
            WZ_ADD_TYPE(kAssetTypeFactionDefinition);
            WZ_ADD_TYPE(kAssetTypeEconomyTable);
            WZ_ADD_TYPE(kAssetTypeCraftingRecipe);
            WZ_ADD_TYPE(kAssetTypeInventoryDefinition);
            WZ_ADD_TYPE(kAssetTypeProgressionCurve);
            WZ_ADD_TYPE(kAssetTypeStatBlock);
            WZ_ADD_TYPE(kAssetTypeDamageTypeTable);
            WZ_ADD_TYPE(kAssetTypeStatusEffectDefinition);
            WZ_ADD_TYPE(kAssetTypeBehaviorTree);
            WZ_ADD_TYPE(kAssetTypeDecisionTree);
            WZ_ADD_TYPE(kAssetTypeUtilityAIConfig);
            WZ_ADD_TYPE(kAssetTypeGOAPDomain);
            WZ_ADD_TYPE(kAssetTypeBlackboardSchema);
            WZ_ADD_TYPE(kAssetTypeNavigationGraph);
            WZ_ADD_TYPE(kAssetTypeCoverPointSet);
            WZ_ADD_TYPE(kAssetTypePatrolRoute);
            WZ_ADD_TYPE(kAssetTypeTacticalMap);
            WZ_ADD_TYPE(kAssetTypeCrowdSimulationConfig);
            WZ_ADD_TYPE(kAssetTypeNPCBrainDefinition);
            WZ_ADD_TYPE(kAssetTypeParticleEmitter);
            WZ_ADD_TYPE(kAssetTypeParticleSystem);
            WZ_ADD_TYPE(kAssetTypeParticleGraph);
            WZ_ADD_TYPE(kAssetTypeVFXGraph);
            WZ_ADD_TYPE(kAssetTypeTrailRenderer);
            WZ_ADD_TYPE(kAssetTypeBeam);
            WZ_ADD_TYPE(kAssetTypeRibbon);
            WZ_ADD_TYPE(kAssetTypeDecal);
            WZ_ADD_TYPE(kAssetTypeVectorField);
            WZ_ADD_TYPE(kAssetTypeSimulationCache);
            WZ_ADD_TYPE(kAssetTypeDirectLight);
            WZ_ADD_TYPE(kAssetTypeAmbientLighting);
            WZ_ADD_TYPE(kAssetTypeLightProbe);
            WZ_ADD_TYPE(kAssetTypeReflectionProbe);
            WZ_ADD_TYPE(kAssetTypeIrradianceVolume);
            WZ_ADD_TYPE(kAssetTypeEnvironmentMap);
            WZ_ADD_TYPE(kAssetTypeSkybox);
            WZ_ADD_TYPE(kAssetTypeSkyAtmosphereSettings);
            WZ_ADD_TYPE(kAssetTypeFogVolume);
            WZ_ADD_TYPE(kAssetTypeVolumetricCloud);
            WZ_ADD_TYPE(kAssetTypeShadowMapCache);
            WZ_ADD_TYPE(kAssetTypeBakedLightmap);
            WZ_ADD_TYPE(kAssetTypeBakedIrradiance);
            WZ_ADD_TYPE(kAssetTypeLightmapUVData);
            WZ_ADD_TYPE(kAssetTypeRadianceCache);
            WZ_ADD_TYPE(kAssetTypePostProcessProfile);
            WZ_ADD_TYPE(kAssetTypeColorGradingProfile);
            WZ_ADD_TYPE(kAssetTypeExposureProfile);
            WZ_ADD_TYPE(kAssetTypeTonemapProfile);
            WZ_ADD_TYPE(kAssetTypeSkyGaussian);
            WZ_ADD_TYPE(kAssetTypeStarCatalog);
            WZ_ADD_TYPE(kAssetTypeCameraRig);
            WZ_ADD_TYPE(kAssetTypeCameraPath);
            WZ_ADD_TYPE(kAssetTypeTimeline);
            WZ_ADD_TYPE(kAssetTypeCutscene);
            WZ_ADD_TYPE(kAssetTypeSequencerTrack);
            WZ_ADD_TYPE(kAssetTypeAnimationSequence);
            WZ_ADD_TYPE(kAssetTypeAudioSequence);
            WZ_ADD_TYPE(kAssetTypeSubtitleSequence);
            WZ_ADD_TYPE(kAssetTypeCameraShake);
            WZ_ADD_TYPE(kAssetTypeShotDefinition);
            WZ_ADD_TYPE(kAssetTypeImportRecipe);
            WZ_ADD_TYPE(kAssetTypeImporterSettings);
            WZ_ADD_TYPE(kAssetTypeAssetMetadata);
            WZ_ADD_TYPE(kAssetTypeThumbnail);
            WZ_ADD_TYPE(kAssetTypePreviewMesh);
            WZ_ADD_TYPE(kAssetTypePreviewScene);
            WZ_ADD_TYPE(kAssetTypeDebugVisualization);
            WZ_ADD_TYPE(kAssetTypeValidationReport);
            WZ_ADD_TYPE(kAssetTypeCookedAsset);
            WZ_ADD_TYPE(kAssetTypeBuildManifest);
            WZ_ADD_TYPE(kAssetTypeAssetDependencyReport);
            WZ_ADD_TYPE(kAssetTypeHotReloadRecord);
            WZ_ADD_TYPE(kAssetTypeEditorGizmo);
            WZ_ADD_TYPE(kAssetTypeEditorIcon);
            WZ_ADD_TYPE(kAssetTypeDataTable);
            WZ_ADD_TYPE(kAssetTypeDiagnosticResampledTimeSeries);
            WZ_ADD_TYPE(kAssetTypeCSVExport);
            WZ_ADD_TYPE(kAssetTypeDiagnosticTimeframeSummary);
            WZ_ADD_TYPE(kAssetTypeGaussianSplatColorLOD);

            WZ_SET_TYPE_NAME(wz::asset::AssetType::Unknown, "Unknown");
            WZ_SET_TYPE_NAME(wz::asset::AssetType::Mesh, "Mesh");
            WZ_SET_TYPE_NAME(wz::asset::AssetType::Texture, "Texture");
            WZ_SET_TYPE_NAME(wz::asset::AssetType::Material, "Material");
            WZ_SET_TYPE_NAME(wz::asset::AssetType::Shader, "Shader");
            WZ_SET_TYPE_NAME(wz::asset::AssetType::Audio, "Audio");
            WZ_SET_TYPE_NAME(wz::asset::AssetType::Font, "Font");
            WZ_SET_TYPE_NAME(wz::asset::AssetType::ShaderSource, "Shader source");
            WZ_SET_TYPE_NAME(
                wz::asset::AssetType::GaussianSplatCloud,
                "Gaussian splat");
            // Port-type wildcard (issue #228): shown on Any-typed input ports
            // in the graph editor; never the type of a node.
            WZ_SET_TYPE_NAME(wz::asset::AssetType::Any, "Any");
            WZ_SET_TYPE_NAME(kAssetTypeRawFile, "Raw file");
            WZ_SET_TYPE_NAME(kAssetTypeTextFile, "Text file");
            WZ_SET_TYPE_NAME(kAssetTypeBinaryBlob, "Binary blob");
            WZ_SET_TYPE_NAME(kAssetTypeImportedSourceFile, "Imported source");
            WZ_SET_TYPE_NAME(kAssetTypeScalarField, "Scalar field");
            WZ_SET_TYPE_NAME(kAssetTypeVectorField, "Vector field");
            WZ_SET_TYPE_NAME(kAssetTypeMesh, "Mesh");
            WZ_SET_TYPE_NAME(kAssetTypeTerrain, "Terrain");
            WZ_SET_TYPE_NAME(kAssetTypeCollisionAsset, "Collision");
            WZ_SET_TYPE_NAME(
                kAssetTypeTerrainVisualProxy,
                "Terrain visual proxy");
            WZ_SET_TYPE_NAME(
                kAssetTypeMeshDerivedField,
                "Mesh derived field");
            WZ_SET_TYPE_NAME(
                kAssetTypeMeshSparseOperator,
                "Mesh sparse operator");
            WZ_SET_TYPE_NAME(
                kAssetTypeMeshClusterHierarchy,
                "Mesh cluster hierarchy");
            WZ_SET_TYPE_NAME(kAssetTypePlacement, "Placement");
            WZ_SET_TYPE_NAME(kAssetTypePlacedField, "Placed field");
            WZ_SET_TYPE_NAME(
                kAssetTypeClipmapLatticeSchedule,
                "Clipmap lattice schedule");
            WZ_SET_TYPE_NAME(kAssetTypeAtmosphere, "Atmosphere");
            WZ_SET_TYPE_NAME(kAssetTypeFrameEnvironment, "Frame environment");
            WZ_SET_TYPE_NAME(kAssetTypeAudioClip, "Audio clip");
            WZ_SET_TYPE_NAME(kAssetTypeAudioBank, "Audio clip bank");
            WZ_SET_TYPE_NAME(kAssetTypeAudioRenderable, "Audio renderable");
            WZ_SET_TYPE_NAME(kAssetTypeCSVTable, "CSV table");
            WZ_SET_TYPE_NAME(kAssetTypeJSONDocument, "JSON");
            WZ_SET_TYPE_NAME(kAssetTypeTOMLDocument, "TOML");
            WZ_SET_TYPE_NAME(
                kAssetTypeGPUMeshFieldBuffer,
                "GPU mesh field buffer");
            WZ_SET_TYPE_NAME(kAssetTypeRenderable, "Renderable");
            WZ_SET_TYPE_NAME(kAssetTypeRenderProgram, "Render program");
            WZ_SET_TYPE_NAME(kAssetTypePuppet, "Puppet");
            WZ_SET_TYPE_NAME(kAssetTypeComputePipeline, "Compute pipeline");
            WZ_SET_TYPE_NAME(
                kAssetTypeRhiRenderableRecipe,
                "RHI renderable recipe");
            WZ_SET_TYPE_NAME(
                kAssetTypeRenderBindingLayout,
                "Render binding layout");
            WZ_SET_TYPE_NAME(kAssetTypeMeshRenderStyle, "Mesh render style");
            WZ_SET_TYPE_NAME(kAssetTypeScene, "Scene");
            WZ_SET_TYPE_NAME(kAssetTypeDirectLight, "Direct light");
            WZ_SET_TYPE_NAME(kAssetTypeAmbientLighting, "Ambient lighting");
            WZ_SET_TYPE_NAME(kAssetTypeEnvironmentMap, "Environment");
            WZ_SET_TYPE_NAME(kAssetTypeSkyGaussian, "Sky gaussian");
            WZ_SET_TYPE_NAME(kAssetTypeStarCatalog, "Star catalog");
            WZ_SET_TYPE_NAME(kAssetTypeDataTable, "Data table");
            WZ_SET_TYPE_NAME(
                kAssetTypeDiagnosticResampledTimeSeries,
                "Diagnostic resampled time series");
            WZ_SET_TYPE_NAME(kAssetTypeCSVExport, "CSV export");
            WZ_SET_TYPE_NAME(
                kAssetTypeDiagnosticTimeframeSummary,
                "Diagnostic timeframe summary");
            WZ_SET_TYPE_NAME(
                kAssetTypeGaussianSplatColorLOD,
                "Splat color LOD");

#undef WZ_SET_TYPE_NAME
#undef WZ_ADD_TYPE

            return names;
        }

        void add_schema_name(
            SchemaDisplayNameMap& names,
            wz::asset::SchemaID schema,
            std::string_view display_name,
            const char* symbol)
        {
            const auto [_, inserted] = names.emplace(
                schema.value,
                display_name);
            if (!inserted) {
                assert(false && "duplicate engine SchemaID code");
                std::abort();
            }
            (void)symbol;
        }

        SchemaDisplayNameMap build_schema_display_names()
        {
            SchemaDisplayNameMap names;

#define WZ_ADD_SCHEMA(symbol, label) \
            add_schema_name(names, symbol, label, #symbol)

            WZ_ADD_SCHEMA(kRawFileSchema, "Raw file");
            WZ_ADD_SCHEMA(kHLSLFileSchema, "HLSL source file");
            WZ_ADD_SCHEMA(kHLSLShaderSchema, "HLSL shader");
            WZ_ADD_SCHEMA(kBuiltinRenderProgramSchema, "Builtin render program");
            WZ_ADD_SCHEMA(kComputePipelineSchema, "Compute pipeline");
            WZ_ADD_SCHEMA(kCustomRenderProgramSchema, "Custom render program");
            WZ_ADD_SCHEMA(kRenderBindingLayoutSchema, "Render binding layout");
            WZ_ADD_SCHEMA(kScalarFieldFromRawF32Schema, "Scalar field from raw F32");
            WZ_ADD_SCHEMA(kScalarFieldProceduralSchema, "Procedural scalar field");
            WZ_ADD_SCHEMA(kScalarFieldTerrainSchema, "Procedural terrain field");
            WZ_ADD_SCHEMA(kScalarFieldFromGaeaR32Schema, "Scalar field from Gaea R32");
            WZ_ADD_SCHEMA(kVectorFieldFromRawF32Schema, "Vector field from raw F32");
            WZ_ADD_SCHEMA(kGaussianSplatFromPLYSchema, "Gaussian splat from PLY");
            WZ_ADD_SCHEMA(kGaussianSplatFromFieldSchema, "Gaussian splat from field");
            WZ_ADD_SCHEMA(kPuppetFromFileSchema, "Puppet from file");
            WZ_ADD_SCHEMA(kPuppetProgramSchema, "Puppet program");
            WZ_ADD_SCHEMA(kPuppetRhiRenderableSchema, "Puppet renderable");
            WZ_ADD_SCHEMA(kGaussianSplatColorLODSchema, "Gaussian splat color LOD");
            WZ_ADD_SCHEMA(
                kGaussianSplatTerrainSurfaceFromHeightFieldSchema,
                "Terrain splat surface from height field");
            WZ_ADD_SCHEMA(kTerrainSplatFromGaeaR32Schema, "Terrain splat from Gaea R32");
            WZ_ADD_SCHEMA(kMeshDecimationSchema, "Mesh decimation");
            WZ_ADD_SCHEMA(kMeshDerivedFieldExplicitSchema, "Explicit mesh field");
            WZ_ADD_SCHEMA(kMeshWaveletAnalysisSchema, "Mesh wavelet analysis");
            WZ_ADD_SCHEMA(kBehaviorFieldPlaceholderSchema, "Behavior field placeholder");
            WZ_ADD_SCHEMA(kMeshComputeDerivedFieldSchema, "Mesh compute derived field");
            WZ_ADD_SCHEMA(kBuiltinMeshDerivedFieldSchema, "Builtin mesh derived field");
            WZ_ADD_SCHEMA(kMeshSparseApplyFieldSchema, "Mesh sparse apply field");
            WZ_ADD_SCHEMA(kMeshSparseDiffusionBandsSchema, "Mesh sparse diffusion bands");
            WZ_ADD_SCHEMA(kMeshFieldLevelMaskSchema, "Mesh field level mask");
            WZ_ADD_SCHEMA(kMeshClusterHierarchySchema, "Mesh cluster hierarchy");
            WZ_ADD_SCHEMA(
                kMeshClusterHierarchyPreviewMeshSchema,
                "Mesh cluster hierarchy preview mesh");
            WZ_ADD_SCHEMA(kDebugTriangleStrideMeshSchema, "Debug triangle-stride mesh");
            WZ_ADD_SCHEMA(kMeshSparseOperatorSchema, "Mesh sparse operator");
            WZ_ADD_SCHEMA(kGpuSparseMeshFromMeshSchema, "GPU sparse mesh from mesh");
            WZ_ADD_SCHEMA(kTerrainFromHeightFieldSchema, "Terrain from height field");
            WZ_ADD_SCHEMA(kTerrainFromMeshSchema, "Terrain from mesh");
            WZ_ADD_SCHEMA(kTerrainVisualProxySchema, "Terrain visual proxy");
            WZ_ADD_SCHEMA(kPlacementSchema, "Placement");
            WZ_ADD_SCHEMA(kPlacedFieldSchema, "Placed field");
            WZ_ADD_SCHEMA(
                kClipmapLatticeScheduleSchema, "Clipmap lattice schedule");
            WZ_ADD_SCHEMA(kAtmosphereSchema, "Atmosphere");
            WZ_ADD_SCHEMA(kFrameEnvironmentSchema, "Frame environment");
            WZ_ADD_SCHEMA(kAudioClipFromWavSchema, "Audio clip from WAV");
            WZ_ADD_SCHEMA(kAudioClipProceduralToneSchema, "Procedural tone");
            WZ_ADD_SCHEMA(kAudioRenderableSchema, "Audio renderable");
            WZ_ADD_SCHEMA(
                kAudioClipBankFromClipsSchema, "Audio clip bank from clips");
            WZ_ADD_SCHEMA(
                kAudioClipBankRenderableSchema, "Audio clip bank renderable");
            WZ_ADD_SCHEMA(
                kAudioClipBankFromDirectorySchema,
                "Audio clip bank from directory");
            WZ_ADD_SCHEMA(
                kAudioGrainCloudRenderableSchema, "Grain cloud renderable");
            WZ_ADD_SCHEMA(kCollisionFromMeshSchema, "Collision from mesh");
            WZ_ADD_SCHEMA(kCollisionFromTerrainSchema, "Collision from terrain");
            WZ_ADD_SCHEMA(
                kCollisionFromHeightFieldSchema,
                "Collision from height field");
            WZ_ADD_SCHEMA(kCSVTableSchema, "CSV table");
            WZ_ADD_SCHEMA(kTextFileSchema, "Text file");
            WZ_ADD_SCHEMA(kBinaryBlobSchema, "Binary blob");
            WZ_ADD_SCHEMA(kManifestSchema, "Manifest");
            WZ_ADD_SCHEMA(kAssetBundleSchema, "Asset bundle");
            WZ_ADD_SCHEMA(kPackageSchema, "Package");
            WZ_ADD_SCHEMA(kImportedSourceFileSchema, "Imported source file");
            WZ_ADD_SCHEMA(kExternalReferenceSchema, "External reference");
            WZ_ADD_SCHEMA(kJSONFileSchema, "JSON file");
            WZ_ADD_SCHEMA(kYAMLFileSchema, "YAML file");
            WZ_ADD_SCHEMA(kTOMLFileSchema, "TOML file");
            WZ_ADD_SCHEMA(kXMLFileSchema, "XML file");
            WZ_ADD_SCHEMA(kCSVFileSchema, "CSV file");
            WZ_ADD_SCHEMA(kCustomBinaryFileSchema, "Custom binary file");
            WZ_ADD_SCHEMA(kDirectoryAssetSchema, "Directory");
            WZ_ADD_SCHEMA(kArchiveAssetSchema, "Archive");
            WZ_ADD_SCHEMA(kJSONDocumentSchema, "JSON document");
            WZ_ADD_SCHEMA(kTOMLDocumentSchema, "TOML document");
            WZ_ADD_SCHEMA(kProceduralTriangleMeshSchema, "Procedural triangle mesh");
            WZ_ADD_SCHEMA(kProceduralQuadMeshSchema, "Procedural quad mesh");
            WZ_ADD_SCHEMA(kProceduralCubeMeshSchema, "Procedural cube mesh");
            WZ_ADD_SCHEMA(kProceduralSphereMeshSchema, "Procedural sphere mesh");
            WZ_ADD_SCHEMA(
                kProceduralClipmapLatticeMeshSchema,
                "Procedural clipmap mesh");
            WZ_ADD_SCHEMA(kGLBMeshSchema, "GLB mesh");
            WZ_ADD_SCHEMA(kMeshFromGLBSceneSchema, "Mesh from GLB scene");
            WZ_ADD_SCHEMA(kPlaceholderMeshSchema, "Placeholder mesh");
            WZ_ADD_SCHEMA(
                kProceduralGaussianSplatCloudSchema,
                "Procedural gaussian splat cloud");
            WZ_ADD_SCHEMA(kInlineDataTableSchema, "Inline data table");
            WZ_ADD_SCHEMA(
                kDiagnosticTableResampleTimeSeriesSchema,
                "Diagnostic table resample time series");
            WZ_ADD_SCHEMA(kCSVExportSchema, "CSV export");
            WZ_ADD_SCHEMA(
                kDiagnosticResampledTimeSeriesToDataTableSchema,
                "Diagnostic resampled time series to data table");
            WZ_ADD_SCHEMA(
                kDiagnosticTimeframeSummarySchema,
                "Diagnostic timeframe summary");
            WZ_ADD_SCHEMA(
                kDiagnosticTimeframeSummaryToDataTableSchema,
                "Diagnostic timeframe summary to data table");
            WZ_ADD_SCHEMA(kMeshRenderStyleSchema, "Mesh render style");
            WZ_ADD_SCHEMA(kRhiPullMeshRenderableSchema, "RHI pull mesh renderable");
            WZ_ADD_SCHEMA(
                kGpuSparseMeshRenderableSchema,
                "GPU sparse mesh renderable");
            // 0x708 (clipmap landscape renderable) retired by #234 -- the
            // clipmap is now a 0x70A custom renderable (below).
            // 0x702 stays alive after the #195 legacy-schema deletion: its
            // RenderableAssetData feeds the resolver's preview-mesh branch —
            // the editor's scalar-field visualization — not the (deleted)
            // legacy field-texture path.
            WZ_ADD_SCHEMA(
                kScalarFieldDebugRenderableSchema,
                "Scalar field preview renderable");
            WZ_ADD_SCHEMA(kTerrainDebugRenderableSchema, "Terrain debug renderable");
            WZ_ADD_SCHEMA(kTerrainSurfaceRenderableSchema, "Terrain surface renderable");
            WZ_ADD_SCHEMA(kSceneFromJSONSchema, "Scene from JSON");
            WZ_ADD_SCHEMA(kSceneFromGLBSchema, "Scene from GLB");
            WZ_ADD_SCHEMA(kDirectLightSchema, "Direct light");
            WZ_ADD_SCHEMA(kAmbientLightingSchema, "Ambient lighting");
            WZ_ADD_SCHEMA(kHDRIEnvironmentSchema, "HDRI environment");
            WZ_ADD_SCHEMA(kSkyGaussianFromJSONSchema, "Sky gaussian from JSON");
            WZ_ADD_SCHEMA(kStarCatalogFromJSONSchema, "Star catalog from JSON");
            WZ_ADD_SCHEMA(kStarCatalogFromPLYSchema, "Star catalog from PLY");

#undef WZ_ADD_SCHEMA

            return names;
        }
    }

    const AssetTypeDisplayNameMap& asset_type_display_names()
    {
        static const AssetTypeDisplayNameMap names =
            build_asset_type_display_names();
        return names;
    }

    void validate_asset_type_display_names()
    {
        (void)asset_type_display_names();
    }

    std::string_view asset_type_display_name_view(
        wz::asset::AssetType type) noexcept
    {
        const auto& names = asset_type_display_names();
        const auto it = names.find(type);
        return it == names.end() ? std::string_view{ "Asset" } : it->second;
    }

    const char* asset_type_display_name(
        wz::asset::AssetType type) noexcept
    {
        return asset_type_display_name_view(type).data();
    }

    const SchemaDisplayNameMap& schema_display_names()
    {
        static const SchemaDisplayNameMap names =
            build_schema_display_names();
        return names;
    }

    std::string_view schema_display_name_view(
        wz::asset::SchemaID schema) noexcept
    {
        const auto& names = schema_display_names();
        const auto it = names.find(schema.value);
        return it == names.end() ? std::string_view{ "Schema" } : it->second;
    }

    const char* schema_display_name(wz::asset::SchemaID schema) noexcept
    {
        return schema_display_name_view(schema).data();
    }
} // namespace wz::engine::assets
