namespace Wozzits.Editor.ViewModels.EditorPanes;

using System.ComponentModel;
using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.Input;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;

public sealed class AssetGraphEditorPaneViewModel : ViewModelBase
{
    private const double CanvasPadding = 28.0;
    private const double CardWidth = 220.0;
    private const double CardHeight = 116.0;
    private const double PortRowBaseY = 82.0;
    private const double PortRowSpacing = 18.0;
    private const double MinZoom = 0.25;
    private const double MaxZoom = 4.0;
    private const double WheelZoomFactor = 1.1;
    private readonly IWozzitsEngineEditorSession? _editorSession;
    private string _emptyState = "No asset graph loaded.";
    private string _lastEditError = string.Empty;
    private bool _isDraftGraph;
    private AssetGraphOperationState _commitState = AssetGraphOperationState.Neutral;
    private AssetGraphOperationState _compileState = AssetGraphOperationState.Neutral;
    private double _graphWidth = 640.0;
    private double _graphHeight = 420.0;
    private double _zoom = 1.0;
    private AssetGraphNodeCardViewModel? _selectedNode;
    public event Action<AssetGraphNodeCardViewModel?>? SelectedNodeChanged;

    public AssetGraphEditorPaneViewModel(
        IWozzitsEngineEditorSession? editorSession = null)
    {
        _editorSession = editorSession;
        CommitGraphCommand = new RelayCommand(CommitGraph, () => HasGraph);
        CompileGraphCommand = new RelayCommand(CompileGraph, () => HasGraph);
    }

    public ObservableCollection<AssetGraphNodeCardViewModel> Nodes { get; } = [];

    public ObservableCollection<AssetGraphEdgeViewModel> Edges { get; } = [];

    public ObservableCollection<AssetGraphNodeCardViewModel> SelectedNodes { get; } = [];

    public IRelayCommand CommitGraphCommand { get; }

    public IRelayCommand CompileGraphCommand { get; }

    public string EmptyState
    {
        get => _emptyState;
        private set => SetProperty(ref _emptyState, value);
    }

    public bool HasGraph => Nodes.Count > 0;

    public bool HasNoGraph => !HasGraph;

    public bool IsDraftGraph
    {
        get => _isDraftGraph;
        private set
        {
            if (SetProperty(ref _isDraftGraph, value))
            {
                OnPropertyChanged(nameof(GraphModeStatus));
                OnPropertyChanged(nameof(IsActualGraph));
            }
        }
    }

    public bool IsActualGraph => !IsDraftGraph;

    public string GraphModeStatus => IsDraftGraph
        ? "Draft graph"
        : "Actual graph";

    public bool HasCommitSucceeded => _commitState == AssetGraphOperationState.Succeeded;

    public bool HasCommitFailed => _commitState == AssetGraphOperationState.Failed;

    public bool HasCompileSucceeded => _compileState == AssetGraphOperationState.Succeeded;

    public bool HasCompileFailed => _compileState == AssetGraphOperationState.Failed;

    public string LastEditError
    {
        get => _lastEditError;
        private set
        {
            if (SetProperty(ref _lastEditError, value))
            {
                OnPropertyChanged(nameof(HasLastEditError));
            }
        }
    }

    public bool HasLastEditError => !string.IsNullOrWhiteSpace(LastEditError);

    public double GraphWidth
    {
        get => _graphWidth;
        private set
        {
            if (SetProperty(ref _graphWidth, value))
            {
                OnPropertyChanged(nameof(ScaledGraphWidth));
            }
        }
    }

    public double GraphHeight
    {
        get => _graphHeight;
        private set
        {
            if (SetProperty(ref _graphHeight, value))
            {
                OnPropertyChanged(nameof(ScaledGraphHeight));
            }
        }
    }

    public double ScaledGraphWidth => GraphWidth * Zoom;

    public double ScaledGraphHeight => GraphHeight * Zoom;

    public double Zoom
    {
        get => _zoom;
        private set
        {
            if (SetProperty(ref _zoom, value))
            {
                OnPropertyChanged(nameof(ScaledGraphWidth));
                OnPropertyChanged(nameof(ScaledGraphHeight));
            }
        }
    }

    public AssetGraphNodeCardViewModel? SelectedNode
    {
        get => _selectedNode;
        private set
        {
            if (SetProperty(ref _selectedNode, value))
            {
                SelectedNodeChanged?.Invoke(_selectedNode);
            }
        }
    }

