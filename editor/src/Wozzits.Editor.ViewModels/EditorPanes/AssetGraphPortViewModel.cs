namespace Wozzits.Editor.ViewModels.EditorPanes;

using Wozzits.Editor.Protocol;

public sealed class AssetGraphPortViewModel : ViewModelBase
{
    private bool _isConnectionTarget;
    private bool _isConnectionRejected;

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
}
