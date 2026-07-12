namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.Collections.ObjectModel;
using System.Text.Json.Nodes;
using CommunityToolkit.Mvvm.Input;
using Wozzits.Editor.Statecharts;

/// <summary>
/// Projects a statechart's PERSISTENT dataflow layer (bindings, agents, pure ops) onto a
/// left-to-right DAG canvas. Reads (marginal/committed/memory) stay as plain op cards for
/// now rather than folding into agent output ports, so every node has a single output
/// anchor and the wire model matches the asset-graph canvas exactly: agent->read,
/// binding->proximity, op->op. `Const` operands render as inline literals, not wires.
///
/// Auto-layout is longest-path layering: column == dependency depth (sources at column 0),
/// which guarantees every wire runs strictly left-to-right. Positions are transient here;
/// persisting hand-placed layout comes with the canvas view (E2b/E2c).
/// </summary>
public sealed class DataflowPaneViewModel : ViewModelBase, IEditorCanvas, IWiringCanvas
{
    private const double CanvasPadding = 28.0;
    // Card geometry is public so the port VMs (drag-preview anchors) and the wire VMs share it.
    public const double CardWidth = 220.0;
    private const double CardHeight = 116.0;
    public const double PortRowBaseY = 82.0;
    public const double PortRowSpacing = 18.0;
    private const double ColumnGap = 120.0;
    private const double RowGap = 40.0;
    private const double MinZoom = 0.25;
    private const double MaxZoom = 4.0;

    private double _zoom = 1.0;
    private DataflowNodeViewModel? _selectedNode;
    private Chart? _chart;
    private bool _isLayoutDirty;
    private bool _isChartDirty;

    public DataflowPaneViewModel()
    {
        SelectedNodes.CollectionChanged += (_, _) => UpdateSelectedNode();
        AddOpCommand = new RelayCommand<OpKind>(kind => AddOp(kind));
        AddAgentCommand = new RelayCommand(() => AddAgent());
        AddBindingCommand = new RelayCommand(() => AddBinding());
        DeleteSelectedCommand = new RelayCommand(DeleteSelected);
    }

    // Toolbar: add a pure op of the given kind; add an agent / binding; delete the selection.
    public IRelayCommand<OpKind> AddOpCommand { get; }

    public IRelayCommand AddAgentCommand { get; }

    public IRelayCommand AddBindingCommand { get; }

    public IRelayCommand DeleteSelectedCommand { get; }

    public ObservableCollection<DataflowNodeViewModel> Nodes { get; } = [];

    public ObservableCollection<DataflowWireViewModel> Wires { get; } = [];

    public ObservableCollection<DataflowNodeViewModel> SelectedNodes { get; } = [];

    public bool HasGraph => Nodes.Count > 0;

    // A chart is loaded (possibly still empty) -- drives the toolbar so you can author from scratch.
    public bool HasChart => _chart is not null;

    public DataflowNodeViewModel? SelectedNode
    {
        get => _selectedNode;
        private set
        {
            if (SetProperty(ref _selectedNode, value))
            {
                OnPropertyChanged(nameof(HasSelectedNode));
            }
        }
    }

    public bool HasSelectedNode => _selectedNode is not null;

    public bool IsLayoutDirty
    {
        get => _isLayoutDirty;
        private set => SetProperty(ref _isLayoutDirty, value);
    }

    public bool IsDirty
    {
        get => _isChartDirty;
        private set => SetProperty(ref _isChartDirty, value);
    }

    public void MarkChartDirty() => IsDirty = true;

    public void ClearDirty()
    {
        IsLayoutDirty = false;
        IsDirty = false;
    }

    public double Zoom
    {
        get => _zoom;
        set
        {
            if (SetProperty(ref _zoom, Math.Clamp(value, MinZoom, MaxZoom)))
            {
                OnPropertyChanged(nameof(ScaledGraphWidth));
                OnPropertyChanged(nameof(ScaledGraphHeight));
            }
        }
    }

    public double GraphWidth => Nodes.Count == 0 ? 0 : Nodes.Max(n => n.X + CardWidth) + CanvasPadding;

