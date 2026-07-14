namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.Globalization;
using System.Linq;
using CommunityToolkit.Mvvm.Input;
using Wozzits.Editor.Protocol;
using Wozzits.Editor.Statecharts;

// One effect line in the state inspector (a do/entry/exit action). The descriptive
// prefix (kind + agent/target) is read-only; when the effect carries a CONSTANT value
// it is exposed as an editable field that writes straight into the shared Effect's
// ValueRef -- the same in-place, no-reproject path the dataflow op constants use.
// Op-valued effects show their source op as a read-only marker instead.
public sealed class EffectRowViewModel : ViewModelBase
{
    private readonly Effect _effect;
    private readonly Action? _edited;

    public EffectRowViewModel(Effect effect, Action? edited)
    {
        _effect = effect;
        _edited = edited;
        Label = Describe(effect);

        if (effect.Value is { Kind: RefKind.Const } value)
        {
            ValueEditor = new EditableFieldViewModel(
                "value",
                () => FormatConst(value),
                text => SetConst(value, text),
                edited);
        }
    }

    // Actuator effects (set_scale/set_visible/play_sound) drive a bound entity; their TARGET
    // binding is pickable so an effect can target ANY of the chart's bindings, not just the first.
    // The state VM supplies the chart's binding ports in TargetBindings. Agent effects have none.
    public bool IsActuator =>
        _effect.Kind is EffectKind.SetScale or EffectKind.SetVisible or EffectKind.PlaySound;

    private IReadOnlyList<string> _targetBindings = Array.Empty<string>();

    public IReadOnlyList<string> TargetBindings
    {
        get => _targetBindings;
        set
        {
            _targetBindings = value ?? Array.Empty<string>();
            foreach (var a in CallArgs) a.Bindings = _targetBindings;   // call-arg binding pickers
        }
    }

    public string SelectedTargetBind
    {
        get => _effect.TargetBind;
        set
        {
            // Guard null (a ComboBox rebind fires a transient null); scalar edit, no reproject.
            if (value is not null && _effect.TargetBind != value)
            {
                _effect.TargetBind = value;
                OnPropertyChanged();
                _edited?.Invoke();
            }
        }
    }

    // The picker entry that means "a literal constant" rather than an op's output.
    public const string ConstSentinel = "(constant)";

    // The read-only prefix: kind plus its agent/target, without the "= value" tail.
    public string Label { get; }

    // The full formatted line (used by the state's derived string list / tests).
    public string Display => Format(_effect);

    // Editable constant value, or null when the effect is op-valued or valueless.
    public EditableFieldViewModel? ValueEditor { get; }

    public bool IsEditable => ValueEditor is not null;

    // Effects with a value (set_goal/scale/…) can source it from a constant or an op's output —
    // this is the terminal link that lets a read op reach an actuator. Valueless effects
    // (rearm/play_sound) have no source. The state VM supplies the op choices in ValueSources.
    public bool HasValueSource => _effect.Value is not null;

    private IReadOnlyList<string> _valueSources = Array.Empty<string>();

    public IReadOnlyList<string> ValueSources
    {
        get => _valueSources;
        set
        {
            _valueSources = value ?? Array.Empty<string>();
            foreach (var a in CallArgs) a.ValueSources = _valueSources;   // call-arg scalar pickers
        }
    }

    public Action? ValueSourceChanged { get; set; }

    public string SelectedValueSource
    {
        get => _effect.Value is { Kind: RefKind.Op } op ? op.Op : ConstSentinel;
        set
        {
            if (_effect.Value is null || value is null || value == SelectedValueSource)
            {
                return;
            }

            if (value == ConstSentinel)
            {
                _effect.Value.Kind = RefKind.Const;
                _effect.Value.Op = string.Empty;
            }
            else
            {
                _effect.Value.Kind = RefKind.Op;
                _effect.Value.Op = value;
            }

            OnPropertyChanged();
            ValueSourceChanged?.Invoke();
        }
    }

    // ── call (a behavior-registered actuator) ──────────────────────────────────
    // A Call effect picks an actuator by name and fills its declared args. The catalog
    // (name + param schema) and the agent-id list are pushed in by the state VM, just
    // like TargetBindings/ValueSources; CallArgs rebuilds when either the catalog or the
    // chosen actuator changes so the arg rows always match the schema, and _effect.Args
    // is kept shaped to that schema so the emitted IR is valid however the user got here.
    public bool IsCall => _effect.Kind == EffectKind.Call;

    private IReadOnlyList<EngineActuator> _catalog = Array.Empty<EngineActuator>();

    public IReadOnlyList<EngineActuator> ActuatorCatalog
    {
        get => _catalog;
        set
        {
            _catalog = value ?? Array.Empty<EngineActuator>();
            if (IsCall)
            {
                EnsureFnDefault();
                BuildCallArgs();
                OnPropertyChanged(nameof(ActuatorNames));
                OnPropertyChanged(nameof(SelectedActuator));
                OnPropertyChanged(nameof(CallArgs));
            }
        }
    }

    public IReadOnlyList<string> ActuatorNames => _catalog.Select(a => a.Name).ToList();

    public string SelectedActuator
    {
        get => _effect.Fn;
        set
        {
            if (value is null || value == _effect.Fn)
            {
                return;
            }
            _effect.Fn = value;
            BuildCallArgs();
            OnPropertyChanged();
            OnPropertyChanged(nameof(CallArgs));
            _edited?.Invoke();
        }
    }

    public IReadOnlyList<CallArgRowViewModel> CallArgs { get; private set; } =
        Array.Empty<CallArgRowViewModel>();

