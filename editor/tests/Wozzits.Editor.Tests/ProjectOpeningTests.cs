using System.Threading;
using Wozzits.Editor.Core.Projects;
using Wozzits.Editor.Core.Logging;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;
using Wozzits.Editor.ViewModels;
using Wozzits.Editor.ViewModels.EditorPanes;

namespace Wozzits.Editor.Tests;

public sealed partial class ProjectOpeningTests
{
    [Fact]
    public void LaunchOptionsUseFirstPositionalArgumentAsProjectDirectory()
    {
        var options = ProjectLaunchOptions.FromCommandLine(["--verbose", @"D:\work\project"]);

        Assert.Equal(@"D:\work\project", options.ProjectDirectory);
    }

    [Fact]
    public void BootstrapAllowsCreateOnlyWhenEngineReportsMissingProject()
    {
        var viewModel = new ProjectBootstrapViewModel(
            new ProjectDirectory(@"D:\work\project"),
            new WozzitsEngineNativeClient(),
            new EngineProjectResponse
            {
                Status = EngineProjectStatus.Missing,
                Error = "missing",
            },
            openProject: _ => { },
            quit: () => { });

        Assert.True(viewModel.CanCreateProject);
    }

    [Fact]
    public void BootstrapDisablesCreateWhenEngineReportsInvalidProject()
    {
        var viewModel = new ProjectBootstrapViewModel(
            new ProjectDirectory(@"D:\work\project"),
            new WozzitsEngineNativeClient(),
            new EngineProjectResponse
            {
                Status = EngineProjectStatus.Invalid,
                Error = "bad project",
            },
            openProject: _ => { },
            quit: () => { });

        Assert.False(viewModel.CanCreateProject);
        Assert.Equal("bad project", viewModel.ErrorMessage);
    }

    [Fact]
    public void NativeEngineClientRejectsEmptyProjectDirectoryBeforeLoadingAbi()
    {
        var snapshot = new WozzitsEngineNativeClient().LoadProjectSnapshot("");

        Assert.False(snapshot.Ok);
        Assert.Equal(EngineProjectStatus.Invalid, snapshot.Status);
        Assert.Contains("Project directory is empty", snapshot.Error);

        var created = new WozzitsEngineNativeClient().CreateProject("");

        Assert.False(created.Ok);
        Assert.Equal(EngineProjectStatus.Invalid, created.Status);
        Assert.Contains("Project directory is empty", created.Error);

        var mutation = new WozzitsEngineNativeClient()
            .OpenEditorSession("")
            .SetSceneNodeProperties("node", "name", visible: true);

        Assert.False(mutation.Ok);
        Assert.Contains("Project directory is empty", mutation.Error);

        var graphMutation = new WozzitsEngineNativeClient()
            .OpenEditorSession("")
            .SetAssetGraphNodePosition(1, 10, 20);

        Assert.False(graphMutation.Ok);
        Assert.Contains("Project directory is empty", graphMutation.Error);

        var graphCommit = new WozzitsEngineNativeClient()
            .OpenEditorSession("")
            .CommitAssetGraph();

        Assert.False(graphCommit.Ok);
        Assert.Contains("Project directory is empty", graphCommit.Error);
    }

    [Fact]
    public void NativeEngineClientUsesConfiguredAbiPath()
    {
        var previous = Environment.GetEnvironmentVariable(
            WozzitsEngineNativeClient.AbiPathEnvironmentVariable);
        try
        {
            Environment.SetEnvironmentVariable(
                WozzitsEngineNativeClient.AbiPathEnvironmentVariable,
                @"D:\engine\wozzits_abi.dll");

            Assert.Equal(
                @"D:\engine\wozzits_abi.dll",
                WozzitsEngineNativeClient.ResolveDefaultAbiPath());
        }
        finally
        {
            Environment.SetEnvironmentVariable(
                WozzitsEngineNativeClient.AbiPathEnvironmentVariable,
                previous);
        }
    }

