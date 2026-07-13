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
        else
        {
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
