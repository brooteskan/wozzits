using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;

namespace Wozzits.Editor.ViewModels;

public sealed class EditorDockLayoutFactory
{
    private readonly Factory _factory = new();
    private readonly MainWindowViewModel _owner;

    public EditorDockLayoutFactory(MainWindowViewModel owner)
    {
        _owner = owner;
    }

    public IFactory Factory => _factory;

    public IRootDock CreateLayout()
    {
        var sceneTree = new Tool
        {
            Id = "SceneTree",
            Title = "Scene Tree",
            Context = _owner.SceneTree,
            CanClose = false,
            CanPin = false,
            Proportion = 0.24,
        };

        var sceneTreeDock = new ToolDock
        {
            Id = "SceneTreeDock",
            Title = "Scene Tree",
            ActiveDockable = sceneTree,
            VisibleDockables = _factory.CreateList<IDockable>(sceneTree),
            Proportion = 0.24,
        };

        var assetGraph = new Document
        {
            Id = "AssetGraph",
            Title = "Asset Graph",
            Context = _owner.AssetGraph,
            CanClose = false,
            Proportion = 0.76,
        };

        var assetGraphDock = new DocumentDock
        {
            Id = "AssetGraphDock",
            Title = "Asset Graph",
            ActiveDockable = assetGraph,
            VisibleDockables = _factory.CreateList<IDockable>(assetGraph),
            CanCreateDocument = false,
            Proportion = 0.56,
        };

        var inspector = new Tool
        {
            Id = "Inspector",
            Title = "Inspector",
            Context = _owner.Inspector,
            CanClose = false,
            CanPin = false,
            Proportion = 0.20,
        };

        var inspectorDock = new ToolDock
        {
            Id = "InspectorDock",
            Title = "Inspector",
            ActiveDockable = inspector,
            VisibleDockables = _factory.CreateList<IDockable>(inspector),
            Proportion = 0.20,
        };

        var console = new Tool
        {
            Id = "Console",
            Title = "Console",
            Context = _owner.Console,
            CanClose = false,
            CanPin = false,
        };

        var consoleDock = new ToolDock
        {
            Id = "ConsoleDock",
            Title = "Console",
            ActiveDockable = console,
            VisibleDockables = _factory.CreateList<IDockable>(console),
            Proportion = 0.28,
        };

        var workspace = new ProportionalDock
        {
            Id = "Workspace",
            Orientation = Orientation.Horizontal,
            VisibleDockables = _factory.CreateList<IDockable>(
                sceneTreeDock,
                _factory.CreateProportionalDockSplitter(),
                assetGraphDock,
                _factory.CreateProportionalDockSplitter(),
                inspectorDock),
            Proportion = 0.72,
        };

        var shell = new ProportionalDock
        {
            Id = "EditorShell",
            Orientation = Orientation.Vertical,
            VisibleDockables = _factory.CreateList<IDockable>(
                workspace,
                _factory.CreateProportionalDockSplitter(),
                consoleDock),
        };

        var root = new RootDock
        {
            Id = "Root",
            Title = string.Empty,
            ActiveDockable = shell,
            DefaultDockable = shell,
            VisibleDockables = _factory.CreateList<IDockable>(shell),
        };

        _factory.InitLayout(root);
        return root;
    }
}
