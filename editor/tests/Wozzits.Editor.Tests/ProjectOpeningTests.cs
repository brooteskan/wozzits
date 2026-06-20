using Wozzits.Editor.Core.Projects;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;
using Wozzits.Editor.ViewModels;
using Wozzits.Editor.ViewModels.EditorPanes;

namespace Wozzits.Editor.Tests;

public sealed class ProjectOpeningTests
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
    public void MainWindowDisplaysEditorHostStartupErrors()
    {
        var viewModel = new MainWindowViewModel(
            projectSnapshot: null,
            editorHostSession: new WozzitsEditorHostSession(
                @"D:\definitely\missing\wozzits_editor_host.exe",
                @"D:\work\project"));

        try
        {
            Assert.Contains("Editor host executable not found", viewModel.EngineLogText);
            Assert.Contains(
                "Editor host executable not found",
                viewModel.Console.LogText);
        }
        finally
        {
            viewModel.Shutdown();
        }
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
    public void NativeEngineClientKeepsCommitAndCompileOutOfInProcessAbiWhenEngineAbiIsBuilt()
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

            var commit = editorSession.CommitAssetGraph();
            Assert.False(commit.Ok);
            Assert.Contains("editor host/runtime channel", commit.Error);

            var compile = editorSession.CompileAssetGraph();
            Assert.False(compile.Ok);
            Assert.Contains("editor host/runtime channel", compile.Error);
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
    public void MainWindowCanDispatchEditorHostLogsToConsole()
    {
        List<Action> posted = [];
        var viewModel = new MainWindowViewModel(
            projectSnapshot: null,
            editorHostSession: new WozzitsEditorHostSession(
                @"D:\definitely\missing\wozzits_editor_host.exe",
                @"D:\work\project"),
            dispatch: action => posted.Add(action));

        try
        {
            Assert.Single(posted);

            posted[0]();

            Assert.Contains(
                "Editor host executable not found",
                viewModel.Console.LogText);
        }
        finally
        {
            viewModel.Shutdown();
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
    public void AssetGraphPaneBuildsCardsFromEngineSnapshot()
    {
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                assetGraph: new EngineAssetGraphSnapshotResponse
                {
                    Ok = true,
                    Snapshot = new EngineAssetGraphSnapshot
                    {
                        Schema = "wozzits.scene_editor.assets.graph.v2",
                        Zoom = 1.5,
                        Nodes =
                        [
                            new EngineAssetGraphNode
                            {
                                Id = 1,
                                Type = 7,
                                TypeName = "ShaderSource",
                                Schema = "e7000002",
                                DisplayName = "shader.hlsl",
                                CompileStatus = "compiled",
                                X = 10,
                                Y = 20,
                                OutputPorts = [new EngineAssetGraphPort { Index = 0, Label = "Shader source" }],
                            },
                            new EngineAssetGraphNode
                            {
                                Id = 2,
                                Type = 1048,
                                TypeName = "Renderable",
                                Schema = "e7000707",
                                DisplayName = "Renderable",
                                CompileStatus = "not resolved",
                                X = 280,
                                Y = 20,
                                InputPorts = [new EngineAssetGraphPort { Index = 0, Label = "Shader source" }],
                                OutputPorts = [new EngineAssetGraphPort { Index = 0, Label = "Renderable" }],
                            },
                        ],
                        Edges =
                        [
                            new EngineAssetGraphEdge
                            {
                                From = 1,
                                To = 2,
                                ToInputPort = 0,
                            },
                        ],
                    },
                }));

        Assert.True(viewModel.AssetGraph.HasGraph);
        Assert.Equal(1.5, viewModel.AssetGraph.Zoom);
        Assert.Equal(viewModel.AssetGraph.GraphWidth * 1.5, viewModel.AssetGraph.ScaledGraphWidth);

        Assert.Collection(
            viewModel.AssetGraph.Nodes,
            source =>
            {
                Assert.Equal(1ul, source.Id);
                Assert.Equal("shader.hlsl", source.DisplayName);
                Assert.Equal("ShaderSource", source.TypeName);
                Assert.Equal("compiled", source.CompileStatus);
                Assert.Equal("e7000002", source.SchemaDisplay);
                Assert.False(source.HasInputPorts);
                var sourceOutput = Assert.Single(source.OutputPorts);
                Assert.Equal(0u, sourceOutput.Index);
                Assert.Equal("Shader source", sourceOutput.Label);
                Assert.Equal(38, source.X);
                Assert.Equal(48, source.Y);
            },
            renderable =>
            {
                Assert.Equal(2ul, renderable.Id);
                Assert.Equal("Renderable", renderable.DisplayName);
                Assert.Equal("Renderable", renderable.TypeName);
                var renderableInput = Assert.Single(renderable.InputPorts);
                Assert.Equal(0u, renderableInput.Index);
                Assert.Equal("Shader source", renderableInput.Label);
            });

        var wire = Assert.Single(viewModel.AssetGraph.Edges);
        Assert.Equal(1ul, wire.FromNodeId);
        Assert.Equal(2ul, wire.ToNodeId);
        Assert.Equal(0u, wire.ToInputPort);
        Assert.Equal(258, wire.StartX);
        Assert.Equal(130, wire.StartY);
        Assert.Equal(308, wire.EndX);
        Assert.Equal(130, wire.EndY);

        var changed = new List<string?>();
        wire.PropertyChanged += (_, e) => changed.Add(e.PropertyName);
        viewModel.AssetGraph.MoveNode(viewModel.AssetGraph.Nodes[0], 12, 8);

        Assert.Equal(270, wire.StartX);
        Assert.Equal(138, wire.StartY);
        Assert.Contains(nameof(AssetGraphEdgeViewModel.StartX), changed);
        Assert.Contains(nameof(AssetGraphEdgeViewModel.StartY), changed);
    }

    [Fact]
    public void AssetGraphPaneConnectsPortsThroughEngineSession()
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
                            new EngineAssetGraphNode
                            {
                                Id = 1,
                                TypeName = "Shader",
                                DisplayName = "shader",
                                OutputPorts =
                                [
                                    new EngineAssetGraphPort
                                    {
                                        Index = 0,
                                        Label = "Shader source",
                                        TypeName = "shader",
                                    },
                                ],
                            },
                            new EngineAssetGraphNode
                            {
                                Id = 2,
                                TypeName = "Renderable",
                                DisplayName = "renderable",
                                X = 260,
                                InputPorts =
                                [
                                    new EngineAssetGraphPort
                                    {
                                        Index = 0,
                                        Label = "Shader source",
                                        TypeName = "shader",
                                    },
                                ],
                            },
                        ],
                    },
                }),
            editorSession: editorSession);

        editorSession.AssetGraphSnapshot = new EngineAssetGraphSnapshotResponse
        {
            Ok = true,
            Snapshot = new EngineAssetGraphSnapshot
            {
                Nodes =
                [
                    new EngineAssetGraphNode
                    {
                        Id = 1,
                        TypeName = "Shader",
                        DisplayName = "shader",
                        OutputPorts = [new EngineAssetGraphPort { Index = 0, Label = "Shader source" }],
                    },
                    new EngineAssetGraphNode
                    {
                        Id = 2,
                        TypeName = "Renderable",
                        DisplayName = "renderable",
                        X = 260,
                        InputPorts = [new EngineAssetGraphPort { Index = 0, Label = "Shader source" }],
                    },
                ],
                Edges =
                [
                    new EngineAssetGraphEdge
                    {
                        Id = 42,
                        From = 1,
                        To = 2,
                        ToInputPort = 0,
                    },
                ],
            },
        };

        var from = viewModel.AssetGraph.Nodes[0];
        var to = viewModel.AssetGraph.Nodes[1];
        var output = Assert.Single(from.OutputPorts);
        var input = Assert.Single(to.InputPorts);

        viewModel.AssetGraph.BeginConnectionPreview(from, output);
        viewModel.AssetGraph.PreviewConnectionTarget(from, input);

        Assert.True(input.IsConnectionTarget);
        Assert.False(input.IsConnectionRejected);

        Assert.True(viewModel.AssetGraph.ConnectToInputPort(from, input));

        Assert.Collection(
            editorSession.ConnectionChecks,
            check =>
            {
                Assert.Equal(1ul, check.FromNodeId);
                Assert.Equal(2ul, check.ToNodeId);
                Assert.Equal(0u, check.ToInputPort);
            });
        Assert.Collection(
            editorSession.Connections,
            connection =>
            {
                Assert.Equal(1ul, connection.FromNodeId);
                Assert.Equal(2ul, connection.ToNodeId);
                Assert.Equal(0u, connection.ToInputPort);
            });
        var edge = Assert.Single(viewModel.AssetGraph.Edges);
        Assert.Equal(42ul, edge.Id);
        Assert.True(viewModel.AssetGraph.IsDraftGraph);
    }

    [Fact]
    public void AssetGraphPaneFindsPortsByGraphCoordinate()
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
                                Id = 1,
                                TypeName = "Renderable",
                                DisplayName = "renderable",
                                X = 100,
                                Y = 40,
                                InputPorts = [new EngineAssetGraphPort { Index = 0, Label = "Shader source" }],
                                OutputPorts = [new EngineAssetGraphPort { Index = 0, Label = "Renderable" }],
                            },
                        ],
                    },
                }));

        var node = Assert.Single(viewModel.AssetGraph.Nodes);
        var input = Assert.Single(node.InputPorts);
        var output = Assert.Single(node.OutputPorts);

        Assert.Same(input, viewModel.AssetGraph.FindInputPortAt(node.X + 4, node.Y + 82));
        Assert.Same(output, viewModel.AssetGraph.FindOutputPortAt(node.X + 218, node.Y + 82));
        Assert.Null(viewModel.AssetGraph.FindInputPortAt(node.X + 218, node.Y + 82));
        Assert.Null(viewModel.AssetGraph.FindOutputPortAt(node.X + 4, node.Y + 82));
    }

    [Fact]
    public void MainWindowDoesNotReplaceProjectGraphWithEmptySessionGraph()
    {
        var editorSession = new RecordingEditorSession
        {
            AssetGraphSnapshot = new EngineAssetGraphSnapshotResponse
            {
                Ok = true,
                Snapshot = new EngineAssetGraphSnapshot(),
            },
        };
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
                                Id = 1,
                                TypeName = "Shader",
                                DisplayName = "shader",
                            },
                        ],
                    },
                }),
            editorSession: editorSession);

        var node = Assert.Single(viewModel.AssetGraph.Nodes);
        Assert.Equal(1ul, node.Id);
        Assert.Equal("shader", node.DisplayName);
    }

    [Fact]
    public void AssetGraphPaneDisconnectsEdgesThroughEngineSession()
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
                            new EngineAssetGraphNode
                            {
                                Id = 1,
                                TypeName = "Shader",
                                DisplayName = "shader",
                                OutputPorts = [new EngineAssetGraphPort { Index = 0, Label = "Shader source" }],
                            },
                            new EngineAssetGraphNode
                            {
                                Id = 2,
                                TypeName = "Renderable",
                                DisplayName = "renderable",
                                X = 260,
                                InputPorts = [new EngineAssetGraphPort { Index = 0, Label = "Shader source" }],
                            },
                        ],
                        Edges =
                        [
                            new EngineAssetGraphEdge
                            {
                                Id = 88,
                                From = 1,
                                To = 2,
                                ToInputPort = 0,
                            },
                        ],
                    },
                }),
            editorSession: editorSession);

        editorSession.AssetGraphSnapshot = new EngineAssetGraphSnapshotResponse
        {
            Ok = true,
            Snapshot = new EngineAssetGraphSnapshot
            {
                Nodes =
                [
                    new EngineAssetGraphNode
                    {
                        Id = 1,
                        TypeName = "Shader",
                        DisplayName = "shader",
                        OutputPorts = [new EngineAssetGraphPort { Index = 0, Label = "Shader source" }],
                    },
                    new EngineAssetGraphNode
                    {
                        Id = 2,
                        TypeName = "Renderable",
                        DisplayName = "renderable",
                        X = 260,
                        InputPorts = [new EngineAssetGraphPort { Index = 0, Label = "Shader source" }],
                    },
                ],
            },
        };

        var edge = Assert.Single(viewModel.AssetGraph.Edges);

        Assert.True(viewModel.AssetGraph.DisconnectEdge(edge));

        Assert.Collection(
            editorSession.DisconnectedEdges,
            edgeId => Assert.Equal(88ul, edgeId));
        Assert.Empty(viewModel.AssetGraph.Edges);
        Assert.True(viewModel.AssetGraph.IsDraftGraph);
    }

    [Fact]
    public void AssetGraphPanePersistsMovedNodePositionThroughEngineSession()
    {
        var editorSession = new RecordingEditorSession();
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                assetGraph: new EngineAssetGraphSnapshotResponse
                {
                    Ok = true,
                    Snapshot = new EngineAssetGraphSnapshot
                    {
                        Zoom = 2.0,
                        Nodes =
                        [
                            new EngineAssetGraphNode
                            {
                                Id = 4,
                                TypeName = "Shader",
                                DisplayName = "shader",
                                X = 12,
                                Y = 20,
                            },
                        ],
                    },
                }),
            editorSession: editorSession);

        var node = Assert.Single(viewModel.AssetGraph.Nodes);

        viewModel.AssetGraph.SelectNode(node);
        viewModel.AssetGraph.MoveNodeByScreenDelta(node, 30, 40);
        viewModel.AssetGraph.CommitNodePosition(node);

        Assert.True(node.IsSelected);
        Assert.Equal(55, node.X);
        Assert.Equal(68, node.Y);

        var position = Assert.Single(editorSession.AssetGraphPositions);
        Assert.Equal(4ul, position.NodeId);
        Assert.Equal(27, position.X);
        Assert.Equal(40, position.Y);
    }

    [Fact]
    public void AssetGraphPaneCanMoveAndPersistMultipleSelectedNodes()
    {
        var editorSession = new RecordingEditorSession();
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                assetGraph: new EngineAssetGraphSnapshotResponse
                {
                    Ok = true,
                    Snapshot = new EngineAssetGraphSnapshot
                    {
                        Zoom = 2.0,
                        Nodes =
                        [
                            new EngineAssetGraphNode
                            {
                                Id = 1,
                                TypeName = "Shader",
                                DisplayName = "shader",
                                X = 10,
                                Y = 20,
                            },
                            new EngineAssetGraphNode
                            {
                                Id = 2,
                                TypeName = "Mesh",
                                DisplayName = "mesh",
                                X = 280,
                                Y = 20,
                            },
                            new EngineAssetGraphNode
                            {
                                Id = 3,
                                TypeName = "Renderable",
                                DisplayName = "renderable",
                                X = 600,
                                Y = 20,
                            },
                        ],
                    },
                }),
            editorSession: editorSession);

        var first = viewModel.AssetGraph.Nodes[0];
        var second = viewModel.AssetGraph.Nodes[1];
        var third = viewModel.AssetGraph.Nodes[2];

        viewModel.AssetGraph.SelectNode(first);
        viewModel.AssetGraph.ToggleNodeSelection(second);
        viewModel.AssetGraph.MoveSelectedNodesByScreenDelta(first, 20, 40);
        viewModel.AssetGraph.CommitSelectedNodePositions(first);

        Assert.True(first.IsSelected);
        Assert.True(second.IsSelected);
        Assert.False(third.IsSelected);
        Assert.Equal(48, first.X);
        Assert.Equal(68, first.Y);
        Assert.Equal(318, second.X);
        Assert.Equal(68, second.Y);
        Assert.Equal(628, third.X);
        Assert.Equal(48, third.Y);

        Assert.Collection(
            editorSession.AssetGraphPositions,
            position =>
            {
                Assert.Equal(1ul, position.NodeId);
                Assert.Equal(20, position.X);
                Assert.Equal(40, position.Y);
            },
            position =>
            {
                Assert.Equal(2ul, position.NodeId);
                Assert.Equal(290, position.X);
                Assert.Equal(40, position.Y);
            });
    }

    [Fact]
    public void AssetGraphPaneMovesSelectedNodesByGraphDelta()
    {
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                assetGraph: new EngineAssetGraphSnapshotResponse
                {
                    Ok = true,
                    Snapshot = new EngineAssetGraphSnapshot
                    {
                        Zoom = 0.5,
                        Nodes =
                        [
                            new EngineAssetGraphNode
                            {
                                Id = 1,
                                TypeName = "Shader",
                                DisplayName = "shader",
                                X = 10,
                                Y = 20,
                            },
                            new EngineAssetGraphNode
                            {
                                Id = 2,
                                TypeName = "Mesh",
                                DisplayName = "mesh",
                                X = 280,
                                Y = 20,
                            },
                        ],
                    },
                }));

        var first = viewModel.AssetGraph.Nodes[0];
        var second = viewModel.AssetGraph.Nodes[1];

        viewModel.AssetGraph.SelectNode(first);
        viewModel.AssetGraph.ToggleNodeSelection(second);
        viewModel.AssetGraph.MoveSelectedNodesByGraphDelta(first, 12, 8);

        Assert.Equal(50, first.X);
        Assert.Equal(56, first.Y);
        Assert.Equal(320, second.X);
        Assert.Equal(56, second.Y);
    }

    [Fact]
    public void AssetGraphPaneCanSelectNodesByRectangle()
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
                                Id = 1,
                                TypeName = "Shader",
                                DisplayName = "shader",
                                X = 10,
                                Y = 20,
                            },
                            new EngineAssetGraphNode
                            {
                                Id = 2,
                                TypeName = "Mesh",
                                DisplayName = "mesh",
                                X = 280,
                                Y = 20,
                            },
                            new EngineAssetGraphNode
                            {
                                Id = 3,
                                TypeName = "Renderable",
                                DisplayName = "renderable",
                                X = 600,
                                Y = 20,
                            },
                        ],
                    },
                }));

        var first = viewModel.AssetGraph.Nodes[0];
        var second = viewModel.AssetGraph.Nodes[1];
        var third = viewModel.AssetGraph.Nodes[2];

        viewModel.AssetGraph.SelectNodesInRectangle(
            0,
            0,
            280,
            180,
            addToSelection: false);

        var selected = Assert.Single(viewModel.AssetGraph.SelectedNodes);
        Assert.Same(first, selected);
        Assert.True(first.IsSelected);
        Assert.False(second.IsSelected);

        viewModel.AssetGraph.SelectNodesInRectangle(
            300,
            40,
            540,
            180,
            addToSelection: true);

        Assert.Collection(
            viewModel.AssetGraph.SelectedNodes,
            node => Assert.Same(first, node),
            node => Assert.Same(second, node));
        Assert.True(second.IsSelected);
        Assert.False(third.IsSelected);

        viewModel.AssetGraph.SelectNodesInRectangle(
            600,
            40,
            900,
            180,
            addToSelection: false);

        selected = Assert.Single(viewModel.AssetGraph.SelectedNodes);
        Assert.Same(third, selected);
        Assert.False(first.IsSelected);
        Assert.False(second.IsSelected);
        Assert.True(third.IsSelected);
    }

    [Fact]
    public void AssetGraphPaneTracksDraftAndOperationIndicatorStates()
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
                                Id = 4,
                                TypeName = "Shader",
                                DisplayName = "shader",
                            },
                        ],
                    },
                }));

        Assert.True(viewModel.AssetGraph.IsActualGraph);
        Assert.False(viewModel.AssetGraph.IsDraftGraph);
        Assert.False(viewModel.AssetGraph.HasCommitSucceeded);
        Assert.False(viewModel.AssetGraph.HasCommitFailed);
        Assert.False(viewModel.AssetGraph.HasCompileSucceeded);
        Assert.False(viewModel.AssetGraph.HasCompileFailed);

        viewModel.AssetGraph.MarkGraphDraft();

        Assert.False(viewModel.AssetGraph.IsActualGraph);
        Assert.True(viewModel.AssetGraph.IsDraftGraph);
        Assert.False(viewModel.AssetGraph.HasCommitSucceeded);
        Assert.False(viewModel.AssetGraph.HasCompileSucceeded);

        viewModel.AssetGraph.MarkCommitResult(succeeded: false, "commit failed");

        Assert.True(viewModel.AssetGraph.HasCommitFailed);
        Assert.False(viewModel.AssetGraph.HasCommitSucceeded);
        Assert.Equal("commit failed", viewModel.AssetGraph.LastEditError);

        viewModel.AssetGraph.MarkCompileResult(succeeded: true);

        Assert.True(viewModel.AssetGraph.HasCompileSucceeded);
        Assert.False(viewModel.AssetGraph.HasCompileFailed);
        Assert.Equal(string.Empty, viewModel.AssetGraph.LastEditError);

        viewModel.AssetGraph.MarkCommitResult(succeeded: true);

        Assert.True(viewModel.AssetGraph.IsActualGraph);
        Assert.False(viewModel.AssetGraph.IsDraftGraph);
        Assert.True(viewModel.AssetGraph.HasCommitSucceeded);
        Assert.False(viewModel.AssetGraph.HasCommitFailed);
        Assert.Equal(string.Empty, viewModel.AssetGraph.LastEditError);
    }

    [Fact]
    public void AssetGraphPaneCommitCommandCommitsThroughEngineSession()
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
                            new EngineAssetGraphNode
                            {
                                Id = 4,
                                TypeName = "Shader",
                                DisplayName = "shader",
                            },
                        ],
                    },
                }),
            editorSession: editorSession);

        viewModel.AssetGraph.MarkGraphDraft();
        viewModel.AssetGraph.CommitGraphCommand.Execute(null);

        Assert.Equal(1, editorSession.CommitCount);
        Assert.True(viewModel.AssetGraph.IsActualGraph);
        Assert.True(viewModel.AssetGraph.HasCommitSucceeded);
        Assert.False(viewModel.AssetGraph.HasCommitFailed);
        Assert.Equal(string.Empty, viewModel.AssetGraph.LastEditError);
    }

    [Fact]
    public void AssetGraphPaneCompileCommandCompilesThroughEngineSession()
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
                            new EngineAssetGraphNode
                            {
                                Id = 4,
                                TypeName = "Shader",
                                DisplayName = "shader",
                            },
                        ],
                    },
                }),
            editorSession: editorSession);

        viewModel.AssetGraph.CompileGraphCommand.Execute(null);

        Assert.Equal(1, editorSession.CompileCount);
        Assert.True(viewModel.AssetGraph.HasCompileSucceeded);
        Assert.False(viewModel.AssetGraph.HasCompileFailed);
        Assert.Equal(string.Empty, viewModel.AssetGraph.LastEditError);
    }

    [Fact]
    public void AssetGraphPaneCommitAndCompileCommandsShowEngineFailures()
    {
        var editorSession = new RecordingEditorSession
        {
            CommitResponse = new EngineMutationResponse
            {
                Ok = false,
                Error = "commit failed",
            },
            CompileResponse = new EngineMutationResponse
            {
                Ok = false,
                Error = "compile failed",
            },
        };
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
                                Id = 4,
                                TypeName = "Shader",
                                DisplayName = "shader",
                            },
                        ],
                    },
                }),
            editorSession: editorSession);

        viewModel.AssetGraph.CommitGraphCommand.Execute(null);

        Assert.True(viewModel.AssetGraph.HasCommitFailed);
        Assert.False(viewModel.AssetGraph.HasCommitSucceeded);
        Assert.Equal("commit failed", viewModel.AssetGraph.LastEditError);

        viewModel.AssetGraph.CompileGraphCommand.Execute(null);

        Assert.True(viewModel.AssetGraph.HasCompileFailed);
        Assert.False(viewModel.AssetGraph.HasCompileSucceeded);
        Assert.Equal("compile failed", viewModel.AssetGraph.LastEditError);
    }

    [Fact]
    public void AssetGraphPanePersistsLatestZoomWhenCommitRequested()
    {
        var editorSession = new RecordingEditorSession();
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                assetGraph: new EngineAssetGraphSnapshotResponse
                {
                    Ok = true,
                    Snapshot = new EngineAssetGraphSnapshot
                    {
                        Zoom = 1.0,
                        Nodes =
                        [
                            new EngineAssetGraphNode
                            {
                                Id = 4,
                                TypeName = "Shader",
                                DisplayName = "shader",
                            },
                        ],
                    },
                }),
            editorSession: editorSession);

        viewModel.AssetGraph.ZoomByWheelDelta(1);
        viewModel.AssetGraph.ZoomByWheelDelta(1);

        Assert.Equal(1.21, viewModel.AssetGraph.Zoom, precision: 6);
        Assert.Empty(editorSession.AssetGraphZooms);
        viewModel.AssetGraph.CommitZoom();
        Assert.Collection(
            editorSession.AssetGraphZooms,
            zoom => Assert.Equal(1.21, zoom, precision: 6));
    }

    [Fact]
    public void MainWindowShowsAssetGraphSnapshotErrors()
    {
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                assetGraph: new EngineAssetGraphSnapshotResponse
                {
                    Ok = false,
                    Error = "asset graph parse failed",
                }));

        Assert.True(viewModel.AssetGraph.HasNoGraph);
        Assert.Equal(
            "Could not load asset graph: asset graph parse failed",
            viewModel.AssetGraph.EmptyState);
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
                                TypeName = "Renderable",
                                Schema = "e7000707",
                                DisplayName = "renderable",
                                CompileStatus = "not resolved",
                                InputPorts = [new EngineAssetGraphPort { Index = 0, Label = "Shader source" }],
                                OutputPorts = [new EngineAssetGraphPort { Index = 0, Label = "Renderable" }],
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
        Assert.Equal("e7000707", viewModel.Inspector.AssetGraphNodeSchema);
        Assert.Equal("not resolved", viewModel.Inspector.AssetGraphNodeCompileStatus);
        Assert.Collection(
            viewModel.Inspector.AssetGraphInputPorts,
            port => Assert.Equal("Shader source", port.Label));
        Assert.Collection(
            viewModel.Inspector.AssetGraphOutputPorts,
            port => Assert.Equal("Renderable", port.Label));

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
        viewModel.Inspector.TranslationX = "9";
        viewModel.Inspector.RotationZ = "45";
        viewModel.Inspector.ScaleY = "6";

        Assert.Empty(editorSession.NodeProperties);
        Assert.Empty(editorSession.Transforms);

        viewModel.Inspector.ApplyNodePropertiesCommand.Execute(null);
        viewModel.Inspector.ApplyTransformCommand.Execute(null);

        var nodeProperties = Assert.Single(editorSession.NodeProperties);
        Assert.Equal("mesh", nodeProperties.NodeId);
        Assert.Equal("renamed mesh", nodeProperties.Name);
        Assert.False(nodeProperties.Visible);

        var transform = Assert.Single(editorSession.Transforms);
        Assert.Equal("mesh", transform.NodeId);
        Assert.Equal("9", transform.Edit.TranslationX);
        Assert.Equal("45", transform.Edit.RotationZ);
        Assert.Equal("6", transform.Edit.ScaleY);

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

        public int CommitCount { get; private set; }

        public int CompileCount { get; private set; }

        public List<AssetGraphPositionEdit> AssetGraphPositions { get; } = [];

        public List<double> AssetGraphZooms { get; } = [];

        public List<AssetGraphConnectionEdit> ConnectionChecks { get; } = [];

        public List<AssetGraphConnectionEdit> Connections { get; } = [];

        public List<ulong> DisconnectedEdges { get; } = [];

        public List<NodePropertiesEdit> NodeProperties { get; } = [];

        public List<TransformEdit> Transforms { get; } = [];

        public List<CameraEdit> Cameras { get; } = [];

        public EngineAssetGraphSnapshotResponse LoadAssetGraphSnapshot()
        {
            return AssetGraphSnapshot;
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

        public EngineMutationResponse CommitAssetGraph()
        {
            ++CommitCount;
            return CommitResponse;
        }

        public EngineMutationResponse CompileAssetGraph()
        {
            ++CompileCount;
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

        public EngineMutationResponse SetSceneNodeTransform(
            string nodeId,
            EngineSceneTransformEdit edit)
        {
            Transforms.Add(new TransformEdit(nodeId, edit));
            return new EngineMutationResponse { Ok = true };
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

    private sealed record TransformEdit(
        string NodeId,
        EngineSceneTransformEdit Edit);

    private sealed record CameraEdit(
        string NodeId,
        EngineSceneCameraEdit Edit);
}
