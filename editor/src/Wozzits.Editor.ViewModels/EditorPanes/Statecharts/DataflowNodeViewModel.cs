namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.Collections.ObjectModel;
using System.Globalization;
using System.Text.Json.Nodes;
using Wozzits.Editor.Statecharts;

public enum DataflowNodeKind
{
    Binding,
    Agent,
    Op,
}

// A card on the dataflow canvas: a binding, an agent, or a pure op. `Model` is the
// backing authoring object (Binding | AgentDecl | PureOp) for editing/inspection.
public sealed class DataflowNodeViewModel : ViewModelBase, ICanvasNode
{
    private double _x;
    private double _y;
    private bool _isSelected;

    public DataflowNodeViewModel(DataflowNodeKind kind, string nodeId, string subtitle, object model)
    {
        Kind = kind;
        NodeId = nodeId;
        Subtitle = subtitle;
        Model = model;
        PropertyRows = BuildRows(model);
    }

    public DataflowNodeKind Kind { get; }

    public string NodeId { get; }

    public string Title => NodeId;

    public string Subtitle { get; }

    public object Model { get; }

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

    // Assigned by the pane's auto-layout: column == dependency depth, row == order within.
    public int Column { get; set; }

    public int Row { get; set; }

    public ObservableCollection<DataflowPortViewModel> InputPorts { get; } = [];

    public ObservableCollection<DataflowPortViewModel> OutputPorts { get; } = [];

    public bool HasInputPorts => InputPorts.Count > 0;

    // Kind-specific detail lines for the inspector panel (binding find/scope, agent
    // owned/host + spec fields, read agent/slot, proximity target). Math/select ops carry
    // no extras -- their operands show as the input-port rows.
    public IReadOnlyList<InspectorRow> PropertyRows { get; }

    private static IReadOnlyList<InspectorRow> BuildRows(object model) => model switch
    {
        Binding b => new List<InspectorRow>
        {
            new("find", b.Find),
            new("scope", b.Subtree ? "subtree" : "global"),
        },
        AgentDecl a => BuildAgentRows(a),
        PureOp p => BuildOpRows(p),
        _ => Array.Empty<InspectorRow>(),
    };

    private static IReadOnlyList<InspectorRow> BuildAgentRows(AgentDecl a)
    {
        var rows = new List<InspectorRow>
        {
            new("owned", a.Owned ? "yes" : "no"),
            new("host", a.Host),
        };
        if (a.Spec is JsonObject spec)
        {
            foreach (var member in spec)
            {
                rows.Add(new InspectorRow(member.Key, member.Value?.ToJsonString() ?? "null"));
            }
        }

        return rows;
    }

    private static IReadOnlyList<InspectorRow> BuildOpRows(PureOp p) => p.Op switch
    {
        OpKind.Marginal or OpKind.Committed => new List<InspectorRow>
        {
            new("agent", p.Agent),
            new("slot", p.Slot.ToString(CultureInfo.InvariantCulture)),
        },
        OpKind.Memory => new List<InspectorRow>
        {
            new("agent", p.Agent),
            new("qubit", p.Slot.ToString(CultureInfo.InvariantCulture)),
        },
        OpKind.Proximity => new List<InspectorRow> { new("target", p.Target) },
        _ => Array.Empty<InspectorRow>(),
    };
}
