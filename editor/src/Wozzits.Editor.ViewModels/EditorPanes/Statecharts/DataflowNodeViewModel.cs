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
    private bool _isDimmed;

    public DataflowNodeViewModel(DataflowNodeKind kind, string nodeId, string subtitle, object model)
    {
        Kind = kind;
        NodeId = nodeId;
        Subtitle = subtitle;
        Model = model;
        PropertyRows = BuildRows(model);
        SpecFields = kind == DataflowNodeKind.Agent && model is AgentDecl agent
            ? BuildSpecFields(agent)
            : Array.Empty<EditableFieldViewModel>();
        BindingFields = kind == DataflowNodeKind.Binding && model is Binding binding
            ? new[] { new EditableFieldViewModel("find", () => binding.Find, v => binding.Find = v, () => BindingEdited?.Invoke()) }
            : Array.Empty<EditableFieldViewModel>();

        if (model is PureOp op)
        {
            IsReadOp = op.IsRead;
            IsProximityOp = op.Op == OpKind.Proximity;
            if (op.IsRead)
            {
                SlotEditor = new EditableFieldViewModel(
                    op.Op == OpKind.Memory ? "qubit" : "slot",
                    () => op.Slot.ToString(CultureInfo.InvariantCulture),
                    v => { if (int.TryParse(v, NumberStyles.Integer, CultureInfo.InvariantCulture, out var s) && s >= 0) op.Slot = s; },
                    () => SlotEdited?.Invoke());
            }
        }
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

    // Cross-layer focus: when a control state is selected, dataflow nodes that don't feed it
    // are dimmed (DataflowPaneViewModel.FocusOnState). Layout stays put; deselect restores.
    public bool IsDimmed
    {
        get => _isDimmed;
        set
        {
            if (SetProperty(ref _isDimmed, value))
            {
                OnPropertyChanged(nameof(DimOpacity));
            }
        }
    }

    public double DimOpacity => _isDimmed ? 0.28 : 1.0;

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

    // Editable agent quantum_agent spec fields (E3b-ii); empty for non-agent nodes. The pane
    // sets SpecEdited so a commit marks the chart dirty (spec isn't drawn, so no re-project).
    public IReadOnlyList<EditableFieldViewModel> SpecFields { get; }

    public bool HasSpecFields => SpecFields.Count > 0;

    public Action? SpecEdited { get; set; }

    // Editable binding fields (the entity `find` name); empty for non-binding nodes. The pane
    // sets BindingEdited so a commit marks the chart dirty (find isn't drawn on the card).
    public IReadOnlyList<EditableFieldViewModel> BindingFields { get; }

    public bool HasBindingFields => BindingFields.Count > 0;

    public Action? BindingEdited { get; set; }

    // Read ops (marginal/committed/memory) pull from an agent + slot; proximity senses a target
    // binding. Both are picked in the inspector rather than wired. The pane supplies the choices
    // (ReadAgents / ProximityTargets) and the callbacks: a ref change reprojects (the wire moves),
    // a slot edit just marks dirty.
    public bool IsReadOp { get; }

    public bool IsProximityOp { get; }

    // The operand-inputs section is for value inputs only -- reads/proximity take a ref, not a
    // value, so they show their picker instead.
    public bool ShowOperandInputs => HasInputPorts && !IsReadOp && !IsProximityOp;

    public EditableFieldViewModel? SlotEditor { get; }

    public IReadOnlyList<string> ReadAgents { get; set; } = Array.Empty<string>();

    public IReadOnlyList<string> ProximityTargets { get; set; } = Array.Empty<string>();

    public Action? ReadRefChanged { get; set; }

    public Action? SlotEdited { get; set; }

    public string SelectedReadAgent
    {
        get => (Model as PureOp)?.Agent ?? string.Empty;
        set
        {
            if (Model is PureOp op && value is not null && op.Agent != value)
            {
                op.Agent = value;
                OnPropertyChanged();
                ReadRefChanged?.Invoke();
            }
        }
    }

    public string SelectedProximityTarget
    {
        get => (Model as PureOp)?.Target ?? string.Empty;
        set
        {
            if (Model is PureOp op && value is not null && op.Target != value)
            {
                op.Target = value;
                OnPropertyChanged();
                ReadRefChanged?.Invoke();
            }
        }
    }

    private static IReadOnlyList<InspectorRow> BuildRows(object model) => model switch
    {
        Binding b => new List<InspectorRow>
        {
            new("scope", b.Subtree ? "subtree" : "global"),
        },
        AgentDecl a => BuildAgentRows(a),
        PureOp p => BuildOpRows(p),
        _ => Array.Empty<InspectorRow>(),
    };

    private static IReadOnlyList<InspectorRow> BuildAgentRows(AgentDecl a) => new List<InspectorRow>
    {
        new("owned", a.Owned ? "yes" : "no"),
        new("host", a.Host),
    };

    // Read/proximity refs are now editable pickers (SelectedReadAgent / SlotEditor /
    // SelectedProximityTarget); ops carry no read-only property rows.
    private static IReadOnlyList<InspectorRow> BuildOpRows(PureOp p) => Array.Empty<InspectorRow>();

    private IReadOnlyList<EditableFieldViewModel> BuildSpecFields(AgentDecl a)
    {
        if (a.Spec is not JsonObject spec)
        {
            return Array.Empty<EditableFieldViewModel>();
        }

        var fields = new List<EditableFieldViewModel>();
        foreach (var member in spec)
        {
            var key = member.Key;
            fields.Add(new EditableFieldViewModel(
                key,
                () => JsonScalarText(spec[key]),
                v => spec[key] = ParseJsonScalar(v),
                () => SpecEdited?.Invoke()));
        }

        return fields;
    }

    private static string JsonScalarText(JsonNode? node)
    {
        if (node is JsonValue value)
        {
            if (value.TryGetValue<double>(out var d)) return d.ToString(CultureInfo.InvariantCulture);
            if (value.TryGetValue<bool>(out var b)) return b ? "true" : "false";
            if (value.TryGetValue<string>(out var s)) return s;
        }

        return node?.ToJsonString() ?? string.Empty;
    }

    private static JsonNode ParseJsonScalar(string text)
    {
        if (double.TryParse(text, NumberStyles.Any, CultureInfo.InvariantCulture, out var d))
        {
            return JsonValue.Create(d);
        }
        if (bool.TryParse(text, out var b))
        {
            return JsonValue.Create(b);
        }

        return JsonValue.Create(text)!;
    }
}
