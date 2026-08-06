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
    private int _groupIndex = -1;
    private bool _isExclusive;

    public MindNodeViewModel(MindQubit qubit, int index)
    {
        Qubit = qubit;
        Index = index;
        GoalEditor = new EditableFieldViewModel(
            "goal",
            () => Qubit.Goal.ToString("0.###", CultureInfo.InvariantCulture),
            v =>
            {
                if (double.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, out var g)
                    && g != Qubit.Goal)
                {
                    Qubit.Goal = g;
                    OnPropertyChanged(nameof(GoalLabel));
                    OnPropertyChanged(nameof(LeansZero));
                    OnPropertyChanged(nameof(LeansOne));
                }
            },
            () => GoalEdited?.Invoke());
    }

    public MindQubit Qubit { get; }

    // Edit the qubit's longitudinal goal bias (shown on the card + in the inspector).
    public EditableFieldViewModel GoalEditor { get; }

    // Fired when the goal is committed, so the pane can mark the mind dirty.
    public Action? GoalEdited { get; set; }

    // Positional index in the mind (qubit 0, qubit 1, ...) -- the display + bond emit use it.
    public int Index { get; }

    public string NodeId => Qubit.Id;

    public double X { get => _x; set => SetProperty(ref _x, value); }

    public double Y { get => _y; set => SetProperty(ref _y, value); }

    public bool IsSelected { get => _isSelected; set => SetProperty(ref _isSelected, value); }

    // Which multi-disposition AGENT this qubit belongs to, as a stable color slot among
    // the grouped agents (-1 = a single-disposition agent of its own, the default -- drawn
    // plain). Set by the pane on projection; drives the node's group tint + hull.
    public int GroupIndex
    {
        get => _groupIndex;
        set
        {
            if (SetProperty(ref _groupIndex, value))
            {
                OnPropertyChanged(nameof(IsGrouped));
                OnPropertyChanged(nameof(GroupTag));
            }
        }
    }

    // Is the owning agent an EXCLUSIVE (one-hot) choice? Only meaningful when grouped;
    // the canvas shows it by the group tag's colour, and it is set in the properties panel.
    public bool IsExclusive
    {
        get => _isExclusive;
        set => SetProperty(ref _isExclusive, value);
    }

    // In an agent with siblings (not its own singleton).
    public bool IsGrouped => _groupIndex >= 0;

    // The corner tag on a grouped qubit -- the shared agent, e.g. "A0" -- so co-agent qubits
    // read as one decision-maker. Empty for a singleton, which is drawn without agent chrome.
    public string GroupTag => IsGrouped ? $"A{_groupIndex}" : string.Empty;

    public double CenterX => _x + NodeWidth / 2.0;

    public double CenterY => _y + NodeHeight / 2.0;

    // "qubit N", matching what a statechart read op calls its qubit -- so slot ↔ qubit lines up.
    public string Title => $"qubit {Index}";

    // "+0.4" / "-0.25" / "—" (no bias); drives the node's goal line.
    public string GoalLabel => Qubit.Goal == 0.0
        ? "—"
        : Qubit.Goal.ToString("+0.##;-0.##", CultureInfo.InvariantCulture);

    public bool LeansZero => Qubit.Goal > 0.0;   // goal favors the |0> outcome
    public bool LeansOne => Qubit.Goal < 0.0;    // goal favors |1>
}
