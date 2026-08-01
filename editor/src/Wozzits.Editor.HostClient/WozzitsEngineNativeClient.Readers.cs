using System.Runtime.InteropServices;
using System.Text;
using Wozzits.Editor.Protocol;

namespace Wozzits.Editor.HostClient;

public sealed partial class WozzitsEngineNativeClient
{
    private static EngineProjectSnapshotResponse ReadProjectSnapshot(WzBuffer buffer)
    {
        var bytes = ReadBufferBytes(buffer, "Engine ABI returned an empty project snapshot buffer.");
        var snapshot = ReadStruct<WzEditorProjectSnapshotAbi>(bytes, offset: 0);
        ValidateAbiVersion(snapshot.AbiVersion);

        return new EngineProjectSnapshotResponse
        {
            Ok = snapshot.Ok != 0,
            Status = ToProjectStatus(snapshot.Status),
            Error = ReadString(bytes, snapshot.Error),
            ProjectName = ReadString(bytes, snapshot.ProjectName),
            AssetGraph = ReadAssetGraphSnapshot(bytes, snapshot.AssetGraph),
            Scene = ReadSceneSnapshot(bytes, snapshot.Scene),
        };
    }

    private static EngineProjectResponse ReadProjectCreate(WzBuffer buffer)
    {
        var bytes = ReadBufferBytes(buffer, "Engine ABI returned an empty project response buffer.");
        var project = ReadStruct<WzEditorProjectCreateAbi>(bytes, offset: 0);
        ValidateAbiVersion(project.AbiVersion);

        return new EngineProjectResponse
        {
            Ok = project.Ok != 0,
            Status = ToProjectStatus(project.Status),
            Created = project.Created != 0,
            Error = ReadString(bytes, project.Error),
        };
    }

    private static EngineAssetCatalogResponse ReadAssetCatalog(WzBuffer buffer)
    {
        var bytes = ReadBufferBytes(buffer, "Engine ABI returned an empty asset catalog buffer.");
        var catalog = ReadStruct<WzEditorAssetCatalogAbi>(bytes, offset: 0);
        ValidateAbiVersion(catalog.AbiVersion);

        return new EngineAssetCatalogResponse
        {
            Ok = catalog.Ok != 0,
            Entries = ReadTable<WzEditorAssetCatalogEntryAbi, EngineAssetCatalogEntry>(
                bytes,
                catalog.Entries,
                ReadAssetCatalogEntry),
        };
    }

    private static EngineAssetCatalogEntry ReadAssetCatalogEntry(
        byte[] bytes,
        WzEditorAssetCatalogEntryAbi entry)
    {
        return new EngineAssetCatalogEntry
        {
            Type = CheckedInt(entry.Type, "an asset catalog entry type"),
            TypeName = ReadString(bytes, entry.TypeName),
            Category = ReadString(bytes, entry.Category),
            Schemas = ReadTable<WzEditorAssetCatalogSchemaAbi, EngineAssetCatalogSchema>(
                bytes,
                entry.Schemas,
                (b, schema) => new EngineAssetCatalogSchema
                {
                    Schema = schema.Schema,
                    Label = ReadString(b, schema.Label),
                }),
        };
    }

    private static EngineActuatorCatalogResponse ReadActuatorCatalog(WzBuffer buffer)
    {
        var bytes = ReadBufferBytes(buffer, "Engine ABI returned an empty actuator catalog buffer.");
        var catalog = ReadStruct<WzEditorBehaviorActuatorCatalogAbi>(bytes, offset: 0);
        ValidateAbiVersion(catalog.AbiVersion);

        return new EngineActuatorCatalogResponse
        {
            Ok = catalog.Ok != 0,
            Actuators = ReadTable<WzEditorBehaviorActuatorAbi, EngineActuator>(
                bytes,
                catalog.Actuators,
                ReadActuator),
        };
    }

    private static EngineActuator ReadActuator(
        byte[] bytes,
        WzEditorBehaviorActuatorAbi actuator)
    {
        return new EngineActuator
        {
            Name = ReadString(bytes, actuator.Name),
            Label = ReadString(bytes, actuator.Label),
            Params = ReadTable<WzEditorBehaviorActuatorParamAbi, EngineActuatorParam>(
                bytes,
                actuator.Params,
                (b, param) => new EngineActuatorParam
                {
                    Name = ReadString(b, param.Name),
                    Kind = CheckedInt(param.Kind, "an actuator parameter kind"),
                    DefaultValue = param.DefaultValue,
                }),
        };
    }

    private static EngineBehaviorModuleCatalogResponse ReadBehaviorModuleCatalog(
        WzBuffer buffer)
    {
        var bytes = ReadBufferBytes(
            buffer, "Engine ABI returned an empty behavior module catalog buffer.");
        var catalog = ReadStruct<WzEditorBehaviorModuleCatalogAbi>(bytes, offset: 0);
        ValidateAbiVersion(catalog.AbiVersion);

        return new EngineBehaviorModuleCatalogResponse
        {
            Ok = catalog.Ok != 0,
            Modules = ReadTable<WzEditorBehaviorModuleAbi, EngineBehaviorModule>(
                bytes,
                catalog.Modules,
                ReadBehaviorModule),
        };
    }

