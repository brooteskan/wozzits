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
}

public sealed class SceneTreeNodeViewModel : ViewModelBase
{
    private bool _isSelected;
    private string _displayName = string.Empty;

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

    public string? ParentId { get; }

    public string Kind { get; }

    public string KindLabel { get; }

    public bool? Visible { get; }

    public EngineSceneRenderableSource RenderableSource { get; }

    public EngineSceneTransform? Transform { get; }

    public EngineSceneCamera? Camera { get; }

    public EngineSceneRenderable? Renderable { get; }

    public IReadOnlyList<EngineSceneComponent> Components { get; }

    public ObservableCollection<SceneTreeNodeViewModel> Children { get; }

    public bool HasChildren => Children.Count > 0;

    public bool IsSelected
    {
        get => _isSelected;
        internal set => SetProperty(ref _isSelected, value);
    }
}
