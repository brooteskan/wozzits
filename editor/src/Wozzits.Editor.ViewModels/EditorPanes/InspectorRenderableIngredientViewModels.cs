using System;
using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.Input;

namespace Wozzits.Editor.ViewModels.EditorPanes;

// One binding row of the "Renderable bindings" inspector section (issue #230).
// The row set is GENERATED from the effective render program's authored
// binding layout (#227) — one row per declared SRV semantic the geometry does
// not own — so there is no user "add": authoring means picking a source for a
// declared row (the ✕ clears the pick; a cleared declared row surfaces as a
// compile reason on the synthesized renderable, never a silent fallback).
// Picking applies LIVE via the inspector's callback (house pattern: no Apply
// button).
public sealed class InspectorRenderableBindingRowViewModel : ViewModelBase
{
    private readonly Action<string, ulong> _apply;
    private InspectorAssetGraphRefOptionViewModel? _selectedOption;
    private bool _suppressApply;

    public InspectorRenderableBindingRowViewModel(
        string semantic,
        string kindLabel,
        ObservableCollection<InspectorAssetGraphRefOptionViewModel> options,
        ulong? initialSelectedId,
        Action<string, ulong> apply)
    {
        Semantic = semantic;
        KindLabel = kindLabel;
        Options = options;
        _apply = apply;

        if (initialSelectedId is { } id)
        {
            _suppressApply = true;
            foreach (var option in Options)
            {
                if (option.Id == id)
                {
                    _selectedOption = option;
                    break;
                }
            }
            _suppressApply = false;
        }

        ClearCommand = new RelayCommand(Clear);
    }

    // The layout row's canonical descriptor-semantic name (the engine's
    // kDescriptorSemanticNames token, e.g. "scalar_field_texture").
    public string Semantic { get; }

    // "texture" / "buffer" — which resource kind the layout row declares; the
    // Options list is already filtered to asset types publishing that kind.
    public string KindLabel { get; }

    public ObservableCollection<InspectorAssetGraphRefOptionViewModel> Options { get; }

    public bool HasOptions => Options.Count > 0;

    public InspectorAssetGraphRefOptionViewModel? SelectedOption
    {
        get => _selectedOption;
        set
        {
            if (SetProperty(ref _selectedOption, value)
                && !_suppressApply
                && value is not null)
            {
                _apply(Semantic, value.Id);
            }
        }
    }

    public IRelayCommand ClearCommand { get; }

    private void Clear()
    {
        _suppressApply = true;
        SetProperty(ref _selectedOption, null, nameof(SelectedOption));
        _suppressApply = false;
        _apply(Semantic, 0);
    }
}

// One constant row of the "Renderable bindings" inspector section (issue
// #230): a per-instance override for one of the layout's declared constant
// tail fields, typed by the field's declared width (float..float4/color).
// Editing a component applies the override LIVE (a pack-time merge on the
// engine side — no recompile); the ✕ removes the override so the instance
// falls back to the synthesized recipe's default.
public sealed class InspectorRenderableConstantRowViewModel : ViewModelBase
{
    private readonly Action<string, float[]?> _apply;
    private readonly string[] _components = ["0", "0", "0", "0"];
    private bool _isOverridden;
    private bool _suppressApply;

    public InspectorRenderableConstantRowViewModel(
        string name,
        int width,
        string typeLabel,
        float[]? initialValue,
        Action<string, float[]?> apply)
    {
        Name = name;
        Width = Math.Clamp(width, 1, 4);
        TypeLabel = typeLabel;
        _apply = apply;

        if (initialValue is not null)
        {
            _isOverridden = true;
            for (var i = 0; i < 4; ++i)
            {
                _components[i] =
                    initialValue[i].ToString(CultureInfo.InvariantCulture);
            }
        }

        RemoveCommand = new RelayCommand(RemoveOverride);
    }

    // The layout's declared tail-field name (constN_name).
    public string Name { get; }

    // Declared component count (Float=1 .. Float4/Color=4); gates which
    // component boxes show.
    public int Width { get; }

    public string TypeLabel { get; }

    public bool ShowY => Width >= 2;

    public bool ShowZ => Width >= 3;

    public bool ShowW => Width >= 4;

    // True once the node carries an authored override for this field (vs the
    // recipe's zero/graph default). Drives the ✕ affordance.
    public bool IsOverridden
    {
        get => _isOverridden;
        private set => SetProperty(ref _isOverridden, value);
    }

    public string ValueX
    {
        get => _components[0];
        set { if (SetProperty(ref _components[0], value)) OnComponentEdited(); }
    }

    public string ValueY
    {
        get => _components[1];
        set { if (SetProperty(ref _components[1], value)) OnComponentEdited(); }
    }

    public string ValueZ
    {
        get => _components[2];
        set { if (SetProperty(ref _components[2], value)) OnComponentEdited(); }
    }

    public string ValueW
    {
        get => _components[3];
        set { if (SetProperty(ref _components[3], value)) OnComponentEdited(); }
    }

    public IRelayCommand RemoveCommand { get; }

    // Every component box edit re-applies the full 4-float override once all
    // populated boxes parse (a half-typed number is simply not applied yet —
    // the same tolerant idiom as the Motion float fields).
    private void OnComponentEdited()
    {
        if (_suppressApply)
        {
            return;
        }

        var value = new float[4];
        for (var i = 0; i < 4; ++i)
        {
            if (!float.TryParse(
                    _components[i],
                    NumberStyles.Float,
                    CultureInfo.InvariantCulture,
                    out value[i]))
            {
                return;
            }
        }

        IsOverridden = true;
        _apply(Name, value);
    }

    private void RemoveOverride()
    {
        _suppressApply = true;
        for (var i = 0; i < 4; ++i)
        {
            _components[i] = "0";
        }
        OnPropertyChanged(nameof(ValueX));
        OnPropertyChanged(nameof(ValueY));
        OnPropertyChanged(nameof(ValueZ));
        OnPropertyChanged(nameof(ValueW));
        _suppressApply = false;

        IsOverridden = false;
        _apply(Name, null);
    }
}