    private static EngineBehaviorModule ReadBehaviorModule(
        byte[] bytes,
        WzEditorBehaviorModuleAbi module)
    {
        return new EngineBehaviorModule
        {
            Module = ReadString(bytes, module.Module),
            Params = ReadTable<WzEditorBehaviorModuleParamAbi, EngineBehaviorModuleParam>(
                bytes,
                module.Params,
                (b, param) => new EngineBehaviorModuleParam
                {
                    Key = ReadString(b, param.Key),
                    Label = ReadString(b, param.Label),
                    Type = CheckedInt(param.Type, "a behavior parameter type"),
                    DefaultNumber = param.DefaultNumber,
                    DefaultString = ReadString(b, param.DefaultString),
                }),
        };
    }

    private static EngineGlbSceneHierarchy ReadGlbSceneHierarchy(WzBuffer buffer)
    {
        var bytes = ReadBufferBytes(buffer, "Engine ABI returned an empty GLB scene hierarchy buffer.");
        var hierarchy = ReadStruct<WzEditorGlbSceneHierarchyAbi>(bytes, offset: 0);
        ValidateAbiVersion(hierarchy.AbiVersion);

        return new EngineGlbSceneHierarchy
        {
            Ok = hierarchy.Ok != 0,
            Error = ReadString(bytes, hierarchy.Error),
            SceneName = ReadString(bytes, hierarchy.SceneName),
            SceneIndex = hierarchy.SceneIndex,
            Components = ReadTable<WzEditorGlbComponentAbi, EngineGlbComponent>(
                bytes,
                hierarchy.Components,
                ReadGlbComponent),
        };
    }

    private static EngineGlbComponent ReadGlbComponent(
        byte[] bytes,
        WzEditorGlbComponentAbi component)
    {
        return new EngineGlbComponent
        {
            Id = ReadString(bytes, component.Id),
            Name = ReadString(bytes, component.Name),
            ParentId = HasFlag(component.Flags, WzEditorGlbComponentFlags.HasParent)
                ? ReadString(bytes, component.ParentId)
                : null,
            HasMesh = HasFlag(component.Flags, WzEditorGlbComponentFlags.HasMesh),
            MeshIndex = component.MeshIndex,
            NodeIndex = component.NodeIndex,
        };
    }

    private static EngineAssetGraphSnapshotResponse ReadAssetGraphSnapshot(WzBuffer buffer)
    {
        var bytes = ReadBufferBytes(buffer, "Engine ABI returned an empty asset graph snapshot buffer.");
        var snapshot = ReadStruct<WzEditorAssetGraphSnapshotAbi>(bytes, offset: 0);
        ValidateAbiVersion(snapshot.AbiVersion);
        return ReadAssetGraphSnapshot(bytes, snapshot);
    }

    private static EngineAssetGraphConnectionCheckResponse ReadConnectionCheck(
        WzBuffer buffer)
    {
        var bytes = ReadBufferBytes(buffer, "Engine ABI returned an empty asset graph connection check buffer.");
        var check = ReadStruct<WzEditorAssetGraphConnectionCheckAbi>(bytes, offset: 0);
        ValidateAbiVersion(check.AbiVersion);

        return new EngineAssetGraphConnectionCheckResponse
        {
            Ok = true,
            Check = new EngineAssetGraphConnectionCheck
            {
                Compatible = check.Compatible != 0,
                Status = ToConnectionStatus(check.Status),
                ReplacesExisting = check.ReplacesExisting != 0,
                From = check.From,
                To = check.To,
                ToInputPort = check.ToInputPort,
                FromType = check.FromType,
                ToType = check.ToType,
                Message = ReadString(bytes, check.Message),
                FromTypeName = ReadString(bytes, check.FromTypeName),
                ToTypeName = ReadString(bytes, check.ToTypeName),
            },
        };
    }

