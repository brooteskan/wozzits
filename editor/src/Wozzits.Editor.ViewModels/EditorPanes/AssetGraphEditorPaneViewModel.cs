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
    private readonly AssetGraphGroupingModel _grouping;
    private readonly AssetGraphRerouteModel _reroutes;
    private readonly AssetGraphSubGraph? _context;
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
    private AssetGraphSubGraph? _selectedSubGraph;
    public event Action<AssetGraphNodeCardViewModel?>? SelectedNodeChanged;

    // grouping/context are shared by the shell so a drill-in tab and the root canvas see
    // one set of sub-graphs. context is the sub-graph this pane represents (null = root).
    public AssetGraphEditorPaneViewModel(
        IWozzitsEngineEditorSession? editorSession = null,
        AssetGraphGroupingModel? grouping = null,
        AssetGraphSubGraph? context = null,
        AssetGraphRerouteModel? reroutes = null)
    {
        _editorSession = editorSession;
        _grouping = grouping ?? new AssetGraphGroupingModel();
        _reroutes = reroutes ?? new AssetGraphRerouteModel();
        _context = context;
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

    // Editor-only node groupings (sub-graphs) — ALL of them, shared across panes. View-
    // state only; never affects the engine graph, asset keys, or compilation
    // (issue woguls/wozzits-editor#1). Used for persistence; the canvas binds VisibleSubGraphs.
    public ReadOnlyObservableCollection<AssetGraphSubGraph> SubGraphs => _grouping.SubGraphs;

    // The sub-graph proxies drawn on THIS canvas: the children of this pane's context
    // (top-level groups on the root canvas; a sub-graph's nested children inside its
    // drill-in tab). A per-pane projection of the shared grouping model.
    public ObservableCollection<AssetGraphSubGraph> VisibleSubGraphs { get; } = [];

    // Raised when the user opens a sub-graph (double-click / menu) to edit it in its own
    // tab. The shell (MainWindowViewModel) owns the dock, so it services the request.
    public event Action<AssetGraphSubGraph>? OpenSubGraphRequested;

    // Raised when the selected sub-graph proxy changes, so the shell can drive the shared
    // inspector (sub-graphs are nameable there).
    public event Action<AssetGraphSubGraph?>? SelectedSubGraphChanged;

    // Raised when this pane adds or removes nodes, so the shell can re-pull the other open
    // panes. Every pane holds its own snapshot of the one draft, so a structural edit in a
    // drill-in tab leaves the root canvas (and any sibling tab) listing a stale node set.
    // Carries the acting pane, which has already reloaded and must not be reloaded again.
    public event Action<AssetGraphEditorPaneViewModel>? GraphMutated;

    // The currently selected sub-graph proxy (mutually exclusive with node selection).
    public AssetGraphSubGraph? SelectedSubGraph
    {
        get => _selectedSubGraph;
        private set
        {
            if (SetProperty(ref _selectedSubGraph, value))
            {
                SelectedSubGraphChanged?.Invoke(_selectedSubGraph);
            }
        }
    }

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
        var liveNodeIds = Nodes.Select(node => node.Id).ToHashSet();
        _grouping.ReconcileWithLiveNodes(liveNodeIds);
        _reroutes.ReconcileWithLiveNodes(liveNodeIds);
        RefreshCanvasProjection();
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

        // A node dropped on a drill-in tab joins that tab's sub-graph. Without this the
        // node is ungrouped, so the canvas projection's membership test fails and the card
        // is hidden on the very pane it was dropped on — it would surface on the root
        // canvas only. Assign before the reload so the refreshed projection draws it here.
        if (_context is not null)
        {
            _grouping.AddNodes(_context.Id, [response.NodeId]);
        }

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
        }

        MarkGraphDraft();
        GraphMutated?.Invoke(this);

        // Select LAST. Re-pulling the other panes restores each one's own selection, and
        // the restore raises SelectedNodeChanged -- so selecting before the fan-out would
        // leave the inspector pointed at a background pane's node instead of the node the
        // user just dropped here.
        if (added is not null)
        {
            SelectNode(added);
        }

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
        GraphMutated?.Invoke(this);
        return true;
    }

    public void CancelConnectionPreview()
    {
        ClearConnectionTarget();
        IsConnectionPreviewVisible = false;
        IsConnectionPreviewRejected = false;
    }

    // --- Editor-only node grouping (sub-graphs) ---------------------------------------
    // Sub-graphs group nodes under a named, collapsible proxy card. They are a pure
    // editor-side view concept and never touch the engine graph, asset keys, or
    // compilation (issue woguls/wozzits-editor#1) — so, unlike node edits, grouping does
    // NOT mark the graph as a draft.

    // Group the currently selected nodes into a new sub-graph, seeding the collapsed proxy
    // at the selection's centroid, then collapse it on the canvas and select the proxy.
    // Returns null (and sets LastEditError) when the selection is empty.
    public AssetGraphSubGraph? CreateSubGraphFromSelection(string name)
    {
        if (SelectedNodes.Count == 0)
        {
            LastEditError = "Select one or more nodes to group into a sub-graph.";
            return null;
        }

        var memberIds = SelectedNodes.Select(node => node.Id).ToList();
        var (proxyX, proxyY) = SelectionProxyAnchor();

        // Nest the new group under this canvas's context: top-level on the root canvas, a
        // child sub-graph inside a drill-in tab.
        var subGraph = _grouping.CreateSubGraph(
            string.IsNullOrWhiteSpace(name) ? "Sub-graph" : name.Trim(),
            memberIds,
            parentId: _context?.Id);
        subGraph.ProxyX = proxyX;
        subGraph.ProxyY = proxyY;

        RefreshCanvasProjection();
        SelectSubGraph(subGraph);
        LastEditError = string.Empty;
        return subGraph;
    }

    // Dissolve a sub-graph; its member nodes become ungrouped and reappear on the canvas.
    public bool Ungroup(AssetGraphSubGraph subGraph)
    {
        if (!_grouping.Ungroup(subGraph.Id))
        {
            return false;
        }

        if (ReferenceEquals(_selectedSubGraph, subGraph))
        {
            ClearSubGraphSelection();
        }

        RefreshCanvasProjection();
        return true;
    }

    public bool UngroupSelectedSubGraph()
    {
        return _selectedSubGraph is not null && Ungroup(_selectedSubGraph);
    }

    public AssetGraphSubGraph? SubGraphOfNode(ulong nodeId)
    {
        return _grouping.SubGraphOfNode(nodeId);
    }

    // Replace the current groupings with ones loaded from the editor sidecar (project
    // open), dropping any member whose node no longer exists, then re-project the canvas.
    public void LoadSubGraphs(IEnumerable<PersistedSubGraph> subGraphs)
    {
        _grouping.Clear();
        ClearSubGraphSelection();

        foreach (var persisted in subGraphs)
        {
            if (persisted.MemberNodeIds.Count == 0)
            {
                continue;
            }

            var subGraph = _grouping.CreateSubGraph(
                persisted.Name,
                persisted.MemberNodeIds,
                persisted.ParentId,
                persisted.Id);
            subGraph.ProxyX = persisted.ProxyX;
            subGraph.ProxyY = persisted.ProxyY;
        }

        _grouping.ReconcileWithLiveNodes(Nodes.Select(node => node.Id).ToHashSet());
        RefreshCanvasProjection();
    }

    // --- Named reroutes (editor-only fan-out declutter) --------------------------------

    public IReadOnlyDictionary<ulong, string> RerouteNames => _reroutes.Names;

    public bool IsReroute(ulong nodeId) => _reroutes.IsReroute(nodeId);

    // Re-run the canvas projection (e.g. after the inspector renames a reroute in the
    // shared model) so badges + wire-hiding refresh without a full reload.
    public void ReapplyProjection()
    {
        RefreshCanvasProjection();
    }

    // Name a node's output so its outgoing wires collapse into name badges. The default
    // name is the node's display name (renameable in the inspector, seam 2).
    public void CreateReroute(AssetGraphNodeCardViewModel node)
    {
        _reroutes.Set(node.Id, node.DisplayName);
        RefreshCanvasProjection();
    }

    public void RemoveReroute(AssetGraphNodeCardViewModel node)
    {
        _reroutes.Remove(node.Id);
        RefreshCanvasProjection();
    }

    public void LoadReroutes(IEnumerable<PersistedReroute> reroutes)
    {
        _reroutes.Load(reroutes.Select(reroute =>
            new KeyValuePair<ulong, string>(reroute.SourceNodeId, reroute.Name)));
        _reroutes.ReconcileWithLiveNodes(Nodes.Select(node => node.Id).ToHashSet());
        RefreshCanvasProjection();
    }

    // Select a sub-graph proxy. Proxy selection and node selection are mutually exclusive.
    public void SelectSubGraph(AssetGraphSubGraph? subGraph)
    {
        ClearSelection();
        ClearSubGraphSelection();
        if (subGraph is null)
        {
            return;
        }

        subGraph.IsSelected = true;
        SelectedSubGraph = subGraph;
    }

    public void MoveSubGraphProxyByGraphDelta(
        AssetGraphSubGraph subGraph,
        double deltaX,
        double deltaY)
    {
        subGraph.ProxyX = Math.Max(CanvasPadding, subGraph.ProxyX + deltaX);
        subGraph.ProxyY = Math.Max(CanvasPadding, subGraph.ProxyY + deltaY);
        EnsureGraphBounds();
    }

    // Ask the shell to open this sub-graph in its own tab (drill-in).
    public void OpenSubGraph(AssetGraphSubGraph subGraph)
    {
        OpenSubGraphRequested?.Invoke(subGraph);
    }

    // Project the shared grouping onto THIS canvas (whose context is _context): show only
    // the nodes/sub-graphs that live directly under the context, reroute edges to the
    // proxy that represents each hidden endpoint, and hide edges that leave the context.
    // Root context (null) reproduces the flat collapse view; a sub-graph context is the
    // drill-in tab. One projection serves both — and nesting when it ships.
    private void RefreshCanvasProjection()
    {
        foreach (var node in Nodes)
        {
            var visible = ReferenceEquals(_grouping.SubGraphOfNode(node.Id), _context);
            node.IsCanvasVisible = visible;
            if (!visible && node.IsSelected)
            {
                RemoveNodeFromSelection(node);
            }
        }

        foreach (var edge in Edges)
        {
            var from = RepresentativeOnCanvas(edge.FromNodeId);
            var to = RepresentativeOnCanvas(edge.ToNodeId);

            // Hidden if either endpoint is off this canvas, or both collapse into the
            // same proxy (an edge internal to one child sub-graph).
            if (!from.OnCanvas
                || !to.OnCanvas
                || (from.Proxy is not null && ReferenceEquals(from.Proxy, to.Proxy)))
            {
                edge.FromProxy = null;
                edge.ToProxy = null;
                edge.IsRenderHidden = true;
                continue;
            }

            edge.FromProxy = from.Proxy;
            edge.ToProxy = to.Proxy;
            edge.IsRenderHidden = false;
        }

        ApplyRerouteBadges();
        SyncVisibleSubGraphs();
        EnsureGraphBounds();
    }

    // Named reroutes (editor-only): hide a named source's fan-out wires and badge the
    // ports — the source output as the declaration, each fed input port as a usage.
    private void ApplyRerouteBadges()
    {
        var incoming = new Dictionary<(ulong Node, uint Port), ulong>();
        foreach (var edge in Edges)
        {
            incoming[(edge.ToNodeId, edge.ToInputPort)] = edge.FromNodeId;
            if (_reroutes.IsReroute(edge.FromNodeId))
            {
                edge.IsRenderHidden = true;
            }
        }

        foreach (var node in Nodes)
        {
            var declaration = _reroutes.NameOf(node.Id);
            foreach (var port in node.OutputPorts)
            {
                port.RerouteName = declaration;
            }

            foreach (var port in node.InputPorts)
            {
                port.RerouteName =
                    incoming.TryGetValue((node.Id, port.Index), out var source)
                        ? _reroutes.NameOf(source)
                        : null;
            }

            node.RerouteBadges.Clear();
            if (declaration is not null)
            {
                node.RerouteBadges.Add($"→ {declaration}");
            }

            // A usage badge names the input port it feeds, so it's clear which port the
            // reroute connects to.
            foreach (var port in node.InputPorts)
            {
                if (port.RerouteName is { } usage)
                {
                    var label = string.IsNullOrWhiteSpace(port.Label) ? "input" : port.Label;
                    node.RerouteBadges.Add($"{label}  ←  {usage}");
                }
            }
        }
    }

    // How a node appears on this canvas: as a visible card (OnCanvas, no proxy), collapsed
    // behind a child-of-context sub-graph proxy, or off-canvas entirely (a different
    // subtree). Walks the sub-graph parent chain up to the context.
    private (bool OnCanvas, AssetGraphSubGraph? Proxy) RepresentativeOnCanvas(ulong nodeId)
    {
        var container = _grouping.SubGraphOfNode(nodeId);
        if (ReferenceEquals(container, _context))
        {
            return (true, null);
        }

        var contextId = _context?.Id;
        var current = container;
        while (current is not null)
        {
            if (current.ParentId == contextId)
            {
                return (true, current);
            }

            current = current.ParentId is { } parentId
                ? _grouping.FindById(parentId)
                : null;
        }

        return (false, null);
    }

    // Keep VisibleSubGraphs in sync with the shared model: the proxies on this canvas are
    // exactly the sub-graphs whose parent is this pane's context.
    private void SyncVisibleSubGraphs()
    {
        var contextId = _context?.Id;
        for (var index = VisibleSubGraphs.Count - 1; index >= 0; --index)
        {
            var subGraph = VisibleSubGraphs[index];
            if (subGraph.ParentId != contextId || !_grouping.SubGraphs.Contains(subGraph))
            {
                VisibleSubGraphs.RemoveAt(index);
            }
        }

        foreach (var subGraph in _grouping.SubGraphs)
        {
            if (subGraph.ParentId == contextId && !VisibleSubGraphs.Contains(subGraph))
            {
                VisibleSubGraphs.Add(subGraph);
            }
        }
    }

    private void ClearSubGraphSelection()
    {
        if (_selectedSubGraph is not null)
        {
            _selectedSubGraph.IsSelected = false;
        }

        SelectedSubGraph = null;
    }

    private (double X, double Y) SelectionProxyAnchor()
    {
        if (SelectedNodes.Count == 0)
        {
            return (CanvasPadding, CanvasPadding);
        }

        return (
            Math.Max(CanvasPadding, SelectedNodes.Average(node => node.X)),
            Math.Max(CanvasPadding, SelectedNodes.Average(node => node.Y)));
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

    // Select the first node with a compile error so its diagnostics show in the
    // inspector (the red dot always has a matching message visible).
    private void SelectFirstErrorNode()
    {
        var errored = Nodes.FirstOrDefault(node => node.IsCompileError);
        if (errored is not null)
        {
            SelectNode(errored);
        }
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
            if (node.IsCanvasVisible
                && NodeIntersectsRectangle(node, left, top, right, bottom))
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
            if (!node.IsCanvasVisible
                || x < node.X - 24.0
                || x > node.X + CardWidth * 0.55)
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
            if (!node.IsCanvasVisible
                || x < node.X + CardWidth * 0.45
                || x > node.X + CardWidth + 24.0)
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
            // messages) from the failed bind surface on the cards, then jump to
            // the offending node so its message is visible in the inspector.
            ReloadGraphFromSessionPreservingLayout();
            SelectFirstErrorNode();
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
            // messages) from the failed bind surface on the cards, then jump to
            // the offending node so its message is visible in the inspector.
            ReloadGraphFromSessionPreservingLayout();
            SelectFirstErrorNode();
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

    // Re-pull the graph (including node params) from the live engine session
    // after an out-of-pane mutation — e.g. the inspector applying a node param
    // (#218 Phase 3). Without this the cached node cards keep their pre-edit
    // params and re-selecting the node shows the stale value. Reuses the
    // layout-preserving reload the in-pane mutations already use, so positions,
    // zoom and the current selection survive (and re-selecting re-inspects the
    // refreshed card).
    public void RefreshFromSession()
    {
        ReloadGraphFromSessionPreservingLayout();
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
        ClearSubGraphSelection();
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

    public void ClearSelection()
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

    // Size the canvas to what is actually drawn on it — visible nodes + this canvas's
    // proxies — so a drill-in tab isn't stretched by the (hidden) rest of the graph.
    private void EnsureGraphBounds()
    {
        var right = 0.0;
        var bottom = 0.0;
        var hasContent = false;

        foreach (var node in Nodes)
        {
            if (!node.IsCanvasVisible)
            {
                continue;
            }

            hasContent = true;
            right = Math.Max(right, node.X + CardWidth + CanvasPadding);
            bottom = Math.Max(bottom, node.Y + NodeCardHeight(node) + CanvasPadding);
        }

        foreach (var subGraph in VisibleSubGraphs)
        {
            hasContent = true;
            right = Math.Max(
                right,
                subGraph.ProxyX + AssetGraphSubGraph.ProxyWidth + CanvasPadding);
            bottom = Math.Max(
                bottom,
                subGraph.ProxyY + AssetGraphSubGraph.ProxyHeight + CanvasPadding);
        }

        if (!hasContent)
        {
            GraphWidth = 640.0;
            GraphHeight = 420.0;
            return;
        }

        GraphWidth = right;
        GraphHeight = bottom;
    }

    // A node card's rendered height grows past its 116px minimum with each port row, so
    // sizing the scroll bounds by the constant CardHeight clips tall multi-port nodes near
    // the canvas edge (most visible in a drill-in tab). Estimate generously from the port
    // count — over-estimating only adds harmless scroll slack; under-estimating clips.
    private static double NodeCardHeight(AssetGraphNodeCardViewModel node)
    {
        var portRows = Math.Max(node.InputPorts.Count, node.OutputPorts.Count);
        var rows = portRows + node.RerouteBadges.Count;
        return Math.Max(CardHeight, PortRowBaseY + rows * PortRowSpacing + 28.0);
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
