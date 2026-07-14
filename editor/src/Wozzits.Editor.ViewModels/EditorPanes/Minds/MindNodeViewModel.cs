namespace Wozzits.Editor.ViewModels.EditorPanes.Minds;

using System.Globalization;
using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

// One decision qubit on the mind canvas -- a node the shared interaction controller can
// select and drag. Bonds attach to its CENTER (an undirected coupling). The goal bias is
// shown here and (seam 2c) edited here.
public sealed class MindNodeViewModel : ViewModelBase, ICanvasNode
{
    public const double NodeWidth = 96.0;
    public const double NodeHeight = 68.0;

    private double _x;
    private double _y;
    private bool _isSelected;

    public MindNodeViewModel(MindQubit qubit, int index)
    {
        Qubit = qubit;
        Index = index;
    }

    public MindQubit Qubit { get; }

    // Positional index in the mind (q0, q1, ...) -- the display + bond emit use it.
    public int Index { get; }

    public string NodeId => Qubit.Id;

    public double X { get => _x; set => SetProperty(ref _x, value); }

    public double Y { get => _y; set => SetProperty(ref _y, value); }

    public bool IsSelected { get => _isSelected; set => SetProperty(ref _isSelected, value); }

    public double CenterX => _x + NodeWidth / 2.0;

    public double CenterY => _y + NodeHeight / 2.0;

    public string Title => $"q{Index}";

    // "+0.4" / "-0.25" / "—" (no bias); drives the node's goal line.
    public string GoalLabel => Qubit.Goal == 0.0
        ? "—"
        : Qubit.Goal.ToString("+0.##;-0.##", CultureInfo.InvariantCulture);

    public bool LeansZero => Qubit.Goal > 0.0;   // goal favors the |0> outcome
    public bool LeansOne => Qubit.Goal < 0.0;    // goal favors |1>
}
