namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using Wozzits.Editor.Statecharts;

// A control-layer state box (A1: compact -- a title plus effect/transition badge
// counts; effects are edited in the inspector, not on the card).
public sealed class StateNodeViewModel : ViewModelBase, ICanvasNode
{
    private double _x;
    private double _y;
    private bool _isSelected;

    public StateNodeViewModel(State model, bool isInitial)
    {
        Model = model;
        StateId = model.Id;
        IsInitial = isInitial;
        DoCount = model.Do.Count;
        EntryCount = model.Entry.Count;
        ExitCount = model.Exit.Count;
        OutgoingCount = model.Transitions.Count;
    }

    public State Model { get; }

    public string StateId { get; }

    public string Title => StateId;

    // This state is its region's initial state (shown with an entry marker).
    public bool IsInitial { get; }

    public int DoCount { get; }

    public int EntryCount { get; }

    public int ExitCount { get; }

    public int OutgoingCount { get; }

    public string Summary
    {
        get
        {
            var parts = new List<string>(3);
            if (DoCount > 0) parts.Add($"do·{DoCount}");
            if (EntryCount > 0) parts.Add($"entry·{EntryCount}");
            if (ExitCount > 0) parts.Add($"exit·{ExitCount}");
            return string.Join("   ", parts);
        }
    }

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
}