    private static EngineAssetGraphSnapshotResponse ReadAssetGraphSnapshot(
        byte[] bytes,
        WzEditorAssetGraphSnapshotAbi snapshot)
    {
        var nodes = ReadTable<WzEditorAssetGraphNodeAbi, EngineAssetGraphNode>(
            bytes,
            snapshot.Nodes,
            ReadAssetGraphNode);

        // Node ids must be distinct, and nothing upstream proved it (D3-P047).
        // AssetGraphDraft allocates them sequentially so an honest snapshot cannot
        // collide -- which is exactly why a collision means the decode is wrong, and
        // the likely cause is stride skew from a field appended to
        // WzEditorAssetGraphNodeAbi without a version bump, the drift this project
        // has hit before. ValidateAbiVersion cannot catch that (the version did not
        // change) and CheckedBufferOffset cannot either: a wrong stride stays in
        // bounds and decodes to plausible garbage.
        //
        // Refusing HERE rather than letting the consumer throw is what keeps the
        // documented policy intact. AssetGraphEditorPaneViewModel.LoadSnapshot does
        // `Nodes.ToDictionary(node => node.Id)`, whose throwing overload raises
        // ArgumentException on the first collision -- and it runs from the
        // MainWindowViewModel constructor, so the throw escapes
        // OnFrameworkInitializationCompleted and the editor does not start at all.
        // That directly defeats App.axaml.cs's deliberate choice to open a project
        // with a broken asset graph anyway, "refusing would leave no way to open a
        // project in order to repair it". Ok=false lands on that same warning path.
        var seen = new HashSet<ulong>();
        foreach (var node in nodes)
        {
            if (!seen.Add(node.Id))
            {
                return new EngineAssetGraphSnapshotResponse
                {
                    Ok = false,
                    Error =
                        $"Engine ABI returned asset graph node id {node.Id} twice; "
                        + "the node table did not decode correctly.",
                };
            }
        }

        return new EngineAssetGraphSnapshotResponse
        {
            Ok = snapshot.Ok != 0,
            Error = ReadString(bytes, snapshot.Error),
            Snapshot = new EngineAssetGraphSnapshot
            {
                Schema = ReadString(bytes, snapshot.Schema),
                Zoom = snapshot.Zoom,
                Nodes = nodes,
                Edges = ReadTable<WzEditorAssetGraphEdgeAbi, EngineAssetGraphEdge>(
                    bytes,
                    snapshot.Edges,
                    (_, edge) => new EngineAssetGraphEdge
                    {
                        Id = edge.Id,
                        From = edge.From,
                        To = edge.To,
                        ToInputPort = edge.ToInputPort,
                    }),
            },
        };
    }

    private static EngineAssetGraphNode ReadAssetGraphNode(
        byte[] bytes,
        WzEditorAssetGraphNodeAbi node)
    {
        return new EngineAssetGraphNode
        {
            Id = node.Id,
            Type = CheckedInt(node.Type, "an asset graph node type"),
            TypeName = ReadString(bytes, node.TypeName),
            Schema = ReadString(bytes, node.Schema),
            DisplayName = ReadString(bytes, node.DisplayName),
            CompileStatus = ReadString(bytes, node.CompileStatus),
            X = node.X,
            Y = node.Y,
            InputPorts = ReadTable<WzEditorAssetGraphPortAbi, EngineAssetGraphPort>(
                bytes,
                node.InputPorts,
                ReadAssetGraphPort),
            OutputPorts = ReadTable<WzEditorAssetGraphPortAbi, EngineAssetGraphPort>(
                bytes,
                node.OutputPorts,
                ReadAssetGraphPort),
            Diagnostics = ReadTable<WzEditorAssetGraphDiagnosticAbi, EngineAssetGraphDiagnostic>(
                bytes,
                node.Diagnostics,
                ReadAssetGraphDiagnostic),
            Params = ReadTable<WzEditorAssetGraphParamAbi, EngineAssetGraphParam>(
                bytes,
                node.Params,
                ReadAssetGraphParam),
        };
    }

    private static EngineAssetGraphParam ReadAssetGraphParam(
        byte[] bytes,
        WzEditorAssetGraphParamAbi param)
    {
        return new EngineAssetGraphParam
        {
            Name = ReadString(bytes, param.Name),
            Kind = ReadString(bytes, param.Kind),
            Value = ReadString(bytes, param.Value),
            Options = ReadTable<WzEditorStringSpanAbi, string>(
                bytes,
                param.Options,
                static (b, span) => ReadString(b, span)),
        };
    }

    private static EngineAssetGraphPort ReadAssetGraphPort(
        byte[] bytes,
        WzEditorAssetGraphPortAbi port)
    {
        return new EngineAssetGraphPort
        {
            Index = port.Index,
            Type = port.Type,
            Flags = (EngineAssetGraphPortFlags)port.Flags,
            Name = ReadString(bytes, port.Name),
            Label = ReadString(bytes, port.Label),
            TypeName = ReadString(bytes, port.TypeName),
        };
    }

    private static EngineAssetGraphDiagnostic ReadAssetGraphDiagnostic(
        byte[] bytes,
        WzEditorAssetGraphDiagnosticAbi diagnostic)
    {
        return new EngineAssetGraphDiagnostic
        {
            Severity = diagnostic.Severity,
            Code = diagnostic.Code,
            SeverityName = ReadString(bytes, diagnostic.SeverityName),
            CodeName = ReadString(bytes, diagnostic.CodeName),
            Node = diagnostic.Node,
            Edge = diagnostic.Edge,
            InputPort = diagnostic.InputPort,
            Message = ReadString(bytes, diagnostic.Message),
        };
    }

    private static EngineSceneSnapshotResponse ReadSceneSnapshot(
        byte[] bytes,
        WzEditorSceneSnapshotAbi snapshot)
    {
        return new EngineSceneSnapshotResponse
        {
            Ok = snapshot.Ok != 0,
            Error = ReadString(bytes, snapshot.Error),
            Snapshot = new EngineSceneSnapshot
            {
                Schema = ReadString(bytes, snapshot.Schema),
                Name = ReadString(bytes, snapshot.Name),
                Roots = ReadSceneRoots(bytes, snapshot.Roots),
            },
        };
    }

