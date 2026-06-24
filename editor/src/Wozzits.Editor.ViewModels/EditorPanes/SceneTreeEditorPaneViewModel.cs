namespace Wozzits.Editor.ViewModels.EditorPanes;

using System.Collections.ObjectModel;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;

public sealed class SceneTreeEditorPaneViewModel : ViewModelBase
{
    private SceneTreeNodeViewModel? _selectedNode;
    private readonly IWozzitsEngineEditorSession? _editorSession;

    public SceneTreeEditorPaneViewModel(
        IWozzitsEngineEditorSession? editorSession = null)
    {
        _editorSession = editorSession;
    }

    public event Action<SceneTreeNodeViewModel?>? SelectedNodeChanged;

    public ObservableCollection<SceneTreeNodeViewModel> Nodes { get; } = [];

    public string EmptyState { get; private set; } = "No scene loaded.";

    public bool HasScene => Nodes.Count > 0;

    public bool HasNoScene => !HasScene;

    public SceneTreeNodeViewModel? SelectedNode
    {
        get => _selectedNode;
        private set
        {
            if (ReferenceEquals(_selectedNode, value))
            {
                return;
            }

            _selectedNode = value;
            OnPropertyChanged();
            SelectedNodeChanged?.Invoke(_selectedNode);
        }
    }

    public void LoadSnapshot(EngineSceneSnapshotResponse? scene)
    {
        SetSelectedNode(null);
        Nodes.Clear();

        if (scene is null)
        {
            EmptyState = "No scene loaded.";
        }
        else if (!scene.Ok)
        {
            EmptyState = $"Could not load scene: {scene.Error}";
        }
        else
        {
            foreach (var node in scene.Snapshot.Roots)
            {
                Nodes.Add(new SceneTreeNodeViewModel(node));
            }

            EmptyState = Nodes.Count == 0
                ? "Scene has no nodes."
                : string.Empty;
        }

        OnPropertyChanged(nameof(EmptyState));
        OnPropertyChanged(nameof(HasScene));
        OnPropertyChanged(nameof(HasNoScene));
    }

    public void SelectNode(SceneTreeNodeViewModel node)
    {
        ArgumentNullException.ThrowIfNull(node);
        SetSelectedNode(node);
    }

    // Clear the scene-tree selection + its highlight (e.g. when the asset graph
    // takes the active selection). Raises SelectedNodeChanged(null) only if a
    // node was selected.
    public void ClearSelection()
    {
        SetSelectedNode(null);
    }

    private void SetSelectedNode(SceneTreeNodeViewModel? node)
    {
        if (ReferenceEquals(_selectedNode, node))
        {
            return;
        }

        if (_selectedNode is not null)
        {
            _selectedNode.IsSelected = false;
        }

        SelectedNode = node;

        if (SelectedNode is not null)
        {
            SelectedNode.IsSelected = true;
        }
    }

    // Add a new child node under `parent` (null => top level) via the engine,
    // then insert it into the tree with the engine-minted id and select it.
    public void AddChild(SceneTreeNodeViewModel? parent)
    {
        if (_editorSession is null)
        {
            return;
        }

        var response = _editorSession.AddChildNode(parent?.Id ?? string.Empty);
        if (!response.Ok)
        {
            EmptyState = response.Error;
            OnPropertyChanged(nameof(EmptyState));
            return;
        }

        var added = new SceneTreeNodeViewModel(new EngineSceneNode
        {
            Id = response.NodeId,
            ParentId = parent?.Id,
            Kind = "node",
            Visible = true,
        });

        if (parent is null)
        {
            Nodes.Add(added);
        }
        else
        {
            parent.Children.Add(added);
        }

        OnPropertyChanged(nameof(HasScene));
        OnPropertyChanged(nameof(HasNoScene));
        SelectNode(added);
    }

    // Reparent `node` under `newParent` (null => top level) via the engine, then
    // move it in the tree. Rejects dropping a node onto itself or its own
    // descendant; the engine re-validates.
    public void Reparent(
        SceneTreeNodeViewModel node,
        SceneTreeNodeViewModel? newParent)
    {
        if (_editorSession is null || node is null)
        {
            return;
        }
        if (newParent is not null && node.IsSelfOrDescendant(newParent))
        {
            return;
        }

        var response = _editorSession.ReparentNode(
            node.Id,
            newParent?.Id ?? string.Empty);
        if (!response.Ok)
        {
            EmptyState = response.Error;
            OnPropertyChanged(nameof(EmptyState));
            return;
        }

        RemoveFromTree(node);
        if (newParent is null)
        {
            Nodes.Add(node);
        }
        else
        {
            newParent.Children.Add(node);
        }
        node.ParentId = newParent?.Id;
        SelectNode(node);
    }

