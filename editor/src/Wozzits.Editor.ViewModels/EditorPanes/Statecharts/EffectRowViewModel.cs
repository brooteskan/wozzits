namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.Globalization;
using Wozzits.Editor.Statecharts;

// One effect line in the state inspector (a do/entry/exit action). The descriptive
// prefix (kind + agent/target) is read-only; when the effect carries a CONSTANT value
// it is exposed as an editable field that writes straight into the shared Effect's
// ValueRef -- the same in-place, no-reproject path the dataflow op constants use.
// Op-valued effects show their source op as a read-only marker instead.
public sealed class EffectRowViewModel : ViewModelBase
{
    private readonly Effect _effect;

    public EffectRowViewModel(Effect effect, Action? edited)
    {
        _effect = effect;
        Label = Describe(effect);

        if (effect.Value is { Kind: RefKind.Const } value)
        {
            ValueEditor = new EditableFieldViewModel(
                "value",
                () => FormatConst(value),
                text => SetConst(value, text),
                edited);
        }
        else if (effect.Value is { Kind: RefKind.Op } op)
        {
            ReadOnlyValue = $"op:{op.Op}";
        }
    }

    // The read-only prefix: kind plus its agent/target, without the "= value" tail.
    public string Label { get; }

    // The full formatted line (used by the state's derived string list / tests).
    public string Display => Format(_effect);

    // Editable constant value, or null when the effect is op-valued or valueless.
    public EditableFieldViewModel? ValueEditor { get; }

    public bool IsEditable => ValueEditor is not null;

    // A non-editable value marker (an op source); null when there is nothing to show.
    public string? ReadOnlyValue { get; }

    public bool HasReadOnlyValue => ReadOnlyValue is not null;

    private static string Describe(Effect e) => e.Kind switch
    {
        EffectKind.SetGoal => $"set_goal {e.Agent}[{e.Slot}]",
        EffectKind.SetDecoherence => $"set_decoherence {e.Agent}",
        EffectKind.Rearm => $"rearm {e.Agent}",
        EffectKind.Reward => $"reward {e.Agent} q{e.Slot} {(e.Toward ? "toward |0>" : "toward |1>")}",
        EffectKind.SetScale => $"set_scale {e.TargetBind}",
        EffectKind.SetVisible => $"set_visible {e.TargetBind}",
        EffectKind.PlaySound => $"play_sound {e.TargetBind}",
        _ => e.Kind.ToString(),
    };

    private static string Format(Effect e) => e.Kind switch
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

    private static string FormatValue(ValueRef? r) => r is null
        ? string.Empty
        : r.Kind == RefKind.Op
            ? $"op:{r.Op}"
            : FormatConst(r);

    private static string FormatConst(ValueRef r) => r.IsBool
        ? (r.Const != 0 ? "true" : "false")
        : r.Const.ToString(CultureInfo.InvariantCulture);

    private static void SetConst(ValueRef v, string text)
    {
        if (double.TryParse(text, NumberStyles.Any, CultureInfo.InvariantCulture, out var d))
        {
            v.Const = d;
            v.IsBool = false;
        }
        else if (bool.TryParse(text, out var b))
        {
            v.Const = b ? 1 : 0;
            v.IsBool = true;
        }
    }
}