    [Fact]
    public void NativeEngineClientLoadsSampleProjectWhenEngineAbiIsBuilt()
    {
        var abiPath = WozzitsEngineNativeClient.ResolveDefaultAbiPath();
        var sampleProject = Path.GetFullPath(Path.Combine(
            AppContext.BaseDirectory,
            "..",
            "..",
            "..",
            "..",
            "..",
            "..",
            "wozzits-window-engine",
            "window_engine",
            "resources",
            "projects",
            "test_mesh_001"));

        if (!File.Exists(abiPath) || !Directory.Exists(sampleProject))
        {
            return;
        }

        var response = new WozzitsEngineNativeClient().LoadProjectSnapshot(sampleProject);

        Assert.True(response.IsValid, response.Error);
        Assert.Equal("test_mesh_001", response.ProjectName);
        Assert.True(response.AssetGraph.Ok, response.AssetGraph.Error);
        Assert.True(response.Scene.Ok, response.Scene.Error);
        Assert.NotEmpty(response.AssetGraph.Snapshot.Nodes);
        Assert.NotEmpty(response.Scene.Snapshot.Roots);

        var session = new WozzitsEngineNativeClient().OpenEditorSession(sampleProject);
        try
        {
            var sessionGraph = session.LoadAssetGraphSnapshot();

            Assert.True(sessionGraph.Ok, sessionGraph.Error);
            Assert.NotEmpty(sessionGraph.Snapshot.Nodes);
            Assert.Contains(
                sessionGraph.Snapshot.Nodes,
                node => node.InputPorts.Count > 0);
        }
        finally
        {
            (session as IDisposable)?.Dispose();
        }
    }

    [Fact]
    public void NativeEngineClientCreatesProjectFilesWhenEngineAbiIsBuilt()
    {
        var abiPath = WozzitsEngineNativeClient.ResolveDefaultAbiPath();
        if (!File.Exists(abiPath))
        {
            return;
        }

        var projectRoot = Path.Combine(
            Path.GetTempPath(),
            "wozzits_editor_native_create_" + Guid.NewGuid().ToString("N"));
        try
        {
            var created = new WozzitsEngineNativeClient().CreateProject(projectRoot);

            Assert.True(created.IsValid, created.Error);
            Assert.True(created.Created);
            Assert.True(File.Exists(Path.Combine(projectRoot, ".wozzits", "project.json")));
        }
        finally
        {
            if (Directory.Exists(projectRoot))
            {
                Directory.Delete(projectRoot, recursive: true);
            }
        }
    }

    [Fact]
    public void NativeEngineClientSavesGraphAndNeedsRuntimeForCompileCommitWhenEngineAbiIsBuilt()
    {
        var abiPath = WozzitsEngineNativeClient.ResolveDefaultAbiPath();
        if (!File.Exists(abiPath))
        {
            return;
        }

        var projectRoot = Path.Combine(
            Path.GetTempPath(),
            "wozzits_editor_native_asset_graph_" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(Path.Combine(projectRoot, ".wozzits"));
            File.WriteAllText(
                Path.Combine(projectRoot, ".wozzits", "project.json"),
                """
                {
                  "schema": "wozzits.project.v1",
                  "formatVersion": 1,
                  "name": "empty_graph",
                  "asset_graph": "assets.graph.json"
                }
                """);
            File.WriteAllText(
                Path.Combine(projectRoot, "assets.graph.json"),
                """
                {
                  "schema": "wozzits.asset_graph.v2",
                  "nodes": []
                }
                """);

            using var session = new WozzitsEngineNativeClient()
                .OpenEditorSession(projectRoot) as IDisposable;
            var editorSession = Assert.IsAssignableFrom<IWozzitsEngineEditorSession>(session);

            // Save is CPU-only (no engine runtime); it persists the draft.
            var save = editorSession.SaveAssetGraph();
            Assert.True(save.Ok, save.Error);

            // Compile binds the draft to the in-process engine runtime; this
            // session was opened without one (startRuntime defaults false).
            var compile = editorSession.CompileAssetGraph();
            Assert.False(compile.Ok);
            Assert.Contains("runtime is not available", compile.Error);

            // Commit = save + bind; the save half succeeds, the bind half needs
            // the runtime, so commit reports the missing runtime.
            var commit = editorSession.CommitAssetGraph();
            Assert.False(commit.Ok);
            Assert.Contains("runtime is not available", commit.Error);
        }
        finally
        {
            if (Directory.Exists(projectRoot))
            {
                Directory.Delete(projectRoot, recursive: true);
            }
        }
    }