    // Delete `node` (and its subtree) via the engine, then drop it from the tree.
    public void Remove(SceneTreeNodeViewModel node)
    {
        if (_editorSession is null || node is null)
        {
            return;
        }

        var response = _editorSession.RemoveNode(node.Id);
        if (!response.Ok)
        {
            EmptyState = response.Error;
            OnPropertyChanged(nameof(EmptyState));
            return;
        }

        // Clear the selection if it was inside the removed subtree.
        var selectionRemoved = SelectedNode is not null
            && node.IsSelfOrDescendant(SelectedNode);
        RemoveFromTree(node);
        if (selectionRemoved)
        {
            SetSelectedNode(null);
        }
        OnPropertyChanged(nameof(HasScene));
        OnPropertyChanged(nameof(HasNoScene));
    }

    private bool RemoveFromTree(SceneTreeNodeViewModel node)
    {
        return Nodes.Remove(node) || RemoveFromChildren(Nodes, node);
    }

    private static bool RemoveFromChildren(
        ObservableCollection<SceneTreeNodeViewModel> siblings,
        SceneTreeNodeViewModel node)
    {
        foreach (var sibling in siblings)
        {
            if (sibling.Children.Remove(node)
                || RemoveFromChildren(sibling.Children, node))
            {
                return true;
            }
        }
        return false;
    }
}

public sealed class SceneTreeNodeViewModel : ViewModelBase
{
    private bool _isSelected;
    private string _displayName = string.Empty;
    private string? _parentId;

    public SceneTreeNodeViewModel(EngineSceneNode node)
    {
        Id = node.Id;
        DisplayName = node.DisplayName;
        ParentId = node.ParentId;
        Kind = node.Kind;
        KindLabel = node.Kind;
        Visible = node.Visible;
        RenderableSource = node.RenderableSource;
        Transform = node.Transform;
        Camera = node.Camera;
        Renderable = node.Renderable;
        Components = node.Components;
        Behaviors = node.Behaviors;
        Children = new ObservableCollection<SceneTreeNodeViewModel>(
            node.Children.Select(child => new SceneTreeNodeViewModel(child)));
    }

    public string Id { get; }

    public string DisplayName
    {
        get => _displayName;
        internal set
        {
            if (SetProperty(ref _displayName, value))
            {
                OnPropertyChanged(nameof(DisplayText));
            }
        }
    }

    // Tree display: "id:label" (e.g. "14:terrain mesh"). Falls back to just the
    // id when the node has no distinct label yet (engine-side, an unnamed node's
    // name defaults to its id).
    public string DisplayText =>
        string.IsNullOrEmpty(DisplayName) || DisplayName == Id
            ? Id
            : $"{Id}:{DisplayName}";

    public string? ParentId
    {
        get => _parentId;
        internal set => SetProperty(ref _parentId, value);
    }

    public string Kind { get; }

    public string KindLabel { get; }

    public bool? Visible { get; }

    public EngineSceneRenderableSource RenderableSource { get; }

    public EngineSceneTransform? Transform { get; }

    public EngineSceneCamera? Camera { get; }

    public EngineSceneRenderable? Renderable { get; }

    public IReadOnlyList<EngineSceneComponent> Components { get; }

    // Authored behavior bindings on this node. Mutable so the inspector's
    // live add/remove keeps reselection consistent without a snapshot reload.
    public List<EngineSceneBehavior> Behaviors { get; }

    public ObservableCollection<SceneTreeNodeViewModel> Children { get; }

    public bool HasChildren => Children.Count > 0;

    // True if `candidate` is this node or anywhere in its subtree — used to
    // reject reparenting a node onto itself or one of its own descendants.
    public bool IsSelfOrDescendant(SceneTreeNodeViewModel candidate)
    {
        if (ReferenceEquals(candidate, this))
        {
            return true;
        }
        foreach (var child in Children)
        {
            if (child.IsSelfOrDescendant(candidate))
            {
                return true;
            }
        }
        return false;
    }

    public bool IsSelected
    {
        get => _isSelected;
        internal set => SetProperty(ref _isSelected, value);
    }
}