    public double GraphHeight => Nodes.Count == 0 ? 0 : Nodes.Max(n => n.Y + CardHeight) + CanvasPadding;

    public double ScaledGraphWidth => GraphWidth * Zoom;

    public double ScaledGraphHeight => GraphHeight * Zoom;

    public IReadOnlyList<ICanvasNode> CanvasNodes => Nodes;

    public void SelectOnly(ICanvasNode node)
    {
        foreach (var n in Nodes)
        {
            n.IsSelected = false;
        }

        SelectedNodes.Clear();
        if (node is DataflowNodeViewModel dataflow)
        {
            dataflow.IsSelected = true;
            SelectedNodes.Add(dataflow);
        }
    }

    public void ToggleSelection(ICanvasNode node)
    {
        if (node is not DataflowNodeViewModel dataflow)
        {
            return;
        }

        dataflow.IsSelected = !dataflow.IsSelected;
        if (dataflow.IsSelected)
        {
            if (!SelectedNodes.Contains(dataflow))
            {
                SelectedNodes.Add(dataflow);
            }
        }
        else
        {
            SelectedNodes.Remove(dataflow);
        }
    }

    public void ClearSelection()
    {
        foreach (var n in Nodes)
        {
            n.IsSelected = false;
        }

        SelectedNodes.Clear();
    }

    public void SelectInRectangle(double x0, double y0, double x1, double y1, bool additive)
    {
        double left = Math.Min(x0, x1);
        double right = Math.Max(x0, x1);
        double top = Math.Min(y0, y1);
        double bottom = Math.Max(y0, y1);

        if (!additive)
        {
            ClearSelection();
        }

        foreach (var n in Nodes)
        {
            bool overlaps = n.X < right && n.X + CardWidth > left && n.Y < bottom && n.Y + CardHeight > top;
            if (overlaps && !n.IsSelected)
            {
                n.IsSelected = true;
                SelectedNodes.Add(n);
            }
        }
    }

    public void MoveSelectedBy(double dx, double dy)
    {
        foreach (var n in SelectedNodes)
        {
            n.X = Math.Max(0, n.X + dx);
            n.Y = Math.Max(0, n.Y + dy);
        }

        RaiseExtentChanged();
        IsLayoutDirty = true;
    }

    public void ZoomByWheel(double wheelDelta)
    {
        Zoom = wheelDelta > 0 ? Zoom * 1.1 : Zoom / 1.1;
        IsLayoutDirty = true;
    }

    private void RaiseExtentChanged()
    {
        OnPropertyChanged(nameof(GraphWidth));
        OnPropertyChanged(nameof(GraphHeight));
        OnPropertyChanged(nameof(ScaledGraphWidth));
        OnPropertyChanged(nameof(ScaledGraphHeight));
    }

    private void UpdateSelectedNode() =>
        SelectedNode = SelectedNodes.Count == 1 ? SelectedNodes[0] : null;

    // Add a pure op of the given kind with default (constant) operands, select it, and place
    // it at a clear row below the existing nodes. Returns the new node (null if no chart).
    public DataflowNodeViewModel? AddOp(OpKind kind)
    {
        if (_chart is null)
        {
            return null;
        }

        var id = FreshId("op", AllNodeIds());
        _chart.Pure.Add(NewOp(kind, id, _chart));
        IsDirty = true;
        ReprojectPreservingLayout();
        return PlaceAndSelect(id);
    }

    // Add an owned agent (a co-located quantum_agent) with a default spec whose numeric fields
    // are then editable in the inspector.
    public DataflowNodeViewModel? AddAgent()
    {
        if (_chart is null)
        {
            return null;
        }

        var id = FreshId("agent", AllNodeIds());
        _chart.Agents.Add(new AgentDecl { Id = id, Owned = true, Host = "self", Spec = DefaultAgentSpec() });
        IsDirty = true;
        ReprojectPreservingLayout();
        return PlaceAndSelect(id);
    }

    // Add an entity binding (a port that resolves a scene node by name). `find` defaults to the
    // port and is editable in the inspector -- point it at the entity the chart should drive.
    public DataflowNodeViewModel? AddBinding()
    {
        if (_chart is null)
        {
            return null;
        }

        var port = FreshId("bind", AllNodeIds());
        _chart.Bindings.Add(new Binding { Port = port, Find = port, Subtree = true });
        IsDirty = true;
        ReprojectPreservingLayout();
        return PlaceAndSelect(port);
    }