    private IReadOnlyList<string> _availableAgents = Array.Empty<string>();

    public IReadOnlyList<string> AvailableAgents
    {
        get => _availableAgents;
        set
        {
            _availableAgents = value ?? Array.Empty<string>();
            foreach (var a in CallArgs) a.Agents = _availableAgents;
        }
    }

    private void EnsureFnDefault()
    {
        if (string.IsNullOrEmpty(_effect.Fn) && _catalog.Count > 0)
        {
            _effect.Fn = _catalog[0].Name;
        }
    }

    private void BuildCallArgs()
    {
        var actuator = _catalog.FirstOrDefault(a => a.Name == _effect.Fn);
        if (actuator is null)
        {
            CallArgs = Array.Empty<CallArgRowViewModel>();
            return;
        }

        // Reconcile _effect.Args against the schema: keep an existing arg when its kind
        // family still fits (edits survive an actuator re-pick), else seed a default.
        var reconciled = new List<CallArg>(actuator.Params.Count);
        for (int i = 0; i < actuator.Params.Count; i++)
        {
            var desired = MapParamKind(actuator.Params[i].Kind);
            var existing = i < _effect.Args.Count ? _effect.Args[i] : null;
            reconciled.Add(existing is not null && SameFamily(existing.Kind, desired)
                ? existing
                : Seed(desired, actuator.Params[i]));
        }
        _effect.Args.Clear();
        _effect.Args.AddRange(reconciled);

        var rows = new List<CallArgRowViewModel>(actuator.Params.Count);
        for (int i = 0; i < actuator.Params.Count; i++)
        {
            var p = actuator.Params[i];
            rows.Add(new CallArgRowViewModel(p.Name, MapParamKind(p.Kind), _effect.Args[i], () => _edited?.Invoke())
            {
                Bindings = _targetBindings,
                Agents = _availableAgents,
                ValueSources = _valueSources,
            });
        }
        CallArgs = rows;
    }

    private static CallArgKind MapParamKind(int kind) => kind switch
    {
        1 => CallArgKind.Bind,
        2 => CallArgKind.Agent,
        _ => CallArgKind.Const,   // scalar family (const|op)
    };

    private static bool SameFamily(CallArgKind k, CallArgKind desired) => desired switch
    {
        CallArgKind.Bind => k == CallArgKind.Bind,
        CallArgKind.Agent => k == CallArgKind.Agent,
        _ => k is CallArgKind.Const or CallArgKind.Op,
    };

    private CallArg Seed(CallArgKind desired, EngineActuatorParam p) => desired switch
    {
        CallArgKind.Bind => CallArg.ToBind(_targetBindings.FirstOrDefault() ?? string.Empty),
        CallArgKind.Agent => CallArg.ToAgent(_availableAgents.FirstOrDefault() ?? string.Empty),
        _ => CallArg.Number(p.DefaultValue),
    };

    // Invoked to remove this effect from its state (the state VM routes it to the pane).
    public Action? DeleteRequested { get; set; }

    private IRelayCommand? _deleteCommand;

    public IRelayCommand DeleteCommand =>
        _deleteCommand ??= new RelayCommand(() => DeleteRequested?.Invoke());

    private static string Describe(Effect e) => e.Kind switch
    {
        EffectKind.SetGoal => $"set_goal {e.Agent}[{e.Slot}]",
        EffectKind.MeasureAt => $"measure_at {e.Agent}[{e.Slot}]",
        EffectKind.SetDecoherence => $"set_decoherence {e.Agent}",
        EffectKind.Rearm => $"rearm {e.Agent}",
        EffectKind.Reward => $"reward {e.Agent} q{e.Slot} {(e.Toward ? "toward |0>" : "toward |1>")}",
        // Actuators show their target as an editable picker (SelectedTargetBind), not in the label.
        EffectKind.SetScale => "set_scale",
        EffectKind.SetVisible => "set_visible",
        EffectKind.PlaySound => "play_sound",
        // Call shows its actuator picker + arg rows; the label is just the verb.
        EffectKind.Call => "call",
        _ => e.Kind.ToString(),
    };

    private static string Format(Effect e) => e.Kind switch
    {
        EffectKind.SetGoal => $"set_goal {e.Agent}[{e.Slot}] = {FormatValue(e.Value)}",
        EffectKind.MeasureAt => $"measure_at {e.Agent}[{e.Slot}] axis={FormatValue(e.Value)}",
        EffectKind.SetDecoherence => $"set_decoherence {e.Agent} = {FormatValue(e.Value)}",
        EffectKind.Rearm => $"rearm {e.Agent}",
        EffectKind.Reward => $"reward {e.Agent} q{e.Slot} {(e.Toward ? "toward |0>" : "toward |1>")} {FormatValue(e.Value)}",
        EffectKind.SetScale => $"set_scale {e.TargetBind} = {FormatValue(e.Value)}",
        EffectKind.SetVisible => $"set_visible {e.TargetBind} = {FormatValue(e.Value)}",
        EffectKind.PlaySound => $"play_sound {e.TargetBind}",
        EffectKind.Call => $"call {e.Fn}({string.Join(", ", e.Args.Select(FormatArg))})",
        _ => e.Kind.ToString(),
    };

    private static string FormatArg(CallArg a) => a.Kind switch
    {
        CallArgKind.Bind => $"bind:{a.Bind}",
        CallArgKind.Agent => $"agent:{a.Agent}",
        CallArgKind.Op => $"op:{a.Op}",
        _ => a.IsBool ? (a.Const != 0 ? "true" : "false") : a.Const.ToString(CultureInfo.InvariantCulture),
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