    public void LoadSnapshot(EngineAssetGraphSnapshotResponse? response)
    {
        ClearGraph();
        ClearSelection();
        LastEditError = string.Empty;
        IsDraftGraph = false;
        SetCommitState(AssetGraphOperationState.Neutral);
        SetCompileState(AssetGraphOperationState.Neutral);

        if (response is null)
        {
            EmptyState = "No asset graph loaded.";
            GraphWidth = 640.0;
            GraphHeight = 420.0;
            Zoom = 1.0;
            NotifyGraphStateChanged();
            return;
        }

        if (!response.Ok)
        {
            EmptyState = string.IsNullOrWhiteSpace(response.Error)
                ? "Could not load asset graph."
                : $"Could not load asset graph: {response.Error}";
            GraphWidth = 640.0;
            GraphHeight = 420.0;
            Zoom = 1.0;
            NotifyGraphStateChanged();
            return;
        }

        Zoom = ClampZoom(response.Snapshot.Zoom);

        foreach (var node in response.Snapshot.Nodes)
        {
            Nodes.Add(new AssetGraphNodeCardViewModel(node, CanvasPadding));
        }

        var nodesById = Nodes.ToDictionary(node => node.Id);
        foreach (var edge in response.Snapshot.Edges)
        {
            if (!nodesById.TryGetValue(edge.From, out var from)
                || !nodesById.TryGetValue(edge.To, out var to))
            {
                continue;
            }

            Edges.Add(new AssetGraphEdgeViewModel(
                edge,
                from,
                to,
                CardWidth,
                PortRowBaseY,
                PortRowSpacing));
        }

        EmptyState = Nodes.Count == 0
            ? "Asset graph has no nodes."
            : string.Empty;
        GraphWidth = Nodes.Count == 0
            ? 640.0
            : Nodes.Max(node => node.X + CardWidth + CanvasPadding);
        GraphHeight = Nodes.Count == 0
            ? 420.0
            : Nodes.Max(node => node.Y + CardHeight + CanvasPadding);
        NotifyGraphStateChanged();
    }

    public void SelectNode(AssetGraphNodeCardViewModel? node)
    {
        ClearSelection();

        if (node is null)
        {
            return;
        }

        AddNodeToSelection(node);
    }

    public void ToggleNodeSelection(AssetGraphNodeCardViewModel node)
    {
        if (node.IsSelected)
        {
            RemoveNodeFromSelection(node);
            return;
        }

        AddNodeToSelection(node);
    }

    public void SelectNodesInRectangle(
        double startX,
        double startY,
        double endX,
        double endY,
        bool addToSelection)
    {
        var left = Math.Min(startX, endX);
        var right = Math.Max(startX, endX);
        var top = Math.Min(startY, endY);
        var bottom = Math.Max(startY, endY);

        if (!addToSelection)
        {
            ClearSelection();
        }

        foreach (var node in Nodes)
        {
            if (NodeIntersectsRectangle(node, left, top, right, bottom))
            {
                AddNodeToSelection(node);
            }
        }
    }

    public void MoveNode(
        AssetGraphNodeCardViewModel node,
        double deltaX,
        double deltaY)
    {
        node.X = Math.Max(CanvasPadding, node.X + deltaX);
        node.Y = Math.Max(CanvasPadding, node.Y + deltaY);
        EnsureGraphBounds();
    }

    public void MoveNodeByScreenDelta(
        AssetGraphNodeCardViewModel node,
        double deltaX,
        double deltaY)
    {
        MoveNode(node, deltaX / Zoom, deltaY / Zoom);
    }

    public void MoveSelectedNodesByScreenDelta(
        AssetGraphNodeCardViewModel anchorNode,
        double deltaX,
        double deltaY)
    {
        var nodes = NodesToMove(anchorNode);
        if (nodes.Count == 0)
        {
            return;
        }

        var graphDeltaX = deltaX / Zoom;
        var graphDeltaY = deltaY / Zoom;
        graphDeltaX = Math.Max(
            CanvasPadding - nodes.Min(node => node.X),
            graphDeltaX);
        graphDeltaY = Math.Max(
            CanvasPadding - nodes.Min(node => node.Y),
            graphDeltaY);

        foreach (var node in nodes)
        {
            node.X += graphDeltaX;
            node.Y += graphDeltaY;
        }

        EnsureGraphBounds();
    }

    public void ZoomByWheelDelta(double wheelDeltaY)
    {
        if (wheelDeltaY == 0)
        {
            return;
        }

        var next = wheelDeltaY > 0
            ? Zoom * WheelZoomFactor
            : Zoom / WheelZoomFactor;
        SetZoom(next);
    }