    // Rename an agent (its id) and rewrite every reference to it -- read ops, agent-write effects
    // (in states + transition actions), and commit triggers. Rejects an empty / unchanged /
    // colliding name. Reprojects and re-selects the renamed node.
    public void RenameAgent(AgentDecl agent, string newId)
    {
        if (_chart is null)
        {
            return;
        }

        newId = (newId ?? string.Empty).Trim();
        if (newId.Length == 0 || newId == agent.Id || AllNodeIds().Contains(newId))
        {
            return;
        }

        var oldId = agent.Id;
        agent.Id = newId;

        foreach (var p in _chart.Pure)
        {
            if (p.IsRead && p.Agent == oldId)
            {
                p.Agent = newId;
            }
        }
        foreach (var s in _chart.States)
        {
            RenameAgentInEffects(s.Do, oldId, newId);
            RenameAgentInEffects(s.Entry, oldId, newId);
            RenameAgentInEffects(s.Exit, oldId, newId);
            foreach (var t in s.Transitions)
            {
                RenameAgentInEffects(t.Actions, oldId, newId);
                if (t.Trigger.Kind == TriggerKind.Commit && t.Trigger.Agent == oldId)
                {
                    t.Trigger.Agent = newId;
                }
            }
        }

        IsDirty = true;
        ReprojectPreservingLayout();
        if (Nodes.FirstOrDefault(n => n.NodeId == newId) is { } renamed)
        {
            SelectOnly(renamed);
        }
    }

    private static void RenameAgentInEffects(List<Effect> effects, string oldId, string newId)
    {
        foreach (var e in effects)
        {
            if (e.Agent == oldId
                && e.Kind is EffectKind.SetGoal or EffectKind.SetDecoherence or EffectKind.Rearm or EffectKind.Reward)
            {
                e.Agent = newId;
            }
        }
    }

    private IEnumerable<string> AllNodeIds() => _chart is null
        ? Enumerable.Empty<string>()
        : _chart.Pure.Select(p => p.Id)
            .Concat(_chart.Agents.Select(a => a.Id))
            .Concat(_chart.Bindings.Select(b => b.Port));

    // Position a freshly-added node at a clear row below the existing ones and select it.
    private DataflowNodeViewModel? PlaceAndSelect(string id)
    {
        var node = Nodes.FirstOrDefault(n => n.NodeId == id);
        if (node is null)
        {
            return null;
        }

        double bottom = CanvasPadding;
        foreach (var n in Nodes)
        {
            if (n != node)
            {
                bottom = Math.Max(bottom, n.Y + CardHeight);
            }
        }

        node.X = CanvasPadding;
        node.Y = bottom + RowGap;
        SelectOnly(node);
        RaiseExtentChanged();
        return node;
    }

    private static JsonObject DefaultAgentSpec() => new()
    {
        ["decisions"] = 1,
        ["goal"] = 0.4,
        ["gamma_start"] = 2,
        ["anneal_seconds"] = 4,
        ["confidence"] = 0.8,
        ["decoherence"] = 0.0,
        ["think_interval"] = 0.25,
    };

    // Wire an op's output into a target op's operand (the input at row `targetInputIndex`).
    // Rejects when: the source isn't an op (a leaf binding/agent can't feed a value operand —
    // route it through a read op), the target input isn't a value operand (reads/proximity take
    // an agent/binding, not an op ref), or the edge would create a pure-op cycle. Reprojects on
    // success so the wire appears and the formerly-constant input becomes wired.
    public bool TryConnect(string sourceNodeId, string targetNodeId, int targetInputIndex)
    {
        if (_chart is null)
        {
            return false;
        }

        var source = _chart.Pure.FirstOrDefault(p => p.Id == sourceNodeId);
        var target = _chart.Pure.FirstOrDefault(p => p.Id == targetNodeId);
        if (source is null || target is null)
        {
            return false;
        }

        var operand = OperandSlot(target, targetInputIndex);
        if (operand is null || CreatesCycle(sourceNodeId, targetNodeId))
        {
            return false;
        }

        operand.Kind = RefKind.Op;
        operand.Op = sourceNodeId;
        operand.Const = 0;
        operand.IsBool = false;

        IsDirty = true;
        ReprojectPreservingLayout();
        return true;
    }

