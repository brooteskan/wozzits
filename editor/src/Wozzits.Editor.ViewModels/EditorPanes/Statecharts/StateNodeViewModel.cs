namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using CommunityToolkit.Mvvm.Input;
using Wozzits.Editor.Protocol;
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
            new TransitionRowViewModel(t, () => TransitionEdited?.Invoke())
            {
                DeleteRequested = () => TransitionDeleteRequested?.Invoke(t),
                TriggerKindChangeRequested = k => TransitionKindChangeRequested?.Invoke(t, k),
                GuardSourceChanged = () => EffectValueSourceChanged?.Invoke(),
            }).ToList();
        NameEditor = new EditableFieldViewModel("name", () => Model.Id, v => RenameRequested?.Invoke(v));
    }

    public State Model { get; }

    public string StateId { get; }

    public string Title => StateId;

    // Editable state name (its id). Renaming rewrites every transition that targets it and its
    // region membership (initial + states list) -- the pane does that (RenameState) via
    // RenameRequested, which the view binds to a "name" field in the inspector.
    public EditableFieldViewModel NameEditor { get; }

    public Action<string>? RenameRequested { get; set; }

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

    // Invoked when a transition row's editable field (the `after` delay) is edited. Unlike a
    // plain effect-constant edit, a trigger's value is DRAWN on the graph (the arrow label), so
    // the pane wires this to a reproject -- otherwise the label keeps its projection-time value.
    public Action? TransitionEdited { get; set; }

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
        new(effect, () => Edited?.Invoke())
        {
            DeleteRequested = () => EffectDeleteRequested?.Invoke(effect),
            ValueSourceChanged = () => EffectValueSourceChanged?.Invoke(),
        };

    // Invoked when an effect's value source flips between constant and an op (the pane routes it
    // to a reproject, since the row's editable field changes).
    public Action? EffectValueSourceChanged { get; set; }

    // The dataflow op ids an effect value can be sourced from; the pane sets this after building
    // the state, which pushes the "(constant) + ops" choice list into every effect row.
    private IReadOnlyList<string> _availableOps = Array.Empty<string>();

    public IReadOnlyList<string> AvailableOps
    {
        get => _availableOps;
        set
        {
            _availableOps = value;
            var sources = new List<string> { EffectRowViewModel.ConstSentinel };
            sources.AddRange(value);
            foreach (var r in DoEffectRows) r.ValueSources = sources;
            foreach (var r in EntryEffectRows) r.ValueSources = sources;
            foreach (var r in ExitEffectRows) r.ValueSources = sources;

            // A guard picks its condition from the same ops, with "(always)" for a
            // constant guard instead of the effect list's "(constant)".
            var guardSources = new List<string> { TransitionRowViewModel.AlwaysSentinel };
            guardSources.AddRange(value);
            foreach (var r in TransitionRows) r.GuardSources = guardSources;
        }
    }

    // The chart's binding ports, pushed into every effect row so an ACTUATOR effect (set_scale/
    // set_visible/play_sound) can target any binding, not just the first. The pane sets this after
    // building the state.
    private IReadOnlyList<string> _availableBindings = Array.Empty<string>();

    public IReadOnlyList<string> AvailableBindings
    {
        get => _availableBindings;
        set
        {
            _availableBindings = value;
            foreach (var r in DoEffectRows) r.TargetBindings = value;
            foreach (var r in EntryEffectRows) r.TargetBindings = value;
            foreach (var r in ExitEffectRows) r.TargetBindings = value;
        }
    }

    // The chart's agent ids, pushed into every effect row so a `call` effect's AGENT
    // args can pick any agent. The pane sets this after building the state.
    private IReadOnlyList<string> _availableAgents = Array.Empty<string>();

    public IReadOnlyList<string> AvailableAgents
    {
        get => _availableAgents;
        set
        {
            _availableAgents = value;
            foreach (var r in DoEffectRows) r.AvailableAgents = value;
            foreach (var r in EntryEffectRows) r.AvailableAgents = value;
            foreach (var r in ExitEffectRows) r.AvailableAgents = value;
        }
    }

    // The behavior-actuator catalog, pushed into every effect row so a `call` effect can
    // pick an actuator and render its declared arg pickers. Set LAST (after ops/bindings/
    // agents) so each row seeds its args against the schema with the choice lists in hand.
    private IReadOnlyList<EngineActuator> _availableActuators = Array.Empty<EngineActuator>();

    public IReadOnlyList<EngineActuator> AvailableActuators
    {
        get => _availableActuators;
        set
        {
            _availableActuators = value;
            foreach (var r in DoEffectRows) r.ActuatorCatalog = value;
            foreach (var r in EntryEffectRows) r.ActuatorCatalog = value;
            foreach (var r in ExitEffectRows) r.ActuatorCatalog = value;
        }
    }

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
