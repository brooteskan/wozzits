namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using Wozzits.Editor.Statecharts;

// A control-layer state box (A1: compact -- a title plus effect/transition badge
// counts; effects and transitions are edited in the inspector, not on the card).
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

        // Rows late-bind Edited (set by the pane after construction) so editing a
        // constant / after-delay marks the chart dirty without a reproject.
        DoEffectRows = model.Do.Select(e => new EffectRowViewModel(e, () => Edited?.Invoke())).ToList();
        EntryEffectRows = model.Entry.Select(e => new EffectRowViewModel(e, () => Edited?.Invoke())).ToList();
        ExitEffectRows = model.Exit.Select(e => new EffectRowViewModel(e, () => Edited?.Invoke())).ToList();
        TransitionRows = model.Transitions.Select(t => new TransitionRowViewModel(t, () => Edited?.Invoke())).ToList();
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

    // Editable inspector rows for each effect / outgoing transition.
    public IReadOnlyList<EffectRowViewModel> DoEffectRows { get; }

    public IReadOnlyList<EffectRowViewModel> EntryEffectRows { get; }

    public IReadOnlyList<EffectRowViewModel> ExitEffectRows { get; }

    public IReadOnlyList<TransitionRowViewModel> TransitionRows { get; }

    // Invoked when a row's editable field is edited (the pane wires it to mark dirty).
    public Action? Edited { get; set; }

    // Convenience read-only string views of the rows (used by tests / plain text).
    public IReadOnlyList<string> DoEffects => DoEffectRows.Select(r => r.Display).ToList();

    public IReadOnlyList<string> EntryEffects => EntryEffectRows.Select(r => r.Display).ToList();

    public IReadOnlyList<string> ExitEffects => ExitEffectRows.Select(r => r.Display).ToList();

    public IReadOnlyList<string> OutgoingTransitions => TransitionRows.Select(r => r.Display).ToList();

    public bool HasDoEffects => DoEffectRows.Count > 0;

    public bool HasEntryEffects => EntryEffectRows.Count > 0;

    public bool HasExitEffects => ExitEffectRows.Count > 0;

    public bool HasTransitions => TransitionRows.Count > 0;

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