    // Non-mutating twin of TryConnect: does the same source/target/operand/cycle validation but
    // touches nothing. The wiring gesture calls it on hover to colour the target port dot
    // (accent = will connect, red = rejected) before the drop.
    public bool CanConnect(string sourceNodeId, string targetNodeId, int targetInputIndex)
    {
        if (_chart is null)
        {
            return false;
        }

        var source = _chart.Pure.FirstOrDefault(p => p.Id == sourceNodeId);
        var target = _chart.Pure.FirstOrDefault(p => p.Id == targetNodeId);
        if (source is null || target is null)
        {
            return false;
        }

        return OperandSlot(target, targetInputIndex) is not null && !CreatesCycle(sourceNodeId, targetNodeId);
    }

    // Revert a wired operand back to an editable constant 0 (the inverse of TryConnect / a
    // "delete connection"). Rejects an input that isn't an op-wired value operand. Reprojects.
    public bool Disconnect(string targetNodeId, int inputIndex)
    {
        if (_chart is null)
        {
            return false;
        }

        var target = _chart.Pure.FirstOrDefault(p => p.Id == targetNodeId);
        if (target is null)
        {
            return false;
        }

        var operand = OperandSlot(target, inputIndex);
        if (operand is null || operand.Kind != RefKind.Op)
        {
            return false;
        }

        operand.Kind = RefKind.Const;
        operand.Op = "";
        operand.Const = 0;
        operand.IsBool = false;

        IsDirty = true;
        ReprojectPreservingLayout();
        return true;
    }

    // The ValueRef that an (op, input row index) addresses, or null when that input is not a
    // value operand (a read's agent / a proximity target is wired differently, not as an op ref).
    private static ValueRef? OperandSlot(PureOp op, int index) => op.Op switch
    {
        OpKind.Marginal or OpKind.Committed or OpKind.Memory or OpKind.Proximity => null,
        OpKind.Select => index switch
        {
            0 => op.Cond ??= ValueRef.Number(0),
            1 => op.A ??= ValueRef.Number(0),
            2 => op.B ??= ValueRef.Number(0),
            _ => null,
        },
        _ => index >= 0 && index < op.Ins.Count ? op.Ins[index] : null,
    };

    // Would wiring source -> target (target comes to depend on source) create a cycle? Yes iff
    // source already depends on target transitively (target reachable from source, self included).
    private bool CreatesCycle(string sourceId, string targetId)
    {
        var opsById = _chart!.Pure.ToDictionary(p => p.Id);
        var seen = new HashSet<string>();
        var stack = new Stack<string>();
        stack.Push(sourceId);
        while (stack.Count > 0)
        {
            var id = stack.Pop();
            if (id == targetId)
            {
                return true;
            }
            if (!seen.Add(id) || !opsById.TryGetValue(id, out var op))
            {
                continue;
            }
            foreach (var dep in OpRefIds(op))
            {
                stack.Push(dep);
            }
        }

        return false;
    }

