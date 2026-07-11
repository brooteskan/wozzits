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

    // The asset-graph document host, exposed so the shell can add drill-in sub-graph tabs
    // to it at runtime (issue woguls/wozzits-editor#1).
    public IDocumentDock? AssetGraphDock { get; private set; }

    public IRootDock CreateLayout()
    {
        var sceneTree = new Tool
        {
            Id = "SceneTree",
            Title = "Scene Tree",
            Context = _owner.SceneTree,
            CanClose = false,
            CanPin = false,
            CanFloat = false,
            CanDrag = true,
            CanDrop = true,
            CanDockAsDocument = false,
            DockCapabilityOverrides = DockOverrides(),
            DockGroup = "tools",
            Proportion = 0.24,
        };

        var assetBrowser = new Tool
        {
            Id = "AssetBrowser",
            Title = "Asset Browser",
            Context = _owner.AssetBrowser,
            CanClose = false,
            CanPin = false,
            CanFloat = false,
            CanDrag = true,
            CanDrop = true,
            CanDockAsDocument = false,
            DockCapabilityOverrides = DockOverrides(),
            DockGroup = "tools",
            Proportion = 0.24,
        };

        var sceneTreeDock = new ToolDock
        {
            Id = "SceneTreeDock",
            Title = "Scene Tree",
            ActiveDockable = sceneTree,
            VisibleDockables = _factory.CreateList<IDockable>(sceneTree, assetBrowser),
            CanClose = false,
            CanPin = false,
            CanFloat = false,
            CanDrag = true,
            CanDrop = true,
            CanDockAsDocument = false,
            DockCapabilityOverrides = DockOverrides(),
            DockCapabilityPolicy = DockPolicy(),
            DockGroup = "tools",
            Proportion = 0.24,
        };

        var assetGraph = new Document
        {
            Id = "AssetGraph",
            Title = "Asset Graph",
            Context = _owner.AssetGraph,
            CanClose = false,
            CanFloat = false,
            CanDrag = true,
            CanDrop = true,
            CanDockAsDocument = true,
            DockCapabilityOverrides = DockOverrides(canDockAsDocument: true),
            DockGroup = "documents",
            Proportion = 0.76,
        };

        var assetGraphDock = new DocumentDock
        {
            Id = "AssetGraphDock",
            Title = "Asset Graph",
            ActiveDockable = assetGraph,
            VisibleDockables = _factory.CreateList<IDockable>(assetGraph),
            CanCreateDocument = false,
            CanClose = false,
            CanPin = false,
            CanFloat = false,
            CanDrag = true,
            CanDrop = true,
            CanDockAsDocument = true,
            DockCapabilityOverrides = DockOverrides(canDockAsDocument: true),
            DockCapabilityPolicy = DockPolicy(canDockAsDocument: true),
            DockGroup = "documents",
            Proportion = 0.56,
        };
        AssetGraphDock = assetGraphDock;

        var inspector = new Tool
        {
            Id = "Inspector",
            Title = "Inspector",
            Context = _owner.Inspector,
            CanClose = false,
            CanPin = false,
            CanFloat = false,
            CanDrag = true,
            CanDrop = true,
            CanDockAsDocument = false,
            DockCapabilityOverrides = DockOverrides(),
            DockGroup = "tools",
            Proportion = 0.20,
        };

        var inspectorDock = new ToolDock
        {
            Id = "InspectorDock",
            Title = "Inspector",
            ActiveDockable = inspector,
            VisibleDockables = _factory.CreateList<IDockable>(inspector),
            CanClose = false,
            CanPin = false,
            CanFloat = false,
            CanDrag = true,
            CanDrop = true,
            CanDockAsDocument = false,
            DockCapabilityOverrides = DockOverrides(),
            DockCapabilityPolicy = DockPolicy(),
            DockGroup = "tools",
            Proportion = 0.20,
        };

        var console = new Tool
        {
            Id = "Console",
            Title = "Console",
            Context = _owner.Console,
            CanClose = false,
            CanPin = false,
            CanFloat = false,
            CanDrag = true,
            CanDrop = true,
            CanDockAsDocument = false,
            DockCapabilityOverrides = DockOverrides(),
            DockGroup = "tools",
        };

        var consoleDock = new ToolDock
        {
            Id = "ConsoleDock",
            Title = "Console",
            ActiveDockable = console,
            VisibleDockables = _factory.CreateList<IDockable>(console),
            CanClose = false,
            CanPin = false,
            CanFloat = false,
            CanDrag = true,
            CanDrop = true,
            CanDockAsDocument = false,
            DockCapabilityOverrides = DockOverrides(),
            DockCapabilityPolicy = DockPolicy(),
            DockGroup = "tools",
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
            CanClose = false,
            CanPin = false,
            CanFloat = false,
            CanDrag = true,
            CanDrop = true,
            CanDockAsDocument = false,
            DockCapabilityOverrides = DockOverrides(),
            DockCapabilityPolicy = DockPolicy(),
            DockGroup = "workspace",
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
            CanClose = false,
            CanPin = false,
            CanFloat = false,
            CanDrag = true,
            CanDrop = true,
            CanDockAsDocument = false,
            DockCapabilityOverrides = DockOverrides(),
            DockCapabilityPolicy = DockPolicy(),
            DockGroup = "shell",
        };

        var root = new RootDock
        {
            Id = "Root",
            Title = string.Empty,
            ActiveDockable = shell,
            DefaultDockable = shell,
            VisibleDockables = _factory.CreateList<IDockable>(shell),
            CanClose = false,
            CanPin = false,
            CanFloat = false,
            CanDrag = true,
            CanDrop = true,
            CanDockAsDocument = false,
            DockCapabilityOverrides = DockOverrides(),
            DockCapabilityPolicy = DockPolicy(),
            DockGroup = "root",
            RootDockCapabilityPolicy = DockPolicy(),
        };

        _factory.InitLayout(root);
        return root;
    }

    private static DockCapabilityPolicy DockPolicy(
        bool canClose = false,
        bool canPin = false,
        bool canFloat = false,
        bool canDrag = true,
        bool canDrop = true,
        bool canDockAsDocument = false)
    {
        return new DockCapabilityPolicy
        {
            CanClose = canClose,
            CanPin = canPin,
            CanFloat = canFloat,
            CanDrag = canDrag,
            CanDrop = canDrop,
            CanDockAsDocument = canDockAsDocument,
        };
    }

    private static DockCapabilityOverrides DockOverrides(
        bool canClose = false,
        bool canPin = false,
        bool canFloat = false,
        bool canDrag = true,
        bool canDrop = true,
        bool canDockAsDocument = false)
    {
        return new DockCapabilityOverrides
        {
            CanClose = canClose,
            CanPin = canPin,
            CanFloat = canFloat,
            CanDrag = canDrag,
            CanDrop = canDrop,
            CanDockAsDocument = canDockAsDocument,
        };
    }
}
