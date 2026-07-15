namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Linq;
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
        AgentNameEditor = kind == DataflowNodeKind.Agent && model is AgentDecl ag
            ? new EditableFieldViewModel("name", () => ag.Id, v => AgentRenameRequested?.Invoke(v))
            : null;
        // A REF (owned == false) names WHICH quantum_agent on the host it reads.
        AgentTargetEditor = kind == DataflowNodeKind.Agent && model is AgentDecl agRef && !agRef.Owned
            ? new EditableFieldViewModel("reads agent", () => agRef.AgentName, v => agRef.AgentName = v, () => AgentTargetEdited?.Invoke())
            : null;
        // An op's id is editable (like an agent's) so op1 can become a readable name;
        // the pane's RenameOp rewrites every reference to it.
        OpNameEditor = kind == DataflowNodeKind.Op && model is PureOp pop
            ? new EditableFieldViewModel("name", () => pop.Id, v => OpRenameRequested?.Invoke(v))
            : null;

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

    // Field-guide colour class flags for the card border (agent = violet, op/binding = teal).
    public bool IsAgentNode => Kind == DataflowNodeKind.Agent;

    public bool IsOpNode => Kind == DataflowNodeKind.Op;

    public bool IsBindingNode => Kind == DataflowNodeKind.Binding;

    public string NodeId { get; }

    public string Title => NodeId;

    public string Subtitle { get; }

    public object Model { get; }

    // A REF (owned == false) can NAME which mind (.mind.json) it reads. The pane supplies the
    // available mind names (with a "(none)" first); double-clicking the ref opens the chosen
    // mind and a read op's slot picker bounds to its qubits -- no scene node needed.
    public bool HasMindPicker => Kind == DataflowNodeKind.Agent && Model is AgentDecl { Owned: false };

    public const string NoMind = "(none)";

    private IReadOnlyList<string> _mindChoices = new[] { NoMind };

    public IReadOnlyList<string> MindChoices
    {
        get => _mindChoices;
        set { _mindChoices = value is { Count: > 0 } ? value : new[] { NoMind }; OnPropertyChanged(); }
    }

    public string SelectedMind
    {
        get => Model is AgentDecl { Mind.Length: > 0 } a ? a.Mind : NoMind;
        set
        {
            if (Model is not AgentDecl a)
            {
                return;
            }
            var mind = string.IsNullOrEmpty(value) || value == NoMind ? string.Empty : value;
            if (a.Mind != mind)
            {
                a.Mind = mind;
                OnPropertyChanged();
                MindRefChanged?.Invoke();
            }
        }
    }

    public Action? MindRefChanged { get; set; }

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

    // Sentinel property name raised when a measured dot offset changes, so wires observing this
    // node re-read their endpoints (alongside X/Y).
    public const string PortGeometryChanged = "PortGeometry";

    // Measured centres of the output dot and the first input dot, relative to the card's
    // top-left, written by the view once the card is laid out (DataflowPaneView.MeasurePorts).
    // Wires and the drag preview add these to the node's X/Y so they meet the dots exactly --
    // independent of how tall the card's header renders. They default to the shared layout
    // constants so geometry is sane before the first measure.
    private double _outputDotX = DataflowPaneViewModel.CardWidth;
    private double _outputDotY = DataflowPaneViewModel.PortRowBaseY;
    private double _inputDotX;
    private double _inputDotBaseY = DataflowPaneViewModel.PortRowBaseY;

    public double OutputDotX => _outputDotX;

    public double OutputDotY => _outputDotY;

    public double InputDotX => _inputDotX;

    public double InputDotBaseY => _inputDotBaseY;

    public void SetMeasuredOutputDot(double x, double y)
    {
        if (Close(_outputDotX, x) && Close(_outputDotY, y))
        {
            return;
        }

        _outputDotX = x;
        _outputDotY = y;
        OnPropertyChanged(nameof(OutputDotX));
        OnPropertyChanged(nameof(OutputDotY));
        OnPropertyChanged(PortGeometryChanged);
    }

    public void SetMeasuredInputDot(double x, double baseY)
    {
        if (Close(_inputDotX, x) && Close(_inputDotBaseY, baseY))
        {
            return;
        }

        _inputDotX = x;
        _inputDotBaseY = baseY;
        OnPropertyChanged(nameof(InputDotX));
        OnPropertyChanged(nameof(InputDotBaseY));
        OnPropertyChanged(PortGeometryChanged);
    }

    private static bool Close(double a, double b) => Math.Abs(a - b) < 0.5;

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

    // The single `find` field, surfaced so the binding card can show it inline (not just the
    // inspector). Card + inspector bind the same object, so an edit in one updates both.
    public EditableFieldViewModel? BindingFindEditor => BindingFields.Count > 0 ? BindingFields[0] : null;

    public Action? BindingEdited { get; set; }

    // Editable binding scope: "subtree" resolves the find only within the runner node's
    // descendants (the demo pattern -- runner on a container above its named children); "global"
    // resolves the find by name ANYWHERE, so it works wherever the runner sits. Scalar edit (not
    // drawn), so it just marks the chart dirty.
    public IReadOnlyList<string> BindingScopes { get; } = new[] { "subtree", "global" };

    public string SelectedBindingScope
    {
        get => Model is Binding b && !b.Subtree ? "global" : "subtree";
        set
        {
            // Guard null: a ComboBox fires a transient null SelectedItem on rebind, and treating
            // that as "not global" would silently revert the scope to subtree (matches the null
            // guard the read-agent / proximity pickers use).
            if (Model is Binding b && value is not null)
            {
                bool subtree = value != "global";
                if (b.Subtree != subtree)
                {
                    b.Subtree = subtree;
                    OnPropertyChanged();
                    BindingEdited?.Invoke();
                }
            }
        }
    }

    // Editable agent name (its id) for an agent node; null otherwise. Renaming rewrites every
    // reference to the agent -- the pane does that (RenameAgent) via AgentRenameRequested.
    public EditableFieldViewModel? AgentNameEditor { get; }

    public bool HasAgentName => AgentNameEditor is not null;

    public Action<string>? AgentRenameRequested { get; set; }

    // For a REF agent (owned == false): the LABEL of the quantum_agent on the host node
    // it reads. Empty = the first agent found (back-compat). Null for an owned agent
    // (which creates its own from Spec). Editing marks the chart dirty -- the target is
    // an abstract label, not a drawn wire, so no re-project.
    public EditableFieldViewModel? AgentTargetEditor { get; }

    public bool HasAgentTarget => AgentTargetEditor is not null;

    public Action? AgentTargetEdited { get; set; }

    // Editable op id (its name) for an op node; null otherwise. Renaming rewrites every
    // reference to the op -- the pane does that (RenameOp) via OpRenameRequested.
    public EditableFieldViewModel? OpNameEditor { get; }

    public bool HasOpName => OpNameEditor is not null;

    public Action<string>? OpRenameRequested { get; set; }

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

    // When the read op targets an OWNED agent (or a ref that names a mind), the pane sets this
    // to the decision count so the slot becomes a 0..N-1 picker (SlotChoices). 0 = unknown (an
    // unresolved ref, or a memory op) -- the inspector then keeps the free-text SlotEditor.
    // Notifies so a live re-bound (e.g. after the ref's mind changes) refreshes the picker.
    private int _slotChoiceCount;

    public int SlotChoiceCount
    {
        get => _slotChoiceCount;
        set
        {
            if (_slotChoiceCount != value)
            {
                _slotChoiceCount = value;
                OnPropertyChanged();
                OnPropertyChanged(nameof(HasSlotChoices));
                OnPropertyChanged(nameof(SlotChoices));
            }
        }
    }

    public bool HasSlotChoices => SlotChoiceCount > 0;

    public IReadOnlyList<string> SlotChoices => HasSlotChoices
        ? Enumerable.Range(0, SlotChoiceCount)
            .Select(i => i.ToString(CultureInfo.InvariantCulture)).ToList()
        : Array.Empty<string>();

    public string SelectedSlot
    {
        get => (Model as PureOp)?.Slot.ToString(CultureInfo.InvariantCulture) ?? "0";
        set
        {
            if (Model is PureOp op
                && int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var s)
                && s >= 0 && op.Slot != s)
            {
                op.Slot = s;
                OnPropertyChanged();
                SlotEdited?.Invoke();
            }
        }
    }

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
        // Binding scope is now an editable picker (SelectedBindingScope), not a read-only row.
        Binding => Array.Empty<InspectorRow>(),
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