    // A scene tree is a few levels deep; the cap only has to be past anything real.
    // Without one, a blob whose node lists itself as its own child (Children.Offset
    // equal to its own, Count 1) recursed forever. CheckedBufferOffset cannot catch
    // that -- it only proves a span lies INSIDE the buffer, and a self-referential
    // span does. The result was StackOverflowException, which .NET cannot catch: no
    // log line, no error dialog, instant process death and unsaved work lost.
    private const int MaxSceneNodeDepth = 64;

    private static List<EngineSceneNode> ReadSceneRoots(
        byte[] bytes,
        WzEditorTableSpanAbi roots)
    {
        return ReadTable<WzEditorSceneNodeAbi, EngineSceneNode>(
            bytes,
            roots,
            (b, node) => ReadSceneNode(b, node, depth: 0));
    }

    private static EngineSceneNode ReadSceneNode(
        byte[] bytes,
        WzEditorSceneNodeAbi node,
        int depth)
    {
        if (depth >= MaxSceneNodeDepth)
        {
            throw new InvalidOperationException(
                $"Engine ABI returned a scene tree deeper than {MaxSceneNodeDepth} "
                + "levels, which means the node spans are self-referential.");
        }

        return new EngineSceneNode
        {
            Id = ReadString(bytes, node.Id),
            DisplayName = ReadString(bytes, node.DisplayName),
            ParentId = HasFlag(node.Flags, WzEditorSceneNodeFlags.HasParent)
                ? ReadString(bytes, node.ParentId)
                : null,
            Kind = ReadString(bytes, node.Kind),
            Visible = HasFlag(node.Flags, WzEditorSceneNodeFlags.HasVisible)
                ? HasFlag(node.Flags, WzEditorSceneNodeFlags.Visible)
                : null,
            RenderOrder = node.RenderOrder,
            RenderableSource = ReadSceneRenderableSource(
                bytes,
                node.RenderableSource),
            Transform = HasFlag(node.Flags, WzEditorSceneNodeFlags.HasTransform)
                ? ReadSceneTransform(bytes, node.Transform)
                : null,
            Camera = HasFlag(node.Flags, WzEditorSceneNodeFlags.HasCamera)
                ? ReadSceneCamera(node.Camera)
                : null,
            Renderable = HasFlag(node.Flags, WzEditorSceneNodeFlags.HasRenderable)
                ? ReadSceneRenderable(node.Flags, node.Renderable)
                : null,
            Components = ReadTable<WzEditorSceneComponentAbi, EngineSceneComponent>(
                bytes,
                node.Components,
                ReadSceneComponent),
            Behaviors = ReadTable<WzEditorSceneBehaviorAbi, EngineSceneBehavior>(
                bytes,
                node.Behaviors,
                ReadSceneBehavior),
            SceneSource = HasFlag(node.Flags, WzEditorSceneNodeFlags.HasSceneSource)
                ? ReadSceneSceneSource(bytes, node.SceneSource)
                : null,
            // Persisted Collision/Motion component field values (read-back gap
            // fix): the authored fields or null, so the inspector restores them on
            // select + after reload instead of resetting to defaults.
            Collision = HasFlag(node.Flags, WzEditorSceneNodeFlags.HasCollision)
                ? ReadSceneCollision(node.Collision)
                : null,
            Motion = HasFlag(node.Flags, WzEditorSceneNodeFlags.HasMotion)
                ? ReadSceneMotion(node.Motion)
                : null,
            MotionFilter =
                HasFlag(node.Flags, WzEditorSceneNodeFlags.HasMotionFilter)
                    ? ReadSceneMotionFilter(node.MotionFilter)
                    : null,
            AudioSource =
                HasFlag(node.Flags, WzEditorSceneNodeFlags.HasAudioSource)
                    ? ReadSceneAudioSource(node.AudioSource)
                    : null,
            Atmosphere =
                HasFlag(node.Flags, WzEditorSceneNodeFlags.HasAtmosphere)
                    ? ReadSceneAtmosphere(node.Atmosphere)
                    : null,
            Environment =
                HasFlag(node.Flags, WzEditorSceneNodeFlags.HasEnvironment)
                    ? ReadSceneEnvironment(node.Environment)
                    : null,
            RenderToTexture =
                HasFlag(node.Flags, WzEditorSceneNodeFlags.HasRenderToTexture)
                    ? ReadSceneRenderToTexture(node.RenderToTexture)
                    : null,
            // Authored render-binding refs (issue #213): the node id or null, so the
            // inspector reveals + pre-selects these sections from persisted state.
            SceneSourceNodeId =
                HasFlag(node.Flags, WzEditorSceneNodeFlags.HasSceneSourceRef)
                    ? node.SceneSourceNodeId
                    : null,
            GeometryNodeId =
                HasFlag(node.Flags, WzEditorSceneNodeFlags.HasGeometry)
                    ? node.GeometryNodeId
                    : null,
            RenderProgramNodeId =
                HasFlag(node.Flags, WzEditorSceneNodeFlags.HasRenderProgram)
                    ? node.RenderProgramNodeId
                    : null,
            // Custom-renderable ingredients (issue #229/#230): always-valid
            // tables, possibly empty — presence is the count, no HAS_* flag.
            RenderableBindings = ReadTable<
                WzEditorSceneRenderableBindingAbi,
                EngineSceneRenderableBinding>(
                bytes,
                node.RenderableBindings,
                ReadSceneRenderableBinding),
            RenderableConstants = ReadTable<
                WzEditorSceneRenderableConstantAbi,
                EngineSceneRenderableConstant>(
                bytes,
                node.RenderableConstants,
                ReadSceneRenderableConstant),
            Children = ReadTable<WzEditorSceneNodeAbi, EngineSceneNode>(
                bytes,
                node.Children,
                (b, child) => ReadSceneNode(b, child, depth + 1)),
        };
    }

