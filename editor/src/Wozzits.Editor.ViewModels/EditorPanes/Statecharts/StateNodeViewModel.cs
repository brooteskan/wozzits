namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.Globalization;
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
        DoEffects = model.Do.Select(FormatEffect).ToList();
        EntryEffects = model.Entry.Select(FormatEffect).ToList();
        ExitEffects = model.Exit.Select(FormatEffect).ToList();
        OutgoingTransitions = model.Transitions.Select(FormatTransition).ToList();
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

    // Read-only detail for the inspector panel: each effect / outgoing transition as a line.
    public IReadOnlyList<string> DoEffects { get; }

    public IReadOnlyList<string> EntryEffects { get; }

    public IReadOnlyList<string> ExitEffects { get; }

    public IReadOnlyList<string> OutgoingTransitions { get; }

    public bool HasDoEffects => DoEffects.Count > 0;

    public bool HasEntryEffects => EntryEffects.Count > 0;

    public bool HasExitEffects => ExitEffects.Count > 0;

    public bool HasTransitions => OutgoingTransitions.Count > 0;

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

    private static string FormatEffect(Effect e) => e.Kind switch
    {
        EffectKind.SetGoal => $"set_goal {e.Agent}[{e.Slot}] = {FormatValue(e.Value)}",
        EffectKind.SetDecoherence => $"set_decoherence {e.Agent} = {FormatValue(e.Value)}",
        EffectKind.Rearm => $"rearm {e.Agent}",
        EffectKind.Reward => $"reward {e.Agent} q{e.Slot} {(e.Toward ? "toward |0>" : "toward |1>")} {FormatValue(e.Value)}",
        EffectKind.SetScale => $"set_scale {e.TargetBind} = {FormatValue(e.Value)}",
        EffectKind.SetVisible => $"set_visible {e.TargetBind} = {FormatValue(e.Value)}",
        EffectKind.PlaySound => $"play_sound {e.TargetBind}",
        _ => e.Kind.ToString(),
    };

    private static string FormatTransition(Transition t)
    {
        string actions = t.Actions.Count > 0 ? $"  ·  {t.Actions.Count} action(s)" : string.Empty;
        return $"→ {t.Target}   on {TriggerLabel(t.Trigger)}{actions}";
    }

    private static string TriggerLabel(Trigger t) => t.Kind switch
    {
        TriggerKind.Commit => t.Outcome is int outcome ? $"commit={outcome}" : "commit",
        TriggerKind.After => $"after {t.Seconds.ToString("0.###", CultureInfo.InvariantCulture)}s",
        TriggerKind.Guard => "guard",
        TriggerKind.Event => string.IsNullOrEmpty(t.EventName) ? "event" : $"event {t.EventName}",
        _ => string.Empty,
    };

    private static string FormatValue(ValueRef? r) => r is null
        ? string.Empty
        : r.Kind == RefKind.Op
            ? $"op:{r.Op}"
            : r.IsBool
                ? (r.Const != 0 ? "true" : "false")
                : r.Const.ToString(CultureInfo.InvariantCulture);
}
