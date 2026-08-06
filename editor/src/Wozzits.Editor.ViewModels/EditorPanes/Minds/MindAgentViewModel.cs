namespace Wozzits.Editor.ViewModels.EditorPanes.Minds;

using System;
using System.Globalization;
using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

// The agent that owns the selected qubit, shown in the document properties panel. An
// agent groups one or more dispositions (qubits); when it owns several, its exclusivity
// (one_hot) can be tuned here -- 0 leaves the dispositions independent, a positive weight
// makes them a mutually-exclusive "pick one" choice. Rebuilt on selection / reproject, so
// it always reflects the live MindAgent.
public sealed class MindAgentViewModel : ViewModelBase
{
    // A reasonable exclusivity weight to suggest: the engine's one_hot doc recommends
    // ~2x the largest goal you expect, and too small lets two dispositions be active.
    public const double DefaultOneHot = 2.0;

    public MindAgentViewModel(MindAgent agent, int memberCount, int groupIndex, Action onEdited)
    {
        MemberCount = memberCount;
        GroupIndex = groupIndex;
        OneHotEditor = new EditableFieldViewModel(
            "one_hot",
            () => agent.OneHot.ToString("0.###", CultureInfo.InvariantCulture),
            v =>
            {
                if (double.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, out var x))
                {
                    agent.OneHot = Math.Max(0.0, x);
                }
            },
            onEdited);
    }

    public int MemberCount { get; }

    // Color slot among the grouped agents (-1 for a singleton) -- matches the canvas tint.
    public int GroupIndex { get; }

    // Owns more than one disposition, so exclusivity is meaningful.
    public bool IsGrouped => MemberCount > 1;

    public EditableFieldViewModel OneHotEditor { get; }

    public string Label => IsGrouped
        ? $"agent A{GroupIndex} · {MemberCount} dispositions"
        : "own agent (1 disposition)";
}
