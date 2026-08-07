namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.Globalization;
using Wozzits.Editor.Statecharts;

// One argument of a `call` effect, rendered with the picker its DECLARED param kind
// calls for: a Scalar param shows the const|op value-source (+ an editable constant),
// a Binding param a binding dropdown, an Agent param an agent dropdown. The row edits
// the shared CallArg in place -- the same no-reproject path the effect-value editor
// uses. The choice lists (bindings/agents/value-sources) are pushed in by the owning
// effect row, exactly as they are for the effect's own pickers.
public sealed class CallArgRowViewModel : ViewModelBase
{
    private readonly CallArg _arg;
    private readonly Action? _edited;

    public CallArgRowViewModel(string paramName, CallArgKind paramKind, CallArg arg, Action? edited)
    {
        ParamName = paramName;
        ParamKind = paramKind;
        _arg = arg;
        _edited = edited;

        if (IsScalar)
        {
            ValueEditor = new EditableFieldViewModel(
                paramName,
                () => FormatConst(_arg),
                text => SetConst(_arg, text),
                edited);
        }
    }

    // The param's name ("target", "speed") and its authoring kind (which picker shows).
    public string ParamName { get; }

    public CallArgKind ParamKind { get; }

    // A Scalar param is authored as Const (default) OR an op output; Binding/Agent are
    // single-choice dropdowns. Const here means "the scalar family" (const|op).
    public bool IsScalar => ParamKind == CallArgKind.Const;

    public bool IsBinding => ParamKind == CallArgKind.Bind;

    public bool IsAgent => ParamKind == CallArgKind.Agent;

    // ── scalar: const|op value-source + editable constant (mirrors EffectRowViewModel) ──
    public const string ConstSentinel = "(constant)";

    public IReadOnlyList<string> ValueSources { get; set; } = Array.Empty<string>();

    public EditableFieldViewModel? ValueEditor { get; }

    public bool IsEditable => ValueEditor is not null && _arg.Kind == CallArgKind.Const;

    public string SelectedValueSource
    {
        get => _arg.Kind == CallArgKind.Op ? _arg.Op : ConstSentinel;
        set
        {
            if (!IsScalar || value is null || value == SelectedValueSource)
            {
                return;
            }

            if (value == ConstSentinel)
            {
                _arg.Kind = CallArgKind.Const;
                _arg.Op = string.Empty;
            }
            else
            {
                _arg.Kind = CallArgKind.Op;
                _arg.Op = value;
            }

            OnPropertyChanged();
            OnPropertyChanged(nameof(IsEditable));
            _edited?.Invoke();
        }
    }

    // ── binding dropdown ──
    public IReadOnlyList<string> Bindings { get; set; } = Array.Empty<string>();

    public string SelectedBinding
    {
        get => _arg.Bind;
        set
        {
            if (value is not null && _arg.Bind != value)
            {
                _arg.Bind = value;
                OnPropertyChanged();
                _edited?.Invoke();
            }
        }
    }

    // ── agent dropdown ──
    public IReadOnlyList<string> Agents { get; set; } = Array.Empty<string>();

    public string SelectedAgent
    {
        get => _arg.Agent;
        set
        {
            if (value is not null && _arg.Agent != value)
            {
                _arg.Agent = value;
                OnPropertyChanged();
                _edited?.Invoke();
            }
        }
    }

    private static string FormatConst(CallArg a) => a.IsBool
        ? (a.Const != 0 ? "true" : "false")
        : a.Const.ToString(CultureInfo.InvariantCulture);

    private static void SetConst(CallArg a, string text)
    {
        if (double.TryParse(text, NumberStyles.Any, CultureInfo.InvariantCulture, out var d))
        {
            a.Const = d;
            a.IsBool = false;
        }
        else if (bool.TryParse(text, out var b))
        {
            a.Const = b ? 1 : 0;
            a.IsBool = true;
        }
    }
}
