namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.Collections.ObjectModel;
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
public sealed class DataflowPaneViewModel : ViewModelBase
{
    private const double CanvasPadding = 28.0;
    private const double CardWidth = 220.0;
    private const double CardHeight = 116.0;
    private const double PortRowBaseY = 82.0;
    private const double PortRowSpacing = 18.0;
    private const double ColumnGap = 120.0;
    private const double RowGap = 40.0;
    private const double MinZoom = 0.25;
    private const double MaxZoom = 4.0;

    private double _zoom = 1.0;

    public ObservableCollection<DataflowNodeViewModel> Nodes { get; } = [];

    public ObservableCollection<DataflowWireViewModel> Wires { get; } = [];

    public ObservableCollection<DataflowNodeViewModel> SelectedNodes { get; } = [];

    public bool HasGraph => Nodes.Count > 0;

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

    /// <summary>Rebuild the canvas from a chart's dataflow layer.</summary>
    public void Project(Chart chart)
    {
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
            bindingNodes[b.Port] = node;
            Nodes.Add(node);
        }

        foreach (var a in chart.Agents)
        {
            var node = new DataflowNodeViewModel(DataflowNodeKind.Agent, a.Id, a.Owned ? "agent" : "agent (ref)", a);
            node.OutputPorts.Add(new DataflowPortViewModel(node, 0, "agent", isInput: false));
            agentNodes[a.Id] = node;
            Nodes.Add(node);
        }

        foreach (var p in chart.Pure)
        {
            var node = new DataflowNodeViewModel(DataflowNodeKind.Op, p.Id, p.Op.ToString(), p);
            node.OutputPorts.Add(new DataflowPortViewModel(node, 0, "out", isInput: false));
            AddOpInputPorts(node, p);
            opNodes[p.Id] = node;
            opsById[p.Id] = p;
            Nodes.Add(node);
        }

        LayOut(opsById);
        Wire(chart, bindingNodes, agentNodes, opNodes);

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
                Wires.Add(new DataflowWireViewModel(source, target, index, CardWidth, PortRowBaseY, PortRowSpacing));
            }
        }
    }

    private static void AddOpInputPorts(DataflowNodeViewModel node, PureOp p)
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

    private static DataflowPortViewModel InputPort(DataflowNodeViewModel node, int index, string label, ValueRef? r)
    {
        bool wired = r is { Kind: RefKind.Op };
        return new DataflowPortViewModel(node, index, label, isInput: true)
        {
            IsWired = wired,
            Constant = wired ? null : r,
        };
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
