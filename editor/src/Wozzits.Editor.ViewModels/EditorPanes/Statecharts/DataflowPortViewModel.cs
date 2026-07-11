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
}