    // Delete the selected nodes -- ops, agents, or bindings -- cleaning up every reference so
    // the chart stays structurally sound: an op orphaned by a deleted agent (a read) or binding
    // (a proximity) is removed too; op-refs pointing at a removed op fall back to a constant 0;
    // effects that write a deleted agent or drive a deleted binding are dropped; a commit trigger
    // whose agent is gone reverts to a plain timer.
    public void DeleteSelected()
    {
        if (_chart is null)
        {
            return;
        }

        var ops = SelectedNodes.Where(n => n.Kind == DataflowNodeKind.Op).Select(n => n.NodeId).ToHashSet();
        var agents = SelectedNodes.Where(n => n.Kind == DataflowNodeKind.Agent).Select(n => n.NodeId).ToHashSet();
        var bindings = SelectedNodes.Where(n => n.Kind == DataflowNodeKind.Binding).Select(n => n.NodeId).ToHashSet();
        if (ops.Count + agents.Count + bindings.Count == 0)
        {
            return;
        }

        _chart.Agents.RemoveAll(a => agents.Contains(a.Id));
        _chart.Bindings.RemoveAll(b => bindings.Contains(b.Port));
        foreach (var p in _chart.Pure)
        {
            if ((p.IsRead && agents.Contains(p.Agent)) || (p.Op == OpKind.Proximity && bindings.Contains(p.Target)))
            {
                ops.Add(p.Id);   // a read/proximity can't outlive its source
            }
        }
        _chart.Pure.RemoveAll(p => ops.Contains(p.Id));

        foreach (var p in _chart.Pure)
        {
            foreach (var r in p.Ins) Disconnect(r, ops);
            Disconnect(p.Cond, ops);
            Disconnect(p.A, ops);
            Disconnect(p.B, ops);
        }
        foreach (var s in _chart.States)
        {
            PruneEffects(s.Do, agents, bindings, ops);
            PruneEffects(s.Entry, agents, bindings, ops);
            PruneEffects(s.Exit, agents, bindings, ops);
            foreach (var t in s.Transitions)
            {
                PruneEffects(t.Actions, agents, bindings, ops);
                if (t.Trigger.Kind == TriggerKind.Guard)
                {
                    Disconnect(t.Trigger.Cond, ops);
                }
                if (t.Trigger.Kind == TriggerKind.Commit && agents.Contains(t.Trigger.Agent))
                {
                    t.Trigger.Kind = TriggerKind.After;
                    t.Trigger.Agent = string.Empty;
                    t.Trigger.Seconds = 1.0;
                }
            }
        }

        IsDirty = true;
        ReprojectPreservingLayout();
    }

    // Remove effects that write a deleted agent or drive a deleted binding, and disconnect any
    // op-ref value pointing at a removed op.
    private static void PruneEffects(List<Effect> effects, HashSet<string> agents, HashSet<string> bindings, HashSet<string> ops)
    {
        effects.RemoveAll(e =>
            ((e.Kind is EffectKind.SetGoal or EffectKind.SetDecoherence or EffectKind.Rearm or EffectKind.Reward) && agents.Contains(e.Agent))
            || ((e.Kind is EffectKind.SetScale or EffectKind.SetVisible or EffectKind.PlaySound) && bindings.Contains(e.TargetBind)));
        foreach (var e in effects)
        {
            Disconnect(e.Value, ops);
        }
    }

    private static void Disconnect(ValueRef? r, HashSet<string> doomed)
    {
        if (r is { Kind: RefKind.Op } && doomed.Contains(r.Op))
        {
            r.Kind = RefKind.Const;
            r.Op = "";
            r.Const = 0;
            r.IsBool = false;
        }
    }

    private static PureOp NewOp(OpKind kind, string id, Chart chart)
    {
        var op = new PureOp { Id = id, Op = kind };
        switch (kind)
        {
            case OpKind.Marginal:
            case OpKind.Committed:
            case OpKind.Memory:
                op.Agent = chart.Agents.Count > 0 ? chart.Agents[0].Id : string.Empty;
                op.Slot = 0;
                break;
            case OpKind.Proximity:
                op.Target = chart.Bindings.Count > 0 ? chart.Bindings[0].Port : string.Empty;
                break;
            case OpKind.Clamp01:
            case OpKind.Not:
                op.Ins.Add(ValueRef.Number(0));
                break;
            case OpKind.MulAdd:
                op.Ins.Add(ValueRef.Number(0));
                op.Ins.Add(ValueRef.Number(0));
                op.Ins.Add(ValueRef.Number(0));
                break;
            case OpKind.Select:
                op.Cond = ValueRef.Number(0);
                op.A = ValueRef.Number(0);
                op.B = ValueRef.Number(0);
                break;
            default:   // Add/Sub/Mul/Min/Max/Eq/Lt/Gt/And/Or: two operands
                op.Ins.Add(ValueRef.Number(0));
                op.Ins.Add(ValueRef.Number(0));
                break;
        }

        return op;
    }