    private static EngineSceneRenderableBinding ReadSceneRenderableBinding(
        byte[] bytes,
        WzEditorSceneRenderableBindingAbi binding)
    {
        return new EngineSceneRenderableBinding
        {
            Semantic = ReadString(bytes, binding.Semantic),
            AssetGraphNodeId = binding.HasSourceRef != 0
                ? binding.AssetGraphNodeId
                : null,
        };
    }

    private static EngineSceneRenderableConstant ReadSceneRenderableConstant(
        byte[] bytes,
        WzEditorSceneRenderableConstantAbi constant)
    {
        return new EngineSceneRenderableConstant
        {
            Name = ReadString(bytes, constant.Name),
            Value =
            [
                constant.Value0,
                constant.Value1,
                constant.Value2,
                constant.Value3,
            ],
        };
    }

    private static EngineSceneNodeCollision ReadSceneCollision(
        WzEditorSceneCollisionAbi collision)
    {
        return new EngineSceneNodeCollision
        {
            CollisionAssetNodeId = collision.HasCollisionRef != 0
                ? collision.CollisionAssetNodeId
                : null,
            ConstrainMovement = collision.ConstrainMovement != 0,
        };
    }

    private static EngineSceneNodeAudioSource ReadSceneAudioSource(
        WzEditorSceneAudioSourceAbi audioSource)
    {
        return new EngineSceneNodeAudioSource
        {
            AudioRenderableNodeId = audioSource.HasRenderableRef != 0
                ? audioSource.AudioRenderableNodeId
                : null,
            AutoPlay = audioSource.AutoPlay != 0,
            Enabled = audioSource.Enabled != 0,
        };
    }

    private static EngineSceneNodeAtmosphere ReadSceneAtmosphere(
        WzEditorSceneAtmosphereAbi atmosphere)
    {
        return new EngineSceneNodeAtmosphere
        {
            AtmosphereAssetNodeId = atmosphere.HasAtmosphereRef != 0
                ? atmosphere.AtmosphereAssetNodeId
                : null,
            Enabled = atmosphere.Enabled != 0,
        };
    }

    private static EngineSceneNodeEnvironment ReadSceneEnvironment(
        WzEditorSceneEnvironmentAbi environment)
    {
        return new EngineSceneNodeEnvironment
        {
            EnvironmentAssetNodeId = environment.HasEnvironmentRef != 0
                ? environment.EnvironmentAssetNodeId
                : null,
            Enabled = environment.Enabled != 0,
        };
    }

    private static EngineSceneNodeRenderToTexture ReadSceneRenderToTexture(
        WzEditorSceneRenderToTextureAbi renderToTexture)
    {
        return new EngineSceneNodeRenderToTexture
        {
            TargetAssetNodeId = renderToTexture.HasTargetRef != 0
                ? renderToTexture.TargetAssetNodeId
                : null,
            IncludeDescendants = renderToTexture.IncludeDescendants != 0,
            AlsoDrawInScene = renderToTexture.AlsoDrawInScene != 0,
            Enabled = renderToTexture.Enabled != 0,
        };
    }

    private static EngineSceneNodeMotion ReadSceneMotion(
        WzEditorSceneMotionAbi motion)
    {
        return new EngineSceneNodeMotion
        {
            TerrainConstrained = motion.TerrainConstrained != 0,
            RideHeight = motion.RideHeight,
            FootprintRadius = motion.FootprintRadius,
            AlignToSurface = motion.AlignToSurface != 0,
            AlignmentStrength = motion.AlignmentStrength,
        };
    }

    private static EngineSceneNodeMotionFilter ReadSceneMotionFilter(
        WzEditorSceneMotionFilterAbi filter)
    {
        static EngineSceneNodeMotionFilterRotationAxis Axis(
            WzEditorSceneMotionFilterRotationAxisAbi a) => new()
            {
                SmoothingTime = a.SmoothingTime,
                Level = a.Level != 0,
                Limit = a.Limit != 0,
                LimitMinDegrees = a.LimitMinDegrees,
                LimitMaxDegrees = a.LimitMaxDegrees,
            };

        return new EngineSceneNodeMotionFilter
        {
            TranslationSmoothing =
            [
                filter.TranslationSmoothing0,
                filter.TranslationSmoothing1,
                filter.TranslationSmoothing2,
            ],
            TerrainFloor = filter.TerrainFloor != 0,
            TerrainFloorOffset = filter.TerrainFloorOffset,
            Roll = Axis(filter.Roll),
            Pitch = Axis(filter.Pitch),
            Yaw = Axis(filter.Yaw),
            Enabled = filter.Enabled != 0,
        };
    }

