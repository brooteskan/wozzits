using System.Threading;
using Dock.Model.Core;
using Dock.Model.Mvvm.Controls;
using Wozzits.Editor.Protocol;
using Wozzits.Editor.ViewModels;
using Wozzits.Editor.ViewModels.EditorPanes;

namespace Wozzits.Editor.Tests;

public sealed partial class ProjectOpeningTests
{
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
                                CompileStatus = "ready",
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
        Assert.Equal("Actual graph", viewModel.AssetGraph.GraphOperationStatus);
        Assert.False(viewModel.AssetGraph.HasCommitSucceeded);
        Assert.False(viewModel.AssetGraph.HasCommitFailed);
        Assert.False(viewModel.AssetGraph.IsCommitInProgress);
        Assert.False(viewModel.AssetGraph.HasCompileSucceeded);
        Assert.False(viewModel.AssetGraph.HasCompileFailed);
        Assert.False(viewModel.AssetGraph.IsCompileInProgress);
        Assert.False(viewModel.AssetGraph.IsGraphOperationRunning);

        viewModel.AssetGraph.MarkGraphDraft();

        Assert.False(viewModel.AssetGraph.IsActualGraph);
        Assert.True(viewModel.AssetGraph.IsDraftGraph);
        Assert.Equal("Draft graph", viewModel.AssetGraph.GraphOperationStatus);
        Assert.False(viewModel.AssetGraph.HasCommitSucceeded);
        Assert.False(viewModel.AssetGraph.HasCompileSucceeded);

        viewModel.AssetGraph.MarkCompileResult(succeeded: true);
        Assert.False(viewModel.AssetGraph.IsCompileInProgress);
        Assert.Equal("Graph compiled", viewModel.AssetGraph.GraphOperationStatus);
        Assert.True(viewModel.AssetGraph.HasCompileSucceeded);
        Assert.False(viewModel.AssetGraph.HasCompileFailed);
        Assert.Equal(string.Empty, viewModel.AssetGraph.LastEditError);

        viewModel.AssetGraph.MarkCommitResult(succeeded: false, "commit failed");

        Assert.True(viewModel.AssetGraph.HasCommitFailed);
        Assert.False(viewModel.AssetGraph.HasCommitSucceeded);
        Assert.Equal("Commit failed", viewModel.AssetGraph.GraphOperationStatus);
        Assert.Equal("commit failed", viewModel.AssetGraph.LastEditError);

        viewModel.AssetGraph.MarkCommitResult(succeeded: true);

