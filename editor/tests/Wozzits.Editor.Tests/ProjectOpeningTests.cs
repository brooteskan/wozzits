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

    // issue #213: the grafted-scene-nodes session method round-trips through the
    // real engine ABI. A session opened without a viewport runtime has nothing
    // grafted, so it returns an empty snapshot (no roots) and never throws —
    // exercising the session/client wiring against the built DLL. Graceful-skip
    // when the engine ABI is not built.
    [Fact]
    public void NativeEngineSessionGraftedSceneNodesIsEmptyWithoutRuntime()
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

        // startRuntime defaults false => no viewport, so the session short-circuits
        // to an empty snapshot. (The populated path needs a GPU and is covered by
        // the engine on-device test.)
        var session = new WozzitsEngineNativeClient().OpenEditorSession(sampleProject);
        try
        {
            var grafted = session.LoadGraftedSceneNodes();
            Assert.NotNull(grafted);
            Assert.Empty(grafted.Roots);
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
        // Camera is surfaced via its own Camera section (HasCameraComponent +
        // the parameter fields below), not duplicated in the generic component
        // list.
        Assert.DoesNotContain(
            viewModel.Inspector.Components,
            component => component.Kind == "camera");
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

    // Inspector "Subtree from asset" section (issue #213 piece 2): the asset graph's
    // "Scene from GLB" nodes are threaded into the picker (matched by schema, NOT by
    // the shared asset-type name); "Add Component → subtree_from_asset" reveals the
    // section; selecting one + applying points the scene node at it via
    // SetNodeSceneSource (Instance / consumeMode 0) and shows the pick optimistically;
    // Clear sends id 0 and drops the reference.
    [Fact]
    public void InspectorReferencesAndClearsSceneSourceSubtreeThroughEngineSession()
    {
        var editorSession = new RecordingEditorSession();
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                assetGraph: new EngineAssetGraphSnapshotResponse
                {
                    Ok = true,
                    Snapshot = new EngineAssetGraphSnapshot
                    {
                        Nodes =
                        [
                            // The graftable source the picker should offer. Its
                            // TypeName is the shared asset-type label "Scene"; the
                            // GLB schema is identified by SchemaLabel "e7000711"
                            // (schema_tail of kSceneFromGLBSchema 0xF11ECA55E7000711).
                            new EngineAssetGraphNode
                            {
                                Id = 42,
                                Type = 80,
                                TypeName = "Scene",
                                Schema = "e7000711",
                                DisplayName = "tank scene",
                                CompileStatus = "ready",
                            },
                            // A Scene-from-JSON node: same asset-type name "Scene",
                            // different schema -> must be excluded (the old TypeName
                            // filter would have wrongly matched neither/both).
                            new EngineAssetGraphNode
                            {
                                Id = 9,
                                Type = 80,
                                TypeName = "Scene",
                                Schema = "e7000710",
                                DisplayName = "Scene from JSON",
                                CompileStatus = "ready",
                            },
                            // A non-scene node that must be filtered out.
                            new EngineAssetGraphNode
                            {
                                Id = 7,
                                Type = 41,
                                TypeName = "Renderable",
                                Schema = "e7000707",
                                DisplayName = "renderable",
                                CompileStatus = "ready",
                            },
                        ],
                    },
                },
                scene: SceneSnapshot(
                    Node(
                        "root",
                        children:
                        [
                            Node("host", parentId: "root", visible: true),
                        ]))),
            editorSession: editorSession);

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        var host = Assert.Single(root.Children, n => n.Id == "host");

        // Selecting the scene node threads only the Scene-from-GLB node into the
        // picker (scene-from-JSON and the renderable are excluded), and nothing is
        // referenced yet. The section starts hidden — it is attached on demand.
        viewModel.SceneTree.SelectNode(host);
        var option = Assert.Single(viewModel.Inspector.AvailableSceneSources);
        Assert.Equal(42ul, option.Id);
        Assert.Equal("tank scene", option.Label);
        Assert.True(viewModel.Inspector.HasAvailableSceneSources);
        Assert.False(viewModel.Inspector.HasSubtreeSection);
        Assert.False(viewModel.Inspector.HasSubtreeReference);
        Assert.Equal("(none)", viewModel.Inspector.SubtreeReferenceDisplay);

        // "Add Component → subtree_from_asset" reveals the picker section without
        // calling the generic add-component verb (mirrors renderable).
        viewModel.Inspector.AddComponentCommand.Execute("subtree_from_asset");
        Assert.True(viewModel.Inspector.HasSubtreeSection);
        Assert.Empty(editorSession.AddedComponents);

        // Picking a Scene-from-GLB node in the combo applies immediately (no Apply
        // button): the node is pointed at that graph node as an Instance subtree
        // source and the pick shows optimistically.
        viewModel.Inspector.SelectedSceneSourceOption = option;

        var referenced = Assert.Single(editorSession.SceneSources);
        Assert.Equal("host", referenced.NodeId);
        Assert.Equal(42ul, referenced.AssetGraphNodeId);
        Assert.Equal(0u, referenced.ConsumeMode);   // WZ_SCENE_SOURCE_INSTANCE
        Assert.True(viewModel.Inspector.HasSubtreeReference);
        Assert.Equal(
            "Referencing: tank scene",
            viewModel.Inspector.SubtreeReferenceDisplay);

        // The ✕ removes the component: id-0 clear signal, the optimistic reference is
        // dropped, and the section is hidden (re-attach via Add Component).
        viewModel.Inspector.RemoveSubtreeComponentCommand.Execute(null);

        var cleared = editorSession.SceneSources[^1];
        Assert.Equal("host", cleared.NodeId);
        Assert.Equal(0ul, cleared.AssetGraphNodeId);   // 0 = clear
        Assert.Equal(0u, cleared.ConsumeMode);
        Assert.False(viewModel.Inspector.HasSubtreeReference);
        Assert.False(viewModel.Inspector.HasSubtreeSection);
        Assert.Null(viewModel.Inspector.SelectedSceneSourceOption);
    }

    // issue #213 piece 2 (filter fix): the "Subtree from asset" picker matches
    // Scene-from-GLB nodes by the stable schema discriminator (SchemaLabel
    // "e7000711"), regardless of the asset-type name "Scene" it shares with
    // scene-from-JSON, and falls back to the deterministic DisplayName "Scene from
    // GLB" when present. Non-GLB nodes (renderable, scene-from-JSON) are excluded.
    [Fact]
    public void SubtreeFromAssetPickerMatchesSceneFromGlbBySchemaNotTypeName()
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
                            // Scene-from-GLB by schema label; carries an authored
                            // name, so DisplayName is NOT "Scene from GLB" — the
                            // schema match is what includes it.
                            new EngineAssetGraphNode
                            {
                                Id = 1u,
                                TypeName = "Scene",
                                Schema = "e7000711",
                                DisplayName = "renamed tank",
                            },
                            // Scene-from-GLB matched via the DisplayName fallback
                            // (e.g. a future/altered schema label): no name param =>
                            // DisplayName is the schema's "Scene from GLB".
                            new EngineAssetGraphNode
                            {
                                Id = 2u,
                                TypeName = "Scene",
                                Schema = "deadbeef",
                                DisplayName = "Scene from GLB",
                            },
                            // Scene-from-JSON: same asset-type name "Scene", a
                            // different schema and display => excluded.
                            new EngineAssetGraphNode
                            {
                                Id = 3u,
                                TypeName = "Scene",
                                Schema = "e7000710",
                                DisplayName = "Scene from JSON",
                            },
                            // A renderable => excluded.
                            new EngineAssetGraphNode
                            {
                                Id = 4u,
                                TypeName = "Renderable",
                                Schema = "e7000707",
                                DisplayName = "renderable",
                            },
                        ],
                    },
                },
                scene: SceneSnapshot(Node("host", visible: true))),
            editorSession: new RecordingEditorSession());

        var host = Assert.Single(viewModel.SceneTree.Nodes);
        viewModel.SceneTree.SelectNode(host);

        // Exactly the two Scene-from-GLB nodes (by schema and by display fallback),
        // never the scene-from-JSON or the renderable.
        var ids = viewModel.Inspector.AvailableSceneSources
            .Select(o => o.Id)
            .OrderBy(i => i)
            .ToList();
        Assert.Equal([1ul, 2ul], ids);
    }

    // issue #213 piece 2 (reveal): "Add Component → subtree_from_asset" reveals the
    // picker section by setting HasSubtreeSection, WITHOUT calling the generic
    // add-component verb (mirroring how renderable reveals its section), and the
    // section starts hidden on a freshly inspected node.
    [Fact]
    public void AddComponentSubtreeFromAssetRevealsPickerSectionWithoutVerb()
    {
        var session = new RecordingEditorSession();
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(scene: SceneSnapshot(Node("host", visible: true))),
            editorSession: session);

        var host = Assert.Single(viewModel.SceneTree.Nodes);
        viewModel.SceneTree.SelectNode(host);

        // Hidden until attached (parallel to HasRenderableReference for a node with
        // no renderable).
        Assert.False(viewModel.Inspector.HasSubtreeSection);

        viewModel.Inspector.AddComponentCommand.Execute("subtree_from_asset");

        Assert.True(viewModel.Inspector.HasSubtreeSection);
        // The engine rejects the generic verb for this kind, so it is never called.
        Assert.Empty(session.AddedComponents);
        Assert.DoesNotContain(
            viewModel.Inspector.Components,
            c => c.Kind == "subtree_from_asset");
    }

    // Smoke: the GLB scene-source host verb is reachable through the live v23 DLL
    // (issue #213 Phase 3a). With no viewport runtime the native session reports a
    // no-op success — this proves the P/Invoke + session/client wiring round-trips
    // against the real engine ABI without throwing. Skips if the DLL isn't built.
    [Fact]
    public void NativeEngineClientSetGlbSceneSourceVerbWhenEngineAbiIsBuilt()
    {
        var abiPath = WozzitsEngineNativeClient.ResolveDefaultAbiPath();
        if (!File.Exists(abiPath))
        {
            return;
        }

        var projectRoot = Path.Combine(
            Path.GetTempPath(),
            "wozzits_editor_glb_verb_" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(Path.Combine(projectRoot, ".wozzits"));
            File.WriteAllText(
                Path.Combine(projectRoot, ".wozzits", "project.json"),
                """
                {
                  "schema": "wozzits.project.v1",
                  "formatVersion": 1,
                  "name": "glb_verb_smoke",
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
            var editorSession =
                Assert.IsAssignableFrom<IWozzitsEngineEditorSession>(session);

            // No runtime was started (startRuntime defaults false), so the verb is
            // a no-op success — but it exercises the live DLL entry point.
            var set = editorSession.SetNodeGlbSceneSource(
                "host", "gltf/tank1.glb", sceneIndex: 0u, consumeMode: 0u);
            Assert.True(set.Ok, set.Error);

            // The empty-path clear form is equally reachable.
            var clear = editorSession.SetNodeGlbSceneSource(
                "host", string.Empty, sceneIndex: 0u, consumeMode: 0u);
            Assert.True(clear.Ok, clear.Error);
        }
        finally
        {
            try
            {
                Directory.Delete(projectRoot, recursive: true);
            }
            catch
            {
                // Best-effort cleanup of the temp project.
            }
        }
    }

    // Smoke: the "Subtree from asset" host verb (set_node_scene_source, issue #213
    // piece 2) is reachable through the live v25 DLL. With no viewport runtime the
    // native session reports a no-op success — this proves the new P/Invoke +
    // session/client wiring marshals and the entry point resolves against the real
    // engine ABI without throwing. Skips if the DLL isn't built.
    [Fact]
    public void NativeEngineClientSetSceneSourceVerbWhenEngineAbiIsBuilt()
    {
        var abiPath = WozzitsEngineNativeClient.ResolveDefaultAbiPath();
        if (!File.Exists(abiPath))
        {
            return;
        }

        var projectRoot = Path.Combine(
            Path.GetTempPath(),
            "wozzits_editor_scene_source_verb_" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(Path.Combine(projectRoot, ".wozzits"));
            File.WriteAllText(
                Path.Combine(projectRoot, ".wozzits", "project.json"),
                """
                {
                  "schema": "wozzits.project.v1",
                  "formatVersion": 1,
                  "name": "scene_source_verb_smoke",
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
            var editorSession =
                Assert.IsAssignableFrom<IWozzitsEngineEditorSession>(session);

            // No runtime was started (startRuntime defaults false), so the verb is
            // a no-op success — but it exercises the live DLL entry point.
            var set = editorSession.SetNodeSceneSource(
                "host", assetGraphNodeId: 42u, consumeMode: 0u);
            Assert.True(set.Ok, set.Error);

            // The id-0 clear form is equally reachable.
            var clear = editorSession.SetNodeSceneSource(
                "host", assetGraphNodeId: 0u, consumeMode: 0u);
            Assert.True(clear.Ok, clear.Error);
        }
        finally
        {
            try
            {
                Directory.Delete(projectRoot, recursive: true);
            }
            catch
            {
                // Best-effort cleanup of the temp project.
            }
        }
    }

    // The read-only GLB hierarchy import (issue #213 Phase 3b-1) round-trips
    // through the live v24 DLL: it imports tank1.glb (staged next to the engine
    // DLL) and decodes the model. Also exercises a bad path -> Ok=false. The
    // layout self-check (now v25) runs on first P/Invoke. Skips if the DLL or the
    // staged GLB isn't built.
    [Fact]
    public void NativeEngineClientImportsGlbSceneHierarchyWhenEngineAbiIsBuilt()
    {
        var abiPath = WozzitsEngineNativeClient.ResolveDefaultAbiPath();
        // Resources are staged next to the engine DLL under
        // build/<preset>/resources/gltf/tank1.glb.
        var glbPath = Path.Combine(
            Path.GetDirectoryName(abiPath) ?? string.Empty,
            "resources",
            "gltf",
            "tank1.glb");

        if (!File.Exists(abiPath) || !File.Exists(glbPath))
        {
            return;
        }

        var client = new WozzitsEngineNativeClient();

        var hierarchy = client.ImportGlbSceneHierarchy(glbPath, sceneIndex: 0u);
        Assert.True(hierarchy.Ok, hierarchy.Error);
        Assert.Equal("Scene", hierarchy.SceneName);
        Assert.Equal(0u, hierarchy.SceneIndex);

        // tank1.glb scene 0 is a single chain body -> turret -> gun, all meshes.
        Assert.Equal(3, hierarchy.Components.Count);
        var body = Assert.Single(hierarchy.Components, c => c.Id == "body");
        Assert.Null(body.ParentId);
        Assert.True(body.HasMesh);
        var turret = Assert.Single(hierarchy.Components, c => c.Id == "turret");
        Assert.Equal("body", turret.ParentId);
        var gun = Assert.Single(hierarchy.Components, c => c.Id == "gun");
        Assert.Equal("turret", gun.ParentId);

        // A bad path returns a well-formed Ok=false model (never throws).
        var missing = client.ImportGlbSceneHierarchy(
            Path.Combine(
                Path.GetDirectoryName(abiPath) ?? string.Empty,
                "resources",
                "gltf",
                "does_not_exist.glb"),
            sceneIndex: 0u);
        Assert.False(missing.Ok);
        Assert.False(string.IsNullOrWhiteSpace(missing.Error));
    }

    [Fact]
    public async Task RebuildBehaviorsSkipsReloadWhenNoBehaviorProject()
    {
        var projectDir = Path.Combine(
            Path.GetTempPath(),
            "wz-rebuild-vm-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(projectDir);
        try
        {
            var session = new RecordingEditorSession();
            var viewModel = new MainWindowViewModel(
                ProjectSnapshot(),
                editorSession: session,
                projectDirectory: projectDir);

            await viewModel.RebuildBehaviorsCommand.ExecuteAsync(null);

            // The temp project has no behavior/CMakeLists.txt, so the build is
            // skipped and the engine is never asked to reload (nothing was built).
            Assert.Equal(0, session.ReloadBehaviorModulesCount);
        }
        finally
        {
            Directory.Delete(projectDir, recursive: true);
        }
    }

    [Fact]
    public void InspectorOffersImportedBehaviorModulesAndAddsSelected()
    {
        var session = new RecordingEditorSession
        {
            BehaviorModuleCatalog = ["move_up_on_frame", "spin"],
            NextAddedBehaviorId = "behavior.1",
        };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(scene: SceneSnapshot(Node("root"))),
            editorSession: session);

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        viewModel.SceneTree.SelectNode(root);

        // The "+" add menu lists the engine's imported behavior modules.
        Assert.Equal(
            new[] { "move_up_on_frame", "spin" },
            viewModel.Inspector.AvailableBehaviorModules.Select(o => o.Module));
        Assert.True(viewModel.Inspector.HasAvailableBehaviorModules);

        // Choosing one binds that module through the engine session and mints a
        // local row so the selection stays stable.
        var spin = viewModel.Inspector.AvailableBehaviorModules
            .First(o => o.Module == "spin");
        spin.AddCommand.Execute(null);

        var added = Assert.Single(session.AddedBehaviors);
        Assert.Equal("root", added.NodeId);
        Assert.Equal("spin", added.Module);
        Assert.Contains(viewModel.Inspector.Behaviors, b => b.Module == "spin");
    }

    [Fact]
    public void RefreshBehaviorModuleCatalogPicksUpModulesLoadedAfterSelection()
    {
        // The engine loads behavior modules asynchronously, so a node can be
        // selected before any are registered (or before a rebuild registers more).
        var session = new RecordingEditorSession { BehaviorModuleCatalog = [] };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(scene: SceneSnapshot(Node("root"))),
            editorSession: session);

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        viewModel.SceneTree.SelectNode(root);
        Assert.False(viewModel.Inspector.HasAvailableBehaviorModules);

        // The engine finishes loading (or a rebuild registers modules); opening
        // the "+" menu re-queries the catalog, so they now appear without needing
        // to reselect the node.
        session.BehaviorModuleCatalog = ["tank_controller"];
        viewModel.Inspector.RefreshBehaviorModuleCatalog();

        Assert.True(viewModel.Inspector.HasAvailableBehaviorModules);
        Assert.Equal(
            new[] { "tank_controller" },
            viewModel.Inspector.AvailableBehaviorModules.Select(o => o.Module));
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

    // issue #213: the grafted "Subtree from asset" children (which live only in
    // the runtime, not scene.json) are merged under their authored host, marked
    // instanced, and a re-merge does not duplicate them.
    [Fact]
    public void SceneTreeMergesGraftedNodesUnderHost()
    {
        var session = new RecordingEditorSession
        {
            // body (root of the graft) parents to the host "tank_host"; turret
            // nests under body. The graft roots carry the host id as ParentId.
            GraftedScene = new EngineSceneSnapshot
            {
                Roots =
                [
                    new EngineSceneNode
                    {
                        Id = "tank_host/body",
                        DisplayName = "body",
                        ParentId = "tank_host",
                        Kind = "renderable",
                        Children =
                        [
                            new EngineSceneNode
                            {
                                Id = "tank_host/body/turret",
                                DisplayName = "turret",
                                ParentId = "tank_host/body",
                                Kind = "node",
                            },
                        ],
                    },
                ],
            },
        };
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
                        Id = "tank_host",
                        DisplayName = "tank_host",
                        Kind = "node",
                    },
                ],
            },
        });

        // LoadSnapshot no longer auto-merges; the host trigger does it.
        sceneTree.MergeGraftedNodes();

        var host = Assert.Single(sceneTree.Nodes);
        Assert.Equal("tank_host", host.Id);
        Assert.False(host.IsInstanced);  // the authored host is not a graft

        var body = Assert.Single(host.Children);
        Assert.Equal("tank_host/body", body.Id);
        Assert.True(body.IsInstanced);   // grafted => marked instanced
        var turret = Assert.Single(body.Children);
        Assert.Equal("tank_host/body/turret", turret.Id);
        Assert.True(turret.IsInstanced); // the whole sub-tree is instanced

        // A re-merge (e.g. after a re-assign) drops the prior graft and re-adds it
        // exactly once — no duplicate body under the host.
        sceneTree.MergeGraftedNodes();
        var hostAfter = Assert.Single(sceneTree.Nodes);
        var bodyAfter = Assert.Single(hostAfter.Children);
        Assert.Equal("tank_host/body", bodyAfter.Id);
    }

    // issue #213: a graft whose host is not in the tree is skipped (defensive), and
    // an empty grafted result leaves the authored tree untouched.
    [Fact]
    public void SceneTreeMergeIgnoresUnknownHostAndEmptyGrafts()
    {
        var session = new RecordingEditorSession
        {
            GraftedScene = new EngineSceneSnapshot
            {
                Roots =
                [
                    new EngineSceneNode
                    {
                        Id = "ghost/child",
                        ParentId = "ghost",  // no such host in the tree
                        Kind = "node",
                    },
                ],
            },
        };
        var sceneTree = new SceneTreeEditorPaneViewModel(session);
        sceneTree.LoadSnapshot(new EngineSceneSnapshotResponse
        {
            Ok = true,
            Snapshot = new EngineSceneSnapshot
            {
                Roots = [new EngineSceneNode { Id = "root", Kind = "node" }],
            },
        });

        sceneTree.MergeGraftedNodes();

        // Unknown host => nothing merged; the authored root stands alone.
        var root = Assert.Single(sceneTree.Nodes);
        Assert.Empty(root.Children);

        // An empty graft result is a clean no-op too.
        session.GraftedScene = new EngineSceneSnapshot();
        sceneTree.MergeGraftedNodes();
        Assert.Empty(Assert.Single(sceneTree.Nodes).Children);
    }

    // issue #213: assigning a "Subtree from asset" reference in the inspector
    // re-merges the runtime's grafted children into the scene tree (the
    // MainWindowViewModel wiring), so they appear under the host without a reload.
    [Fact]
    public void AssigningSceneSourceMergesGraftedChildrenIntoTree()
    {
        var session = new RecordingEditorSession();
        var assetGraph = new EngineAssetGraphSnapshotResponse
        {
            Ok = true,
            Snapshot = new EngineAssetGraphSnapshot
            {
                Nodes =
                [
                    // Scene-from-GLB node, identified by its schema label.
                    new EngineAssetGraphNode
                    {
                        Id = 5u,
                        TypeName = "Scene",
                        Schema = "e7000711",
                        DisplayName = "tank scene",
                    },
                ],
            },
        };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                assetGraph: assetGraph,
                scene: SceneSnapshot(new EngineSceneNode
                {
                    Id = "host",
                    DisplayName = "host",
                    Kind = "node",
                    Visible = true,
                })),
            editorSession: session,
            projectDirectory: Path.Combine(Path.GetTempPath(), "wz-graft-merge-vm"));

        var host = Assert.Single(viewModel.SceneTree.Nodes);
        Assert.Empty(host.Children);  // nothing grafted yet

        // Selecting the host threads the "Scene from GLB" node into the picker.
        viewModel.SceneTree.SelectNode(host);
        var option = Assert.Single(viewModel.Inspector.AvailableSceneSources);

        // The runtime now reports a graft for the host (as it would after the
        // engine applies the assignment on its next frame).
        session.GraftedScene = new EngineSceneSnapshot
        {
            Roots =
            [
                new EngineSceneNode
                {
                    Id = "host/body",
                    DisplayName = "body",
                    ParentId = "host",
                    Kind = "renderable",
                },
            ],
        };

        // Picking applies immediately (apply-on-select), firing SceneSourceChanged.
        viewModel.Inspector.SelectedSceneSourceOption = option;

        // The assignment fired SceneSourceChanged, which re-merged the grafts.
        var hostAfter = Assert.Single(viewModel.SceneTree.Nodes);
        var body = Assert.Single(hostAfter.Children);
        Assert.Equal("host/body", body.Id);
        Assert.True(body.IsInstanced);
    }

    [Fact]
    public void SceneTreeReparentMovesNodeAndRejectsCycle()
    {
        var session = new RecordingEditorSession();
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
                        Id = "a",
                        Kind = "node",
                        Children =
                        [
                            new EngineSceneNode
                            {
                                Id = "b",
                                ParentId = "a",
                                Kind = "node",
                            },
                        ],
                    },
                    new EngineSceneNode { Id = "c", Kind = "node" },
                ],
            },
        });

        var a = Assert.Single(sceneTree.Nodes, n => n.Id == "a");
        var b = Assert.Single(a.Children, n => n.Id == "b");
        var c = Assert.Single(sceneTree.Nodes, n => n.Id == "c");

        // Move c under b.
        sceneTree.Reparent(c, b);
        Assert.Equal(("c", "b"), Assert.Single(session.Reparents));
        Assert.Single(sceneTree.Nodes);          // only a remains at the top level
        Assert.Same(c, Assert.Single(b.Children));
        Assert.Equal("b", c.ParentId);

        // Reparenting a under c (now a's descendant) is a cycle -> rejected.
        sceneTree.Reparent(a, c);
        Assert.Single(session.Reparents);         // no second engine call
        Assert.Contains(a, sceneTree.Nodes);      // a still at the top level
    }

    [Fact]
    public void SceneTreeRemoveDropsNodeAndClearsSelection()
    {
        var session = new RecordingEditorSession();
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
                        Id = "a",
                        Kind = "node",
                        Children =
                        [
                            new EngineSceneNode
                            {
                                Id = "b",
                                ParentId = "a",
                                Kind = "node",
                            },
                        ],
                    },
                ],
            },
        });

        var a = Assert.Single(sceneTree.Nodes);
        var b = Assert.Single(a.Children);
        sceneTree.SelectNode(b);

        sceneTree.Remove(a);  // removes a and its child b

        Assert.Equal("a", Assert.Single(session.Removed));
        Assert.Empty(sceneTree.Nodes);
        Assert.Null(sceneTree.SelectedNode);  // selection was in the removed subtree
    }

    [Fact]
    public void AssetBrowserSearchFiltersTypesByNameAndSchema()
    {
        var browser = new AssetBrowserPaneViewModel();
        browser.Load(new EngineAssetCatalogResponse
        {
            Ok = true,
            Entries =
            [
                new EngineAssetCatalogEntry
                {
                    Type = 1,
                    TypeName = "Mesh",
                    Category = "Geometry",
                    Schemas =
                    [
                        new EngineAssetCatalogSchema { Schema = 10, Label = "Cube" },
                    ],
                },
                new EngineAssetCatalogEntry
                {
                    Type = 2,
                    TypeName = "Light",
                    Category = "Lighting",
                    Schemas =
                    [
                        new EngineAssetCatalogSchema { Schema = 20, Label = "Point" },
                    ],
                },
            ],
        });

        Assert.Equal(2, browser.Types.Count);

        browser.SearchText = "mesh";   // matches a type name (case-insensitive)
        Assert.Equal("Mesh", Assert.Single(browser.Types).TypeName);

        browser.SearchText = "point";  // matches a schema label, not the type name
        Assert.Equal("Light", Assert.Single(browser.Types).TypeName);

        browser.SearchText = "zzz";    // nothing matches
        Assert.Empty(browser.Types);
        Assert.True(browser.HasNoTypes);

        browser.SearchText = "";       // cleared -> all visible again
        Assert.Equal(2, browser.Types.Count);
    }

    [Fact]
    public void NativeEngineClientSurfacesGlbSceneSourceWhenEngineAbiIsBuilt()
    {
        var abiPath = WozzitsEngineNativeClient.ResolveDefaultAbiPath();
        // The fixture project is staged next to the engine DLL under
        // build/<preset>/resources/projects/glb_scene_source_fixture.
        var fixtureProject = Path.Combine(
            Path.GetDirectoryName(abiPath) ?? string.Empty,
            "resources",
            "projects",
            "glb_scene_source_fixture");

        if (!File.Exists(abiPath) || !Directory.Exists(fixtureProject))
        {
            return;
        }

        var response = new WozzitsEngineNativeClient()
            .LoadProjectSnapshot(fixtureProject);

        Assert.True(response.IsValid, response.Error);
        Assert.True(response.Scene.Ok, response.Scene.Error);

        // Find the tank_host node anywhere in the tree (it sits under root).
        var host = FindSceneNode(
            response.Scene.Snapshot.Roots,
            node => node.Id == "tank_host");
        Assert.NotNull(host);

        var sceneSource = host!.SceneSource;
        Assert.NotNull(sceneSource);
        Assert.Equal("glb", sceneSource!.Kind);
        Assert.Equal("gltf/tank1.glb", sceneSource.Path);
        Assert.Equal("instance", sceneSource.ConsumeMode);
        Assert.Equal(0u, sceneSource.SceneIndex);
        Assert.Equal(1u, sceneSource.StyleOverrideCount);
        Assert.True(sceneSource.HasBaseStyle);

        // Phase 3b-2: the base style + the one per-mesh override round-trip through
        // the v25 snapshot. The fixture's base style is wireframe-on/surface-off;
        // its mesh-index-1 override is surface-on/wireframe-off.
        Assert.True(sceneSource.BaseStyle.WireframeEnabled);
        Assert.False(sceneSource.BaseStyle.SurfaceEnabled);
        var fixtureOverride = Assert.Single(sceneSource.StyleOverrides);
        Assert.Equal(1u, fixtureOverride.MeshIndex);
        Assert.True(fixtureOverride.Style.SurfaceEnabled);
        Assert.False(fixtureOverride.Style.WireframeEnabled);

        // A node without a glb_scene_source block must not carry one.
        var plain = FindSceneNode(
            response.Scene.Snapshot.Roots,
            node => node.Id == "root");
        Assert.NotNull(plain);
        Assert.Null(plain!.SceneSource);
    }

    private static EngineSceneNode? FindSceneNode(
        IEnumerable<EngineSceneNode> nodes,
        Func<EngineSceneNode, bool> predicate)
    {
        foreach (var node in nodes)
        {
            if (predicate(node))
            {
                return node;
            }

            var found = FindSceneNode(node.Children, predicate);
            if (found is not null)
            {
                return found;
            }
        }

        return null;
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

        public List<(string NodeId, string NewParentId)> Reparents { get; } = [];

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

        public List<(string NodeId, string Module)> AddedBehaviors { get; } = [];

        public string NextAddedBehaviorId { get; set; } = "behavior.1";

        public List<(string NodeId, string BindingId)> RemovedBehaviors { get; } = [];

        public EngineAddSceneNodeResponse AddNodeBehavior(string nodeId, string module)
        {
            AddedBehaviors.Add((nodeId, module));
            return new EngineAddSceneNodeResponse
            {
                Ok = true,
                NodeId = NextAddedBehaviorId,
            };
        }

        public EngineMutationResponse RemoveNodeBehavior(string nodeId, string bindingId)
        {
            RemovedBehaviors.Add((nodeId, bindingId));
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse SetNodeBehaviorEnabled(
            string nodeId,
            string bindingId,
            bool enabled)
        {
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse SetNodeBehaviorFields(
            string nodeId,
            string bindingId,
            string label,
            string module)
        {
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse SetNodeBehaviorEvents(
            string nodeId,
            string bindingId,
            string events)
        {
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse SetNodeBehaviorConfig(
            string nodeId,
            string bindingId,
            string key,
            string kind,
            string value)
        {
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse ClearNodeBehaviorConfig(
            string nodeId,
            string bindingId,
            string key)
        {
            return new EngineMutationResponse { Ok = true };
        }

        public List<(string NodeId, string Kind)> AddedComponents { get; } = [];

        public List<(string NodeId, string Kind)> RemovedComponents { get; } = [];

        public EngineMutationResponse AddNodeComponent(string nodeId, string kind)
        {
            AddedComponents.Add((nodeId, kind));
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse RemoveNodeComponent(string nodeId, string kind)
        {
            RemovedComponents.Add((nodeId, kind));
            return new EngineMutationResponse { Ok = true };
        }

        public List<(string NodeId, ulong AssetGraphNodeId)> RenderableAssets { get; } = [];

        public EngineMutationResponse SetNodeRenderableAsset(
            string nodeId,
            ulong assetGraphNodeId)
        {
            RenderableAssets.Add((nodeId, assetGraphNodeId));
            return new EngineMutationResponse { Ok = true };
        }

        public List<(string NodeId, ulong AssetGraphNodeId, uint ConsumeMode)>
            SceneSources { get; } = [];

        public EngineMutationResponse SetNodeSceneSource(
            string nodeId,
            ulong assetGraphNodeId,
            uint consumeMode)
        {
            SceneSources.Add((nodeId, assetGraphNodeId, consumeMode));
            return new EngineMutationResponse { Ok = true };
        }

        public List<(string NodeId, string GlbPath, uint SceneIndex, uint ConsumeMode)>
            GlbSceneSources { get; } = [];

        public EngineMutationResponse SetNodeGlbSceneSource(
            string nodeId,
            string glbPath,
            uint sceneIndex,
            uint consumeMode)
        {
            GlbSceneSources.Add((nodeId, glbPath, sceneIndex, consumeMode));
            return new EngineMutationResponse { Ok = true };
        }

        public record GlbStyleEdit(
            string NodeId,
            bool TargetBase,
            uint MeshIndex,
            bool SurfaceEnabled,
            float[]? SurfaceRgba,
            bool WireframeEnabled,
            float[]? WireframeRgba);

        public List<GlbStyleEdit> GlbComponentStyles { get; } = [];

        public List<(string NodeId, uint MeshIndex)> GlbStyleClears { get; } = [];

        public EngineMutationResponse SetNodeGlbComponentStyle(
            string nodeId,
            bool targetBase,
            uint meshIndex,
            bool surfaceEnabled,
            float[]? surfaceRgba,
            bool wireframeEnabled,
            float[]? wireframeRgba)
        {
            GlbComponentStyles.Add(new GlbStyleEdit(
                nodeId,
                targetBase,
                meshIndex,
                surfaceEnabled,
                surfaceRgba,
                wireframeEnabled,
                wireframeRgba));
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse ClearNodeGlbComponentStyle(
            string nodeId,
            uint meshIndex)
        {
            GlbStyleClears.Add((nodeId, meshIndex));
            return new EngineMutationResponse { Ok = true };
        }

        public List<(string Path, uint SceneIndex)> GlbHierarchyImports { get; } = [];

        public EngineGlbSceneHierarchy GlbHierarchy { get; set; } =
            new() { Ok = true };

        public EngineGlbSceneHierarchy ImportGlbSceneHierarchy(
            string absoluteGlbPath,
            uint sceneIndex)
        {
            GlbHierarchyImports.Add((absoluteGlbPath, sceneIndex));
            return GlbHierarchy;
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

        // The grafted-scene-nodes snapshot the editor merges into the tree
        // (issue #213). Defaults to empty (nothing grafted). LoadGraftedCount lets
        // a test assert the re-merge re-queried the runtime.
        public EngineSceneSnapshot GraftedScene { get; set; } = new();

        public int LoadGraftedCount { get; private set; }

        public EngineSceneSnapshot LoadGraftedSceneNodes()
        {
            LoadGraftedCount++;
            return GraftedScene;
        }

        public EngineMutationResponse ReparentNode(string nodeId, string newParentId)
        {
            Reparents.Add((nodeId, newParentId));
            return new EngineMutationResponse { Ok = true };
        }

        public List<string> Removed { get; } = [];

        public EngineMutationResponse RemoveNode(string nodeId)
        {
            Removed.Add(nodeId);
            return new EngineMutationResponse { Ok = true };
        }

        public int SaveSceneCount { get; private set; }

        public EngineMutationResponse SaveScene()
        {
            SaveSceneCount++;
            return new EngineMutationResponse { Ok = true };
        }

        public int ReloadBehaviorModulesCount { get; private set; }

        public EngineMutationResponse ReloadBehaviorModules()
        {
            ReloadBehaviorModulesCount++;
            return new EngineMutationResponse { Ok = true };
        }

        public IReadOnlyList<string> BehaviorModuleCatalog { get; set; } = [];

        public IReadOnlyList<string> GetBehaviorModuleCatalog()
        {
            return BehaviorModuleCatalog;
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
