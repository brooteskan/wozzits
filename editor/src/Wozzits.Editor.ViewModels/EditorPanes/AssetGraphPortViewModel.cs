namespace Wozzits.Editor.ViewModels.EditorPanes;

using Wozzits.Editor.Protocol;

public sealed class AssetGraphPortViewModel : ViewModelBase
{
    private bool _isConnectionTarget;
    private bool _isConnectionRejected;
    private string? _rerouteName;

    public AssetGraphPortViewModel(
        AssetGraphNodeCardViewModel owner,
        EngineAssetGraphPort port,
        bool isInput)
    {
        Owner = owner;
        Index = port.Index;
        Type = port.Type;
        Flags = port.Flags;
        Name = port.Name;
        Label = port.Label;
        TypeName = port.TypeName;
        IsInput = isInput;
    }

    public AssetGraphNodeCardViewModel Owner { get; }

    public uint Index { get; }

    public uint Type { get; }

    public EngineAssetGraphPortFlags Flags { get; }

    public string Name { get; }

    public string Label { get; }

    public string TypeName { get; }

    public bool IsInput { get; }

    public bool IsOutput => !IsInput;

    public bool HasTypeName => !string.IsNullOrWhiteSpace(TypeName);

    public bool IsConnectionTarget
    {
        get => _isConnectionTarget;
        set => SetProperty(ref _isConnectionTarget, value);
    }

    public bool IsConnectionRejected
    {
        get => _isConnectionRejected;
        set => SetProperty(ref _isConnectionRejected, value);
    }

    // The named reroute this port participates in: on an OUTPUT port it labels the
    // declaration (this output is the named source); on an INPUT port it labels the usage
    // (this input is fed by that named reroute). Null when the port is not rerouted.
    // Editor-only display (issue woguls/wozzits-editor#1).
    public string? RerouteName
    {
        get => _rerouteName;
        set
        {
            if (SetProperty(ref _rerouteName, value))
            {
                OnPropertyChanged(nameof(HasReroute));
            }
        }
    }

    public bool HasReroute => !string.IsNullOrEmpty(RerouteName);
}