    private static EngineSceneNodeSceneSource ReadSceneSceneSource(
        byte[] bytes,
        WzEditorSceneSceneSourceAbi source)
    {
        return new EngineSceneNodeSceneSource
        {
            Kind = ReadString(bytes, source.Kind),
            Path = ReadString(bytes, source.Path),
            ConsumeMode = ReadString(bytes, source.ConsumeMode),
            SceneIndex = source.SceneIndex,
            StyleOverrideCount = source.StyleOverrideCount,
            HasBaseStyle = source.HasBaseStyle != 0,
            BaseStyle = ReadGlbStyle(source.BaseStyle),
            StyleOverrides =
                ReadTable<WzEditorGlbStyleOverrideAbi, EngineGlbStyleOverride>(
                    bytes,
                    source.StyleOverrides,
                    ReadGlbStyleOverride),
        };
    }

    private static EngineGlbStyle ReadGlbStyle(WzEditorGlbStyleAbi style)
    {
        return new EngineGlbStyle
        {
            SurfaceEnabled = style.SurfaceEnabled != 0,
            SurfaceRgba =
                [style.SurfaceR, style.SurfaceG, style.SurfaceB, style.SurfaceA],
            WireframeEnabled = style.WireframeEnabled != 0,
            WireframeRgba =
            [
                style.WireframeR,
                style.WireframeG,
                style.WireframeB,
                style.WireframeA,
            ],
        };
    }

    private static EngineGlbStyleOverride ReadGlbStyleOverride(
        byte[] bytes,
        WzEditorGlbStyleOverrideAbi over)
    {
        // bytes is unused (the override is plain POD — no blob strings/tables),
        // but ReadTable requires the (bytes, abi) converter shape.
        _ = bytes;
        return new EngineGlbStyleOverride
        {
            MeshIndex = over.MeshIndex,
            Style = ReadGlbStyle(over.Style),
        };
    }

    private static EngineSceneComponent ReadSceneComponent(
        byte[] bytes,
        WzEditorSceneComponentAbi component)
    {
        return new EngineSceneComponent
        {
            Kind = ReadString(bytes, component.Kind),
            DisplayName = ReadString(bytes, component.DisplayName),
        };
    }

    private static EngineSceneBehavior ReadSceneBehavior(
        byte[] bytes,
        WzEditorSceneBehaviorAbi behavior)
    {
        return new EngineSceneBehavior
        {
            Id = ReadString(bytes, behavior.Id),
            Label = ReadString(bytes, behavior.Label),
            Module = ReadString(bytes, behavior.Module),
            Name = ReadString(bytes, behavior.Name),
            Enabled = behavior.Enabled != 0,
            Events = ReadTable<WzEditorStringSpanAbi, string>(
                bytes,
                behavior.Events,
                ReadString),
            Config = ReadTable<WzEditorAssetGraphParamAbi, EngineSceneBehaviorConfig>(
                bytes,
                behavior.Config,
                ReadBehaviorConfig),
        };
    }

    private static EngineSceneBehaviorConfig ReadBehaviorConfig(
        byte[] bytes,
        WzEditorAssetGraphParamAbi param)
    {
        return new EngineSceneBehaviorConfig
        {
            Name = ReadString(bytes, param.Name),
            Kind = ReadString(bytes, param.Kind),
            Value = ReadString(bytes, param.Value),
        };
    }

    private static EngineSceneRenderableSource ReadSceneRenderableSource(
        byte[] bytes,
        WzEditorSceneRenderableSourceAbi source)
    {
        return new EngineSceneRenderableSource
        {
            Kind = ReadString(bytes, source.Kind),
            DisplayName = ReadString(bytes, source.DisplayName),
        };
    }

    private static EngineSceneTransform ReadSceneTransform(
        byte[] bytes,
        WzEditorSceneTransformAbi transform)
    {
        return new EngineSceneTransform
        {
            Translation =
            [
                transform.TranslationX,
                transform.TranslationY,
                transform.TranslationZ,
            ],
            RotationQuaternion =
            [
                transform.RotationQuaternionX,
                transform.RotationQuaternionY,
                transform.RotationQuaternionZ,
                transform.RotationQuaternionW,
            ],
            RotationEulerDegrees =
            [
                transform.RotationEulerDegreesX,
                transform.RotationEulerDegreesY,
                transform.RotationEulerDegreesZ,
            ],
            Scale =
            [
                transform.ScaleX,
                transform.ScaleY,
                transform.ScaleZ,
            ],
            Display = new EngineSceneTransformDisplay
            {
                TranslationX = ReadString(bytes, transform.DisplayTranslationX),
                TranslationY = ReadString(bytes, transform.DisplayTranslationY),
                TranslationZ = ReadString(bytes, transform.DisplayTranslationZ),
                RotationX = ReadString(bytes, transform.DisplayRotationX),
                RotationY = ReadString(bytes, transform.DisplayRotationY),
                RotationZ = ReadString(bytes, transform.DisplayRotationZ),
                ScaleX = ReadString(bytes, transform.DisplayScaleX),
                ScaleY = ReadString(bytes, transform.DisplayScaleY),
                ScaleZ = ReadString(bytes, transform.DisplayScaleZ),
            },
        };
    }