    public void CommitZoom()
    {
        if (_editorSession is null)
        {
            LastEditError = "Engine editor session is not available.";
            return;
        }

        var response = _editorSession.SetAssetGraphZoom(Zoom);
        LastEditError = response.Ok ? string.Empty : response.Error;
    }

    public void CommitNodePosition(AssetGraphNodeCardViewModel node)
    {
        if (_editorSession is null)
        {
            LastEditError = "Engine editor session is not available.";
            return;
        }

        var response = _editorSession.SetAssetGraphNodePosition(
            node.Id,
            node.X - CanvasPadding,
            node.Y - CanvasPadding);
        LastEditError = response.Ok ? string.Empty : response.Error;
    }

    public void CommitSelectedNodePositions(AssetGraphNodeCardViewModel anchorNode)
    {
        var nodes = NodesToMove(anchorNode);
        if (nodes.Count == 0)
        {
            return;
        }

        if (_editorSession is null)
        {
            LastEditError = "Engine editor session is not available.";
            return;
        }

        LastEditError = string.Empty;
        foreach (var node in nodes)
        {
            var response = _editorSession.SetAssetGraphNodePosition(
                node.Id,
                node.X - CanvasPadding,
                node.Y - CanvasPadding);
            if (!response.Ok)
            {
                LastEditError = response.Error;
                return;
            }
        }
    }

    public void MarkGraphDraft()
    {
        IsDraftGraph = true;
        SetCommitState(AssetGraphOperationState.Neutral);
        SetCompileState(AssetGraphOperationState.Neutral);
    }

    public void MarkCommitResult(bool succeeded, string error = "")
    {
        SetCommitState(succeeded
            ? AssetGraphOperationState.Succeeded
            : AssetGraphOperationState.Failed);
        LastEditError = succeeded ? string.Empty : error;
        if (succeeded)
        {
            IsDraftGraph = false;
        }
    }

    public void MarkCompileResult(bool succeeded, string error = "")
    {
        SetCompileState(succeeded
            ? AssetGraphOperationState.Succeeded
            : AssetGraphOperationState.Failed);
        LastEditError = succeeded ? string.Empty : error;
    }

    private void SetZoom(double zoom)
    {
        var next = ClampZoom(zoom);
        if (Math.Abs(next - Zoom) < 0.0001)
        {
            return;
        }

        Zoom = next;
    }

    private static double ClampZoom(double zoom)
    {
        if (!double.IsFinite(zoom))
        {
            return 1.0;
        }

        return Math.Clamp(zoom, MinZoom, MaxZoom);
    }

    private void CommitGraph()
    {
    }

    private void CompileGraph()
    {
    }

    private void SetCommitState(AssetGraphOperationState state)
    {
        if (_commitState == state)
        {
            return;
        }

        _commitState = state;
        OnPropertyChanged(nameof(HasCommitSucceeded));
        OnPropertyChanged(nameof(HasCommitFailed));
    }

    private void SetCompileState(AssetGraphOperationState state)
    {
        if (_compileState == state)
        {
            return;
        }

        _compileState = state;
        OnPropertyChanged(nameof(HasCompileSucceeded));
        OnPropertyChanged(nameof(HasCompileFailed));
    }

    private void AddNodeToSelection(AssetGraphNodeCardViewModel node)
    {
        if (SelectedNodes.Contains(node))
        {
            SelectedNode = node;
            return;
        }

        SelectedNodes.Add(node);
        node.IsSelected = true;
        SelectedNode = node;
    }

    private void RemoveNodeFromSelection(AssetGraphNodeCardViewModel node)
    {
        if (!SelectedNodes.Remove(node))
        {
            return;
        }

        node.IsSelected = false;
        SelectedNode = SelectedNodes.LastOrDefault();
    }

    private void ClearSelection()
    {
        foreach (var node in SelectedNodes)
        {
            node.IsSelected = false;
        }

        SelectedNodes.Clear();
        SelectedNode = null;
    }

    private static bool NodeIntersectsRectangle(
        AssetGraphNodeCardViewModel node,
        double left,
        double top,
        double right,
        double bottom)
    {
        return node.X <= right
            && node.X + CardWidth >= left
            && node.Y <= bottom
            && node.Y + CardHeight >= top;
    }

    private List<AssetGraphNodeCardViewModel> NodesToMove(
        AssetGraphNodeCardViewModel anchorNode)
    {
        if (anchorNode.IsSelected)
        {
            return [.. SelectedNodes];
        }

        return [anchorNode];
    }