    private static string FreshId(string prefix, IEnumerable<string> taken)
    {
        var used = taken.ToHashSet();
        for (int i = 1; ; i++)
        {
            var candidate = prefix + i;
            if (!used.Contains(candidate))
            {
                return candidate;
            }
        }
    }

    // Snapshot the current node positions + zoom so a structural reproject can restore hand
    // placement (surviving nodes keep their spot; a new node keeps its auto-layout position).
    public StatechartLayout CaptureLayout()
    {
        var layout = new StatechartLayout { DataflowZoom = Zoom };
        foreach (var node in Nodes)
        {
            layout.NodePositions[node.NodeId] = new StatechartLayout.Point(node.X, node.Y);
        }

        return layout;
    }

    private void ReprojectPreservingLayout()
    {
        var layout = CaptureLayout();
        Project(_chart!);
        ApplyLayout(layout);
    }

    // As above, but re-select the previously selected node by id after the rebuild, so an
    // inspector-driven ref change (a read's agent / a proximity target) keeps its selection.
    private void ReprojectPreservingSelection()
    {
        var id = _selectedNode?.NodeId;
        ReprojectPreservingLayout();
        if (id is not null && Nodes.FirstOrDefault(n => n.NodeId == id) is { } again)
        {
            SelectOnly(again);
        }
    }

    // Cross-layer focus: dim every dataflow node/wire that doesn't feed the given control
    // state -- its effects + transitions, transitively through op inputs, read agents, and
    // proximity bindings. Null clears the focus (all full opacity).
    public void FocusOnState(State? state)
    {
        if (state is null || _chart is null)
        {
            foreach (var node in Nodes) node.IsDimmed = false;
            foreach (var wire in Wires) wire.IsDimmed = false;
            return;
        }

        var opsById = _chart.Pure.ToDictionary(p => p.Id);
        var relevant = new HashSet<string>();
        var pending = new Stack<string>();

        void SeedOp(ValueRef? r)
        {
            if (r is { Kind: RefKind.Op } && relevant.Add(r.Op))
            {
                pending.Push(r.Op);
            }
        }

        void SeedEffect(Effect e)
        {
            if (e.Agent.Length > 0) relevant.Add(e.Agent);
            if (e.TargetBind.Length > 0) relevant.Add(e.TargetBind);
            SeedOp(e.Value);
        }

        foreach (var e in state.Do) SeedEffect(e);
        foreach (var e in state.Entry) SeedEffect(e);
        foreach (var e in state.Exit) SeedEffect(e);
        foreach (var t in state.Transitions)
        {
            if (t.Trigger.Kind == TriggerKind.Commit && t.Trigger.Agent.Length > 0)
            {
                relevant.Add(t.Trigger.Agent);
            }
            if (t.Trigger.Kind == TriggerKind.Guard) SeedOp(t.Trigger.Cond);
            foreach (var a in t.Actions) SeedEffect(a);
        }

        while (pending.Count > 0)
        {
            if (!opsById.TryGetValue(pending.Pop(), out var op))
            {
                continue;
            }
            if (op.IsRead && op.Agent.Length > 0) relevant.Add(op.Agent);
            if (op.Op == OpKind.Proximity && op.Target.Length > 0) relevant.Add(op.Target);
            foreach (var dep in OpRefIds(op))
            {
                if (relevant.Add(dep)) pending.Push(dep);
            }
        }

        foreach (var node in Nodes) node.IsDimmed = !relevant.Contains(node.NodeId);
        foreach (var wire in Wires) wire.IsDimmed = wire.From.IsDimmed || wire.To.IsDimmed;
    }

    // Apply a saved layout (zoom + hand-placed node positions) over the auto-layout; a restore,
    // so it does not mark the pane dirty.
    public void ApplyLayout(StatechartLayout layout)
    {
        Zoom = layout.DataflowZoom;
        foreach (var node in Nodes)
        {
            if (layout.NodePositions.TryGetValue(node.NodeId, out var p))
            {
                node.X = p.X;
                node.Y = p.Y;
            }
        }

        RaiseExtentChanged();
    }

