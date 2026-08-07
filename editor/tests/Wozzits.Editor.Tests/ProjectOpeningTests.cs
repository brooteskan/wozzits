using System.Threading;
using Wozzits.Editor.Core.Projects;
using Wozzits.Editor.Core.Logging;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;
using Wozzits.Editor.ViewModels;
using Wozzits.Editor.ViewModels.EditorPanes;
using Wozzits.Editor.ViewModels.EditorPanes.Minds;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

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
            // Not running is a legitimate "no grafts", so Ok stays true (D3-P039).
            Assert.True(grafted.Ok);
            Assert.Empty(grafted.Snapshot.Roots);
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
                  "schema": "wozzits.scene_editor.assets.graph.v2",
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

    // "Export subtree as scene…": invoking the scene-tree command for a node calls
    // the session's export method with that node's authored id and the chosen
    // absolute path. The save-file dialog itself is GUI-only (driven in the view),
    // so the test exercises the command's call into the session given a path.
    [Fact]
    public void ExportSubtreeCallsSessionWithNodeIdAndChosenPath()
    {
        var editorSession = new RecordingEditorSession();
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                scene: SceneSnapshot(
                    Node(
                        "root",
                        children:
                        [
                            Node("tank", displayName: "tank", kind: "renderable"),
                        ]))),
            editorSession: editorSession);

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        var tank = Assert.Single(root.Children, n => n.Id == "tank");

        var outPath = @"D:\project\scenelets\tank.scene.json";
        viewModel.SceneTree.ExportSubtree(tank, outPath);

        var export = Assert.Single(editorSession.SubtreeExports);
        Assert.Equal("tank", export.RootNodeId);
        Assert.Equal(outPath, export.OutPath);

        // The suggested file name is derived from the node's display name.
        Assert.Equal(
            "tank.scene.json",
            SceneTreeEditorPaneViewModel.SuggestExportFileName(tank));
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

        // Visibility mirrors onto the tree node too (issue #213 fix): the node's
        // Visible reflects the edit, so re-selecting it shows the edited value
        // instead of reverting to the stale snapshot value.
        Assert.False(mesh.Visible);
        viewModel.SceneTree.SelectNode(camera);
        viewModel.SceneTree.SelectNode(mesh);
        Assert.False(viewModel.Inspector.NodeVisible);

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

    // Removing the "Renderable Asset" reference must stay removed when the node is
    // re-selected (the reported bug: it reappeared after refreshing the scene tree).
    // The remove verb cleared the engine + the optimistic flag but not the cached
    // tree-node VM, so Inspect re-derived HasRenderableReference from the stale
    // snapshot renderable on the next select. The fix mirrors the cleared renderable
    // onto the tree node (like the Visible fix), so it stays gone; applying a new
    // reference likewise survives reselect.
    [Fact]
    public void InspectorRenderableRemovalSurvivesReselect()
    {
        var editorSession = new RecordingEditorSession();
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                scene: SceneSnapshot(
                    Node(
                        "root",
                        children:
                        [
                            Node("other", parentId: "root", visible: true),
                            Node(
                                "tank",
                                parentId: "root",
                                visible: true,
                                renderableSource: new EngineSceneRenderableSource
                                {
                                    Kind = "asset-node",
                                    DisplayName = "tank renderable",
                                },
                                renderable: new EngineSceneRenderable
                                {
                                    AssetGraphNodeId = 7u,
                                }),
                        ]))),
            editorSession: editorSession);

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        var tank = Assert.Single(root.Children, n => n.Id == "tank");
        var other = Assert.Single(root.Children, n => n.Id == "other");

        // The node starts with a renderable reference shown.
        viewModel.SceneTree.SelectNode(tank);
        Assert.True(viewModel.Inspector.HasRenderableReference);

        // Remove it via the section ✕: the engine gets the id-0 clear and the
        // section hides.
        viewModel.Inspector.RemoveRenderableCommand.Execute(null);
        var cleared = Assert.Single(editorSession.RenderableAssets);
        Assert.Equal("tank", cleared.NodeId);
        Assert.Equal(0ul, cleared.AssetGraphNodeId);
        Assert.False(viewModel.Inspector.HasRenderableReference);

        // Re-selecting the node (e.g. after refreshing the scene tree) must keep it
        // removed — previously it came back from the stale cached snapshot.
        viewModel.SceneTree.SelectNode(other);
        viewModel.SceneTree.SelectNode(tank);
        Assert.False(viewModel.Inspector.HasRenderableReference);

        // Applying a new reference likewise survives a reselect.
        viewModel.Inspector.RenderableAssetGraphNodeId = "12";
        viewModel.Inspector.ApplyRenderableCommand.Execute(null);
        viewModel.SceneTree.SelectNode(other);
        viewModel.SceneTree.SelectNode(tank);
        Assert.True(viewModel.Inspector.HasRenderableReference);
        Assert.Equal("12", viewModel.Inspector.RenderableAssetGraphNodeId);
    }

    // The renderable-revert generalizes to the camera and the generic component rows
    // (proximity/collision/motion): adding or removing a component must survive a
    // reselect, not revert to the startup snapshot. Each mirrors the change onto the
    // cached tree-node VM (like Behaviors), so Inspect re-derives the right state.
    [Fact]
    public void InspectorComponentAddRemoveSurvivesReselect()
    {
        var editorSession = new RecordingEditorSession();
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                scene: SceneSnapshot(
                    Node(
                        "root",
                        children:
                        [
                            Node("other", parentId: "root", visible: true),
                            Node(
                                "node",
                                parentId: "root",
                                visible: true,
                                camera: new EngineSceneCamera { FieldOfViewY = 1.0 },
                                components: [Component("camera", "Camera")]),
                        ]))),
            editorSession: editorSession);

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        var node = Assert.Single(root.Children, n => n.Id == "node");
        var other = Assert.Single(root.Children, n => n.Id == "other");

        // Start with a camera and no proximity component.
        viewModel.SceneTree.SelectNode(node);
        Assert.True(viewModel.Inspector.HasCameraComponent);

        // Add a proximity component, then remove the camera.
        viewModel.Inspector.AddComponentCommand.Execute("proximity");
        viewModel.Inspector.RemoveCameraCommand.Execute(null);
        Assert.False(viewModel.Inspector.HasCameraComponent);
        Assert.Contains(
            viewModel.Inspector.Components,
            c => c.Kind == "proximity");

        // Reselect: the camera stays removed and the proximity component stays added
        // (previously both reverted to the snapshot).
        viewModel.SceneTree.SelectNode(other);
        viewModel.SceneTree.SelectNode(node);
        Assert.False(viewModel.Inspector.HasCameraComponent);
        Assert.Contains(
            viewModel.Inspector.Components,
            c => c.Kind == "proximity");

        // And removing the proximity component also survives a reselect.
        var proximity = viewModel.Inspector.Components.Single(c => c.Kind == "proximity");
        proximity.RemoveCommand.Execute(null);
        viewModel.SceneTree.SelectNode(other);
        viewModel.SceneTree.SelectNode(node);
        Assert.DoesNotContain(
            viewModel.Inspector.Components,
            c => c.Kind == "proximity");
    }

    // D3-C15. A component with its own parameter section must NOT also get a
    // generic row -- two controls for one component means removing it via the
    // generic row leaves the dedicated section live, and touching that section
    // silently re-creates what the user just removed. "environment" was missing
    // from SetComponentFields' skip list while its atmosphere twin was present.
    // Asserted for the whole family so a future section-owned component that
    // forgets the list fails here rather than shipping the double control.
    [Theory]
    [InlineData("environment")]
    [InlineData("atmosphere")]
    [InlineData("collision")]
    [InlineData("motion")]
    [InlineData("motion_filter")]
    [InlineData("audio_source")]
    public void SectionOwnedComponentsDoNotAlsoGetAGenericRow(string kind)
    {
        // The component has to be in the node's SNAPSHOT component list, which is
        // what SetComponentFields iterates. Driving AddComponentCommand instead
        // makes this test vacuous -- it passes with the skip list neutered.
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                scene: SceneSnapshot(
                    Node(
                        "root",
                        children:
                        [
                            Node(
                                "node",
                                parentId: "root",
                                visible: true,
                                components: [Component(kind, kind)]),
                        ]))),
            editorSession: new RecordingEditorSession());

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        var node = Assert.Single(root.Children, n => n.Id == "node");

        viewModel.SceneTree.SelectNode(node);

        Assert.DoesNotContain(viewModel.Inspector.Components, c => c.Kind == kind);
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

    // issue #213: the "Render program" picker offers asset-graph nodes by their
    // OUTPUT asset type (RenderProgram = 1049), not schema label. Attaching the
    // component reveals the section without the generic verb; picking a program
    // applies it live via the dedicated verb; the ✕ clears it and hides the section.
    // (The geometry picker was dropped — geometry is intrinsic to a grafted GLB part
    // or supplied by a "Renderable" component.)
    [Fact]
    public void InspectorAuthorsRenderProgramThroughEngineSession()
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
                            // A render program (output type 1049) — the only kind the
                            // picker offers.
                            new EngineAssetGraphNode
                            {
                                Id = 10,
                                DisplayName = "lit program",
                                CompileStatus = "ready",
                                OutputPorts =
                                [
                                    new EngineAssetGraphPort { Type = 1049 },
                                ],
                            },
                            // A mesh node (537) — NOT a program, excluded.
                            new EngineAssetGraphNode
                            {
                                Id = 9,
                                DisplayName = "tank body mesh",
                                CompileStatus = "ready",
                                OutputPorts =
                                [
                                    new EngineAssetGraphPort { Type = 537 },
                                ],
                            },
                        ],
                    },
                },
                scene: SceneSnapshot(Node("host", visible: true))),
            editorSession: editorSession);

        var host = Assert.Single(viewModel.SceneTree.Nodes);
        viewModel.SceneTree.SelectNode(host);

        // The picker lists only the render program; the mesh node is excluded. The
        // section is hidden until attached.
        var program = Assert.Single(viewModel.Inspector.AvailableRenderPrograms);
        Assert.Equal(10ul, program.Id);
        Assert.Equal("lit program", program.Label);
        Assert.False(viewModel.Inspector.HasRenderProgramSection);

        // "Add Component → render_program" reveals the section without the generic
        // add-component verb (mirrors renderable/subtree).
        viewModel.Inspector.AddComponentCommand.Execute("render_program");
        Assert.True(viewModel.Inspector.HasRenderProgramSection);
        Assert.Empty(editorSession.AddedComponents);

        // Picking a program applies immediately via SetNodeRenderProgram.
        viewModel.Inspector.SelectedRenderProgramOption = program;
        var progEdit = Assert.Single(editorSession.RenderPrograms);
        Assert.Equal("host", progEdit.NodeId);
        Assert.Equal(10ul, progEdit.AssetGraphNodeId);
        Assert.Equal(
            "Referencing: lit program",
            viewModel.Inspector.RenderProgramReferenceDisplay);

        // The ✕ removes the component: the program is cleared (id 0) and the section
        // is hidden.
        viewModel.Inspector.RemoveRenderProgramComponentCommand.Execute(null);
        Assert.Equal(0ul, editorSession.RenderPrograms[^1].AssetGraphNodeId);
        Assert.False(viewModel.Inspector.HasRenderProgramSection);
        Assert.Null(viewModel.Inspector.SelectedRenderProgramOption);
        Assert.Equal("(none)", viewModel.Inspector.RenderProgramReferenceDisplay);
    }

    // Issue #230: the renderable-ingredients form is GENERATED from the wired
    // program's authored binding layout — one kind-filtered source picker per
    // declared SRV semantic (pulled_mesh_* rows are geometry-owned and skipped)
    // and one typed value editor per declared constant. Picking a source applies
    // SetNodeRenderableBinding live; editing a constant applies
    // SetNodeRenderableParam live; the ✕s clear/remove (id 0 / null).
    [Fact]
    public void InspectorGeneratesRenderableIngredientFormFromLayout()
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
                            // The custom render program (output 1049) the node wires.
                            new EngineAssetGraphNode
                            {
                                Id = 16,
                                DisplayName = "field program",
                                CompileStatus = "ready",
                                OutputPorts =
                                [
                                    new EngineAssetGraphPort { Type = 1049 },
                                ],
                            },
                            // Its authored binding layout (#227), wired on input
                            // port 2: pulled positions/indices (geometry-owned),
                            // a scalar-field texture row, and a float4 'tint'
                            // tail constant. Enum params surface as option
                            // LABELS (the snapshot's display form).
                            new EngineAssetGraphNode
                            {
                                Id = 15,
                                DisplayName = "field layout",
                                CompileStatus = "ready",
                                Params =
                                [
                                    new EngineAssetGraphParam
                                    {
                                        Name = "binding0_semantic",
                                        Value = "pulled_mesh_positions",
                                    },
                                    new EngineAssetGraphParam
                                    {
                                        Name = "binding1_semantic",
                                        Value = "pulled_mesh_indices",
                                    },
                                    new EngineAssetGraphParam
                                    {
                                        Name = "binding2_semantic",
                                        Value = "scalar_field_texture",
                                    },
                                    new EngineAssetGraphParam
                                    {
                                        Name = "binding2_kind",
                                        Value = "Texture SRV",
                                    },
                                    new EngineAssetGraphParam
                                    {
                                        Name = "const0_name",
                                        Value = "tint",
                                    },
                                    new EngineAssetGraphParam
                                    {
                                        Name = "const0_type",
                                        Value = "Float4",
                                    },
                                ],
                            },
                            // A scalar field (128) — a TEXTURE publisher, offered.
                            new EngineAssetGraphNode
                            {
                                Id = 17,
                                DisplayName = "height field",
                                CompileStatus = "ready",
                                OutputPorts =
                                [
                                    new EngineAssetGraphPort { Type = 128 },
                                ],
                            },
                            // A splat cloud (131) — a BUFFER publisher, excluded
                            // from the texture row's options.
                            new EngineAssetGraphNode
                            {
                                Id = 12,
                                DisplayName = "splats",
                                CompileStatus = "ready",
                                OutputPorts =
                                [
                                    new EngineAssetGraphPort { Type = 131 },
                                ],
                            },
                        ],
                        Edges =
                        [
                            new EngineAssetGraphEdge
                            {
                                Id = 1,
                                From = 15,
                                To = 16,
                                ToInputPort = 2,
                            },
                        ],
                    },
                },
                scene: SceneSnapshot(
                    Node("bound", visible: true, renderProgramNodeId: 16))),
            editorSession: editorSession);

        var bound = Assert.Single(viewModel.SceneTree.Nodes);
        viewModel.SceneTree.SelectNode(bound);

        // The section revealed from the resolved layout: ONE binding row (the
        // pulled_mesh_* rows are geometry-owned) offering only the texture
        // publisher, and ONE float4 constant row.
        Assert.True(viewModel.Inspector.HasRenderableIngredientsSection);
        var bindingRow = Assert.Single(viewModel.Inspector.RenderableBindingRows);
        Assert.Equal("scalar_field_texture", bindingRow.Semantic);
        Assert.Equal("texture", bindingRow.KindLabel);
        var sourceOption = Assert.Single(bindingRow.Options);
        Assert.Equal(17ul, sourceOption.Id);

        var constantRow = Assert.Single(viewModel.Inspector.RenderableConstantRows);
        Assert.Equal("tint", constantRow.Name);
        Assert.Equal(4, constantRow.Width);
        Assert.False(constantRow.IsOverridden);

        // Picking a source applies live (upsert of the semantic's row).
        bindingRow.SelectedOption = sourceOption;
        var bindingEdit = Assert.Single(editorSession.RenderableBindings);
        Assert.Equal("bound", bindingEdit.NodeId);
        Assert.Equal("scalar_field_texture", bindingEdit.Semantic);
        Assert.Equal(17ul, bindingEdit.AssetGraphNodeId);

        // Editing a constant component applies the full 4-float override live.
        constantRow.ValueX = "0.2";
        var paramEdit = editorSession.RenderableParams[^1];
        Assert.Equal("bound", paramEdit.NodeId);
        Assert.Equal("tint", paramEdit.Name);
        Assert.NotNull(paramEdit.Value);
        Assert.Equal(0.2f, paramEdit.Value![0], 3);
        Assert.True(constantRow.IsOverridden);

        // The ✕s: clearing the binding posts id 0; removing the override posts
        // null (the instance falls back to the recipe default).
        bindingRow.ClearCommand.Execute(null);
        Assert.Equal(0ul, editorSession.RenderableBindings[^1].AssetGraphNodeId);
        constantRow.RemoveCommand.Execute(null);
        Assert.Null(editorSession.RenderableParams[^1].Value);
        Assert.False(constantRow.IsOverridden);
    }

    // Terrain-stick track: the "Collision" picker offers asset-graph nodes by their
    // OUTPUT asset type (Collision = 150), not schema label. The section shows when
    // the node has a Collision component; picking a node applies SetNodeCollision
    // with the current constrain-movement flag, and toggling the flag re-applies it.
    // The ✕ removes the component (generic verb) and hides the section.
    [Fact]
    public void InspectorAuthorsCollisionThroughEngineSession()
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
                            // A collision asset (output type 150) — the only kind the
                            // picker offers.
                            new EngineAssetGraphNode
                            {
                                Id = 7,
                                DisplayName = "terrain collision",
                                CompileStatus = "ready",
                                OutputPorts =
                                [
                                    new EngineAssetGraphPort { Type = 150 },
                                ],
                            },
                            // A render program (1049) — NOT collision, excluded.
                            new EngineAssetGraphNode
                            {
                                Id = 10,
                                DisplayName = "lit program",
                                CompileStatus = "ready",
                                OutputPorts =
                                [
                                    new EngineAssetGraphPort { Type = 1049 },
                                ],
                            },
                        ],
                    },
                },
                scene: SceneSnapshot(Node("host", visible: true))),
            editorSession: editorSession);

        var host = Assert.Single(viewModel.SceneTree.Nodes);
        viewModel.SceneTree.SelectNode(host);

        // The picker lists only the collision node; the program is excluded. The
        // section is hidden until the component is added.
        var collision = Assert.Single(viewModel.Inspector.AvailableCollisionSources);
        Assert.Equal(7ul, collision.Id);
        Assert.Equal("terrain collision", collision.Label);
        Assert.False(viewModel.Inspector.HasCollisionComponent);

        // Adding the Collision component reveals the section AND calls the generic
        // add-component verb (collision is a real removable component, unlike
        // render_program).
        viewModel.Inspector.AddComponentCommand.Execute("collision");
        Assert.True(viewModel.Inspector.HasCollisionComponent);
        Assert.Contains(editorSession.AddedComponents, c => c.Kind == "collision");

        // Picking a collision node applies immediately with the current flag (off).
        viewModel.Inspector.SelectedCollisionOption = collision;
        var pick = Assert.Single(editorSession.Collisions);
        Assert.Equal("host", pick.NodeId);
        Assert.Equal(7u, pick.AssetGraphNodeId);
        Assert.False(pick.ConstrainMovement);
        Assert.Equal(
            "Referencing: terrain collision",
            viewModel.Inspector.CollisionReferenceDisplay);

        // Toggling constrain-movement re-applies with the same selection + new flag.
        viewModel.Inspector.CollisionConstrainMovement = true;
        var toggled = editorSession.Collisions[^1];
        Assert.Equal(7u, toggled.AssetGraphNodeId);
        Assert.True(toggled.ConstrainMovement);

        // The ✕ removes the component (generic verb) and hides the section.
        viewModel.Inspector.RemoveCollisionComponentCommand.Execute(null);
        Assert.Contains(editorSession.RemovedComponents, c => c.Kind == "collision");
        Assert.False(viewModel.Inspector.HasCollisionComponent);
        Assert.Null(viewModel.Inspector.SelectedCollisionOption);
        Assert.Equal("(none)", viewModel.Inspector.CollisionReferenceDisplay);
    }

    // Audio-track item 10: the "Audio Source" picker offers asset-graph nodes by
    // their OUTPUT asset type (AudioRenderable = 2142). The section shows when the
    // node has an audio_source component; picking a node applies SetNodeAudio-
    // Renderable with the stable node id, toggling auto_play/enabled applies
    // SetNodeAudioSourcePlay, and the ✕ removes the component.
    [Fact]
    public void InspectorAuthorsAudioSourceThroughEngineSession()
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
                            // An audio renderable (output type 2142) — the only kind
                            // the picker offers.
                            new EngineAssetGraphNode
                            {
                                Id = 9,
                                DisplayName = "beep renderable",
                                CompileStatus = "ready",
                                OutputPorts =
                                [
                                    new EngineAssetGraphPort { Type = 2142 },
                                ],
                            },
                            // A render program (1049) — NOT audio, excluded.
                            new EngineAssetGraphNode
                            {
                                Id = 10,
                                DisplayName = "lit program",
                                CompileStatus = "ready",
                                OutputPorts =
                                [
                                    new EngineAssetGraphPort { Type = 1049 },
                                ],
                            },
                        ],
                    },
                },
                scene: SceneSnapshot(Node("host", visible: true))),
            editorSession: editorSession);

        var host = Assert.Single(viewModel.SceneTree.Nodes);
        viewModel.SceneTree.SelectNode(host);

        // The picker lists only the audio renderable; the program is excluded.
        var renderable =
            Assert.Single(viewModel.Inspector.AvailableAudioRenderables);
        Assert.Equal(9ul, renderable.Id);
        Assert.Equal("beep renderable", renderable.Label);
        Assert.False(viewModel.Inspector.HasAudioSourceComponent);

        // Adding the AudioSource component reveals the section AND calls the
        // generic add-component verb.
        viewModel.Inspector.AddComponentCommand.Execute("audio_source");
        Assert.True(viewModel.Inspector.HasAudioSourceComponent);
        Assert.Contains(
            editorSession.AddedComponents, c => c.Kind == "audio_source");

        // Picking an audio renderable applies immediately with the STABLE node id.
        viewModel.Inspector.SelectedAudioRenderableOption = renderable;
        var pick = Assert.Single(editorSession.AudioRenderables);
        Assert.Equal("host", pick.NodeId);
        Assert.Equal(9ul, pick.AssetGraphNodeId);
        Assert.Equal(
            "Referencing: beep renderable",
            viewModel.Inspector.AudioRenderableReferenceDisplay);

        // Toggling a play flag re-applies the play policy.
        viewModel.Inspector.AudioSourceAutoPlay = false;
        var play = editorSession.AudioSourcePlays[^1];
        Assert.Equal("host", play.NodeId);
        Assert.False(play.AutoPlay);
        Assert.True(play.Enabled);

        // The ✕ removes the component (generic verb) and hides the section.
        viewModel.Inspector.RemoveAudioSourceComponentCommand.Execute(null);
        Assert.Contains(
            editorSession.RemovedComponents, c => c.Kind == "audio_source");
        Assert.False(viewModel.Inspector.HasAudioSourceComponent);
        Assert.Null(viewModel.Inspector.SelectedAudioRenderableOption);
        Assert.Equal(
            "(none)", viewModel.Inspector.AudioRenderableReferenceDisplay);
    }

    // Terrain-stick track: the "Motion" section shows when the node has a Motion
    // component; each terrain-constraint field edit applies live via
    // SetNodeMotionTerrain. The ✕ removes the component and hides the section.
    [Fact]
    public void InspectorAuthorsMotionTerrainThroughEngineSession()
    {
        var editorSession = new RecordingEditorSession();
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(scene: SceneSnapshot(Node("tank", visible: true))),
            editorSession: editorSession);

        var tank = Assert.Single(viewModel.SceneTree.Nodes);
        viewModel.SceneTree.SelectNode(tank);

        Assert.False(viewModel.Inspector.HasMotionComponent);

        // Adding the Motion component reveals the section + calls the generic verb.
        viewModel.Inspector.AddComponentCommand.Execute("motion");
        Assert.True(viewModel.Inspector.HasMotionComponent);
        Assert.Contains(editorSession.AddedComponents, c => c.Kind == "motion");

        // Toggling "terrain constrained" applies live.
        viewModel.Inspector.MotionTerrainConstrained = true;
        var first = editorSession.MotionTerrains[^1];
        Assert.Equal("tank", first.NodeId);
        Assert.True(first.TerrainConstrained);

        // Numeric fields parse + apply live.
        viewModel.Inspector.MotionRideHeight = "1.5";
        viewModel.Inspector.MotionFootprintRadius = "2.25";
        viewModel.Inspector.MotionAlignToSurface = true;
        viewModel.Inspector.MotionAlignmentStrength = "0.75";

        var latest = editorSession.MotionTerrains[^1];
        Assert.True(latest.TerrainConstrained);
        Assert.Equal(1.5f, latest.RideHeight);
        Assert.Equal(2.25f, latest.FootprintRadius);
        Assert.True(latest.AlignToSurface);
        Assert.Equal(0.75f, latest.AlignmentStrength);

        // The ✕ removes the component and hides the section.
        viewModel.Inspector.RemoveMotionComponentCommand.Execute(null);
        Assert.Contains(editorSession.RemovedComponents, c => c.Kind == "motion");
        Assert.False(viewModel.Inspector.HasMotionComponent);
    }

    // The terrain-stick sections reveal when the node already carries the component
    // (Collision / Motion are real, persisted components surfaced in the snapshot):
    // selecting such a node shows the section with no "Add Component" needed, and a
    // node without them shows neither.
    [Fact]
    public void InspectorRevealsCollisionAndMotionSectionsWhenComponentPresent()
    {
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                scene: SceneSnapshot(
                    Node(
                        "root",
                        children:
                        [
                            Node("plain", parentId: "root", visible: true),
                            Node(
                                "landscape",
                                parentId: "root",
                                visible: true,
                                components: [Component("collision", "Collision")]),
                            Node(
                                "vehicle",
                                parentId: "root",
                                visible: true,
                                components: [Component("motion", "Motion")]),
                        ]))));

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        var plain = Assert.Single(root.Children, n => n.Id == "plain");
        var landscape = Assert.Single(root.Children, n => n.Id == "landscape");
        var vehicle = Assert.Single(root.Children, n => n.Id == "vehicle");

        // The collision host reveals the Collision section (and not Motion); it is
        // NOT listed as a generic component row.
        viewModel.SceneTree.SelectNode(landscape);
        Assert.True(viewModel.Inspector.HasCollisionComponent);
        Assert.False(viewModel.Inspector.HasMotionComponent);
        Assert.DoesNotContain(
            viewModel.Inspector.Components, c => c.Kind == "collision");

        // The motion host reveals the Motion section (and not Collision).
        viewModel.SceneTree.SelectNode(vehicle);
        Assert.True(viewModel.Inspector.HasMotionComponent);
        Assert.False(viewModel.Inspector.HasCollisionComponent);
        Assert.DoesNotContain(
            viewModel.Inspector.Components, c => c.Kind == "motion");

        // A node with neither shows neither section.
        viewModel.SceneTree.SelectNode(plain);
        Assert.False(viewModel.Inspector.HasCollisionComponent);
        Assert.False(viewModel.Inspector.HasMotionComponent);
    }

    // Read-back gap fix: the Collision/Motion sections restore their PERSISTED field
    // values from the snapshot (the v27 snapshot surfaces them), so selecting a node
    // that carries the components shows the saved values — not defaults — and the
    // state survives a reselect (snapshot-driven, not a session toggle).
    [Fact]
    public void InspectorRestoresCollisionAndMotionFieldValuesFromSnapshot()
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
                            // The collision asset the node references (output type
                            // 150), so the picker can pre-select it by id.
                            new EngineAssetGraphNode
                            {
                                Id = 7,
                                DisplayName = "terrain collision",
                                CompileStatus = "ready",
                                OutputPorts =
                                [
                                    new EngineAssetGraphPort { Type = 150 },
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
                            Node("other", parentId: "root", visible: true),
                            // A landscape-rider carrying both components with
                            // persisted field values.
                            Node(
                                "vehicle",
                                parentId: "root",
                                visible: true,
                                components:
                                [
                                    Component("collision", "Collision"),
                                    Component("motion", "Motion"),
                                ],
                                collision: new EngineSceneNodeCollision
                                {
                                    CollisionAssetNodeId = 7u,
                                    ConstrainMovement = true,
                                },
                                motion: new EngineSceneNodeMotion
                                {
                                    TerrainConstrained = true,
                                    RideHeight = 2.5f,
                                    FootprintRadius = 0.75f,
                                    AlignToSurface = true,
                                    AlignmentStrength = 0.5f,
                                }),
                        ]))));

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        var vehicle = Assert.Single(root.Children, n => n.Id == "vehicle");
        var other = Assert.Single(root.Children, n => n.Id == "other");

        // Selecting the node restores the persisted Collision + Motion fields.
        viewModel.SceneTree.SelectNode(vehicle);
        Assert.True(viewModel.Inspector.HasCollisionComponent);
        Assert.Equal(7ul, viewModel.Inspector.SelectedCollisionOption?.Id);
        Assert.True(viewModel.Inspector.CollisionConstrainMovement);
        Assert.Equal(
            "Referencing: terrain collision",
            viewModel.Inspector.CollisionReferenceDisplay);

        Assert.True(viewModel.Inspector.HasMotionComponent);
        Assert.True(viewModel.Inspector.MotionTerrainConstrained);
        Assert.Equal("2.5", viewModel.Inspector.MotionRideHeight);
        Assert.Equal("0.75", viewModel.Inspector.MotionFootprintRadius);
        Assert.True(viewModel.Inspector.MotionAlignToSurface);
        Assert.Equal("0.5", viewModel.Inspector.MotionAlignmentStrength);

        // The values survive a reselect (snapshot-driven, not a one-shot reveal).
        viewModel.SceneTree.SelectNode(other);
        viewModel.SceneTree.SelectNode(vehicle);
        Assert.Equal(7ul, viewModel.Inspector.SelectedCollisionOption?.Id);
        Assert.True(viewModel.Inspector.CollisionConstrainMovement);
        Assert.Equal("2.5", viewModel.Inspector.MotionRideHeight);
        Assert.True(viewModel.Inspector.MotionAlignToSurface);
        Assert.Equal("0.5", viewModel.Inspector.MotionAlignmentStrength);

        // A node without the components shows neither section.
        viewModel.SceneTree.SelectNode(other);
        Assert.False(viewModel.Inspector.HasCollisionComponent);
        Assert.False(viewModel.Inspector.HasMotionComponent);
    }

    // The "Subtree from asset" and "Render program" sections are driven by the
    // node's PERSISTED state (issue #213, the v26 snapshot surfaces the authored
    // node ids): a node that already has them reveals + pre-selects them on select,
    // with no "Add Component" needed — the component is the confirmation of what the
    // node carries. The state survives a reselect (it reads from the snapshot, not a
    // session toggle).
    [Fact]
    public void InspectorRevealsPersistedSubtreeAndRenderProgramOnSelect()
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
                                Id = 42,
                                Schema = "e7000711",
                                DisplayName = "tank scene",
                                CompileStatus = "ready",
                            },
                            new EngineAssetGraphNode
                            {
                                Id = 9,
                                DisplayName = "tank body mesh",
                                CompileStatus = "ready",
                                OutputPorts = [new EngineAssetGraphPort { Type = 537 }],
                            },
                            new EngineAssetGraphNode
                            {
                                Id = 10,
                                DisplayName = "lit program",
                                CompileStatus = "ready",
                                OutputPorts = [new EngineAssetGraphPort { Type = 1049 }],
                            },
                        ],
                    },
                },
                scene: SceneSnapshot(
                    Node(
                        "root",
                        children:
                        [
                            Node("other", parentId: "root", visible: true),
                            // A render-program host: persisted program ref.
                            Node(
                                "program",
                                parentId: "root",
                                visible: true,
                                renderProgramNodeId: 10u),
                            // A subtree host: persisted scene-source node ref.
                            Node(
                                "subtree",
                                parentId: "root",
                                visible: true,
                                sceneSourceNodeId: 42u),
                        ]))));

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        var program = Assert.Single(root.Children, n => n.Id == "program");
        var subtree = Assert.Single(root.Children, n => n.Id == "subtree");
        var other = Assert.Single(root.Children, n => n.Id == "other");

        // Selecting the render-program node reveals the section pre-selected from the
        // persisted id — no Add Component.
        viewModel.SceneTree.SelectNode(program);
        Assert.True(viewModel.Inspector.HasRenderProgramSection);
        Assert.Equal(10ul, viewModel.Inspector.SelectedRenderProgramOption?.Id);
        Assert.Equal(
            "Referencing: lit program",
            viewModel.Inspector.RenderProgramReferenceDisplay);
        // It is not a subtree host, so that section stays hidden.
        Assert.False(viewModel.Inspector.HasSubtreeSection);

        // Selecting the subtree node reveals its section pre-selected, and the
        // render-program section is hidden for it.
        viewModel.SceneTree.SelectNode(subtree);
        Assert.True(viewModel.Inspector.HasSubtreeSection);
        Assert.Equal(42ul, viewModel.Inspector.SelectedSceneSourceOption?.Id);
        Assert.Equal(
            "Referencing: tank scene",
            viewModel.Inspector.SubtreeReferenceDisplay);
        Assert.False(viewModel.Inspector.HasRenderProgramSection);

        // Reselecting the render-program node still shows its state (snapshot-driven,
        // not a one-shot reveal).
        viewModel.SceneTree.SelectNode(other);
        viewModel.SceneTree.SelectNode(program);
        Assert.True(viewModel.Inspector.HasRenderProgramSection);
        Assert.Equal(10ul, viewModel.Inspector.SelectedRenderProgramOption?.Id);

        // A node with neither shows neither section.
        viewModel.SceneTree.SelectNode(other);
        Assert.False(viewModel.Inspector.HasRenderProgramSection);
        Assert.False(viewModel.Inspector.HasSubtreeSection);
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
                  "schema": "wozzits.scene_editor.assets.graph.v2",
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
                "host", "gltf/sample_rig.glb", sceneIndex: 0u, consumeMode: 0u);
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
                  "schema": "wozzits.scene_editor.assets.graph.v2",
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

    // Smoke: the "Render binding" host verbs (set_node_geometry_asset /
    // set_node_render_program, issue #213 increment 2) are reachable through the
    // live engine DLL. With no viewport runtime the native session reports a no-op
    // success — this proves the new P/Invoke + session/client wiring marshals and
    // both entry points resolve against the real engine ABI without throwing. Skips
    // if the DLL isn't built.
    [Fact]
    public void NativeEngineClientRenderBindingVerbsWhenEngineAbiIsBuilt()
    {
        var abiPath = WozzitsEngineNativeClient.ResolveDefaultAbiPath();
        if (!File.Exists(abiPath))
        {
            return;
        }

        var projectRoot = Path.Combine(
            Path.GetTempPath(),
            "wozzits_editor_render_binding_verb_" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(Path.Combine(projectRoot, ".wozzits"));
            File.WriteAllText(
                Path.Combine(projectRoot, ".wozzits", "project.json"),
                """
                {
                  "schema": "wozzits.project.v1",
                  "formatVersion": 1,
                  "name": "render_binding_verb_smoke",
                  "asset_graph": "assets.graph.json"
                }
                """);
            File.WriteAllText(
                Path.Combine(projectRoot, "assets.graph.json"),
                """
                {
                  "schema": "wozzits.scene_editor.assets.graph.v2",
                  "nodes": []
                }
                """);

            using var session = new WozzitsEngineNativeClient()
                .OpenEditorSession(projectRoot) as IDisposable;
            var editorSession =
                Assert.IsAssignableFrom<IWozzitsEngineEditorSession>(session);

            // No runtime was started, so each verb is a no-op success — but it
            // exercises the live DLL entry points (set + the id-0 clear form).
            Assert.True(
                editorSession.SetNodeGeometryAsset("host", 9u).Ok);
            Assert.True(
                editorSession.SetNodeGeometryAsset("host", 0u).Ok);
            Assert.True(
                editorSession.SetNodeRenderProgram("host", 10u).Ok);
            Assert.True(
                editorSession.SetNodeRenderProgram("host", 0u).Ok);
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

    // Smoke: the terrain-stick host verbs (set_node_collision /
    // set_node_motion_terrain) are reachable through the live engine DLL. With no
    // viewport runtime the native session reports a no-op success — this proves the
    // new P/Invoke + session/client wiring marshals (uint/byte/float blittables) and
    // both entry points resolve against the real engine ABI without throwing. Skips
    // if the DLL isn't built.
    [Fact]
    public void NativeEngineClientTerrainStickVerbsWhenEngineAbiIsBuilt()
    {
        var abiPath = WozzitsEngineNativeClient.ResolveDefaultAbiPath();
        if (!File.Exists(abiPath))
        {
            return;
        }

        var projectRoot = Path.Combine(
            Path.GetTempPath(),
            "wozzits_editor_terrain_stick_verb_" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(Path.Combine(projectRoot, ".wozzits"));
            File.WriteAllText(
                Path.Combine(projectRoot, ".wozzits", "project.json"),
                """
                {
                  "schema": "wozzits.project.v1",
                  "formatVersion": 1,
                  "name": "terrain_stick_verb_smoke",
                  "asset_graph": "assets.graph.json"
                }
                """);
            File.WriteAllText(
                Path.Combine(projectRoot, "assets.graph.json"),
                """
                {
                  "schema": "wozzits.scene_editor.assets.graph.v2",
                  "nodes": []
                }
                """);

            using var session = new WozzitsEngineNativeClient()
                .OpenEditorSession(projectRoot) as IDisposable;
            var editorSession =
                Assert.IsAssignableFrom<IWozzitsEngineEditorSession>(session);

            // No runtime was started, so each verb is a no-op success — but it
            // exercises the live DLL entry points (set + the id-0 clear form).
            Assert.True(
                editorSession.SetNodeCollision("host", 7u, constrainMovement: true).Ok);
            Assert.True(
                editorSession.SetNodeCollision("host", 0u, constrainMovement: false).Ok);
            Assert.True(
                editorSession.SetNodeMotionTerrain(
                    "host",
                    terrainConstrained: true,
                    rideHeight: 1.5f,
                    footprintRadius: 2.0f,
                    alignToSurface: true,
                    alignmentStrength: 0.5f).Ok);
            Assert.True(
                editorSession.SetNodeMotionTerrain(
                    "host",
                    terrainConstrained: false,
                    rideHeight: 0f,
                    footprintRadius: 0f,
                    alignToSurface: false,
                    alignmentStrength: 0f).Ok);
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
    // through the live DLL: it imports a test-owned GLB fixture and decodes the
    // model. Also exercises a bad path -> Ok=false. Skips if the engine DLL isn't
    // built. The GLB is a frozen copy under Fixtures/ (NOT the engine's shared
    // resources), so re-authoring a resources asset can never break this test.
    [Fact]
    public void NativeEngineClientImportsGlbSceneHierarchyWhenEngineAbiIsBuilt()
    {
        var abiPath = WozzitsEngineNativeClient.ResolveDefaultAbiPath();
        var glbDir = Path.Combine(AppContext.BaseDirectory, "Fixtures", "glb");
        var glbPath = Path.Combine(glbDir, "sample_rig.glb");

        if (!File.Exists(abiPath) || !File.Exists(glbPath))
        {
            return;
        }

        var client = new WozzitsEngineNativeClient();

        // Absolute path with no resource root.
        var hierarchy = client.ImportGlbSceneHierarchy(
            glbPath, resourceRoot: null, sceneIndex: 0u);
        Assert.True(hierarchy.Ok, hierarchy.Error);
        Assert.Equal("Scene", hierarchy.SceneName);
        Assert.Equal(0u, hierarchy.SceneIndex);

        // The engine roots a resource-relative path against the supplied resource
        // root (the rooting the editor used to do itself) — same hierarchy.
        var rooted = client.ImportGlbSceneHierarchy(
            "sample_rig.glb",
            resourceRoot: glbDir,
            sceneIndex: 0u);
        Assert.True(rooted.Ok, rooted.Error);
        Assert.Equal(4, rooted.Components.Count);

        // sample_rig.glb scene 0 is a chain body -> turret -> gun (all meshes)
        // -> barrel_orientation (a mesh-less orientation marker).
        Assert.Equal(4, hierarchy.Components.Count);
        var body = Assert.Single(hierarchy.Components, c => c.Id == "body");
        Assert.Null(body.ParentId);
        Assert.True(body.HasMesh);
        var turret = Assert.Single(hierarchy.Components, c => c.Id == "turret");
        Assert.Equal("body", turret.ParentId);
        Assert.True(turret.HasMesh);
        var gun = Assert.Single(hierarchy.Components, c => c.Id == "gun");
        Assert.Equal("turret", gun.ParentId);
        Assert.True(gun.HasMesh);
        var marker = Assert.Single(
            hierarchy.Components, c => c.Id == "barrel_orientation");
        Assert.Equal("gun", marker.ParentId);
        Assert.False(marker.HasMesh);

        // A bad path returns a well-formed Ok=false model (never throws).
        var missing = client.ImportGlbSceneHierarchy(
            Path.Combine(glbDir, "does_not_exist.glb"),
            resourceRoot: null,
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

    // A test-owned statechart file (under the test's own output directory), so nothing here
    // depends on the mutable engine corpus (a live scratch project).
    private static string WriteRunnerFixtureChart(string name, string bindingPort, string bindingFind)
    {
        var dir = Path.Combine(AppContext.BaseDirectory, "runner-fixtures");
        Directory.CreateDirectory(dir);
        var path = Path.Combine(dir, name + ".sc.json");
        File.WriteAllText(
            path,
            "{\n"
            + "  \"schema\": \"wozzits.statechart.ir.v0\",\n"
            + $"  \"name\": \"{name}\",\n"
            + $"  \"bindings\": [ {{ \"port\": \"{bindingPort}\", \"find\": \"{bindingFind}\", \"scope\": \"subtree\" }} ],\n"
            + "  \"agents\": [],\n"
            + "  \"pure\": [],\n"
            + "  \"regions\": [ { \"id\": \"r0\", \"initial\": \"s0\", \"states\": [\"s0\"] } ],\n"
            + "  \"states\": [ { \"id\": \"s0\", \"do\": [], \"transitions\": [] } ]\n"
            + "}\n");
        return path;
    }

    // D3-C29. StatechartDocumentViewModel.Save() runs EmitValidated only when the
    // CHART changed (Control.IsDirty || Dataflow.IsDirty). A pan/zoom/drag sets
    // IsLayoutDirty instead, which still makes document.IsDirty true -- so Save()
    // returned without validating and SaveOpenStatecharts then read CompiledIr,
    // pushing IR the engine refuses into every attached runner AND the scenelet
    // FILES. CompiledIr's own comment claims "its only persisting reader runs
    // after Save() has already validated"; this is the case where that is false.
    [Fact]
    public void ALayoutOnlySaveDoesNotEmbedUnvalidatedChartIr()
    {
        var dir = Path.Combine(AppContext.BaseDirectory, "runner-fixtures");
        Directory.CreateDirectory(dir);
        var path = Path.Combine(dir, "layout_only_invalid.sc.json");
        // Parses fine, but names a state that does not exist -- exactly what
        // parse_chart refuses, and what EmitValidated is there to catch.
        File.WriteAllText(
            path,
            "{\n"
            + "  \"schema\": \"wozzits.statechart.ir.v0\",\n"
            + "  \"name\": \"layout_only_invalid\",\n"
            + "  \"bindings\": [ { \"port\": \"p\", \"find\": \"x\", \"scope\": \"subtree\" } ],\n"
            + "  \"agents\": [], \"pure\": [],\n"
            + "  \"regions\": [ { \"id\": \"r0\", \"initial\": \"s0\", \"states\": [\"s0\"] } ],\n"
            + "  \"states\": [ { \"id\": \"s0\", \"do\": [], \"transitions\": "
            + "[ { \"target\": \"NO_SUCH_STATE\", "
            + "\"trigger\": { \"kind\": \"event\", \"event\": \"e\" } } ] } ]\n"
            + "}\n");

        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(scene: SceneSnapshot(Node("host", "Host"))),
            editorSession: new RecordingEditorSession { RuntimeRunning = true },
            projectDirectory: dir);
        viewModel.OpenStatechartCommand.Execute(
            new StatechartFileInfo("layout_only_invalid", path));

        var document = FindStatechartDocuments(viewModel.EditorLayout).FirstOrDefault();
        Assert.NotNull(document);

        // A real user gesture that dirties the LAYOUT only, not the chart.
        document!.Control.ZoomByWheel(1);
        Assert.False(document.Control.IsDirty);
        Assert.True(document.Control.IsLayoutDirty);
        Assert.True(document.IsDirty);

        viewModel.SaveAllCommand.Execute(null);

        // The invalid IR must not be announced as saved and must not be embedded.
        Assert.Contains("NOT saved", viewModel.EngineLogText);
        Assert.DoesNotContain("Saved statechart 'layout_only_invalid'", viewModel.EngineLogText);
    }

    // D3-C14. AttachStatechartRunner discarded all four of its trailing calls
    // (chart config, chart_ir config, events, SaveScene) and then reported
    // "Running 'X'." unconditionally. chart_ir is the load-bearing one -- it is
    // what actually makes the runner run -- so a refusal there left the user
    // looking at a running-status line for a runner that would never start.
    // D3-C8. RefreshAttachedRunners, called FROM SaveAll, discarded the
    // SetNodeBehaviorConfig response and logged "Refreshed runner on 'X'"
    // unconditionally -- twenty lines below SaveAll's own SaveScene check, whose
    // comment says "silence here reads as saved". So Save All affirmatively
    // claimed work the engine had refused.
    [Fact]
    public void SaveAllReportsARunnerRefreshTheEngineRefused()
    {
        var session = new RecordingEditorSession
        {
            RuntimeRunning = true,
            NextAddedBehaviorId = "runner.1",
            BehaviorModuleCatalog = ["statechart_runner"],
        };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(scene: SceneSnapshot(Node("host", "Host"))),
            editorSession: session);

        var chartPath = WriteRunnerFixtureChart("refresh_fixture", "bind1", "laser");
        viewModel.Inspector.SetStatechartsProvider(() => new[]
        {
            new StatechartFileInfo("refresh_fixture", chartPath),
        });

        // Attach a runner while the engine still accepts everything.
        var host = viewModel.SceneTree.Nodes.First(n => n.Id == "host");
        viewModel.SceneTree.SelectNode(host);
        viewModel.Inspector.AddComponentCommand.Execute("statechart_runner");
        viewModel.Inspector.SelectedStatechartRunnerChart =
            viewModel.Inspector.StatechartRunnerCharts.First(c => c.Name == "refresh_fixture");
        viewModel.Inspector.AttachStatechartRunnerCommand.Execute(null);
        Assert.True(viewModel.Inspector.HasAttachedStatechartRunner);

        // Open the chart and dirty it, so Save All has something to re-embed.
        viewModel.OpenStatechartCommand.Execute(
            new StatechartFileInfo("refresh_fixture", chartPath));
        var document = FindStatechartDocuments(viewModel.EditorLayout).FirstOrDefault();
        Assert.NotNull(document);
        document!.Control.MarkChartDirty();
        Assert.True(document.IsDirty);

        // Now make the re-embed refuse.
        session.RejectBehaviorConfigKey = "chart_ir";

        viewModel.SaveAllCommand.Execute(null);

        Assert.Contains("NOT refreshed", viewModel.EngineLogText);
        Assert.DoesNotContain("Refreshed runner on 'Host'", viewModel.EngineLogText);
    }

    private static IEnumerable<StatechartDocumentViewModel> FindStatechartDocuments(object? dockable)
    {
        if (dockable is Dock.Model.Mvvm.Controls.Document
            { Context: StatechartDocumentViewModel chart })
        {
            yield return chart;
        }

        if (dockable is Dock.Model.Core.IDock dock && dock.VisibleDockables is not null)
        {
            foreach (var child in dock.VisibleDockables)
            {
                foreach (var found in FindStatechartDocuments(child))
                {
                    yield return found;
                }
            }
        }
    }

    [Fact]
    public void AttachingAStatechartRunnerReportsARefusedChartIrWrite()
    {
        var session = new RecordingEditorSession
        {
            NextAddedBehaviorId = "runner.1",
            BehaviorModuleCatalog = ["statechart_runner"],
            RejectBehaviorConfigKey = "chart_ir",
        };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(scene: SceneSnapshot(Node("host", "Host"))),
            editorSession: session);

        var chartPath = WriteRunnerFixtureChart("runner_fixture", "bind1", "laser");
        viewModel.Inspector.SetStatechartsProvider(() => new[]
        {
            new StatechartFileInfo("runner_fixture", chartPath),
        });

        var host = viewModel.SceneTree.Nodes.First(n => n.Id == "host");
        viewModel.SceneTree.SelectNode(host);
        viewModel.Inspector.AddComponentCommand.Execute("statechart_runner");
        viewModel.Inspector.SelectedStatechartRunnerChart =
            viewModel.Inspector.StatechartRunnerCharts.First(c => c.Name == "runner_fixture");

        viewModel.Inspector.AttachStatechartRunnerCommand.Execute(null);

        // The write WAS attempted -- this is a refusal, not a short-circuit.
        Assert.Contains(session.BehaviorConfigs, c => c.Key == "chart_ir");
        // ...and the status must say so instead of claiming the chart is running.
        Assert.Contains("Couldn't attach", viewModel.Inspector.StatechartRunnerStatus);
        Assert.DoesNotContain("Running", viewModel.Inspector.StatechartRunnerStatus);
        Assert.NotEmpty(viewModel.Inspector.LastEditError);
        Assert.False(viewModel.Inspector.HasAttachedStatechartRunner);
    }

    [Fact]
    public void InspectorAttachesStatechartRunnerFromComponentsMenuWithAuthoredConfig()
    {
        var session = new RecordingEditorSession
        {
            NextAddedBehaviorId = "runner.1",
            BehaviorModuleCatalog = ["spin", "statechart_runner"],
        };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(scene: SceneSnapshot(Node("host", "Host"))),
            editorSession: session);

        var chartPath = WriteRunnerFixtureChart("runner_fixture", "bind1", "laser");
        viewModel.Inspector.SetStatechartsProvider(() => new[]
        {
            new StatechartFileInfo("runner_fixture", chartPath),
        });

        var host = viewModel.SceneTree.Nodes.First(n => n.Id == "host");
        viewModel.SceneTree.SelectNode(host);

        // The runner is NOT offered in the Behaviors "+" catalog -- only via Components "+".
        Assert.DoesNotContain(viewModel.Inspector.AvailableBehaviorModules, o => o.Module == "statechart_runner");
        Assert.Contains(viewModel.Inspector.AvailableBehaviorModules, o => o.Module == "spin");

        // The card is hidden until it's added from the Components "+" menu.
        Assert.False(viewModel.Inspector.HasStatechartRunnerSection);
        viewModel.Inspector.AddComponentCommand.Execute("statechart_runner");
        Assert.True(viewModel.Inspector.HasStatechartRunnerSection);

        // Pick the chart and attach.
        viewModel.Inspector.SelectedStatechartRunnerChart =
            viewModel.Inspector.StatechartRunnerCharts.First(c => c.Name == "runner_fixture");
        viewModel.Inspector.AttachStatechartRunnerCommand.Execute(null);

        // A statechart_runner was added to the host with the chart name + the IR compiled AS
        // AUTHORED (its binding keeps the find the chart itself specifies), plus events + save.
        Assert.Contains(("host", "statechart_runner"), session.AddedBehaviors);
        Assert.Contains(session.BehaviorConfigs,
            c => c.BindingId == "runner.1" && c.Key == "chart" && c.Value == "runner_fixture");
        var irConfig = Assert.Single(session.BehaviorConfigs, c => c.Key == "chart_ir");
        Assert.Contains("\"find\":\"laser\"", irConfig.Value);   // authored find, compiled as-is
        Assert.Contains(session.BehaviorEventSets,
            e => e.BindingId == "runner.1"
                 && e.Events.Contains("self.start")
                 && e.Events.Contains("frame.update"));
        Assert.True(session.SaveSceneCount >= 1);
        // The runner is NOT a raw Behaviors row -- it lives only in the card.
        Assert.DoesNotContain(viewModel.Inspector.Behaviors, b => b.Module == "statechart_runner");
        Assert.True(viewModel.Inspector.HasAttachedStatechartRunner);

        // Re-attaching updates the existing runner in place -- it does not add a second.
        viewModel.Inspector.AttachStatechartRunnerCommand.Execute(null);
        Assert.Single(session.AddedBehaviors, b => b.Module == "statechart_runner");

        // Reselecting the node auto-reveals the card, PRE-SELECTS the running chart in the picker
        // (not "unset"), and still shows no raw Behaviors row.
        viewModel.SceneTree.SelectNode(host);
        Assert.True(viewModel.Inspector.HasStatechartRunnerSection);
        Assert.True(viewModel.Inspector.HasAttachedStatechartRunner);
        Assert.Equal("runner_fixture", viewModel.Inspector.SelectedStatechartRunnerChart?.Name);
        Assert.DoesNotContain(viewModel.Inspector.Behaviors, b => b.Module == "statechart_runner");

        // Remove detaches it from the node.
        viewModel.Inspector.RemoveStatechartRunnerCommand.Execute(null);
        Assert.False(viewModel.Inspector.HasAttachedStatechartRunner);
        Assert.Contains(("host", "runner.1"), session.RemovedBehaviors);
    }

    // A module rebuilt with NEW declared params must show them without restarting the
    // editor. The inspector caches the module schema (rebuilding it reloads every project
    // DLL, so it is not per-selection work), and a rebuild reloads the DLLs but used to
    // leave that cache stale -- so a module that just grew params kept showing none, which
    // reads as "the editor ignored my change".
    [Fact]
    public void RebuiltModuleDeclaringNewParamsShowsThemWithoutRestart()
    {
        var session = new RecordingEditorSession();   // param catalog starts empty
        var inspector = new InspectorPaneViewModel(session);
        inspector.Inspect(new SceneTreeNodeViewModel(new EngineSceneNode
        {
            Id = "tank",
            DisplayName = "tank",
            Kind = "node",
            Visible = true,
            Behaviors = [new EngineSceneBehavior { Id = "b.1", Module = "enemy_tank_v1" }],
        }));

        // The module declares nothing yet, so the card has no typed fields.
        Assert.False(inspector.Behaviors.Single().HasConfig);

        // You add declared params and hit Rebuild: the freshly built DLL reports them.
        session.BehaviorModuleParamCatalog = new EngineBehaviorModuleCatalogResponse
        {
            Ok = true,
            Modules =
            [
                new EngineBehaviorModule
                {
                    Module = "enemy_tank_v1",
                    Params =
                    [
                        new EngineBehaviorModuleParam
                        {
                            Key = "target_0",
                            Label = "Target 0",
                            Type = 3,                  // string
                            DefaultString = "tank",
                        },
                    ],
                },
            ],
        };

        inspector.RefreshDeclaredParams();

        var row = Assert.Single(inspector.Behaviors.Single().Config);
        Assert.Equal("target_0", row.Key);
        Assert.Equal("Target 0", row.Label);
    }

    // A quantum_agent whose config names a mind must show that mind in the picker after a
    // reselect / scenelet reopen -- not reset to "Pick a mind". RefreshQuantumAgentMindSection
    // clears + repopulates the minds ItemsSource, and the bound ComboBox writes a transient
    // null back through the setter AFTER the restore set the field; without a null guard that
    // writeback silently clears the selection (it looked fine only until the editor restarted).
    [Fact]
    public void QuantumAgentMindSelectionSurvivesItemsSourceChurn()
    {
        var session = new RecordingEditorSession();
        var inspector = new InspectorPaneViewModel(session);
        inspector.SetMindsProvider(() =>
        [
            new MindFileInfo("first_mind", "behavior/minds/first_mind.mind.json"),
            new MindFileInfo("other_mind", "behavior/minds/other_mind.mind.json"),
        ]);

        inspector.Inspect(new SceneTreeNodeViewModel(new EngineSceneNode
        {
            Id = "tank",
            DisplayName = "tank",
            Kind = "node",
            Visible = true,
            Behaviors =
            [
                new EngineSceneBehavior
                {
                    Id = "b.1",
                    Module = "quantum_agent",
                    Config =
                    [
                        new EngineSceneBehaviorConfig { Name = "mind", Kind = "string", Value = "first_mind" },
                        new EngineSceneBehaviorConfig { Name = "mind_ir", Kind = "string", Value = "{\"qubits\":1}" },
                    ],
                },
            ],
        }));

        // The restore picked the mind the config names.
        Assert.True(inspector.HasQuantumAgentMindSection);
        Assert.Equal("first_mind", inspector.SelectedQuantumAgentMind?.Name);

        // The ComboBox writes null back while its ItemsSource churns -- the setter must ignore it.
        inspector.SelectedQuantumAgentMind = null;

        Assert.Equal("first_mind", inspector.SelectedQuantumAgentMind?.Name);
    }

    // Adding a quantum_agent must reveal its Mind picker immediately -- not only after the
    // node is reselected. AddBehavior updated the behavior list but skipped the section
    // refresh that RemoveBehavior already did, so the picker stayed hidden.
    [Fact]
    public void AddingQuantumAgentRevealsTheMindPickerWithoutReselect()
    {
        var session = new RecordingEditorSession();
        var inspector = new InspectorPaneViewModel(session);
        inspector.SetMindsProvider(() => [new MindFileInfo("doctrine", "behavior/minds/doctrine.mind.json")]);
        inspector.Inspect(new SceneTreeNodeViewModel(new EngineSceneNode
        {
            Id = "spawner",
            DisplayName = "spawner",
            Kind = "node",
            Visible = true,
            Behaviors = [new EngineSceneBehavior { Id = "b.1", Module = "enemy_tank_v1_spawner" }],
        }));

        Assert.False(inspector.HasQuantumAgentMindSection);   // no agent yet

        inspector.NewBehaviorModule = "quantum_agent";
        inspector.AddBehaviorCommand.Execute(null);

        Assert.True(inspector.HasQuantumAgentMindSection);     // revealed without a reselect
        Assert.Contains(inspector.QuantumAgentMinds, m => m.Name == "doctrine");
    }

    // A failed rebuild must be visible ON the card, not just in a console full of live
    // behavior output -- otherwise the stale DLL reads as "the editor ignored my change".
    [Fact]
    public void BuildErrorsSurfaceOnTheModuleTheCompilerNamed()
    {
        var session = new RecordingEditorSession();
        var inspector = new InspectorPaneViewModel(session);
        inspector.Inspect(new SceneTreeNodeViewModel(new EngineSceneNode
        {
            Id = "tank",
            DisplayName = "tank",
            Kind = "node",
            Visible = true,
            Behaviors =
            [
                new EngineSceneBehavior { Id = "b.1", Module = "enemy_tank_v1" },
                new EngineSceneBehavior { Id = "b.2", Module = "hit_logger" },
            ],
        }));

        Assert.False(inspector.HasBuildFailure);
        Assert.All(inspector.Behaviors, b => Assert.False(b.HasBuildErrors));

        inspector.SetBuildErrors(
        [
            "D:/p/behavior/enemy_tank_v1/enemy_tank_v1.cpp:25:13: error: cannot be narrowed",
            "CMake Error at CMakeLists.txt:5 (add_library):",
        ]);

        // The section banner shows the WHOLE list, so an error that matches no card
        // (the CMake one) is still visible.
        Assert.True(inspector.HasBuildFailure);
        Assert.Contains("2 errors", inspector.BuildFailureSummary);
        Assert.Contains("CMake Error", inspector.BuildErrorText);

        // ...and the card the compiler named carries its own diagnostic; the other doesn't.
        var named = inspector.Behaviors.First(b => b.Module == "enemy_tank_v1");
        Assert.True(named.HasBuildErrors);
        Assert.Contains("cannot be narrowed", named.BuildErrors);
        Assert.False(inspector.Behaviors.First(b => b.Module == "hit_logger").HasBuildErrors);

        // A clean rebuild clears it -- a stale error block is its own kind of lie.
        inspector.SetBuildErrors([]);
        Assert.False(inspector.HasBuildFailure);
        Assert.All(inspector.Behaviors, b => Assert.False(b.HasBuildErrors));
    }

    // Regression: removing the quantum_agent behavior must also drop its "Mind" sub-card.
    // That card auto-reveals for a quantum_agent, and it used to linger after the behavior
    // was gone because its visibility was only recomputed on node selection, not on removal.
    [Fact]
    public void RemovingQuantumAgentHidesItsMindCard()
    {
        var session = new RecordingEditorSession();   // viewport running by default
        var inspector = new InspectorPaneViewModel(session);
        inspector.Inspect(new SceneTreeNodeViewModel(new EngineSceneNode
        {
            Id = "tank",
            DisplayName = "tank",
            Kind = "node",
            Visible = true,
            Behaviors = [new EngineSceneBehavior { Id = "qa.1", Module = "quantum_agent" }],
        }));

        Assert.True(inspector.HasQuantumAgentMindSection);   // the Mind card shows for a quantum_agent

        inspector.Behaviors.First(b => b.Module == "quantum_agent").RemoveCommand.Execute(null);

        Assert.Contains(("tank", "qa.1"), session.RemovedBehaviors);
        Assert.DoesNotContain(inspector.Behaviors, b => b.Module == "quantum_agent");
        Assert.False(inspector.HasQuantumAgentMindSection);   // ...its Mind card no longer lingers
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

    // D3-P039. A grafted read-back that FAILED is not "nothing is grafted". The
    // drop-then-fetch order meant a failure removed the grafts already merged and
    // put nothing back, so the sub-trees vanished from the tree while they were
    // alive in the engine -- and nothing said so.
    [Fact]
    public void SceneTreeMergeKeepsExistingGraftsWhenTheReadBackFails()
    {
        var session = new RecordingEditorSession
        {
            GraftedScene = new EngineSceneSnapshot
            {
                Roots =
                [
                    new EngineSceneNode
                    {
                        Id = "root/graft",
                        ParentId = "root",
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
        Assert.Single(Assert.Single(sceneTree.Nodes).Children);

        // The viewport closes mid-call: the engine answers with Ok=false.
        session.GraftedSceneOk = false;
        sceneTree.MergeGraftedNodes();

        Assert.Single(Assert.Single(sceneTree.Nodes).Children);

        // ...and the control, on the same tree: an EMPTY but SUCCESSFUL answer
        // still means "nothing is grafted" and must still drop them. Without this
        // half the fix would read as "never drop grafts", which is a different bug.
        session.GraftedSceneOk = true;
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
    public void SceneTreeMoveUpDownReordersSiblingsViaEngine()
    {
        var session = new RecordingEditorSession();
        var sceneTree = new SceneTreeEditorPaneViewModel(session);
        sceneTree.LoadSnapshot(TopLevelScene("a", "b", "c"));

        // Move 'a' down past 'b': the engine reorder places it just before 'c'
        // (the sibling after 'b'); the tree becomes b, a, c.
        sceneTree.MoveDown(sceneTree.Nodes[0]);
        Assert.Equal(("a", "c"), Assert.Single(session.Reorders));
        Assert.Equal("b", sceneTree.Nodes[0].Id);
        Assert.Equal("a", sceneTree.Nodes[1].Id);
        Assert.Equal("c", sceneTree.Nodes[2].Id);

        // Move 'c' up one: before its new predecessor 'a'; tree becomes b, c, a.
        sceneTree.MoveUp(sceneTree.Nodes[2]);
        Assert.Equal(("c", "a"), session.Reorders[1]);
        Assert.Equal("b", sceneTree.Nodes[0].Id);
        Assert.Equal("c", sceneTree.Nodes[1].Id);
        Assert.Equal("a", sceneTree.Nodes[2].Id);
    }

    [Fact]
    public void SceneTreeMoveDownToEndUsesEmptyBeforeAndBoundariesAreNoOps()
    {
        var session = new RecordingEditorSession();
        var sceneTree = new SceneTreeEditorPaneViewModel(session);
        sceneTree.LoadSnapshot(TopLevelScene("a", "b"));

        var a = sceneTree.Nodes[0];
        var b = sceneTree.Nodes[1];
        Assert.True(sceneTree.CanMoveDown(a));
        Assert.False(sceneTree.CanMoveUp(a));   // first: cannot move up
        Assert.True(sceneTree.CanMoveUp(b));
        Assert.False(sceneTree.CanMoveDown(b)); // last: cannot move down

        // Moving 'a' down past the last sibling => move to the end (empty before).
        sceneTree.MoveDown(a);
        Assert.Equal(("a", string.Empty), Assert.Single(session.Reorders));
        Assert.Equal("b", sceneTree.Nodes[0].Id);
        Assert.Equal("a", sceneTree.Nodes[1].Id);

        // Moving the now-first node up is a no-op at the boundary: no engine call.
        sceneTree.MoveUp(sceneTree.Nodes[0]);
        Assert.Single(session.Reorders);
    }

    [Fact]
    public void SceneTreeReorderIsSkippedAndLoggedWhenViewportNotRunning()
    {
        var logs = new List<string>();
        var session = new RecordingEditorSession { RuntimeRunning = false };
        var sceneTree = new SceneTreeEditorPaneViewModel(session, log: logs.Add);
        sceneTree.LoadSnapshot(TopLevelScene("a", "b"));

        sceneTree.MoveDown(sceneTree.Nodes[0]);

        Assert.Empty(session.Reorders);
        Assert.Equal("a", sceneTree.Nodes[0].Id);  // tree untouched
        Assert.Contains(logs, line => line.Contains("requires the running viewport"));
    }

    // A flat scene of top-level nodes (draw order = this order), for the reorder
    // tests.
    private static EngineSceneSnapshotResponse TopLevelScene(params string[] ids)
    {
        return new EngineSceneSnapshotResponse
        {
            Ok = true,
            Snapshot = new EngineSceneSnapshot
            {
                Roots = ids
                    .Select(id => new EngineSceneNode { Id = id, Kind = "node" })
                    .ToList(),
            },
        };
    }

    [Fact]
    public void SceneTreeDropBetweenSiblingsReordersWithoutReparent()
    {
        var session = new RecordingEditorSession();
        var sceneTree = new SceneTreeEditorPaneViewModel(session);
        sceneTree.LoadSnapshot(TopLevelScene("a", "b", "c"));

        // Drop 'a' AFTER 'b' (same level): reorder before 'c'; no reparent.
        sceneTree.DropNode(
            sceneTree.Nodes[0], sceneTree.Nodes[1], SceneTreeDropPosition.After);
        Assert.Empty(session.Reparents);
        Assert.Equal(("a", "c"), Assert.Single(session.Reorders));
        Assert.Equal("b", sceneTree.Nodes[0].Id);
        Assert.Equal("a", sceneTree.Nodes[1].Id);
        Assert.Equal("c", sceneTree.Nodes[2].Id);
    }

    [Fact]
    public void SceneTreeDropBeforeFirstMovesToFrontAndAfterLastToEnd()
    {
        var session = new RecordingEditorSession();
        var sceneTree = new SceneTreeEditorPaneViewModel(session);
        sceneTree.LoadSnapshot(TopLevelScene("a", "b", "c"));

        // Before the first sibling => reorder before it (drawn first).
        sceneTree.DropNode(
            sceneTree.Nodes[2], sceneTree.Nodes[0], SceneTreeDropPosition.Before);
        Assert.Equal(("c", "a"), Assert.Single(session.Reorders));
        Assert.Equal("c", sceneTree.Nodes[0].Id);

        // After the last sibling => empty before-id (drawn last).
        sceneTree.DropNode(
            sceneTree.Nodes[0], sceneTree.Nodes[2], SceneTreeDropPosition.After);
        Assert.Equal(("c", string.Empty), session.Reorders[1]);
        Assert.Equal("c", sceneTree.Nodes[2].Id);  // c now drawn last
    }

    [Fact]
    public void SceneTreeDropIntoNodeReparentsAndAppendsInDrawOrder()
    {
        var session = new RecordingEditorSession();
        var sceneTree = new SceneTreeEditorPaneViewModel(session);
        sceneTree.LoadSnapshot(TopLevelScene("a", "b"));

        var b = sceneTree.Nodes.Single(n => n.Id == "b");
        sceneTree.DropNode(sceneTree.Nodes[0], b, SceneTreeDropPosition.Into);

        // Reparent under b, plus an append (reorder to end) so the tree-derived
        // engine draw order matches the VM showing 'a' as b's last child.
        Assert.Equal(("a", "b"), Assert.Single(session.Reparents));
        Assert.Equal(("a", string.Empty), Assert.Single(session.Reorders));
        Assert.Same(b, Assert.Single(sceneTree.Nodes));  // only b at top level
        Assert.Equal("a", Assert.Single(b.Children).Id);
    }

    [Fact]
    public void SceneTreeDropIntoOwnDescendantIsRejected()
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
                        Id = "p",
                        Kind = "node",
                        Children =
                        [
                            new EngineSceneNode { Id = "c", ParentId = "p", Kind = "node" },
                        ],
                    },
                ],
            },
        });
        var p = Assert.Single(sceneTree.Nodes);
        var c = Assert.Single(p.Children);

        // Dropping 'p' onto its own child 'c' would nest p under itself: no engine
        // call at all (in particular, no stray append).
        sceneTree.DropNode(p, c, SceneTreeDropPosition.Into);
        Assert.Empty(session.Reparents);
        Assert.Empty(session.Reorders);
    }

    [Fact]
    public void SceneTreeDropBetweenAcrossParentsReparentsThenReorders()
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
                    new EngineSceneNode { Id = "x", Kind = "node" },
                    new EngineSceneNode
                    {
                        Id = "p",
                        Kind = "node",
                        Children =
                        [
                            new EngineSceneNode { Id = "b", ParentId = "p", Kind = "node" },
                            new EngineSceneNode { Id = "c", ParentId = "p", Kind = "node" },
                        ],
                    },
                ],
            },
        });

        var x = sceneTree.Nodes.Single(n => n.Id == "x");
        var p = sceneTree.Nodes.Single(n => n.Id == "p");
        var b = p.Children.Single(n => n.Id == "b");

        // Drop 'x' BEFORE 'b' (a child of 'p'): reparent x under p, reorder before b.
        sceneTree.DropNode(x, b, SceneTreeDropPosition.Before);

        Assert.Equal(("x", "p"), Assert.Single(session.Reparents));
        Assert.Equal(("x", "b"), Assert.Single(session.Reorders));
        Assert.DoesNotContain(sceneTree.Nodes, n => n.Id == "x");  // left top level
        Assert.Equal("x", p.Children[0].Id);                        // before b
        Assert.Equal("b", p.Children[1].Id);
        Assert.Equal("c", p.Children[2].Id);
        Assert.Equal("p", x.ParentId);
    }

    [Fact]
    public void SceneTreeDropBetweenRejectsDropIntoOwnSubtree()
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
                        Id = "p",
                        Kind = "node",
                        Children =
                        [
                            new EngineSceneNode { Id = "c", ParentId = "p", Kind = "node" },
                        ],
                    },
                ],
            },
        });
        var p = Assert.Single(sceneTree.Nodes);
        var c = Assert.Single(p.Children);

        // Dropping 'p' beside its own child 'c' would nest p under itself => reject.
        sceneTree.DropNode(p, c, SceneTreeDropPosition.Before);
        Assert.Empty(session.Reparents);
        Assert.Empty(session.Reorders);
        Assert.Same(p, Assert.Single(sceneTree.Nodes));
    }

    [Fact]
    public void SceneTreeDropIsSkippedWhenViewportNotRunning()
    {
        var logs = new List<string>();
        var session = new RecordingEditorSession { RuntimeRunning = false };
        var sceneTree = new SceneTreeEditorPaneViewModel(session, log: logs.Add);
        sceneTree.LoadSnapshot(TopLevelScene("a", "b"));

        sceneTree.DropNode(
            sceneTree.Nodes[0], sceneTree.Nodes[1], SceneTreeDropPosition.After);

        Assert.Empty(session.Reorders);
        Assert.Empty(session.Reparents);
        Assert.Contains(logs, line => line.Contains("requires the running viewport"));
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

    // Scene-tree structural edits run against the live viewport runtime. With no
    // viewport running the edit must NOT silently do nothing: it is skipped (no
    // engine call, tree untouched) AND logged so the user knows why (rather than
    // the old behavior where a right-click "Add Child" appeared to do nothing).
    [Fact]
    public void SceneTreeEditsAreSkippedAndLoggedWhenViewportNotRunning()
    {
        var logs = new List<string>();
        var session = new RecordingEditorSession { RuntimeRunning = false };
        var sceneTree = new SceneTreeEditorPaneViewModel(session, log: logs.Add);
        sceneTree.LoadSnapshot(new EngineSceneSnapshotResponse
        {
            Ok = true,
            Snapshot = new EngineSceneSnapshot
            {
                Roots =
                [
                    new EngineSceneNode { Id = "root", Kind = "node" },
                ],
            },
        });

        var root = Assert.Single(sceneTree.Nodes);
        Assert.False(sceneTree.CanEditScene);

        sceneTree.AddChild(root);
        sceneTree.Remove(root);

        // No engine mutation was attempted and the tree is untouched...
        Assert.Empty(session.AddChildParents);
        Assert.Empty(session.Removed);
        Assert.Same(root, Assert.Single(sceneTree.Nodes));
        Assert.Empty(root.Children);
        // ...and each attempt logged that the viewport is required.
        Assert.Equal(2, logs.Count);
        Assert.All(
            logs,
            line => Assert.Contains("requires the running viewport", line));
    }

    // Inspector scene-node edits are gated the same way as the scene tree: with
    // no viewport running the edit surface is disabled (CanEditNode) AND any edit
    // that slips through (e.g. the runtime stopped after selection) no-ops + logs
    // via EnsureCanApply, rather than silently succeeding as a {Ok=true} no-op.
    [Fact]
    public void InspectorEditsAreDisabledAndLoggedWhenViewportNotRunning()
    {
        var logs = new List<string>();
        var session = new RecordingEditorSession { RuntimeRunning = false };
        var inspector = new InspectorPaneViewModel(session, logs.Add);
        inspector.Inspect(new SceneTreeNodeViewModel(new EngineSceneNode
        {
            Id = "node",
            DisplayName = "node",
            Kind = "node",
            Visible = true,
        }));

        Assert.True(inspector.HasSceneNodeSelection);
        Assert.False(inspector.CanEditNode);  // edit surface disabled in the view

        // A command edit that slips through is refused before touching the engine.
        inspector.AddComponentCommand.Execute("proximity");

        Assert.Empty(session.AddedComponents);
        Assert.Contains(logs, line => line.Contains("requires the running viewport"));

        // Bringing the viewport back re-enables editing after a refresh.
        session.RuntimeRunning = true;
        inspector.RefreshEditAvailability();
        Assert.True(inspector.CanEditNode);
    }

    [Fact]
    public void InspectorRenderLayerReflectsNodeAndSetsLive()
    {
        var session = new RecordingEditorSession();  // viewport running by default
        var inspector = new InspectorPaneViewModel(session);
        inspector.Inspect(new SceneTreeNodeViewModel(new EngineSceneNode
        {
            Id = "sky",
            DisplayName = "sky",
            Kind = "node",
            Visible = true,
            RenderOrder = -200,  // Background (Sky) layer
        }));

        // The dropdown reflects the node's current layer; populating writes nothing.
        Assert.NotNull(inspector.SelectedRenderLayer);
        Assert.Equal(-200, inspector.SelectedRenderLayer!.Value);
        Assert.Empty(session.RenderOrders);

        // Selecting the World layer pushes render_order 0 to the engine live.
        inspector.SelectedRenderLayer =
            inspector.RenderLayers.Single(option => option.Value == 0);
        Assert.Equal(("sky", 0), Assert.Single(session.RenderOrders));
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
        // A test-owned project fixture under Fixtures/ (a frozen copy, never the
        // engine's shared resources/), so a resources scene edit cannot break this.
        var fixtureProject = Path.Combine(
            AppContext.BaseDirectory,
            "Fixtures",
            "projects",
            "subtree_reference_project");

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
        Assert.Equal("gltf/sample_rig.glb", sceneSource.Path);
        Assert.Equal("instance", sceneSource.ConsumeMode);
        Assert.Equal(0u, sceneSource.SceneIndex);
        Assert.Equal(1u, sceneSource.StyleOverrideCount);
        Assert.True(sceneSource.HasBaseStyle);

        // The base style + the one per-mesh override round-trip through the
        // snapshot. The fixture's base style is wireframe-on/surface-off; its
        // mesh-index-1 override is surface-on/wireframe-off.
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

    // ─── "Mesh from GLB scene" GLB-node tree picker (issue #213) ─────────────────

    // Selecting a "Mesh from GLB scene" node (schema e7000414) resolves the connected
    // GLB by walking its OWN `source_file` edge to the file node's source_path (one
    // hop — no Scene dependency), forwards that authored path verbatim to the import
    // (the engine roots it), and shows the hierarchy as a tree with mesh markers.
    [Fact]
    public void MeshFromGlbScenePickerPopulatesTreeFromConnectedGlb()
    {
        var projectDir = Path.Combine(Path.GetTempPath(), "wz-glb-picker-vm");
        var session = new RecordingEditorSession
        {
            // body -> turret -> gun, all mesh-bearing (turret/gun nested under body).
            GlbHierarchy = new EngineGlbSceneHierarchy
            {
                Ok = true,
                SceneName = "Scene",
                Components =
                [
                    GlbComponent("body", hasMesh: true),
                    GlbComponent("turret", hasMesh: true, parentId: "body"),
                    GlbComponent("gun", hasMesh: true, parentId: "turret"),
                ],
            },
        };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(assetGraph: GlbExtractorGraph()),
            editorSession: session,
            projectDirectory: projectDir);

        var extractor = Assert.Single(
            viewModel.AssetGraph.Nodes,
            n => n.SchemaLabel == "e7000414");
        viewModel.AssetGraph.SelectNode(extractor);

        Assert.True(viewModel.Inspector.HasGlbNodePicker);
        Assert.True(viewModel.Inspector.HasGlbNodes);
        Assert.False(viewModel.Inspector.HasGlbNodePickerHint);

        // The authored source_path is forwarded verbatim; rooting against the
        // project's resource root is the engine's job, not the view-model's.
        var import = Assert.Single(session.GlbHierarchyImports);
        Assert.Equal(0u, import.SceneIndex);
        Assert.Equal("gltf/sample_rig.glb", import.Path);

        // The tree mirrors the GLB hierarchy: a single body root -> turret -> gun.
        var body = Assert.Single(viewModel.Inspector.GlbNodes);
        Assert.Equal("body", body.Id);
        Assert.True(body.HasMesh);
        Assert.True(body.IsSelectable);
        var turret = Assert.Single(body.Children);
        Assert.Equal("turret", turret.Id);
        var gun = Assert.Single(turret.Children);
        Assert.Equal("gun", gun.Id);
        Assert.True(gun.HasMesh);

        // The node_id param is NOT offered in the generic params for the extractor:
        // the tree picker is the only way to set it.
        Assert.DoesNotContain(
            viewModel.Inspector.AssetGraphParams,
            p => p.Name == "node_id");
    }

    // The file node's source_path may be a "Copy as path" value wrapped in double
    // quotes ("...gltf/sample_rig.glb"). The picker forwards it verbatim (only trimmed) —
    // stripping the surrounding quotes is the engine's resolve_path job now, not the
    // view-model's — so the authored quotes survive the hand-off to the import.
    [Fact]
    public void MeshFromGlbScenePickerForwardsQuotedSourcePathVerbatim()
    {
        var projectDir = Path.Combine(Path.GetTempPath(), "wz-glb-quoted-vm");
        var session = new RecordingEditorSession
        {
            GlbHierarchy = new EngineGlbSceneHierarchy
            {
                Ok = true,
                Components = [GlbComponent("body", hasMesh: true)],
            },
        };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(assetGraph: GlbExtractorGraph(
                sourcePath: "\"gltf/sample_rig.glb\"")),
            editorSession: session,
            projectDirectory: projectDir);

        var extractor = Assert.Single(
            viewModel.AssetGraph.Nodes,
            n => n.SchemaLabel == "e7000414");
        viewModel.AssetGraph.SelectNode(extractor);

        // The quoted source_path is forwarded as authored; the engine strips the
        // surrounding quotes when it roots the path.
        var import = Assert.Single(session.GlbHierarchyImports);
        Assert.Equal("\"gltf/sample_rig.glb\"", import.Path);

        Assert.True(viewModel.Inspector.HasGlbNodes);
        Assert.False(viewModel.Inspector.HasGlbNodePickerHint);
    }

    // Clicking a mesh-bearing tree node sets the extractor's `node_id` param (the same
    // param-set path as the text field) and moves the current-pick highlight to it.
    [Fact]
    public void MeshFromGlbScenePickClickSetsNodeIdParam()
    {
        var session = new RecordingEditorSession
        {
            GlbHierarchy = new EngineGlbSceneHierarchy
            {
                Ok = true,
                Components =
                [
                    GlbComponent("body", hasMesh: true),
                    GlbComponent("turret", hasMesh: true, parentId: "body"),
                ],
            },
        };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(assetGraph: GlbExtractorGraph()),
            editorSession: session,
            projectDirectory: Path.Combine(Path.GetTempPath(), "wz-glb-pick-vm"));

        var extractor = Assert.Single(
            viewModel.AssetGraph.Nodes,
            n => n.SchemaLabel == "e7000414");
        viewModel.AssetGraph.SelectNode(extractor);

        var body = Assert.Single(viewModel.Inspector.GlbNodes);
        var turret = Assert.Single(body.Children);
        turret.PickCommand.Execute(null);

        // The pick set node_id = the clicked node's id on the extractor (id 30).
        var paramSet = Assert.Single(session.NodeParams);
        Assert.Equal(30ul, paramSet.NodeId);
        Assert.Equal("node_id", paramSet.Name);
        Assert.Equal("turret", paramSet.Value);

        // The highlight moved to the picked node.
        Assert.True(turret.IsCurrentPick);
        Assert.False(body.IsCurrentPick);
    }

    // A group (mesh-less) node is shown for structure but is not pickable: clicking it
    // sets nothing (the engine extractor errors on a mesh-less node).
    [Fact]
    public void MeshFromGlbSceneGroupNodeIsNotPickable()
    {
        var session = new RecordingEditorSession
        {
            GlbHierarchy = new EngineGlbSceneHierarchy
            {
                Ok = true,
                Components =
                [
                    // A mesh-less group containing one mesh child.
                    GlbComponent("rig", hasMesh: false),
                    GlbComponent("body", hasMesh: true, parentId: "rig"),
                ],
            },
        };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(assetGraph: GlbExtractorGraph()),
            editorSession: session,
            projectDirectory: Path.Combine(Path.GetTempPath(), "wz-glb-group-vm"));

        var extractor = Assert.Single(
            viewModel.AssetGraph.Nodes,
            n => n.SchemaLabel == "e7000414");
        viewModel.AssetGraph.SelectNode(extractor);

        var rig = Assert.Single(viewModel.Inspector.GlbNodes);
        Assert.False(rig.HasMesh);
        Assert.False(rig.IsSelectable);
        Assert.True(rig.IsGroup);

        // Clicking the group is a no-op: no param set.
        rig.PickCommand.Execute(null);
        Assert.Empty(session.NodeParams);

        // Its mesh child remains pickable.
        var body = Assert.Single(rig.Children);
        Assert.True(body.IsSelectable);
    }

    // The current `node_id` param value is reflected as the highlighted tree node.
    [Fact]
    public void MeshFromGlbScenePickerReflectsCurrentNodeIdAsSelected()
    {
        var session = new RecordingEditorSession
        {
            GlbHierarchy = new EngineGlbSceneHierarchy
            {
                Ok = true,
                Components =
                [
                    GlbComponent("body", hasMesh: true),
                    GlbComponent("turret", hasMesh: true, parentId: "body"),
                ],
            },
        };
        var viewModel = new MainWindowViewModel(
            // The extractor already has node_id = "turret" authored.
            ProjectSnapshot(assetGraph: GlbExtractorGraph(currentNodeId: "turret")),
            editorSession: session,
            projectDirectory: Path.Combine(Path.GetTempPath(), "wz-glb-current-vm"));

        var extractor = Assert.Single(
            viewModel.AssetGraph.Nodes,
            n => n.SchemaLabel == "e7000414");
        viewModel.AssetGraph.SelectNode(extractor);

        var body = Assert.Single(viewModel.Inspector.GlbNodes);
        var turret = Assert.Single(body.Children);
        Assert.True(turret.IsCurrentPick);
        Assert.False(body.IsCurrentPick);
    }

    // Defensive: an extractor not connected to a Scene-from-GLB shows the section with
    // a hint (no tree, no import) and never throws. The tree picker is the only way to
    // set node_id, so node_id is NOT offered in the generic params even when unwired.
    [Fact]
    public void MeshFromGlbScenePickerHintsWhenNotConnected()
    {
        var session = new RecordingEditorSession
        {
            GlbHierarchy = new EngineGlbSceneHierarchy { Ok = true },
        };
        // Just the extractor node, with no incoming `scene` edge.
        var graph = new EngineAssetGraphSnapshotResponse
        {
            Ok = true,
            Snapshot = new EngineAssetGraphSnapshot
            {
                Nodes =
                [
                    MeshFromGlbSceneNode(id: 30u),
                ],
            },
        };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(assetGraph: graph),
            editorSession: session,
            projectDirectory: Path.Combine(Path.GetTempPath(), "wz-glb-unconn-vm"));

        var extractor = Assert.Single(viewModel.AssetGraph.Nodes);
        viewModel.AssetGraph.SelectNode(extractor);

        Assert.True(viewModel.Inspector.HasGlbNodePicker);
        Assert.False(viewModel.Inspector.HasGlbNodes);
        Assert.True(viewModel.Inspector.HasGlbNodePickerHint);
        Assert.Contains("Scene from GLB", viewModel.Inspector.GlbNodePickerHint);
        // Not connected => the GLB was never imported.
        Assert.Empty(session.GlbHierarchyImports);
        // The node_id param is hidden from the generic editor — the tree is the only
        // way to set it.
        Assert.DoesNotContain(
            viewModel.Inspector.AssetGraphParams,
            p => p.Name == "node_id");
    }

    // Defensive: when the import returns Ok=false the picker shows a "Couldn't read
    // GLB" hint with no tree, and never throws.
    [Fact]
    public void MeshFromGlbScenePickerHintsWhenImportFails()
    {
        var session = new RecordingEditorSession
        {
            GlbHierarchy = new EngineGlbSceneHierarchy
            {
                Ok = false,
                Error = "could not open GLB",
            },
        };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(assetGraph: GlbExtractorGraph()),
            editorSession: session,
            projectDirectory: Path.Combine(Path.GetTempPath(), "wz-glb-fail-vm"));

        var extractor = Assert.Single(
            viewModel.AssetGraph.Nodes,
            n => n.SchemaLabel == "e7000414");
        viewModel.AssetGraph.SelectNode(extractor);

        // The import was attempted (the connection resolved) but failed.
        Assert.Single(session.GlbHierarchyImports);
        Assert.True(viewModel.Inspector.HasGlbNodePicker);
        Assert.False(viewModel.Inspector.HasGlbNodes);
        Assert.True(viewModel.Inspector.HasGlbNodePickerHint);
        Assert.Contains("Couldn't read GLB", viewModel.Inspector.GlbNodePickerHint);
    }

    // Selecting a different (non-extractor) asset-graph node hides the picker, so the
    // section is scoped to the "Mesh from GLB scene" node only.
    [Fact]
    public void GlbNodePickerHiddenForNonExtractorNodes()
    {
        var session = new RecordingEditorSession
        {
            GlbHierarchy = new EngineGlbSceneHierarchy { Ok = true },
        };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(assetGraph: GlbExtractorGraph()),
            editorSession: session,
            projectDirectory: Path.Combine(Path.GetTempPath(), "wz-glb-other-vm"));

        // The Scene-from-GLB node (e7000711) is not the extractor => no picker.
        var sceneFromGlb = Assert.Single(
            viewModel.AssetGraph.Nodes,
            n => n.SchemaLabel == "e7000711");
        viewModel.AssetGraph.SelectNode(sceneFromGlb);

        Assert.False(viewModel.Inspector.HasGlbNodePicker);
        Assert.False(viewModel.Inspector.HasGlbNodes);
    }

    // An asset graph wired file -> Scene-from-GLB (e7000711) -> Mesh-from-GLB-scene
    // (e7000414): the file node carries source_path "gltf/sample_rig.glb"; the extractor's
    // `scene` input is fed by the Scene-from-GLB node, whose `source_file` input is
    // fed by the file node. currentNodeId, when set, authors the extractor's node_id.
    private static EngineAssetGraphSnapshotResponse GlbExtractorGraph(
        string? currentNodeId = null,
        string sourcePath = "gltf/sample_rig.glb")
    {
        return new EngineAssetGraphSnapshotResponse
        {
            Ok = true,
            Snapshot = new EngineAssetGraphSnapshot
            {
                Nodes =
                [
                    // The GLB file node (id 10): its source_path is the GLB on disk.
                    new EngineAssetGraphNode
                    {
                        Id = 10u,
                        TypeName = "GLB file",
                        Schema = "e7000713",
                        DisplayName = "sample_rig.glb",
                        OutputPorts =
                        [
                            new EngineAssetGraphPort
                            {
                                Index = 0,
                                Name = "output",
                                Label = "GLB file",
                            },
                        ],
                        Params =
                        [
                            new EngineAssetGraphParam
                            {
                                Name = "source_path",
                                Kind = "filepath",
                                Value = sourcePath,
                            },
                        ],
                    },
                    // The Scene-from-GLB node (id 20): `source_file` input <- file node.
                    new EngineAssetGraphNode
                    {
                        Id = 20u,
                        TypeName = "Scene",
                        Schema = "e7000711",
                        DisplayName = "Scene from GLB",
                        InputPorts =
                        [
                            new EngineAssetGraphPort
                            {
                                Index = 0,
                                Name = "source_file",
                                Label = "GLB file",
                            },
                        ],
                        OutputPorts =
                        [
                            new EngineAssetGraphPort
                            {
                                Index = 0,
                                Name = "output",
                                Label = "Scene",
                            },
                        ],
                    },
                    // The Mesh-from-GLB-scene extractor (id 30): `scene` input <- the
                    // Scene-from-GLB node; carries the node_id param (the pick target).
                    MeshFromGlbSceneNode(id: 30u, currentNodeId: currentNodeId),
                ],
                Edges =
                [
                    // file.output -> scene.source_file
                    new EngineAssetGraphEdge
                    {
                        Id = 1u,
                        From = 10u,
                        To = 20u,
                        ToInputPort = 0u,
                    },
                    // scene.output -> extractor.scene
                    new EngineAssetGraphEdge
                    {
                        Id = 2u,
                        From = 20u,
                        To = 30u,
                        ToInputPort = 0u,
                    },
                ],
            },
        };
    }

    private static EngineAssetGraphNode MeshFromGlbSceneNode(
        ulong id,
        string? currentNodeId = null)
    {
        return new EngineAssetGraphNode
        {
            Id = id,
            TypeName = "Mesh",
            Schema = "e7000414",
            DisplayName = "Mesh from GLB scene",
            InputPorts =
            [
                new EngineAssetGraphPort
                {
                    Index = 0,
                    Name = "scene",
                    Label = "Scene",
                },
            ],
            OutputPorts =
            [
                new EngineAssetGraphPort
                {
                    Index = 0,
                    Name = "output",
                    Label = "Mesh",
                },
            ],
            Params =
            [
                new EngineAssetGraphParam
                {
                    Name = "node_id",
                    Kind = "string",
                    Value = currentNodeId ?? string.Empty,
                },
            ],
        };
    }

    private static EngineGlbComponent GlbComponent(
        string id,
        bool hasMesh,
        string? parentId = null,
        string? name = null)
    {
        return new EngineGlbComponent
        {
            Id = id,
            Name = name ?? id,
            ParentId = parentId,
            HasMesh = hasMesh,
        };
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
        ulong? sceneSourceNodeId = null,
        ulong? geometryNodeId = null,
        ulong? renderProgramNodeId = null,
        EngineSceneNodeCollision? collision = null,
        EngineSceneNodeMotion? motion = null,
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
            SceneSourceNodeId = sceneSourceNodeId,
            GeometryNodeId = geometryNodeId,
            RenderProgramNodeId = renderProgramNodeId,
            Collision = collision,
            Motion = motion,
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
        // Defaults to a live viewport so the existing edit tests exercise the
        // happy path; a test flips it off to assert the runtime-down gating.
        public bool RuntimeRunning { get; set; } = true;

        public bool IsRuntimeRunning => RuntimeRunning;

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

        public List<(string NodeId, string BeforeNodeId)> Reorders { get; } = [];

        public List<(string NodeId, int RenderOrder)> RenderOrders { get; } = [];

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

        public EngineActuatorCatalogResponse ActuatorCatalog { get; set; } = new()
        {
            Ok = true,
        };

        public EngineActuatorCatalogResponse LoadActuatorCatalog()
        {
            return ActuatorCatalog;
        }

        public EngineBehaviorModuleCatalogResponse BehaviorModuleParamCatalog { get; set; } =
            new EngineBehaviorModuleCatalogResponse { Ok = true };

        public EngineBehaviorModuleCatalogResponse LoadBehaviorModuleCatalog()
        {
            return BehaviorModuleParamCatalog;
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

        public List<bool> FrameProfilingToggles { get; } = [];

        public void SetFrameProfiling(bool enabled)
        {
            FrameProfilingToggles.Add(enabled);
        }

        public List<bool> SimulationPausedToggles { get; } = [];

        public void SetSimulationPaused(bool paused)
        {
            SimulationPausedToggles.Add(paused);
        }

        public List<(string NodeId, string Module)> AddedBehaviors { get; } = [];

        public List<(string NodeId, string BindingId, string Key, string Kind, string Value)>
            BehaviorConfigs { get; } = [];

        public List<(string NodeId, string BindingId, string Events)> BehaviorEventSets { get; } = [];

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
            BehaviorEventSets.Add((nodeId, bindingId, events));
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse SetNodeBehaviorConfig(
            string nodeId,
            string bindingId,
            string key,
            string kind,
            string value)
        {
            BehaviorConfigs.Add((nodeId, bindingId, key, kind, value));
            // Set RejectBehaviorConfigKey to make ONE key refuse, so a test can
            // drive the "attach reported success but the write was refused" path.
            return string.Equals(key, RejectBehaviorConfigKey, StringComparison.Ordinal)
                ? new EngineMutationResponse
                {
                    Ok = false,
                    Error = $"'{key}' was refused by the engine",
                }
                : new EngineMutationResponse { Ok = true };
        }

        public string? RejectBehaviorConfigKey { get; set; }

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
            return ComponentEditResult();
        }

        public List<(string NodeId, ulong AssetGraphNodeId)> RenderableAssets { get; } = [];

        public EngineMutationResponse SetNodeRenderableAsset(
            string nodeId,
            ulong assetGraphNodeId)
        {
            RenderableAssets.Add((nodeId, assetGraphNodeId));
            return new EngineMutationResponse { Ok = true };
        }

        public List<(string NodeId, ulong AssetGraphNodeId)> AudioRenderables { get; } = [];

        public EngineMutationResponse SetNodeAudioRenderable(
            string nodeId,
            ulong assetGraphNodeId)
        {
            AudioRenderables.Add((nodeId, assetGraphNodeId));
            return ComponentEditResult();
        }

        public List<(string NodeId, bool AutoPlay, bool Enabled)>
            AudioSourcePlays { get; } = [];

        public EngineMutationResponse SetNodeAudioSourcePlay(
            string nodeId,
            bool autoPlay,
            bool enabled)
        {
            AudioSourcePlays.Add((nodeId, autoPlay, enabled));
            return ComponentEditResult();
        }

        public List<(string NodeId, ulong AssetGraphNodeId)> GeometryAssets { get; } = [];

        public EngineMutationResponse SetNodeGeometryAsset(
            string nodeId,
            ulong assetGraphNodeId)
        {
            GeometryAssets.Add((nodeId, assetGraphNodeId));
            return new EngineMutationResponse { Ok = true };
        }

        public List<(string NodeId, ulong AssetGraphNodeId)> RenderPrograms { get; } = [];

        public EngineMutationResponse SetNodeRenderProgram(
            string nodeId,
            ulong assetGraphNodeId)
        {
            RenderPrograms.Add((nodeId, assetGraphNodeId));
            return new EngineMutationResponse { Ok = true };
        }

        public List<(string NodeId, string Semantic, ulong AssetGraphNodeId)>
            RenderableBindings { get; } = [];

        public EngineMutationResponse SetNodeRenderableBinding(
            string nodeId,
            string semantic,
            ulong assetGraphNodeId)
        {
            RenderableBindings.Add((nodeId, semantic, assetGraphNodeId));
            return new EngineMutationResponse { Ok = true };
        }

        public List<(string NodeId, string Name, float[]? Value)>
            RenderableParams { get; } = [];

        public EngineMutationResponse SetNodeRenderableParam(
            string nodeId,
            string name,
            float[]? valueXyzw)
        {
            RenderableParams.Add((nodeId, name, valueXyzw));
            return new EngineMutationResponse { Ok = true };
        }

        public List<(string NodeId, ulong AssetGraphNodeId, bool ConstrainMovement)>
            Collisions { get; } = [];

        // Set to make the component-reference verbs REFUSE, so a test can drive
        // the rejected-edit path. The verbs still record the attempt: the point of
        // D3-C13 is that the engine was asked and said no, and the editor mirrored
        // the edit locally anyway.
        public string? RejectComponentEditsWith { get; set; }

        private EngineMutationResponse ComponentEditResult() =>
            RejectComponentEditsWith is { } error
                ? new EngineMutationResponse { Ok = false, Error = error }
                : new EngineMutationResponse { Ok = true };

        public EngineMutationResponse SetNodeCollision(
            string nodeId,
            ulong assetGraphNodeId,
            bool constrainMovement)
        {
            Collisions.Add((nodeId, assetGraphNodeId, constrainMovement));
            return ComponentEditResult();
        }

        public List<(string NodeId, ulong AtmosphereAssetNodeId, bool Enabled)>
            Atmospheres { get; } = [];

        public EngineMutationResponse SetNodeAtmosphere(
            string nodeId,
            ulong atmosphereAssetNodeId,
            bool enabled)
        {
            Atmospheres.Add((nodeId, atmosphereAssetNodeId, enabled));
            return ComponentEditResult();
        }

        public List<(string NodeId, ulong EnvironmentAssetNodeId, bool Enabled)>
            Environments { get; } = [];

        public EngineMutationResponse SetNodeEnvironment(
            string nodeId,
            ulong environmentAssetNodeId,
            bool enabled)
        {
            Environments.Add((nodeId, environmentAssetNodeId, enabled));
            return ComponentEditResult();
        }

        public List<(string NodeId, ulong TargetAssetNodeId, bool IncludeDescendants,
            bool AlsoDrawInScene, bool Enabled)> RenderTargets { get; } = [];

        public EngineMutationResponse SetNodeRenderToTexture(
            string nodeId,
            ulong targetAssetNodeId,
            bool includeDescendants,
            bool alsoDrawInScene,
            bool enabled)
        {
            RenderTargets.Add((nodeId, targetAssetNodeId, includeDescendants,
                alsoDrawInScene, enabled));
            return ComponentEditResult();
        }

        public record MotionTerrainEdit(
            string NodeId,
            bool TerrainConstrained,
            float RideHeight,
            float FootprintRadius,
            bool AlignToSurface,
            float AlignmentStrength);

        public List<MotionTerrainEdit> MotionTerrains { get; } = [];

        public EngineMutationResponse SetNodeMotionTerrain(
            string nodeId,
            bool terrainConstrained,
            float rideHeight,
            float footprintRadius,
            bool alignToSurface,
            float alignmentStrength)
        {
            MotionTerrains.Add(new MotionTerrainEdit(
                nodeId,
                terrainConstrained,
                rideHeight,
                footprintRadius,
                alignToSurface,
                alignmentStrength));
            return new EngineMutationResponse { Ok = true };
        }

        public List<(string NodeId, EngineSceneNodeMotionFilter Filter)>
            MotionFilters { get; } = [];

        public EngineMutationResponse SetNodeMotionFilter(
            string nodeId,
            EngineSceneNodeMotionFilter filter)
        {
            MotionFilters.Add((nodeId, filter));
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
            string glbPath,
            uint sceneIndex)
        {
            GlbHierarchyImports.Add((glbPath, sceneIndex));
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

        public EngineAddNodeResponse AddInochiSharedAssets()
        {
            return new EngineAddNodeResponse { Ok = true, NodeId = NextAddedNodeId++ };
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

        // D3-P039: a read-back that FAILED is not "nothing is grafted". The fake
        // could only ever report success, which is part of why the distinction
        // went unnoticed -- same enabler as the Ok=true-only mutation verbs.
        public bool GraftedSceneOk { get; set; } = true;

        public int LoadGraftedCount { get; private set; }

        public EngineSceneSnapshotResponse LoadGraftedSceneNodes()
        {
            LoadGraftedCount++;
            if (!GraftedSceneOk)
            {
                // A FAILED response carries no snapshot -- that is what the engine
                // actually produces (failed_blob writes no scene nodes) and what
                // FailedSceneSnapshot returns. Handing back the roots anyway made
                // the fake unfalsifiable: the caller re-merged them and the test
                // passed with its guard removed.
                return new EngineSceneSnapshotResponse
                {
                    Ok = false,
                    Error = "grafted read-back failed",
                };
            }

            return new EngineSceneSnapshotResponse
            {
                Ok = true,
                Snapshot = GraftedScene,
            };
        }

        public EngineSceneSnapshotResponse RuntimeSceneSnapshot { get; set; } =
            new EngineSceneSnapshotResponse();

        public int LoadRuntimeSceneSnapshotCount { get; private set; }

        public EngineSceneSnapshotResponse LoadRuntimeSceneSnapshot()
        {
            LoadRuntimeSceneSnapshotCount++;
            return RuntimeSceneSnapshot;
        }

        public EngineMutationResponse ReparentNode(string nodeId, string newParentId)
        {
            Reparents.Add((nodeId, newParentId));
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse ReorderNode(string nodeId, string beforeNodeId)
        {
            Reorders.Add((nodeId, beforeNodeId));
            return new EngineMutationResponse { Ok = true };
        }

        public EngineMutationResponse SetNodeRenderOrder(string nodeId, int renderOrder)
        {
            RenderOrders.Add((nodeId, renderOrder));
            return new EngineMutationResponse { Ok = true };
        }

        public List<string> Removed { get; } = [];

        public EngineMutationResponse RemoveNode(string nodeId)
        {
            Removed.Add(nodeId);
            return new EngineMutationResponse { Ok = true };
        }

        public int SaveSceneCount { get; private set; }

        // Mirrors the real session's contract: the scene lives in the engine, so
        // with no viewport there is nothing to persist and the save FAILS rather
        // than reporting a phantom success.
        public EngineMutationResponse SaveScene()
        {
            SaveSceneCount++;
            return RuntimeRunning
                ? new EngineMutationResponse { Ok = true }
                : new EngineMutationResponse
                {
                    Ok = false,
                    Error = "No engine viewport is running - scene changes were not saved.",
                };
        }

        public List<(string RootNodeId, string OutPath)> SubtreeExports { get; } = [];

        public EngineMutationResponse SubtreeExportResponse { get; set; } = new()
        {
            Ok = true,
        };

        public EngineMutationResponse ExportSubtreeAsScene(
            string rootNodeId,
            string outPath)
        {
            SubtreeExports.Add((rootNodeId, outPath));
            return SubtreeExportResponse;
        }

        public int ReloadBehaviorModulesCount { get; private set; }

        public EngineMutationResponse ReloadBehaviorModules()
        {
            ReloadBehaviorModulesCount++;
            return new EngineMutationResponse { Ok = true };
        }

        public IReadOnlyList<string> BehaviorModuleCatalog { get; set; } = [];

        public List<string> DroppedEdits { get; } = [];

        public IReadOnlyList<string> TakeDroppedEdits()
        {
            var drained = DroppedEdits.ToList();
            DroppedEdits.Clear();   // TAKE semantics, like the engine
            return drained;
        }

        public IReadOnlyList<string> GetBehaviorModuleCatalog()
        {
            return BehaviorModuleCatalog;
        }

        public IReadOnlyList<SceneletInfo> SceneletCatalog { get; set; } = [];

        public IReadOnlyList<SceneletInfo> GetSceneletCatalog()
        {
            return SceneletCatalog;
        }

        public List<string> OpenedScenes { get; } = [];

        public EngineMutationResponse OpenSceneResponse { get; set; } =
            new EngineMutationResponse { Ok = true };

        public EngineMutationResponse OpenScene(string scenePath)
        {
            OpenedScenes.Add(scenePath);
            return OpenSceneResponse;
        }

        // Scenelet names the editor asked the engine to mint, and the canned
        // reply. The engine owns the path, so the fake hands one back rather
        // than the test constructing one.
        public List<string> CreatedScenelets { get; } = [];

        public EngineCreateSceneletResponse CreateSceneletResponse { get; set; } =
            new EngineCreateSceneletResponse { Ok = true, Path = "scenelets/new.scene.json" };

        public EngineCreateSceneletResponse CreateScenelet(string name)
        {
            CreatedScenelets.Add(name);
            return CreateSceneletResponse;
        }

        // Scene-FILE behaviour config writes (issue #303): the editor no longer
        // parses scenelet documents itself, so what a test can observe is which
        // (scene, module, match, key, value) tuples it asked the engine to apply.
        public sealed record SceneFileConfigEdit(
            string ScenePath,
            string Module,
            string MatchKey,
            string MatchValue,
            string ConfigKey,
            string Value);

        public List<SceneFileConfigEdit> SceneFileConfigEdits { get; } = [];

        public EngineSceneFileConfigResponse SceneFileConfigResponse { get; set; } =
            new EngineSceneFileConfigResponse { Ok = true, UpdatedCount = 1 };

        public EngineSceneFileConfigResponse SetSceneFileBehaviorConfig(
            string sceneRelativePath,
            string module,
            string matchKey,
            string matchValue,
            string configKey,
            string value)
        {
            SceneFileConfigEdits.Add(new SceneFileConfigEdit(
                sceneRelativePath, module, matchKey, matchValue, configKey, value));
            return SceneFileConfigResponse;
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
