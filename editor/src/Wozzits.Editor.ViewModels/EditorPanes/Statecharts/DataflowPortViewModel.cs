namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.Globalization;
using Wozzits.Editor.Statecharts;

// A port on a dataflow node. Ops have one output ("out") and one input per operand;
// bindings/agents are leaf sources with a single output. An input operand is either
// WIRED (fed by another node's output) or an inline CONSTANT literal.
public sealed class DataflowPortViewModel : ViewModelBase
{
    public DataflowPortViewModel(DataflowNodeViewModel owner, int index, string label, bool isInput)
    {
        Owner = owner;
        Index = index;
        Label = label;
        IsInput = isInput;
    }

    public DataflowNodeViewModel Owner { get; }

    public int Index { get; }

    public string Label { get; }

    public bool IsInput { get; }

    public bool IsOutput => !IsInput;

    // Graph-space anchor of this port's dot (matches the wire endpoint geometry): the single
    // output sits at the card's right edge, inputs stack down the left edge. Used by the
    // wiring gesture to draw the drag preview from the source port.
    public double AnchorX => IsOutput
        ? Owner.X + DataflowPaneViewModel.CardWidth
        : Owner.X;

    public double AnchorY => Owner.Y + DataflowPaneViewModel.PortRowBaseY
        + (IsInput ? Index * DataflowPaneViewModel.PortRowSpacing : 0);

    // Input ports only: an unwired operand carries a literal constant instead of a wire.
    public ValueRef? Constant { get; init; }

    // An input fed by a wire from another node's output (an op/agent/binding ref).
    public bool IsWired { get; init; }

    public bool IsConstant => Constant is not null;

    public string ConstantText => Constant is null
        ? string.Empty
        : Constant.IsBool
            ? (Constant.Const != 0 ? "true" : "false")
            : Constant.Const.ToString(CultureInfo.InvariantCulture);

    // Value shown for this input in the inspector: the literal, or a "wired" marker.
    public string InspectorValue => IsConstant ? ConstantText : (IsWired ? "← wired" : string.Empty);

    // Invoked when the constant is edited in the inspector (the pane wires it to mark dirty).
    public Action? Edited { get; set; }

    private EditableFieldViewModel? _constEditor;

    // An editable field for a constant input (null for wired inputs). Writes the parsed value
    // straight into the shared ValueRef, so the card literal + the model update with no reproject.
    public EditableFieldViewModel? ConstEditor => IsConstant
        ? _constEditor ??= new EditableFieldViewModel("value", () => ConstantText, SetConstant, () => Edited?.Invoke())
        : null;

    private void SetConstant(string text)
    {
        if (Constant is null)
        {
            return;
        }

        if (double.TryParse(text, NumberStyles.Any, CultureInfo.InvariantCulture, out var d))
        {
            Constant.Const = d;
            Constant.IsBool = false;
        }
        else if (bool.TryParse(text, out var b))
        {
            Constant.Const = b ? 1 : 0;
            Constant.IsBool = true;
        }
        else
        {
            return;
        }

        OnPropertyChanged(nameof(ConstantText));
        OnPropertyChanged(nameof(InspectorValue));
    }
}