    /// <summary>Rebuild the canvas from a chart's dataflow layer.</summary>
    public void Project(Chart chart)
    {
        _chart = chart;

        foreach (var w in Wires)
        {
            w.Dispose();
        }

        Wires.Clear();
        Nodes.Clear();
        SelectedNodes.Clear();

        var bindingNodes = new Dictionary<string, DataflowNodeViewModel>();
        var agentNodes = new Dictionary<string, DataflowNodeViewModel>();
        var opNodes = new Dictionary<string, DataflowNodeViewModel>();
        var opsById = new Dictionary<string, PureOp>();

        foreach (var b in chart.Bindings)
        {
            var node = new DataflowNodeViewModel(DataflowNodeKind.Binding, b.Port, "binding", b);
            node.OutputPorts.Add(new DataflowPortViewModel(node, 0, "entity", isInput: false));
            node.BindingEdited = MarkChartDirty;
            bindingNodes[b.Port] = node;
            Nodes.Add(node);
        }

        foreach (var a in chart.Agents)
        {
            var node = new DataflowNodeViewModel(DataflowNodeKind.Agent, a.Id, a.Owned ? "agent" : "agent (ref)", a);
            node.OutputPorts.Add(new DataflowPortViewModel(node, 0, "agent", isInput: false));
            node.SpecEdited = MarkChartDirty;
            node.AgentRenameRequested = newId => RenameAgent(a, newId);
            agentNodes[a.Id] = node;
            Nodes.Add(node);
        }

        var agentIds = chart.Agents.Select(a => a.Id).ToList();
        var bindingPorts = chart.Bindings.Select(b => b.Port).ToList();
        foreach (var p in chart.Pure)
        {
            var node = new DataflowNodeViewModel(DataflowNodeKind.Op, p.Id, p.Op.ToString(), p);
            node.OutputPorts.Add(new DataflowPortViewModel(node, 0, "out", isInput: false));
            AddOpInputPorts(node, p);
            node.ReadAgents = agentIds;
            node.ProximityTargets = bindingPorts;
            node.SlotEdited = MarkChartDirty;
            node.ReadRefChanged = ReprojectPreservingSelection;
            opNodes[p.Id] = node;
            opsById[p.Id] = p;
            Nodes.Add(node);
        }

        LayOut(opsById);
        Wire(chart, bindingNodes, agentNodes, opNodes);

        OnPropertyChanged(nameof(HasChart));
        OnPropertyChanged(nameof(HasGraph));
        OnPropertyChanged(nameof(GraphWidth));
        OnPropertyChanged(nameof(GraphHeight));
        OnPropertyChanged(nameof(ScaledGraphWidth));
        OnPropertyChanged(nameof(ScaledGraphHeight));
    }

    private void LayOut(IReadOnlyDictionary<string, PureOp> opsById)
    {
        var depth = new Dictionary<string, int>();

        int Depth(string opId, HashSet<string> stack)
        {
            if (depth.TryGetValue(opId, out var cached))
            {
                return cached;
            }
            if (!stack.Add(opId))
            {
                return 0;   // cycle guard (Validate rejects cycles before we get here)
            }

            var op = opsById[opId];
            int best = op.IsRead || op.Op == OpKind.Proximity ? 1 : 0;
            foreach (var refId in OpRefIds(op))
            {
                if (opsById.ContainsKey(refId))
                {
                    best = Math.Max(best, Depth(refId, stack) + 1);
                }
            }

            stack.Remove(opId);
            depth[opId] = best;
            return best;
        }

        var rowInColumn = new Dictionary<int, int>();
        foreach (var node in Nodes)
        {
            int column = node.Kind == DataflowNodeKind.Op ? Depth(node.NodeId, []) : 0;
            int row = rowInColumn.TryGetValue(column, out var r) ? r : 0;
            rowInColumn[column] = row + 1;

            node.Column = column;
            node.Row = row;
            node.X = CanvasPadding + column * (CardWidth + ColumnGap);
            node.Y = CanvasPadding + row * (CardHeight + RowGap);
        }
    }