    private void EnsureGraphBounds()
    {
        GraphWidth = Nodes.Count == 0
            ? 640.0
            : Nodes.Max(node => node.X + CardWidth + CanvasPadding);
        GraphHeight = Nodes.Count == 0
            ? 420.0
            : Nodes.Max(node => node.Y + CardHeight + CanvasPadding);
    }

    private void ClearGraph()
    {
        foreach (var edge in Edges)
        {
            edge.Dispose();
        }

        Edges.Clear();
        Nodes.Clear();
    }

    private void NotifyGraphStateChanged()
    {
        OnPropertyChanged(nameof(HasGraph));
        OnPropertyChanged(nameof(HasNoGraph));
        CommitGraphCommand.NotifyCanExecuteChanged();
        CompileGraphCommand.NotifyCanExecuteChanged();
    }
}

public sealed class AssetGraphNodeCardViewModel : ViewModelBase
{
    private double _x;
    private double _y;
    private bool _isSelected;

    public AssetGraphNodeCardViewModel(EngineAssetGraphNode node, double canvasPadding)
    {
        Id = node.Id;
        X = node.X + canvasPadding;
        Y = node.Y + canvasPadding;
        DisplayName = node.DisplayName;
        TypeName = node.TypeName;
        SchemaDisplay = node.Schema;
        CompileStatus = node.CompileStatus;
        InputPorts = new ObservableCollection<AssetGraphPortViewModel>(
            node.InputPorts.Select(port => new AssetGraphPortViewModel(port.Index, port.Label)));
        OutputPorts = new ObservableCollection<AssetGraphPortViewModel>(
            node.OutputPorts.Select(port => new AssetGraphPortViewModel(port.Index, port.Label)));
    }

    public ulong Id { get; }

    public double X
    {
        get => _x;
        set => SetProperty(ref _x, value);
    }

    public double Y
    {
        get => _y;
        set => SetProperty(ref _y, value);
    }

    public bool IsSelected
    {
        get => _isSelected;
        set => SetProperty(ref _isSelected, value);
    }

    public string DisplayName { get; }

    public string TypeName { get; }

    public string SchemaDisplay { get; }

    public string CompileStatus { get; }

    public bool HasInputPorts => InputPorts.Count > 0;

    public ObservableCollection<AssetGraphPortViewModel> InputPorts { get; }

    public ObservableCollection<AssetGraphPortViewModel> OutputPorts { get; }
}

public sealed class AssetGraphEdgeViewModel : ViewModelBase, IDisposable
{
    private readonly AssetGraphNodeCardViewModel _from;
    private readonly AssetGraphNodeCardViewModel _to;
    private readonly double _cardWidth;
    private readonly double _portRowBaseY;
    private readonly double _portRowSpacing;
    private bool _disposed;

    public AssetGraphEdgeViewModel(
        EngineAssetGraphEdge edge,
        AssetGraphNodeCardViewModel from,
        AssetGraphNodeCardViewModel to,
        double cardWidth,
        double portRowBaseY,
        double portRowSpacing)
    {
        FromNodeId = edge.From;
        ToNodeId = edge.To;
        ToInputPort = edge.ToInputPort;
        _from = from;
        _to = to;
        _cardWidth = cardWidth;
        _portRowBaseY = portRowBaseY;
        _portRowSpacing = portRowSpacing;

        _from.PropertyChanged += NodePositionChanged;
        _to.PropertyChanged += NodePositionChanged;
    }

    public ulong FromNodeId { get; }

    public ulong ToNodeId { get; }

    public uint ToInputPort { get; }

    public double StartX => _from.X + _cardWidth;

    public double StartY => _from.Y + _portRowBaseY;

    public double EndX => _to.X;

    public double EndY => _to.Y + _portRowBaseY + ToInputPort * _portRowSpacing;

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _from.PropertyChanged -= NodePositionChanged;
        _to.PropertyChanged -= NodePositionChanged;
        _disposed = true;
    }

    private void NodePositionChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is not nameof(AssetGraphNodeCardViewModel.X)
            and not nameof(AssetGraphNodeCardViewModel.Y))
        {
            return;
        }

        OnPropertyChanged(nameof(StartX));
        OnPropertyChanged(nameof(StartY));
        OnPropertyChanged(nameof(EndX));
        OnPropertyChanged(nameof(EndY));
    }
}

public sealed record AssetGraphPortViewModel(uint Index, string Label);

public enum AssetGraphOperationState
{
    Neutral,
    Succeeded,
    Failed,
}