    [Fact]
    public void SceneTreeBuildsHierarchyFromEngineSnapshot()
    {
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                scene: SceneSnapshot(
                    Node(
                        "root",
                        children:
                        [
                            Node("camera_01", kind: "camera"),
                            Node("mesh_01", displayName: "mesh", kind: "renderable"),
                        ]))));

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        Assert.Equal("root", root.DisplayName);
        Assert.Collection(
            root.Children,
            camera =>
            {
                Assert.Equal("camera_01", camera.DisplayName);
                Assert.Equal("camera", camera.Kind);
            },
            mesh =>
            {
                Assert.Equal("mesh", mesh.DisplayName);
                Assert.Equal("renderable", mesh.Kind);
            });
    }

    [Fact]
    public void MainWindowUsesProjectNameFromProjectSnapshot()
    {
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                projectName: "test",
                scene: SceneSnapshot(
                    Node(
                        "root",
                        children:
                        [
                            Node("child", displayName: "child node"),
                        ]))));

        Assert.Equal("test", viewModel.WindowTitle);

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        Assert.Equal("root", root.DisplayName);

        var child = Assert.Single(root.Children);
        Assert.Equal("child node", child.DisplayName);
    }

    [Fact]
    public void MainWindowConsoleReplaysAndSubscribesToEditorLogs()
    {
        var editorLog = new EditorLogBuffer();
        editorLog.AppendLine("[editor] before view model");

        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(),
            editorLog: editorLog);

        Assert.Contains("[editor] before view model", viewModel.EngineLogText);

        editorLog.AppendLine("[engine] resident engine message");

        Assert.Contains("[engine] resident engine message", viewModel.EngineLogText);
    }


    [Fact]
    public void SceneTreeSelectionTracksOnlyOneSelectedNode()
    {
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                scene: SceneSnapshot(
                    Node(
                        "root",
                        children:
                        [
                            Node("mesh", kind: "renderable"),
                        ]))));

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        var mesh = Assert.Single(root.Children);

        viewModel.SceneTree.SelectNode(mesh);

        Assert.False(root.IsSelected);
        Assert.True(mesh.IsSelected);

        viewModel.SceneTree.SelectNode(root);

        Assert.True(root.IsSelected);
        Assert.False(mesh.IsSelected);
    }

    [Fact]
    public void InspectorUpdatesWhenSceneNodeIsSelected()
    {
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                scene: SceneSnapshot(
                    Node(
                        "root",
                        children:
                        [
                            Node(
                                "camera",
                                kind: "camera",
                                components: [Component("camera", "Camera")],
                                camera: new EngineSceneCamera
                                {
                                    FieldOfViewY = 1.0472,
                                    NearPlane = 0.1,
                                    FarPlane = 1000,
                                    Aspect = 1.778,
                                }),
                            Node(
                                "mesh",
                                displayName: "mesh node",
                                parentId: "root",
                                kind: "renderable",
                                visible: true,
                                renderableSource: Source("asset-graph-node", "asset-node"),
                                transform: new EngineSceneTransform
                                {
                                    Translation = [101, 102, 103],
                                    RotationQuaternion = [0, 0, 0, 1],
                                    RotationEulerDegrees = [110, 120, 130],
                                    Scale = [202, 203, 204],
                                    Display = new EngineSceneTransformDisplay
                                    {
                                        TranslationX = "1",
                                        TranslationY = "2",
                                        TranslationZ = "3",
                                        RotationX = "10",
                                        RotationY = "20",
                                        RotationZ = "30",
                                        ScaleX = "2",
                                        ScaleY = "3",
                                        ScaleZ = "4",
                                    },
                                },
                                renderable: new EngineSceneRenderable
                                {
                                    AssetGraphNodeId = 8,
                                }),
                        ]))));

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        var camera = Assert.Single(root.Children, node => node.Id == "camera");
        var mesh = Assert.Single(root.Children, node => node.Id == "mesh");

        viewModel.SceneTree.SelectNode(mesh);

        Assert.True(viewModel.Inspector.HasSelection);
        Assert.Equal("mesh node", viewModel.Inspector.Header);
        Assert.Equal("mesh", viewModel.Inspector.NodeId);
        Assert.Equal("root", viewModel.Inspector.ParentId);
        Assert.True(viewModel.Inspector.NodeVisible);
        Assert.Equal("asset-node", viewModel.Inspector.RenderableSource);
        Assert.Equal("asset-graph-node", viewModel.Inspector.RenderableSourceKind);
        Assert.Equal("1", viewModel.Inspector.TranslationX);
        Assert.Equal("2", viewModel.Inspector.TranslationY);
        Assert.Equal("3", viewModel.Inspector.TranslationZ);
        Assert.Equal("10", viewModel.Inspector.RotationX);
        Assert.Equal("20", viewModel.Inspector.RotationY);
        Assert.Equal("30", viewModel.Inspector.RotationZ);
        Assert.Equal("2", viewModel.Inspector.ScaleX);
        Assert.Equal("3", viewModel.Inspector.ScaleY);
        Assert.Equal("4", viewModel.Inspector.ScaleZ);
        Assert.Equal("8", viewModel.Inspector.RenderableAssetGraphNodeId);
        Assert.Equal("mesh node Components", viewModel.Inspector.ComponentsHeader);
        Assert.True(viewModel.Inspector.HasNoComponents);

        viewModel.SceneTree.SelectNode(camera);

        Assert.True(viewModel.Inspector.HasCameraComponent);
        Assert.Equal("camera Components", viewModel.Inspector.ComponentsHeader);
        Assert.Contains(
            viewModel.Inspector.Components,
            component => component.Name == "Camera" && component.Kind == "camera");
        Assert.Equal("1.047", viewModel.Inspector.CameraFovY);
        Assert.Equal("0.1", viewModel.Inspector.CameraNear);
        Assert.Equal("1000", viewModel.Inspector.CameraFar);
        Assert.Equal("1.778", viewModel.Inspector.CameraAspect);
    }

    [Fact]
    public void InspectorSwitchesBetweenAssetGraphAndSceneSelections()
    {
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                assetGraph: new EngineAssetGraphSnapshotResponse
                {
                    Ok = true,
                    Snapshot = new EngineAssetGraphSnapshot
                    {
                        Nodes =
                        [
                            new EngineAssetGraphNode
                            {
                                Id = 7,
                                Type = 41,
                                TypeName = "Renderable",
                                Schema = "e7000707",
                                DisplayName = "renderable",
                                CompileStatus = "ready",
                                X = 12.5,
                                Y = 34.25,
                                InputPorts =
                                [
                                    new EngineAssetGraphPort
                                    {
                                        Index = 0,
                                        Type = 7,
                                        Flags = EngineAssetGraphPortFlags.Required,
                                        Name = "source_file",
                                        Label = "Shader source",
                                        TypeName = "Shader source",
                                    },
                                ],
                                OutputPorts =
                                [
                                    new EngineAssetGraphPort
                                    {
                                        Index = 0,
                                        Type = 9,
                                        Flags = EngineAssetGraphPortFlags.Required
                                            | EngineAssetGraphPortFlags.Many,
                                        Name = "output",
                                        Label = "Renderable",
                                        TypeName = "Renderable",
                                    },
                                ],
                                Diagnostics =
                                [
                                    new EngineAssetGraphDiagnostic
                                    {
                                        Severity = 2,
                                        Code = 5,
                                        SeverityName = "error",
                                        CodeName = "TypeMismatch",
                                        Node = 7,
                                        Edge = ulong.MaxValue,
                                        InputPort = 0,
                                        Message = "edge provider type does not match the input port type",
                                    },
                                ],
                            },
                        ],
                    },
                },
                scene: SceneSnapshot(
                    Node(
                        "root",
                        children:
                        [
                            Node("mesh", displayName: "mesh node"),
                        ]))));

        var graphNode = Assert.Single(viewModel.AssetGraph.Nodes);
        viewModel.AssetGraph.SelectNode(graphNode);

        Assert.True(viewModel.Inspector.HasAssetGraphNodeSelection);
        Assert.False(viewModel.Inspector.HasSceneNodeSelection);
        Assert.Equal("renderable", viewModel.Inspector.Header);
        Assert.Equal("7", viewModel.Inspector.AssetGraphNodeId);
        Assert.Equal("renderable", viewModel.Inspector.AssetGraphNodeName);
        Assert.Equal("Renderable", viewModel.Inspector.AssetGraphNodeType);
        Assert.Equal("41", viewModel.Inspector.AssetGraphNodeTypeId);
        Assert.Equal("e7000707", viewModel.Inspector.AssetGraphNodeSchema);
        Assert.Equal("ready", viewModel.Inspector.AssetGraphNodeCompileStatus);
        Assert.Equal("12.5, 34.25", viewModel.Inspector.AssetGraphNodePosition);
        Assert.Collection(
            viewModel.Inspector.AssetGraphInputPorts,
            port =>
            {
                Assert.Equal("source_file", port.Name);
                Assert.Equal("Shader source", port.Label);
                Assert.Equal("7", port.Type);
                Assert.Equal("required", port.Requirement);
                Assert.Equal("single", port.Arity);
            });
        Assert.Collection(
            viewModel.Inspector.AssetGraphOutputPorts,
            port =>
            {
                Assert.Equal("output", port.Name);
                Assert.Equal("Renderable", port.Label);
                Assert.Equal("9", port.Type);
                Assert.Equal("required", port.Requirement);
                Assert.Equal("many", port.Arity);
            });
        Assert.Collection(
            viewModel.Inspector.AssetGraphDiagnostics,
            diagnostic =>
            {
                Assert.Equal("error", diagnostic.Severity);
                Assert.Equal("TypeMismatch", diagnostic.Code);
                Assert.Equal("7", diagnostic.Node);
                Assert.Equal("0", diagnostic.InputPort);
                Assert.Contains("provider type", diagnostic.Message);
            });

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        var mesh = Assert.Single(root.Children);

        viewModel.SceneTree.SelectNode(mesh);

        Assert.True(viewModel.Inspector.HasSceneNodeSelection);
        Assert.False(viewModel.Inspector.HasAssetGraphNodeSelection);
        Assert.Equal("mesh node", viewModel.Inspector.Header);
        Assert.Equal("mesh", viewModel.Inspector.NodeId);
    }

    [Fact]
    public void InspectorAppliesBufferedSceneEditsThroughEngineSession()
    {
        var editorSession = new RecordingEditorSession();
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                scene: SceneSnapshot(
                    Node(
                        "root",
                        children:
                        [
                            Node(
                                "camera",
                                kind: "camera",
                                components: [Component("camera", "Camera")],
                                camera: new EngineSceneCamera
                                {
                                    FieldOfViewY = 1.0472,
                                    NearPlane = 0.1,
                                    FarPlane = 1000,
                                    Aspect = 1.778,
                                }),
                            Node(
                                "mesh",
                                displayName: "mesh node",
                                parentId: "root",
                                visible: true,
                                transform: new EngineSceneTransform
                                {
                                    Display = new EngineSceneTransformDisplay
                                    {
                                        TranslationX = "1",
                                        TranslationY = "2",
                                        TranslationZ = "3",
                                        RotationX = "10",
                                        RotationY = "20",
                                        RotationZ = "30",
                                        ScaleX = "2",
                                        ScaleY = "3",
                                        ScaleZ = "4",
                                    },
                                }),
                        ]))),
            editorSession: editorSession);

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        var mesh = Assert.Single(root.Children, node => node.Id == "mesh");
        var camera = Assert.Single(root.Children, node => node.Id == "camera");

        viewModel.SceneTree.SelectNode(mesh);
        viewModel.Inspector.NodeName = "renamed mesh";
        viewModel.Inspector.NodeVisible = false;
        // Selecting the node populated the transform fields; that population
        // must not echo back to the engine as a live edit.
        Assert.Empty(editorSession.LiveTransforms);

        viewModel.Inspector.TranslationX = "9";
        viewModel.Inspector.RotationZ = "45";
        viewModel.Inspector.ScaleY = "6";

        Assert.Empty(editorSession.NodeProperties);
        Assert.Empty(editorSession.Transforms);

        // Editing the fields streams live transform previews to the running
        // engine (per keystroke). Transform edits have no disk path now (the
        // Apply button is gone) — the disk Transforms list stays empty.
        Assert.NotEmpty(editorSession.LiveTransforms);
        var live = editorSession.LiveTransforms[^1];
        Assert.Equal("mesh", live.NodeId);
        Assert.Equal("9", live.Edit.TranslationX);
        Assert.Equal("45", live.Edit.RotationZ);
        Assert.Equal("6", live.Edit.ScaleY);

        // Name + visibility stream live too (no Apply button), and the rename
        // reflects in the tree node's display text.
        Assert.NotEmpty(editorSession.LiveProperties);
        var liveProps = editorSession.LiveProperties[^1];
        Assert.Equal("mesh", liveProps.NodeId);
        Assert.Equal("renamed mesh", liveProps.Name);
        Assert.False(liveProps.Visible);
        Assert.Equal("mesh:renamed mesh", mesh.DisplayText);

        Assert.Empty(editorSession.NodeProperties);  // never hits the disk path
        Assert.Empty(editorSession.Transforms);

        viewModel.SceneTree.SelectNode(camera);
        viewModel.Inspector.CameraFovY = "0.8";
        viewModel.Inspector.CameraNear = "0.05";
        viewModel.Inspector.CameraFar = "500";
        viewModel.Inspector.CameraAspect = "1.6";

        viewModel.Inspector.ApplyCameraCommand.Execute(null);

        var cameraEdit = Assert.Single(editorSession.Cameras);
        Assert.Equal("camera", cameraEdit.NodeId);
        Assert.Equal("0.8", cameraEdit.Edit.FieldOfViewY);
        Assert.Equal("0.05", cameraEdit.Edit.NearPlane);
        Assert.Equal("500", cameraEdit.Edit.FarPlane);
        Assert.Equal("1.6", cameraEdit.Edit.Aspect);
    }

    [Fact]
    public void SceneTreeAddChildInsertsMintedNodeUnderParent()
    {
        var session = new RecordingEditorSession { NextAddedChildId = "7" };
        var sceneTree = new SceneTreeEditorPaneViewModel(session);
        sceneTree.LoadSnapshot(new EngineSceneSnapshotResponse
        {
            Ok = true,
            Snapshot = new EngineSceneSnapshot
            {
                Roots =
                [
                    new EngineSceneNode
                    {
                        Id = "root",
                        DisplayName = "Root",
                        Kind = "node",
                    },
                ],
            },
        });

        var root = Assert.Single(sceneTree.Nodes);
        sceneTree.AddChild(root);

        Assert.Equal("root", Assert.Single(session.AddChildParents));
        var child = Assert.Single(root.Children);
        Assert.Equal("7", child.Id);
        Assert.Equal("7", child.DisplayText);  // no label yet -> just the id
        Assert.Same(child, sceneTree.SelectedNode);
    }

    private static EngineProjectSnapshotResponse ProjectSnapshot(
        string projectName = "test",
        EngineAssetGraphSnapshotResponse? assetGraph = null,
        EngineSceneSnapshotResponse? scene = null)
    {
        return new EngineProjectSnapshotResponse
        {
            Ok = true,
            Status = EngineProjectStatus.Valid,
            ProjectName = projectName,
            AssetGraph = assetGraph ?? new EngineAssetGraphSnapshotResponse { Ok = true },
            Scene = scene ?? SceneSnapshot(),
        };
    }

    private static EngineSceneSnapshotResponse SceneSnapshot(params EngineSceneNode[] roots)
    {
        return new EngineSceneSnapshotResponse
        {
            Ok = true,
            Snapshot = new EngineSceneSnapshot
            {
                Schema = "wozzits.scene.v0",
                Name = "test_scene",
                Roots = [.. roots],
            },
        };
    }

    private static EngineSceneNode Node(
        string id,
        string? displayName = null,
        string? parentId = null,
        string kind = "node",
        bool? visible = null,
        EngineSceneRenderableSource? renderableSource = null,
        EngineSceneTransform? transform = null,
        EngineSceneCamera? camera = null,
        EngineSceneRenderable? renderable = null,
        EngineSceneComponent[]? components = null,
        params EngineSceneNode[] children)
    {
        return new EngineSceneNode
        {
            Id = id,
            DisplayName = displayName ?? id,
            ParentId = parentId,
            Kind = kind,
            Visible = visible,
            RenderableSource = renderableSource ?? new EngineSceneRenderableSource(),
            Transform = transform,
            Camera = camera,
            Renderable = renderable,
            Components = components is null ? [] : [.. components],
            Children = [.. children],
        };
    }

    private static EngineSceneComponent Component(
        string kind,
        string displayName)
    {
        return new EngineSceneComponent
        {
            Kind = kind,
            DisplayName = displayName,
        };
    }

    private static EngineSceneRenderableSource Source(
        string kind,
        string displayName)
    {
        return new EngineSceneRenderableSource
        {
            Kind = kind,
            DisplayName = displayName,
        };
    }

    private sealed class RecordingEditorSession : IWozzitsEngineEditorSession
    {
        public EngineAssetGraphSnapshotResponse AssetGraphSnapshot { get; set; } = new()
        {
            Ok = false,
            Error = "No recorded asset graph snapshot.",
        };

        public EngineAssetGraphConnectionCheckResponse ConnectionCheck { get; set; } = new()
        {
            Ok = true,
            Check = new EngineAssetGraphConnectionCheck
            {
                Compatible = true,
                Status = EngineAssetGraphConnectionStatus.Compatible,
            },
        };

        public EngineMutationResponse CommitResponse { get; set; } = new()
        {
            Ok = true,
        };

        public EngineMutationResponse CompileResponse { get; set; } = new()
        {
            Ok = true,
        };

        public EngineMutationResponse SaveResponse { get; set; } = new()
        {
            Ok = true,
        };

        public int CommitCount { get; private set; }

        public int CompileCount { get; private set; }

        public int SaveCount { get; private set; }

        public ManualResetEventSlim? CompileStartedSignal { get; set; }

        public ManualResetEventSlim? ContinueCompileSignal { get; set; }

        public List<AssetGraphPositionEdit> AssetGraphPositions { get; } = [];

        public List<double> AssetGraphZooms { get; } = [];

        public List<AssetGraphConnectionEdit> ConnectionChecks { get; } = [];

        public List<AssetGraphConnectionEdit> Connections { get; } = [];

        public List<ulong> DisconnectedEdges { get; } = [];

        public List<NodeParamEdit> NodeParams { get; } = [];

        public List<NodePropertiesEdit> NodeProperties { get; } = [];

        public List<NodePropertiesEdit> LiveProperties { get; } = [];

        public List<TransformEdit> Transforms { get; } = [];

        public List<TransformEdit> LiveTransforms { get; } = [];

        public List<string> AddChildParents { get; } = [];

        public string NextAddedChildId { get; set; } = "1";

        public List<CameraEdit> Cameras { get; } = [];

        public EngineAssetGraphSnapshotResponse LoadAssetGraphSnapshot()
        {
            return AssetGraphSnapshot;
        }

        public EngineAssetCatalogResponse AssetCatalog { get; set; } = new()
        {
            Ok = true,
        };

        public EngineAssetCatalogResponse LoadAssetCatalog()
        {
            return AssetCatalog;
        }

        public EngineAssetGraphConnectionCheckResponse CanConnectAssetGraphNodes(
            ulong fromNodeId,
            ulong toNodeId,
            uint toInputPort)
        {
            ConnectionChecks.Add(new AssetGraphConnectionEdit(
                fromNodeId,
                toNodeId,
                toInputPort));
            return ConnectionCheck;
        }

        public EngineAssetGraphConnectionCheckResponse ConnectAssetGraphNodes(
            ulong fromNodeId,
            ulong toNodeId,
            uint toInputPort)
        {
            Connections.Add(new AssetGraphConnectionEdit(
                fromNodeId,
                toNodeId,
                toInputPort));
            return ConnectionCheck;
        }

        public EngineMutationResponse DisconnectAssetGraphEdge(ulong edgeId)
        {
            DisconnectedEdges.Add(edgeId);
            return new EngineMutationResponse { Ok = true };
        }

        public List<AddNodeEdit> AddedNodes { get; } = [];

        public ulong NextAddedNodeId { get; set; } = 1000u;

        public EngineAddNodeResponse AddAssetGraphNode(ulong schema, uint type)
        {
            var id = NextAddedNodeId++;
            AddedNodes.Add(new AddNodeEdit(schema, type, id));
            return new EngineAddNodeResponse { Ok = true, NodeId = id };
        }

        public List<ulong> RemovedNodes { get; } = [];

        public EngineMutationResponse RemoveAssetGraphNode(ulong nodeId)
        {
            RemovedNodes.Add(nodeId);
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse SetAssetGraphNodeParamString(
            ulong nodeId,
            string name,
            string value)
        {
            NodeParams.Add(new NodeParamEdit(nodeId, name, value));
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse SaveAssetGraph()
        {
            ++SaveCount;
            return SaveResponse;
        }

        public EngineMutationResponse CommitAssetGraph()
        {
            ++CommitCount;
            return CommitResponse;
        }

        public int RestartRuntimeCount { get; private set; }

        public void RestartRuntime()
        {
            ++RestartRuntimeCount;
        }

        public EngineMutationResponse CompileAssetGraph()
        {
            ++CompileCount;
            CompileStartedSignal?.Set();
            if (ContinueCompileSignal is not null
                && !ContinueCompileSignal.Wait(TimeSpan.FromSeconds(5)))
            {
                throw new TimeoutException("Timed out waiting to continue compile.");
            }

            return CompileResponse;
        }

        public EngineMutationResponse SetAssetGraphNodePosition(
            ulong nodeId,
            double x,
            double y)
        {
            AssetGraphPositions.Add(new AssetGraphPositionEdit(nodeId, x, y));
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse SetAssetGraphZoom(double zoom)
        {
            AssetGraphZooms.Add(zoom);
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse SetSceneNodeProperties(
            string nodeId,
            string name,
            bool visible)
        {
            NodeProperties.Add(new NodePropertiesEdit(nodeId, name, visible));
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse SetSceneNodePropertiesLive(
            string nodeId,
            string name,
            bool visible)
        {
            LiveProperties.Add(new NodePropertiesEdit(nodeId, name, visible));
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse SetSceneNodeTransform(
            string nodeId,
            EngineSceneTransformEdit edit)
        {
            Transforms.Add(new TransformEdit(nodeId, edit));
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse SetSceneNodeTransformLive(
            string nodeId,
            EngineSceneTransformEdit edit)
        {
            LiveTransforms.Add(new TransformEdit(nodeId, edit));
            return new EngineMutationResponse { Ok = true };
        }

        public EngineAddSceneNodeResponse AddChildNode(string parentId)
        {
            AddChildParents.Add(parentId);
            return new EngineAddSceneNodeResponse
            {
                Ok = true,
                NodeId = NextAddedChildId,
            };
        }

        public EngineMutationResponse SetSceneNodeCamera(
            string nodeId,
            EngineSceneCameraEdit edit)
        {
            Cameras.Add(new CameraEdit(nodeId, edit));
            return new EngineMutationResponse { Ok = true };
        }
    }

    private sealed record AssetGraphPositionEdit(
        ulong NodeId,
        double X,
        double Y);

    private sealed record AssetGraphConnectionEdit(
        ulong FromNodeId,
        ulong ToNodeId,
        uint ToInputPort);

    private sealed record NodePropertiesEdit(
        string NodeId,
        string Name,
        bool Visible);

    private sealed record NodeParamEdit(
        ulong NodeId,
        string Name,
        string Value);

    private sealed record AddNodeEdit(
        ulong Schema,
        uint Type,
        ulong NodeId);

    private sealed record TransformEdit(
        string NodeId,
        EngineSceneTransformEdit Edit);

    private sealed record CameraEdit(
        string NodeId,
        EngineSceneCameraEdit Edit);
}
