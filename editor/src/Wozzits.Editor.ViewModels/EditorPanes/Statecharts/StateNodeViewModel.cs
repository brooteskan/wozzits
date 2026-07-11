namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using CommunityToolkit.Mvvm.Input;
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

        // Rows late-bind Edited / *Requested (set by the pane after construction) so an edit or
        // delete marks the chart dirty without the row needing a pane reference.
        DoEffectRows = model.Do.Select(e => EffectRow(e)).ToList();
        EntryEffectRows = model.Entry.Select(e => EffectRow(e)).ToList();
        ExitEffectRows = model.Exit.Select(e => EffectRow(e)).ToList();
        AddDoEffectCommand = new RelayCommand<EffectKind>(k => EffectAddRequested?.Invoke(EffectSlot.Do, k));
        AddEntryEffectCommand = new RelayCommand<EffectKind>(k => EffectAddRequested?.Invoke(EffectSlot.Entry, k));
        AddExitEffectCommand = new RelayCommand<EffectKind>(k => EffectAddRequested?.Invoke(EffectSlot.Exit, k));
        TransitionRows = model.Transitions.Select(t =>
            new TransitionRowViewModel(t, () => Edited?.Invoke())
            {
                DeleteRequested = () => TransitionDeleteRequested?.Invoke(t),
                TriggerKindChangeRequested = k => TransitionKindChangeRequested?.Invoke(t, k),
            }).ToList();
    }

    public State Model { get; }

    public string StateId { get; }

    public string Title => StateId;

    // This state is its region's initial state (shown with an entry marker).
    public bool IsInitial { get; }

    // Offer a "Make initial" action only when this state isn't already the region's initial.
    public bool CanMakeInitial => !IsInitial;

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

    // Invoked to delete one of this state's outgoing transitions (the pane routes it to
    // DeleteTransition); each transition row calls it with its own model.
    public Action<Transition>? TransitionDeleteRequested { get; set; }

    // Invoked to change a transition's trigger kind (the pane routes it to SetTriggerKind).
    public Action<Transition, TriggerKind>? TransitionKindChangeRequested { get; set; }

    // Invoked to add an effect of a given kind to this state's do/entry/exit list, and to
    // remove one (the pane routes them to AddEffect / DeleteEffect).
    public Action<EffectSlot, EffectKind>? EffectAddRequested { get; set; }

    public Action<Effect>? EffectDeleteRequested { get; set; }

    // Invoked to make this state its region's initial (the pane routes it to SetInitial).
    public Action? SetInitialRequested { get; set; }

    private IRelayCommand? _makeInitialCommand;

    public IRelayCommand MakeInitialCommand =>
        _makeInitialCommand ??= new RelayCommand(() => SetInitialRequested?.Invoke());

    // The "+" menu commands for each effect list (command parameter = the effect kind to add).
    public IRelayCommand<EffectKind> AddDoEffectCommand { get; }

    public IRelayCommand<EffectKind> AddEntryEffectCommand { get; }

    public IRelayCommand<EffectKind> AddExitEffectCommand { get; }

    private EffectRowViewModel EffectRow(Effect effect) =>
        new(effect, () => Edited?.Invoke()) { DeleteRequested = () => EffectDeleteRequested?.Invoke(effect) };

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

// Which of a state's effect lists an effect belongs to / is added to.
public enum EffectSlot
{
    Do,
    Entry,
    Exit,
}
