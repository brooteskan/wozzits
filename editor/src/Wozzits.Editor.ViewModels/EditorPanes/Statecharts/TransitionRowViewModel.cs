namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.Globalization;
using CommunityToolkit.Mvvm.Input;
using Wozzits.Editor.Statecharts;

// One outgoing-transition line in the state inspector. The target + trigger read as a
// label; an `after` trigger's delay is the one editable scalar, exposed as a field that
// writes straight into the shared Trigger.Seconds (in place, no reproject). Other trigger
// kinds (commit/guard/event) are shown read-only.
public sealed class TransitionRowViewModel : ViewModelBase
{
    private readonly Transition _transition;

    public TransitionRowViewModel(Transition transition, Action? edited)
    {
        _transition = transition;
        Label = $"→ {transition.Target}";
        _selectedKind = transition.Trigger.Kind;

        if (transition.Trigger.Kind == TriggerKind.After)
        {
            SecondsEditor = new EditableFieldViewModel(
                "after",
                () => transition.Trigger.Seconds.ToString("0.###", CultureInfo.InvariantCulture),
                text => SetSeconds(transition.Trigger, text),
                edited);
        }
        else if (transition.Trigger.Kind == TriggerKind.Event)
        {
            // The behavior-event NAME the transition fires on (a behavior emits it via
            // wz_emit_behavior_event). Editable in place, like the `after` delay.
            EventNameEditor = new EditableFieldViewModel(
                "event",
                () => transition.Trigger.EventName,
                text => transition.Trigger.EventName = text?.Trim() ?? string.Empty,
                edited);
        }
        else if (transition.Trigger.Kind != TriggerKind.Guard)
        {
            // commit (and any future read-only kind); a guard shows its condition picker.
            TriggerText = TriggerLabel(transition.Trigger);
        }

        ActionSummary = transition.Actions.Count > 0
            ? $"· {transition.Actions.Count} action(s)"
            : string.Empty;
    }

    public string Target => _transition.Target;

    // "-> TARGET"; the trigger is shown in the value column beside it.
    public string Label { get; }

    // The full formatted line (used by the state's derived string list / tests).
    public string Display
    {
        get
        {
            string actions = ActionSummary.Length > 0 ? $"  {ActionSummary}" : string.Empty;
            return $"→ {_transition.Target}   on {TriggerLabel(_transition.Trigger)}{actions}";
        }
    }

    // Editable delay for an `after` trigger; null for commit/guard/event triggers.
    public EditableFieldViewModel? SecondsEditor { get; }

    public bool IsAfter => SecondsEditor is not null;

    // Editable event name for an `event` trigger; null otherwise.
    public EditableFieldViewModel? EventNameEditor { get; }

    public bool IsEvent => EventNameEditor is not null;

    // Read-only trigger label for commit/guard triggers; null for `after`/`event`.
    public string? TriggerText { get; }

    public bool HasTriggerText => TriggerText is not null;

    // ── guard condition (the op whose value gates the transition) ──────────────
    // A guard fires when its condition op is non-zero. The condition is picked from the
    // chart's ops the same way an effect's value source is; "(always)" means a constant
    // guard that always fires (the SetTriggerKind seed). Without this the kind picker can
    // select "guard" but there is no way to say WHICH op gates it -- the transition is
    // stuck always-firing. Mirrors EffectRowViewModel.SelectedValueSource.
    public const string AlwaysSentinel = "(always)";

    public bool IsGuard => _transition.Trigger.Kind == TriggerKind.Guard;

    private IReadOnlyList<string> _guardSources = new[] { AlwaysSentinel };

    // The pane threads "(always) + the chart's ops" here after building the state.
    public IReadOnlyList<string> GuardSources
    {
        get => _guardSources;
        set
        {
            _guardSources = value is { Count: > 0 } ? value : new[] { AlwaysSentinel };
            OnPropertyChanged();
        }
    }

    // Invoked when the guard's source flips between an op and the always-constant (the pane
    // reprojects, since the canvas wire op -> transition appears/disappears).
    public Action? GuardSourceChanged { get; set; }

    public string SelectedGuardSource
    {
        get => _transition.Trigger.Cond is { Kind: RefKind.Op } op && !string.IsNullOrEmpty(op.Op)
            ? op.Op
            : AlwaysSentinel;
        set
        {
            var cond = _transition.Trigger.Cond;
            // Guard the transient null a rebinding ComboBox writes back, and no-op an
            // unchanged pick.
            if (cond is null || value is null || value == SelectedGuardSource)
            {
                return;
            }
            if (value == AlwaysSentinel)
            {
                cond.Kind = RefKind.Const;
                cond.Op = string.Empty;
                cond.Const = 1;   // a constant guard fires always
            }
            else
            {
                cond.Kind = RefKind.Op;
                cond.Op = value;
            }
            OnPropertyChanged();
            GuardSourceChanged?.Invoke();
        }
    }

    public string ActionSummary { get; }

    public bool HasActions => ActionSummary.Length > 0;

    // Invoked to delete this transition (the state VM routes it to the pane).
    public Action? DeleteRequested { get; set; }

    private IRelayCommand? _deleteCommand;

    public IRelayCommand DeleteCommand =>
        _deleteCommand ??= new RelayCommand(() => DeleteRequested?.Invoke());

    // The kinds a transition trigger can be, for the inspector's picker.
    public IReadOnlyList<TriggerKind> TriggerKinds { get; } =
        new[] { TriggerKind.Commit, TriggerKind.After, TriggerKind.Guard, TriggerKind.Event };

    // Invoked when the picker changes the kind (the state VM routes it to the pane, which sets
    // sensible defaults for the new kind and re-projects).
    public Action<TriggerKind>? TriggerKindChangeRequested { get; set; }

    private TriggerKind _selectedKind;

    public TriggerKind SelectedTriggerKind
    {
        get => _selectedKind;
        set
        {
            if (_selectedKind == value)
            {
                return;
            }

            _selectedKind = value;
            OnPropertyChanged();
            TriggerKindChangeRequested?.Invoke(value);
        }
    }

    private static string TriggerLabel(Trigger t) => t.Kind switch
    {
        TriggerKind.Commit => t.Outcome is int outcome ? $"commit={outcome}" : "commit",
        TriggerKind.After => $"after {t.Seconds.ToString("0.###", CultureInfo.InvariantCulture)}s",
        TriggerKind.Guard => "guard",
        TriggerKind.Event => string.IsNullOrEmpty(t.EventName) ? "event" : $"event {t.EventName}",
        _ => string.Empty,
    };

    private static void SetSeconds(Trigger t, string text)
    {
        if (double.TryParse(text, NumberStyles.Any, CultureInfo.InvariantCulture, out var s) && s >= 0)
        {
            t.Seconds = s;
        }
    }
}