    private static EngineSceneCamera ReadSceneCamera(WzEditorSceneCameraAbi camera)
    {
        return new EngineSceneCamera
        {
            FieldOfViewY = HasFlag(camera.Flags, WzEditorSceneCameraFlags.HasFovY)
                ? camera.FovY
                : null,
            NearPlane = HasFlag(camera.Flags, WzEditorSceneCameraFlags.HasNearPlane)
                ? camera.NearPlane
                : null,
            FarPlane = HasFlag(camera.Flags, WzEditorSceneCameraFlags.HasFarPlane)
                ? camera.FarPlane
                : null,
            Aspect = HasFlag(camera.Flags, WzEditorSceneCameraFlags.HasAspect)
                ? camera.Aspect
                : null,
        };
    }

    private static EngineSceneRenderable ReadSceneRenderable(
        uint nodeFlags,
        WzEditorSceneRenderableAbi renderable)
    {
        return new EngineSceneRenderable
        {
            AssetGraphNodeId = HasFlag(
                nodeFlags,
                WzEditorSceneNodeFlags.RenderableHasAssetGraphNodeId)
                    ? renderable.AssetGraphNodeId
                    : null,
        };
    }

    // D3-C1 bounded the WORK a scene decode could do, and put the bound in the wrong
    // place. D3-P053/P071: the same aliasing amplifies with NO recursion at all, so
    // no depth cap applies and a scene-node cap cannot see it. ReadAssetGraphSnapshot
    // decodes N node records, each opening four sub-tables (InputPorts, OutputPorts,
    // Diagnostics, Params) and each param a fifth (Options). N records naming ONE
    // shared M-entry sub-table decode N*M elements with every individual span in
    // bounds -- CheckedBufferOffset can only ever reason about one span against the
    // buffer. Same shape reaches a scene node's Components/Behaviors tables.
    //
    // So the budget belongs on the TABLE PRIMITIVE, where it covers every reader
    // including ones not written yet, rather than on any single decoder.
    //
    // The bound is DERIVED, not picked, exactly as D3-C1's was: AbiBlobBuilder::
    // append_table is pure append with no interning (project_snapshot_abi.cpp:50-68),
    // so every table in a well-formed response occupies its own disjoint bytes and
    // the sum of all table extents is at most the response length. Charging past
    // that PROVES two spans alias. No magic number, and it scales with the response.
    //
    // Charged in BYTES over tables ONLY. Strings and the root structs share the same
    // blob, so charging them too could exceed the length on an honest response -- and
    // they cannot amplify by themselves, because ReadString runs once per field of an
    // already-decoded record. Bounding the records transitively bounds the strings.
    // (TAbi is `unmanaged` rather than `struct` for the reason spelled out on
    // ReadStruct below -- it turns a future runtime kill into a compile error.)
    [ThreadStatic]
    private static long _tableByteBudget;

    // Reset at the one choke point every decode passes through (ReadBufferBytes), so
    // a reader cannot be added that forgets to opt in. A nested decode -- there are
    // none today -- would restart the outer budget, which only ever LOOSENS the
    // bound; it can never reject a response that would otherwise have decoded.
    private static void BeginDecodeBudget(int bufferLength)
    {
        _tableByteBudget = bufferLength;
    }

    private static List<TManaged> ReadTable<TAbi, TManaged>(
        byte[] bytes,
        WzEditorTableSpanAbi span,
        Func<byte[], TAbi, TManaged> convert)
        where TAbi : unmanaged
    {
        if (span.Count == 0)
        {
            return [];
        }

        var itemSize = Marshal.SizeOf<TAbi>();
        var offset = CheckedBufferOffset(bytes, span.Offset, itemSize, span.Count);

        // CheckedBufferOffset has already proved this product fits the buffer, so it
        // cannot overflow here.
        _tableByteBudget -= (long)((ulong)itemSize * span.Count);
        if (_tableByteBudget < 0)
        {
            throw new InvalidOperationException(
                "Engine ABI returned more table data than the response can contain, "
                + "which means the table spans overlap.");
        }

        var values = new List<TManaged>(checked((int)span.Count));
        for (var index = 0; index < checked((int)span.Count); ++index)
        {
            var itemOffset = offset + index * itemSize;
            var item = MemoryMarshal.Read<TAbi>(bytes.AsSpan(itemOffset, itemSize));
            values.Add(convert(bytes, item));
        }

        return values;
    }