        Assert.True(viewModel.AssetGraph.IsActualGraph);
        Assert.False(viewModel.AssetGraph.IsDraftGraph);
        Assert.True(viewModel.AssetGraph.HasCommitSucceeded);
        Assert.False(viewModel.AssetGraph.HasCommitFailed);
        Assert.Equal("Graph committed", viewModel.AssetGraph.GraphOperationStatus);
        Assert.Equal(string.Empty, viewModel.AssetGraph.LastEditError);
    }

    [Fact]
    public async Task AssetGraphPaneCommitCommandCommitsThroughEngineSession()
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
        editorSession.AssetGraphSnapshot = new EngineAssetGraphSnapshotResponse
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
                        CompileStatus = "compiled",
                    },
                ],
            },
        };
        await viewModel.AssetGraph.CommitGraphCommand.ExecuteAsync(null);

        Assert.Equal(1, editorSession.CommitCount);
        Assert.True(viewModel.AssetGraph.IsActualGraph);
        Assert.True(viewModel.AssetGraph.HasCommitSucceeded);
        Assert.False(viewModel.AssetGraph.HasCommitFailed);
        Assert.Equal(string.Empty, viewModel.AssetGraph.LastEditError);
        var node = Assert.Single(viewModel.AssetGraph.Nodes);
        Assert.Equal("compiled", node.CompileStatus);
    }

    [Fact]
    public async Task AssetGraphPaneCompileCommandCompilesThroughEngineSession()
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
        editorSession.AssetGraphSnapshot = new EngineAssetGraphSnapshotResponse
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
                        CompileStatus = "compiled",
                    },
                ],
            },
        };
        await viewModel.AssetGraph.CompileGraphCommand.ExecuteAsync(null);

        Assert.Equal(1, editorSession.CompileCount);
        Assert.True(viewModel.AssetGraph.IsDraftGraph);
        Assert.True(viewModel.AssetGraph.HasCompileSucceeded);
        Assert.False(viewModel.AssetGraph.HasCompileFailed);
        Assert.Equal(string.Empty, viewModel.AssetGraph.LastEditError);
        var node = Assert.Single(viewModel.AssetGraph.Nodes);
        Assert.Equal("compiled", node.CompileStatus);
    }

    [Fact]
    public async Task AssetGraphPaneCompileCommandRunsAsync()
    {
        var compileStarted = new ManualResetEventSlim();
        var continueCompile = new ManualResetEventSlim();
        var editorSession = new RecordingEditorSession
        {
            CompileStartedSignal = compileStarted,
            ContinueCompileSignal = continueCompile,
            AssetGraphSnapshot = new EngineAssetGraphSnapshotResponse
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
                            CompileStatus = "compiled",
                        },
                    ],
                },
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

        viewModel.AssetGraph.CompileGraphCommand.Execute(null);
        var compileTask = viewModel.AssetGraph.CompileGraphCommand.ExecutionTask;

        Assert.NotNull(compileTask);
        Assert.True(compileStarted.Wait(TimeSpan.FromSeconds(1)));
        Assert.False(compileTask!.IsCompleted);
        Assert.True(viewModel.AssetGraph.IsCompileInProgress);
        Assert.True(viewModel.AssetGraph.IsGraphOperationRunning);
        Assert.Equal("Compiling graph...", viewModel.AssetGraph.GraphOperationStatus);
        Assert.False(viewModel.AssetGraph.CompileGraphCommand.CanExecute(null));
        Assert.False(viewModel.AssetGraph.CommitGraphCommand.CanExecute(null));

        continueCompile.Set();
        await compileTask;

        Assert.Equal(1, editorSession.CompileCount);
        Assert.False(viewModel.AssetGraph.IsCompileInProgress);
        Assert.False(viewModel.AssetGraph.IsGraphOperationRunning);
        Assert.True(viewModel.AssetGraph.HasCompileSucceeded);
    }

    [Fact]
    public async Task AssetGraphPaneCommitAndCompileCommandsShowEngineFailures()
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

        await viewModel.AssetGraph.CommitGraphCommand.ExecuteAsync(null);

        Assert.True(viewModel.AssetGraph.HasCommitFailed);
        Assert.False(viewModel.AssetGraph.HasCommitSucceeded);
        Assert.Equal("commit failed", viewModel.AssetGraph.LastEditError);

        await viewModel.AssetGraph.CompileGraphCommand.ExecuteAsync(null);

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

    // --- Editing inside a sub-graph drill-in tab (woguls/wozzits-editor#1) -------------

    [Fact]
    public void AddingANodeInsideADrillInTabJoinsThatSubGraph()
    {
        var editorSession = new RecordingEditorSession
        {
            AssetGraphSnapshot = GraphSnapshotOf(1, 2),
            NextAddedNodeId = 1000u,
        };

        var grouping = new AssetGraphGroupingModel();
        var landscape = grouping.CreateSubGraph("Landscape", [1ul]);
        var pane = new AssetGraphEditorPaneViewModel(editorSession, grouping, landscape);
        pane.LoadSnapshot(editorSession.AssetGraphSnapshot);

        // The reload after the add sees the node the engine just created.
        editorSession.AssetGraphSnapshot = GraphSnapshotOf(1, 2, 1000);

        Assert.True(pane.AddNodeAt(schema: 0xE7000002ul, type: 7u, graphX: 300, graphY: 200));

        // It joins the sub-graph the tab is showing...
        Assert.Same(landscape, grouping.SubGraphOfNode(1000));

        // ...and draws on the canvas it was dropped on. Before the fix it was ungrouped,
        // so the projection hid it here and it surfaced on the root canvas only.
        Assert.True(pane.Nodes.Single(node => node.Id == 1000ul).IsCanvasVisible);
    }

    [Fact]
    public void AddingANodeOnTheRootCanvasLeavesItUngrouped()
    {
        var editorSession = new RecordingEditorSession
        {
            AssetGraphSnapshot = GraphSnapshotOf(1, 2),
            NextAddedNodeId = 1000u,
        };

        var grouping = new AssetGraphGroupingModel();
        grouping.CreateSubGraph("Landscape", [1ul]);
        var pane = new AssetGraphEditorPaneViewModel(editorSession, grouping, context: null);
        pane.LoadSnapshot(editorSession.AssetGraphSnapshot);

        editorSession.AssetGraphSnapshot = GraphSnapshotOf(1, 2, 1000);

        Assert.True(pane.AddNodeAt(schema: 0xE7000002ul, type: 7u, graphX: 300, graphY: 200));

        // A root-canvas drop joins no group -- membership follows the pane you drop on.
        Assert.Null(grouping.SubGraphOfNode(1000));
        Assert.True(pane.Nodes.Single(node => node.Id == 1000ul).IsCanvasVisible);
    }

    [Fact]
    public void AddingANodeInADrillInTabRefreshesTheRootCanvas()
    {
        var editorSession = new RecordingEditorSession
        {
            AssetGraphSnapshot = GraphSnapshotOf(1, 2),
            NextAddedNodeId = 1000u,
        };

        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(assetGraph: GraphSnapshotOf(1, 2)),
            editorSession: editorSession);

        viewModel.AssetGraph.SelectNode(
            viewModel.AssetGraph.Nodes.Single(node => node.Id == 1ul));
        var landscape = viewModel.AssetGraph.CreateSubGraphFromSelection("Landscape");
        Assert.NotNull(landscape);

        viewModel.AssetGraph.OpenSubGraph(landscape);
        var drillIn = GraphPanes(viewModel.EditorLayout)
            .Single(pane => !ReferenceEquals(pane, viewModel.AssetGraph));

        editorSession.AssetGraphSnapshot = GraphSnapshotOf(1, 2, 1000);
        Assert.True(drillIn.AddNodeAt(schema: 0xE7000002ul, type: 7u, graphX: 300, graphY: 200));

        // Each pane holds its own snapshot of the one draft, so the shell re-pulls the
        // others; without that the root canvas keeps listing the pre-add node set.
        Assert.Contains(viewModel.AssetGraph.Nodes, node => node.Id == 1000ul);
        Assert.Same(landscape, viewModel.AssetGraph.SubGraphOfNode(1000));
    }

    [Fact]
    public void AddingANodeInADrillInTabLeavesTheInspectorOnTheNewNode()
    {
        var editorSession = new RecordingEditorSession
        {
            AssetGraphSnapshot = GraphSnapshotOf(1, 2),
            NextAddedNodeId = 1000u,
        };

        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(assetGraph: GraphSnapshotOf(1, 2)),
            editorSession: editorSession);

        viewModel.AssetGraph.SelectNode(
            viewModel.AssetGraph.Nodes.Single(node => node.Id == 1ul));
        var landscape = viewModel.AssetGraph.CreateSubGraphFromSelection("Landscape");
        Assert.NotNull(landscape);

        viewModel.AssetGraph.OpenSubGraph(landscape);
        var drillIn = GraphPanes(viewModel.EditorLayout)
            .Single(pane => !ReferenceEquals(pane, viewModel.AssetGraph));

        // Leave a selection on the ROOT pane: re-pulling it restores that selection, which
        // re-raises SelectedNodeChanged and would otherwise steal the inspector back.
        viewModel.AssetGraph.SelectNode(
            viewModel.AssetGraph.Nodes.Single(node => node.Id == 2ul));
        Assert.Equal("2", viewModel.Inspector.AssetGraphNodeId);

        editorSession.AssetGraphSnapshot = GraphSnapshotOf(1, 2, 1000);
        Assert.True(drillIn.AddNodeAt(schema: 0xE7000002ul, type: 7u, graphX: 300, graphY: 200));

        Assert.Equal("1000", viewModel.Inspector.AssetGraphNodeId);
    }

    private static EngineAssetGraphSnapshotResponse GraphSnapshotOf(params ulong[] nodeIds) =>
        new()
        {
            Ok = true,
            Snapshot = new EngineAssetGraphSnapshot
            {
                Zoom = 1.0,
                Nodes = nodeIds
                    .Select((id, index) => new EngineAssetGraphNode
                    {
                        Id = id,
                        TypeName = "Node",
                        DisplayName = $"node {id}",
                        X = index * 300,
                    })
                    .ToList(),
            },
        };

    // Every asset-graph pane in the dock tree: the root canvas plus one drill-in tab per
    // opened sub-graph.
    private static IEnumerable<AssetGraphEditorPaneViewModel> GraphPanes(IDockable? dockable)
    {
        if (dockable is Document { Context: AssetGraphEditorPaneViewModel pane })
        {
            yield return pane;
        }

        if (dockable is IDock { VisibleDockables: { } children })
        {
            foreach (var child in children)
            {
                foreach (var nested in GraphPanes(child))
                {
                    yield return nested;
                }
            }
        }
    }
}
