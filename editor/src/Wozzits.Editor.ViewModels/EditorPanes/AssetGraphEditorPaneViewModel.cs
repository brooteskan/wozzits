namespace Wozzits.Editor.ViewModels.EditorPanes;

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
    private double _connectionPreviewStartX;
    private double _connectionPreviewStartY;
    private double _connectionPreviewEndX;
    private double _connectionPreviewEndY;
    private bool _isConnectionPreviewVisible;
    private bool _isConnectionPreviewRejected;
    private AssetGraphPortViewModel? _connectionTargetPort;
    private AssetGraphNodeCardViewModel? _selectedNode;
    public event Action<AssetGraphNodeCardViewModel?>? SelectedNodeChanged;

    public AssetGraphEditorPaneViewModel(
        IWozzitsEngineEditorSession? editorSession = null)
    {
        _editorSession = editorSession;
        CommitGraphCommand = new AsyncRelayCommand(
            CommitGraphAsync,
            CanRunGraphOperation);
        CompileGraphCommand = new AsyncRelayCommand(
            CompileGraphAsync,
            CanRunGraphOperation);
    }

    public ObservableCollection<AssetGraphNodeCardViewModel> Nodes { get; } = [];

    public ObservableCollection<AssetGraphEdgeViewModel> Edges { get; } = [];

    public ObservableCollection<AssetGraphNodeCardViewModel> SelectedNodes { get; } = [];

    public IAsyncRelayCommand CommitGraphCommand { get; }

    public IAsyncRelayCommand CompileGraphCommand { get; }

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
                OnPropertyChanged(nameof(GraphOperationStatus));
                OnPropertyChanged(nameof(IsActualGraph));
            }
        }
    }

    public bool IsActualGraph => !IsDraftGraph;

    public string GraphModeStatus => IsDraftGraph
        ? "Draft graph"
        : "Actual graph";

    public string GraphOperationStatus
    {
        get
        {
            if (IsCommitInProgress)
            {
                return "Committing graph...";
            }
            if (IsCompileInProgress)
            {
                return "Compiling graph...";
            }
            if (HasCommitSucceeded)
            {
                return "Graph committed";
            }
            if (HasCommitFailed)
            {
                return "Commit failed";
            }
            if (HasCompileSucceeded)
            {
                return "Graph compiled";
            }
            if (HasCompileFailed)
            {
                return "Compile failed";
            }

            return GraphModeStatus;
        }
    }

    public bool HasCommitSucceeded => _commitState == AssetGraphOperationState.Succeeded;

    public bool HasCommitFailed => _commitState == AssetGraphOperationState.Failed;

    public bool IsCommitInProgress => _commitState == AssetGraphOperationState.InProgress;

    public bool HasCompileSucceeded => _compileState == AssetGraphOperationState.Succeeded;

    public bool HasCompileFailed => _compileState == AssetGraphOperationState.Failed;

    public bool IsCompileInProgress => _compileState == AssetGraphOperationState.InProgress;

    public bool IsGraphOperationRunning => IsCommitInProgress || IsCompileInProgress;

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

    public double ConnectionPreviewStartX
    {
        get => _connectionPreviewStartX;
        private set => SetProperty(ref _connectionPreviewStartX, value);
    }

    public double ConnectionPreviewStartY
    {
        get => _connectionPreviewStartY;
        private set => SetProperty(ref _connectionPreviewStartY, value);
    }

    public double ConnectionPreviewEndX
    {
        get => _connectionPreviewEndX;
        private set => SetProperty(ref _connectionPreviewEndX, value);
    }

    public double ConnectionPreviewEndY
    {
        get => _connectionPreviewEndY;
        private set => SetProperty(ref _connectionPreviewEndY, value);
    }

    public bool IsConnectionPreviewVisible
    {
        get => _isConnectionPreviewVisible;
        private set => SetProperty(ref _isConnectionPreviewVisible, value);
    }

    public bool IsConnectionPreviewRejected
    {
        get => _isConnectionPreviewRejected;
        private set => SetProperty(ref _isConnectionPreviewRejected, value);
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

    public void BeginConnectionPreview(
        AssetGraphNodeCardViewModel fromNode,
        AssetGraphPortViewModel outputPort)
    {
        ClearConnectionTarget();
        var anchor = OutputPortAnchor(fromNode, outputPort);
        ConnectionPreviewStartX = anchor.X;
        ConnectionPreviewStartY = anchor.Y;
        ConnectionPreviewEndX = anchor.X;
        ConnectionPreviewEndY = anchor.Y;
        IsConnectionPreviewRejected = false;
        IsConnectionPreviewVisible = true;
    }

    public void UpdateConnectionPreviewEnd(double x, double y)
    {
        if (!IsConnectionPreviewVisible)
        {
            return;
        }

        ConnectionPreviewEndX = x;
        ConnectionPreviewEndY = y;
    }

    public void PreviewConnectionTarget(
        AssetGraphNodeCardViewModel fromNode,
        AssetGraphPortViewModel? inputPort)
    {
        if (ReferenceEquals(_connectionTargetPort, inputPort))
        {
            return;
        }

        ClearConnectionTarget();
        if (inputPort is null)
        {
            IsConnectionPreviewRejected = false;
            return;
        }

        inputPort.IsConnectionTarget = true;
        _connectionTargetPort = inputPort;

        if (_editorSession is null)
        {
            inputPort.IsConnectionRejected = true;
            IsConnectionPreviewRejected = true;
            return;
        }
        if (IsGraphOperationRunning)
        {
            inputPort.IsConnectionRejected = true;
            IsConnectionPreviewRejected = true;
            return;
        }

        var check = _editorSession.CanConnectAssetGraphNodes(
            fromNode.Id,
            inputPort.Owner.Id,
            inputPort.Index);
        var rejected = !check.Ok || !check.Check.Compatible;
        inputPort.IsConnectionRejected = rejected;
        IsConnectionPreviewRejected = rejected;
    }

    public bool ConnectToInputPort(
        AssetGraphNodeCardViewModel fromNode,
        AssetGraphPortViewModel inputPort)
    {
        if (_editorSession is null)
        {
            LastEditError = "Engine editor session is not available.";
            CancelConnectionPreview();
            return false;
        }
        if (RejectIfGraphOperationRunning())
        {
            CancelConnectionPreview();
            return false;
        }

        var check = _editorSession.ConnectAssetGraphNodes(
            fromNode.Id,
            inputPort.Owner.Id,
            inputPort.Index);
        if (!check.Ok || !check.Check.Compatible)
        {
            LastEditError = ConnectionError(check);
            CancelConnectionPreview();
            return false;
        }

        LastEditError = string.Empty;
        CancelConnectionPreview();
        if (!ReloadGraphFromSessionPreservingLayout())
        {
            return false;
        }
        MarkGraphDraft();
        return true;
    }

    public bool DisconnectEdge(AssetGraphEdgeViewModel edge)
    {
        if (_editorSession is null)
        {
            LastEditError = "Engine editor session is not available.";
            return false;
        }
        if (RejectIfGraphOperationRunning())
        {
            return false;
        }

        var response = _editorSession.DisconnectAssetGraphEdge(edge.Id);
        if (!response.Ok)
        {
            LastEditError = response.Error;
            return false;
        }

        LastEditError = string.Empty;
        if (!ReloadGraphFromSessionPreservingLayout())
        {
            return false;
        }
        MarkGraphDraft();
        return true;
    }

    // Create a node for (schema, type) at a graph-space position (card top-left,
    // in canvas content coordinates including padding) — used by asset-browser
    // drag/drop. Reloads from the session so the new card + ports appear, then
    // pins it to the drop point and persists that position.
    public bool AddNodeAt(ulong schema, uint type, double graphX, double graphY)
    {
        if (_editorSession is null)
        {
            LastEditError = "Engine editor session is not available.";
            return false;
        }
        if (RejectIfGraphOperationRunning())
        {
            return false;
        }

        var response = _editorSession.AddAssetGraphNode(schema, type);
        if (!response.Ok)
        {
            LastEditError = response.Error;
            return false;
        }

        LastEditError = string.Empty;
        if (!ReloadGraphFromSessionPreservingLayout())
        {
            return false;
        }

        var added = Nodes.FirstOrDefault(node => node.Id == response.NodeId);
        if (added is not null)
        {
            added.X = Math.Max(CanvasPadding, graphX);
            added.Y = Math.Max(CanvasPadding, graphY);
            EnsureGraphBounds();
            CommitNodePosition(added);
            SelectNode(added);
        }

        MarkGraphDraft();
        return true;
    }

    // Remove all currently selected nodes (and their edges) from the draft.
    public bool RemoveSelectedNodes()
    {
        if (_editorSession is null)
        {
            LastEditError = "Engine editor session is not available.";
            return false;
        }
        if (RejectIfGraphOperationRunning())
        {
            return false;
        }

        var ids = SelectedNodes.Select(node => node.Id).ToList();
        if (ids.Count == 0)
        {
            return false;
        }

        LastEditError = string.Empty;
        foreach (var id in ids)
        {
            var response = _editorSession.RemoveAssetGraphNode(id);
            if (!response.Ok)
            {
                LastEditError = response.Error;
                return false;
            }
        }

        ClearSelection();
        if (!ReloadGraphFromSessionPreservingLayout())
        {
            return false;
        }

        MarkGraphDraft();
        return true;
    }

    public void CancelConnectionPreview()
    {
        ClearConnectionTarget();
        IsConnectionPreviewVisible = false;
        IsConnectionPreviewRejected = false;
    }

    public (double X, double Y) OutputPortAnchor(
        AssetGraphNodeCardViewModel node,
        AssetGraphPortViewModel port)
    {
        return (
            node.X + CardWidth,
            node.Y + PortRowBaseY + port.Index * PortRowSpacing);
    }

    public (double X, double Y) InputPortAnchor(
        AssetGraphNodeCardViewModel node,
        AssetGraphPortViewModel port)
    {
        return (
            node.X,
            node.Y + PortRowBaseY + port.Index * PortRowSpacing);
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
        MoveSelectedNodesByGraphDelta(
            anchorNode,
            deltaX / Zoom,
            deltaY / Zoom);
    }

    public void MoveSelectedNodesByGraphDelta(
        AssetGraphNodeCardViewModel anchorNode,
        double deltaX,
        double deltaY)
    {
        var nodes = NodesToMove(anchorNode);
        if (nodes.Count == 0)
        {
            return;
        }

        var graphDeltaX = deltaX;
        var graphDeltaY = deltaY;
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

    public AssetGraphPortViewModel? FindInputPortAt(double x, double y)
    {
        foreach (var node in Nodes)
        {
            if (x < node.X - 24.0 || x > node.X + CardWidth * 0.55)
            {
                continue;
            }

            foreach (var port in node.InputPorts)
            {
                var (_, portY) = InputPortAnchor(node, port);
                if (Math.Abs(y - portY) <= PortRowSpacing * 0.55)
                {
                    return port;
                }
            }
        }

        return null;
    }

    public AssetGraphPortViewModel? FindOutputPortAt(double x, double y)
    {
        foreach (var node in Nodes)
        {
            if (x < node.X + CardWidth * 0.45 || x > node.X + CardWidth + 24.0)
            {
                continue;
            }

            foreach (var port in node.OutputPorts)
            {
                var (_, portY) = OutputPortAnchor(node, port);
                if (Math.Abs(y - portY) <= PortRowSpacing * 0.55)
                {
                    return port;
                }
            }
        }

        return null;
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
        if (RejectIfGraphOperationRunning())
        {
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
        if (RejectIfGraphOperationRunning())
        {
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
        if (RejectIfGraphOperationRunning())
        {
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

    private bool CanRunGraphOperation()
    {
        return HasGraph && !IsGraphOperationRunning;
    }

    private async Task CommitGraphAsync()
    {
        if (_editorSession is null)
        {
            MarkCommitResult(false, "Engine editor session is not available.");
            return;
        }
        if (IsGraphOperationRunning)
        {
            return;
        }

        LastEditError = string.Empty;
        SetCompileState(AssetGraphOperationState.Neutral);
        SetCommitState(AssetGraphOperationState.InProgress);

        var editorSession = _editorSession;
        EngineMutationResponse response;
        try
        {
            response = await Task.Run(editorSession.CommitAssetGraph);
        }
        catch (Exception ex)
        {
            MarkCommitResult(false, ex.Message);
            return;
        }
        if (!response.Ok)
        {
            // Reload so per-node compile diagnostics (error dots + inspector
            // messages) from the failed bind surface on the cards.
            ReloadGraphFromSessionPreservingLayout();
            MarkCommitResult(false, response.Error);
            return;
        }

        if (!ReloadGraphFromSessionPreservingLayout())
        {
            MarkCommitResult(false, LastEditError);
            return;
        }

        MarkCommitResult(true);
    }

    private async Task CompileGraphAsync()
    {
        if (_editorSession is null)
        {
            MarkCompileResult(false, "Engine editor session is not available.");
            return;
        }
        if (IsGraphOperationRunning)
        {
            return;
        }

        LastEditError = string.Empty;
        SetCommitState(AssetGraphOperationState.Neutral);
        SetCompileState(AssetGraphOperationState.InProgress);

        var editorSession = _editorSession;
        EngineMutationResponse response;
        try
        {
            response = await Task.Run(editorSession.CompileAssetGraph);
        }
        catch (Exception ex)
        {
            MarkCompileResult(false, ex.Message);
            return;
        }
        if (!response.Ok)
        {
            // Reload so per-node compile diagnostics (error dots + inspector
            // messages) from the failed bind surface on the cards.
            ReloadGraphFromSessionPreservingLayout();
            MarkCompileResult(false, response.Error);
            return;
        }

        if (!ReloadGraphFromSessionPreservingLayout())
        {
            MarkCompileResult(false, LastEditError);
            return;
        }

        MarkCompileResult(true);
    }

    private bool ReloadGraphFromSessionPreservingLayout()
    {
        if (_editorSession is null)
        {
            LastEditError = "Engine editor session is not available.";
            return false;
        }

        var wasDraftGraph = IsDraftGraph;
        var positions = Nodes.ToDictionary(
            node => node.Id,
            node => (node.X, node.Y));
        var selectedIds = SelectedNodes
            .Select(node => node.Id)
            .ToHashSet();
        var selectedNodeId = SelectedNode?.Id;
        var zoom = Zoom;

        var response = _editorSession.LoadAssetGraphSnapshot();
        if (!response.Ok)
        {
            LastEditError = response.Error;
            return false;
        }

        LoadSnapshot(response);

        foreach (var node in Nodes)
        {
            if (positions.TryGetValue(node.Id, out var position))
            {
                node.X = position.X;
                node.Y = position.Y;
            }
        }

        Zoom = zoom;
        EnsureGraphBounds();

        foreach (var node in Nodes)
        {
            if (selectedIds.Contains(node.Id))
            {
                AddNodeToSelection(node);
            }
        }

        if (selectedNodeId is not null)
        {
            SelectedNode = Nodes.FirstOrDefault(node => node.Id == selectedNodeId);
        }

        IsDraftGraph = wasDraftGraph;
        return true;
    }

    private void ClearConnectionTarget()
    {
        if (_connectionTargetPort is null)
        {
            return;
        }

        _connectionTargetPort.IsConnectionTarget = false;
        _connectionTargetPort.IsConnectionRejected = false;
        _connectionTargetPort = null;
    }

    private static string ConnectionError(
        EngineAssetGraphConnectionCheckResponse response)
    {
        if (!string.IsNullOrWhiteSpace(response.Error))
        {
            return response.Error;
        }

        if (!string.IsNullOrWhiteSpace(response.Check.Message))
        {
            return response.Check.Message;
        }

        return $"Asset graph connection rejected: {response.Check.Status}.";
    }

    private bool RejectIfGraphOperationRunning()
    {
        if (!IsGraphOperationRunning)
        {
            return false;
        }

        LastEditError = "Asset graph operation is already running.";
        return true;
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
        OnPropertyChanged(nameof(IsCommitInProgress));
        OnPropertyChanged(nameof(IsGraphOperationRunning));
        OnPropertyChanged(nameof(GraphOperationStatus));
        CommitGraphCommand.NotifyCanExecuteChanged();
        CompileGraphCommand.NotifyCanExecuteChanged();
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
        OnPropertyChanged(nameof(IsCompileInProgress));
        OnPropertyChanged(nameof(IsGraphOperationRunning));
        OnPropertyChanged(nameof(GraphOperationStatus));
        CommitGraphCommand.NotifyCanExecuteChanged();
        CompileGraphCommand.NotifyCanExecuteChanged();
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