    // `unmanaged`, not `struct` (D3-P054). MemoryMarshal.Read<T> and Marshal.SizeOf<T>
    // both throw ArgumentException for a type carrying managed references -- and
    // ArgumentException is NOT one of the four types the P/Invoke wrappers catch, so
    // it escapes to the UI thread and terminates the editor on the first project
    // open. `struct` accepts such a type and defers the failure to run time;
    // `unmanaged` makes adding one (a convenience `string Name` beside a
    // WzEditorStringSpanAbi, say) a compile error at the declaration site instead.
    // Zero behavioural change today: all the mirror structs are already primitive.
    private static T ReadStruct<T>(byte[] bytes, ulong offset)
        where T : unmanaged
    {
        var size = Marshal.SizeOf<T>();
        var checkedOffset = CheckedBufferOffset(bytes, offset, size, count: 1);
        return MemoryMarshal.Read<T>(bytes.AsSpan(checkedOffset, size));
    }

    private static string ReadString(byte[] bytes, WzEditorStringSpanAbi span)
    {
        if (span.Size == 0)
        {
            return string.Empty;
        }

        var offset = CheckedBufferOffset(
            bytes,
            span.Offset,
            elementSize: 1,
            count: span.Size);
        return Encoding.UTF8.GetString(bytes, offset, checked((int)span.Size));
    }

    private static byte[] ReadBufferBytes(WzBuffer buffer, string emptyMessage)
    {
        if (buffer.Data == IntPtr.Zero)
        {
            throw new InvalidOperationException(emptyMessage);
        }

        // Bounded well below int.MaxValue on purpose: `new byte[2_000_000_000]`
        // passes an int.MaxValue check and then raises OutOfMemoryException, which is
        // NOT in the wrappers' catch lists -- so a drifted Size would terminate the
        // editor instead of surfacing as a failed call. No real response is anywhere
        // near this; the largest are scene/graph snapshots of a few MB.
        const long maxResponseBytes = 256L * 1024L * 1024L;
        if (buffer.Size > (ulong)maxResponseBytes)
        {
            throw new InvalidOperationException(
                $"Engine ABI returned a response of {buffer.Size} bytes, above the "
                + $"{maxResponseBytes}-byte limit.");
        }

        var bytes = new byte[(int)buffer.Size];
        Marshal.Copy(buffer.Data, bytes, startIndex: 0, length: bytes.Length);
        BeginDecodeBudget(bytes.Length);
        return bytes;
    }

    // The ABI carries these as uint while the managed model uses int. A drifted or
    // hostile blob can hold a value above int.MaxValue, and `checked((int)...)` would
    // raise OverflowException -- which is NOT one of the four types the wrappers
    // catch, so it would reach the UI thread and terminate the editor. Every decode
    // failure has to arrive as InvalidOperationException, the type they do handle.
    private static int CheckedInt(uint value, string what)
    {
        if (value > int.MaxValue)
        {
            throw new InvalidOperationException(
                $"Engine ABI returned {what} out of range: {value}.");
        }

        return (int)value;
    }

    private static int CheckedBufferOffset(
        byte[] bytes,
        ulong offset,
        int elementSize,
        ulong count)
    {
        if (offset > int.MaxValue || count > int.MaxValue)
        {
            throw new InvalidOperationException("Engine ABI returned an offset that is too large.");
        }

        var byteCount = checked((ulong)elementSize * count);
        if (byteCount > ulong.MaxValue - offset
            || offset + byteCount > (ulong)bytes.Length)
        {
            throw new InvalidOperationException("Engine ABI returned a buffer span outside the response.");
        }

        return checked((int)offset);
    }

    private static void ValidateAbiVersion(uint version)
    {
        if (version != WozzitsEngineAbi.AbiVersion)
        {
            throw new InvalidOperationException(
                $"Engine ABI version {version} is not supported by this editor.");
        }
    }

    private static EngineProjectStatus ToProjectStatus(uint status)
    {
        return status switch
        {
            0 => EngineProjectStatus.Missing,
            1 => EngineProjectStatus.Invalid,
            2 => EngineProjectStatus.Valid,
            _ => EngineProjectStatus.Invalid,
        };
    }

    private static EngineAssetGraphConnectionStatus ToConnectionStatus(uint status)
    {
        return status switch
        {
            0 => EngineAssetGraphConnectionStatus.Compatible,
            1 => EngineAssetGraphConnectionStatus.MissingNode,
            2 => EngineAssetGraphConnectionStatus.MissingCompiler,
            3 => EngineAssetGraphConnectionStatus.InvalidInputPort,
            4 => EngineAssetGraphConnectionStatus.TypeMismatch,
            5 => EngineAssetGraphConnectionStatus.SelfDependency,
            6 => EngineAssetGraphConnectionStatus.Cycle,
            7 => EngineAssetGraphConnectionStatus.DuplicateInputPort,
            _ => EngineAssetGraphConnectionStatus.TypeMismatch,
        };
    }

    private static bool HasFlag(uint flags, uint flag)
    {
        return (flags & flag) != 0;
    }
}