    private void Wire(
        Chart chart,
        IReadOnlyDictionary<string, DataflowNodeViewModel> bindingNodes,
        IReadOnlyDictionary<string, DataflowNodeViewModel> agentNodes,
        IReadOnlyDictionary<string, DataflowNodeViewModel> opNodes)
    {
        foreach (var p in chart.Pure)
        {
            var target = opNodes[p.Id];
            foreach (var (index, source) in WiredInputs(p, bindingNodes, agentNodes, opNodes))
            {
                Wires.Add(new DataflowWireViewModel(source, target, index));
            }
        }
    }

    private void AddOpInputPorts(DataflowNodeViewModel node, PureOp p)
    {
        switch (p.Op)
        {
            case OpKind.Marginal:
            case OpKind.Committed:
            case OpKind.Memory:
                node.InputPorts.Add(new DataflowPortViewModel(node, 0, "agent", isInput: true) { IsWired = true });
                break;
            case OpKind.Proximity:
                node.InputPorts.Add(new DataflowPortViewModel(node, 0, "target", isInput: true) { IsWired = true });
                break;
            case OpKind.Select:
                node.InputPorts.Add(InputPort(node, 0, "cond", p.Cond));
                node.InputPorts.Add(InputPort(node, 1, "a", p.A));
                node.InputPorts.Add(InputPort(node, 2, "b", p.B));
                break;
            default:
                for (int i = 0; i < p.Ins.Count; i++)
                {
                    node.InputPorts.Add(InputPort(node, i, "in" + i, p.Ins[i]));
                }
                break;
        }
    }

    private DataflowPortViewModel InputPort(DataflowNodeViewModel node, int index, string label, ValueRef? r)
    {
        bool wired = r is { Kind: RefKind.Op };
        var port = new DataflowPortViewModel(node, index, label, isInput: true)
        {
            IsWired = wired,
            Constant = wired ? null : r,
        };
        if (wired)
        {
            port.DisconnectRequested = () => Disconnect(node.NodeId, index);
        }
        else if (r is not null)
        {
            port.Edited = MarkChartDirty;
        }

        return port;
    }

    // Wired inputs as (input row index, source node). Reads pull from their agent, proximity
    // from its target binding, and op-ref operands from the referenced op.
    private static IEnumerable<(int Index, DataflowNodeViewModel Source)> WiredInputs(
        PureOp p,
        IReadOnlyDictionary<string, DataflowNodeViewModel> bindingNodes,
        IReadOnlyDictionary<string, DataflowNodeViewModel> agentNodes,
        IReadOnlyDictionary<string, DataflowNodeViewModel> opNodes)
    {
        switch (p.Op)
        {
            case OpKind.Marginal:
            case OpKind.Committed:
            case OpKind.Memory:
                if (agentNodes.TryGetValue(p.Agent, out var agent))
                {
                    yield return (0, agent);
                }
                break;
            case OpKind.Proximity:
                if (bindingNodes.TryGetValue(p.Target, out var binding))
                {
                    yield return (0, binding);
                }
                break;
            case OpKind.Select:
                foreach (var w in OpRefWire(0, p.Cond, opNodes)) yield return w;
                foreach (var w in OpRefWire(1, p.A, opNodes)) yield return w;
                foreach (var w in OpRefWire(2, p.B, opNodes)) yield return w;
                break;
            default:
                for (int i = 0; i < p.Ins.Count; i++)
                {
                    foreach (var w in OpRefWire(i, p.Ins[i], opNodes))
                    {
                        yield return w;
                    }
                }
                break;
        }
    }

    private static IEnumerable<(int, DataflowNodeViewModel)> OpRefWire(
        int index, ValueRef? r, IReadOnlyDictionary<string, DataflowNodeViewModel> opNodes)
    {
        if (r is { Kind: RefKind.Op } && opNodes.TryGetValue(r.Op, out var source))
        {
            yield return (index, source);
        }
    }

    private static IEnumerable<string> OpRefIds(PureOp p)
    {
        foreach (var r in p.Ins)
        {
            if (r.Kind == RefKind.Op) yield return r.Op;
        }
        if (p.Cond is { Kind: RefKind.Op }) yield return p.Cond.Op;
        if (p.A is { Kind: RefKind.Op }) yield return p.A.Op;
        if (p.B is { Kind: RefKind.Op }) yield return p.B.Op;
    }
}
